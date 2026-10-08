/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_inspector.c
 *
 * Decode FAT16 as a byte protocol. A file's cluster numbers are relative to its
 * volume, not device LBAs. Every read passes through the partition boundary,
 * both FAT copies are compared when consulted, and a chain is checked before
 * its bytes are published. No on-disk field is ever treated as a C pointer.
 *
 * The limits make malformed-media work finite. They intentionally refuse some
 * otherwise valid large directories/files; this is an inspection profile, not
 * an unrestricted FAT implementation or a whole-volume consistency checker.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_inspector.h"

static void UmicomFatClear(void *target, UmicomSize count)
{
    /* Volatile byte stores avoid importing a hosted memset into the Kernel. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < count; ++i) out[i] = 0U;
}
static void UmicomFatCopy(void *target, const void *source, UmicomSize count)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    const UmicomU8 *in = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < count; ++i) out[i] = in[i];
}
static UmicomBoolean UmicomFatEqual(const void *left, const void *right, UmicomSize count)
{
    const UmicomU8 *a = (const UmicomU8 *)left, *b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < count; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomU16 UmicomFat16Word(const UmicomU8 *bytes)
{
    return (UmicomU16)((UmicomU16)bytes[0] | (UmicomU16)((UmicomU16)bytes[1] << 8U));
}
static UmicomU32 UmicomFat32Word(const UmicomU8 *bytes)
{
    return (UmicomU32)bytes[0] | ((UmicomU32)bytes[1] << 8U) |
        ((UmicomU32)bytes[2] << 16U) | ((UmicomU32)bytes[3] << 24U);
}
static UmicomU8 UmicomFatUpper(UmicomU8 byte)
{
    return byte >= 'a' && byte <= 'z' ? (UmicomU8)(byte - 'a' + 'A') : byte;
}
static UmicomBoolean UmicomFatNameByte(UmicomU8 byte)
{
    byte = UmicomFatUpper(byte);
    if ((byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')) return UMICOM_TRUE;
    /* A deliberately portable subset of the short-name alphabet. Unicode and
     * OEM code-page interpretation are not guessed from a byte above 127. */
    switch (byte) {
    case '_': case '-': case '$': case '~': case '!': case '#': case '%':
    case '&': case '(': case ')': case '@': case '^': case '{': case '}':
    case '\'': return UMICOM_TRUE;
    default: return UMICOM_FALSE;
    }
}
static UmicomBoolean UmicomFatStringEqual(const char *left, const char *right)
{
    /* Both names are locally produced and terminated within thirteen bytes. */
    for (UmicomSize i = 0U; i < 13U; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (!left[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomKernelDiskStatus UmicomFatReadSector(UmicomKernelFat16 *volume,
    UmicomU64 relative, UmicomU8 *output)
{
    if (relative >= volume->info.volumeSectors || relative >= volume->partitionSectors)
        return UMICOM_DISK_RANGE;
    if (volume->reads >= UMICOM_FAT16_IO_LIMIT) return UMICOM_DISK_LIMIT;
    ++volume->reads; /* Charge before the callback, including unsuccessful I/O. */
    const UmicomU64 absolute = volume->info.firstSector + relative;
    if (absolute >= volume->reader.sectors) return UMICOM_DISK_RANGE;
    return volume->reader.read(volume->reader.context, absolute, output) ?
        UMICOM_DISK_OK : UMICOM_DISK_IO_ERROR;
}

UmicomKernelDiskStatus UmicomKernelFat16Open(UmicomKernelFat16 *volume,
    const UmicomKernelDiskReader *reader, UmicomSize partition)
{
    if (!volume || !reader || !reader->read || !reader->sectors ||
        partition >= UMICOM_DISK_PRIMARY_PARTITIONS) return UMICOM_DISK_INVALID_ARGUMENT;
    if ((volume->self && volume->self != volume) || volume->open) return UMICOM_DISK_BAD_STATE;
    if (volume->busy) return UMICOM_DISK_BUSY;
    UmicomKernelPartitionTable table;
    UmicomFatClear(&table, sizeof(table));
    UmicomKernelDiskStatus status = UmicomKernelDiskPartitionsInspect(reader, &table);
    if (status != UMICOM_DISK_OK) return status;
    const UmicomKernelDiskPartition *part = &table.entries[partition];
    if (!part->present) return UMICOM_DISK_NOT_FOUND;
    if (part->type != 0x04U && part->type != 0x06U && part->type != 0x0eU)
        return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    UmicomU8 boot[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 first[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 second[UMICOM_DISK_SECTOR_BYTES];
    UmicomFatClear(boot, sizeof(boot));
    UmicomFatClear(first, sizeof(first));
    UmicomFatClear(second, sizeof(second));
    if (!reader->read(reader->context, part->firstSector, boot)) return UMICOM_DISK_IO_ERROR;
    if (boot[510] != 0x55U || boot[511] != 0xaaU) return UMICOM_DISK_SIGNATURE;
    UmicomKernelFat16Info info;
    UmicomFatClear(&info, sizeof(info));
    info.firstSector = part->firstSector;
    info.sectorsPerCluster = boot[13];
    info.rootEntries = UmicomFat16Word(boot + 17U);
    info.sectorsPerFat = UmicomFat16Word(boot + 22U);
    const UmicomU64 reserved = UmicomFat16Word(boot + 14U);
    const UmicomU64 totalSmall = UmicomFat16Word(boot + 19U);
    const UmicomU64 totalLarge = UmicomFat32Word(boot + 32U);
    if (UmicomFat16Word(boot + 11U) != UMICOM_DISK_SECTOR_BYTES || boot[16] != 2U ||
        !info.sectorsPerCluster || info.sectorsPerCluster > 64U ||
        (info.sectorsPerCluster & (info.sectorsPerCluster - 1U)) != 0U)
        return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    if (!reserved || !info.sectorsPerFat || !info.rootEntries ||
        (info.rootEntries % 16U) != 0U) return UMICOM_DISK_CORRUPT;
    if (info.rootEntries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    /* There is one authoritative size. Ambiguous dual sizes and an inconsistent
     * hidden-sector offset are not silently repaired for a partitioned volume. */
    if ((!totalSmall && !totalLarge) || (totalSmall && totalLarge) ||
        UmicomFat32Word(boot + 28U) != part->firstSector) return UMICOM_DISK_CORRUPT;
    info.volumeSectors = totalSmall ? totalSmall : totalLarge;
    if (info.volumeSectors > part->sectors) return UMICOM_DISK_RANGE;
    const UmicomU64 rootStart = reserved + (UmicomU64)info.sectorsPerFat * 2U;
    const UmicomU64 dataStart = rootStart + info.rootEntries / 16U;
    if (dataStart >= info.volumeSectors) return UMICOM_DISK_CORRUPT;
    const UmicomU64 clusters = (info.volumeSectors - dataStart) / info.sectorsPerCluster;
    /* FAT type follows data-cluster count, not the eight-byte "FAT16" label.
     * The top few numerically reserved cluster IDs are outside this profile. */
    if (clusters < 4085U || clusters >= 65525U) return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    if (clusters + 1U >= 0xfff0U) return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    if ((clusters + 2U) * 2U > (UmicomU64)info.sectorsPerFat * UMICOM_DISK_SECTOR_BYTES)
        return UMICOM_DISK_CORRUPT;
    info.clusters = (UmicomU32)clusters;
    if (boot[21] != 0xf0U && boot[21] < 0xf8U) return UMICOM_DISK_CORRUPT;
    if (!reader->read(reader->context, part->firstSector + reserved, first) ||
        !reader->read(reader->context, part->firstSector + reserved + info.sectorsPerFat, second))
        return UMICOM_DISK_IO_ERROR;
    if (!UmicomFatEqual(first, second, sizeof(first))) return UMICOM_DISK_FAT_MISMATCH;
    if (UmicomFat16Word(first) != (UmicomU16)(0xff00U | boot[21])) return UMICOM_DISK_CORRUPT;
    /* FAT16 entry one has the clean-shutdown and no-I/O-error bits. A read-only
     * inspector must not "repair" those flags or mark an unclean image clean. */
    const UmicomU16 flags = UmicomFat16Word(first + 2U);
    if ((flags & 0x3fffU) != 0x3fffU) return UMICOM_DISK_CORRUPT;
    if ((flags & 0xc000U) != 0xc000U) return UMICOM_DISK_DIRTY;
    if (boot[38] == 0x29U) {
        for (UmicomSize i = 0U; i < 11U; ++i)
            info.label[i] = boot[43U + i] >= 32U && boot[43U + i] <= 126U ? (char)boot[43U + i] : '?';
        /* Right-trim display padding, retaining a bounded terminated string. */
        for (UmicomSize i = 11U; i && info.label[i - 1U] == ' '; --i) info.label[i - 1U] = '\0';
    }
    /* All checks completed in local scratch storage. Only now publish a usable
     * owner, so callers never see a half-accepted geometry after an error. */
    UmicomFatClear(volume, sizeof(*volume));
    volume->self = volume;
    volume->reader = *reader;
    volume->info = info;
    volume->partitionSectors = part->sectors;
    volume->fatStart = reserved;
    volume->rootStart = rootStart;
    volume->dataStart = dataStart;
    volume->open = UMICOM_TRUE;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatBegin(UmicomKernelFat16 *volume)
{
    if (!volume || volume->self != volume || !volume->open) return UMICOM_DISK_BAD_STATE;
    if (volume->busy) return UMICOM_DISK_BUSY;
    volume->busy = UMICOM_TRUE;
    volume->reads = 0U;
    /* Do not carry a FAT sector across separate operations. The caller still
     * promises an immutable medium from Open through Close; this is not locking. */
    volume->fatCached = UMICOM_FALSE;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatFinish(UmicomKernelFat16 *volume, UmicomKernelDiskStatus status)
{
    UmicomFatClear(volume->readStage, sizeof(volume->readStage));
    volume->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelDiskStatus UmicomKernelFat16Close(UmicomKernelFat16 *volume)
{
    if (!volume || volume->self != volume || !volume->open) return UMICOM_DISK_BAD_STATE;
    if (volume->busy) return UMICOM_DISK_BUSY;
    /* Close retires only this interpretation and its scratch contents. The
     * caller must then reset/close the original block transport through its API. */
    UmicomFatClear(volume, sizeof(*volume));
    volume->self = volume;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatNext(UmicomKernelFat16 *volume,
    UmicomU16 cluster, UmicomU16 *outNext)
{
    if (cluster < 2U || cluster > volume->info.clusters + 1U || cluster >= 0xfff0U)
        return UMICOM_DISK_CORRUPT;
    const UmicomU64 sector = (UmicomU64)cluster / 256U;
    if (sector >= volume->info.sectorsPerFat) return UMICOM_DISK_CORRUPT;
    if (!volume->fatCached || volume->cachedFatSector != sector) {
        UmicomKernelDiskStatus status = UmicomFatReadSector(volume, volume->fatStart + sector, volume->fatSector);
        if (status != UMICOM_DISK_OK) return status;
        status = UmicomFatReadSector(volume, volume->fatStart + volume->info.sectorsPerFat + sector,
            volume->mirrorSector);
        if (status != UMICOM_DISK_OK) return status;
        if (!UmicomFatEqual(volume->fatSector, volume->mirrorSector, UMICOM_DISK_SECTOR_BYTES))
            return UMICOM_DISK_FAT_MISMATCH;
        volume->cachedFatSector = sector;
        volume->fatCached = UMICOM_TRUE;
    }
    const UmicomU16 next = UmicomFat16Word(volume->fatSector + ((UmicomSize)cluster % 256U) * 2U);
    if (next >= 0xfff8U) { *outNext = 0U; return UMICOM_DISK_OK; }
    /* Free, reserved, bad and out-of-volume cluster links cannot become reads. */
    if (next < 2U || next >= 0xfff0U || next > volume->info.clusters + 1U) return UMICOM_DISK_CORRUPT;
    *outNext = next;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatChain(UmicomKernelFat16 *volume, UmicomU16 first,
    UmicomBoolean exact, UmicomU64 required, UmicomU16 *chain, UmicomSize *outCount)
{
    if (exact && required == 0U) {
        if (first) return UMICOM_DISK_CORRUPT;
        *outCount = 0U; return UMICOM_DISK_OK;
    }
    if (exact && required > UMICOM_FAT16_CHAIN_LIMIT) return UMICOM_DISK_LIMIT;
    UmicomU16 current = first;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_CHAIN_LIMIT; ++i) {
        for (UmicomSize j = 0U; j < i; ++j)
            if (chain[j] == current) return UMICOM_DISK_CHAIN_CYCLE;
        chain[i] = current;
        UmicomU16 next = 0U;
        UmicomKernelDiskStatus status = UmicomFatNext(volume, current, &next);
        if (status != UMICOM_DISK_OK) return status;
        if (!next) {
            if (exact && required != i + 1U) return UMICOM_DISK_CORRUPT;
            *outCount = i + 1U; return UMICOM_DISK_OK;
        }
        if (exact && required == i + 1U) {
            /* Detect a cycle at the expected tail distinctly from surplus
             * allocation. Neither is silently accepted as a healthy file. */
            for (UmicomSize j = 0U; j <= i; ++j)
                if (chain[j] == next) return UMICOM_DISK_CHAIN_CYCLE;
            return UMICOM_DISK_CORRUPT;
        }
        current = next;
    }
    return UMICOM_DISK_LIMIT;
}
static UmicomKernelDiskStatus UmicomFatDecodeName(const UmicomU8 *raw, char *name)
{
    UmicomSize used = 0U;
    for (UmicomSize field = 0U; field < 2U; ++field) {
        const UmicomSize start = field ? 8U : 0U;
        const UmicomSize width = field ? 3U : 8U;
        UmicomBoolean padding = UMICOM_FALSE;
        for (UmicomSize i = 0U; i < width; ++i) {
            const UmicomU8 byte = raw[start + i];
            if (byte == ' ') { padding = UMICOM_TRUE; continue; }
            if (padding || !UmicomFatNameByte(byte)) return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
            if (field && i == 0U) name[used++] = '.';
            name[used++] = (char)UmicomFatUpper(byte);
        }
        if (!field && !used) return UMICOM_DISK_CORRUPT;
    }
    name[used] = '\0';
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatDirectoryRead(UmicomKernelFat16 *volume, UmicomU16 first)
{
    UmicomU16 chain[UMICOM_FAT16_CHAIN_LIMIT];
    UmicomFatClear(chain, sizeof(chain));
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (first) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, first, UMICOM_FALSE, 0U, chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    UmicomFatClear(&volume->directoryStage, sizeof(volume->directoryStage));
    for (UmicomU64 i = 0U; i < entries; ++i) {
        if (i % 16U == 0U) {
            UmicomU64 sector = volume->rootStart + i / 16U;
            if (first) {
                const UmicomU64 clusterIndex = i / (16U * volume->info.sectorsPerCluster);
                if (clusterIndex >= count) return UMICOM_DISK_CORRUPT;
                sector = volume->dataStart + ((UmicomU64)chain[clusterIndex] - 2U) * volume->info.sectorsPerCluster +
                    (i / 16U) % volume->info.sectorsPerCluster;
            }
            const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, sector, volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *raw = volume->dataSector + (i % 16U) * 32U;
        if (!raw[0]) break; /* The format's end marker ends live directory entries. */
        if (raw[0] == 0xe5U) continue; /* Deleted names never become live handles. */
        if (raw[11] == 0x0fU) {
            ++volume->directoryStage.longNameRecords;
            continue; /* Do not interpret UTF-16 long-name bytes as metadata. */
        }
        if ((raw[11] & 0xc0U) || (raw[12] & ~0x18U) || UmicomFat16Word(raw + 20U))
            return UMICOM_DISK_CORRUPT;
        if (raw[11] & 0x08U) {
            if (raw[11] & 0x10U) return UMICOM_DISK_CORRUPT;
            continue; /* A volume-label record is not a file. */
        }
        if (raw[0] == '.') {
            /* Dot entries are never followed by this path grammar. Validate
             * their small shape instead of allowing arbitrary dot-prefixed names. */
            const UmicomSize dots = raw[1] == '.' ? 2U : 1U;
            for (UmicomSize j = dots; j < 11U; ++j) if (raw[j] != ' ') return UMICOM_DISK_CORRUPT;
            if (!first || !(raw[11] & 0x10U) || UmicomFat32Word(raw + 28U)) return UMICOM_DISK_CORRUPT;
            const UmicomU16 linked = UmicomFat16Word(raw + 26U);
            if ((dots == 1U && linked != first) || linked > volume->info.clusters + 1U)
                return UMICOM_DISK_CORRUPT;
            continue;
        }
        if (volume->directoryStage.count == UMICOM_FAT16_ENTRY_LIMIT) return UMICOM_DISK_LIMIT;
        UmicomKernelFat16Entry *entry = &volume->directoryStage.entries[volume->directoryStage.count];
        UmicomKernelDiskStatus status = UmicomFatDecodeName(raw, entry->name);
        if (status != UMICOM_DISK_OK) return status;
        entry->attributes = raw[11];
        entry->directory = (raw[11] & 0x10U) ? UMICOM_TRUE : UMICOM_FALSE;
        entry->bytes = UmicomFat32Word(raw + 28U);
        entry->firstCluster = UmicomFat16Word(raw + 26U);
        if (entry->directory && entry->bytes) return UMICOM_DISK_CORRUPT;
        if (entry->directory || entry->bytes) {
            if (entry->firstCluster < 2U || entry->firstCluster >= 0xfff0U ||
                entry->firstCluster > volume->info.clusters + 1U) return UMICOM_DISK_CORRUPT;
        } else if (entry->firstCluster) return UMICOM_DISK_CORRUPT;
        for (UmicomSize j = 0U; j < volume->directoryStage.count; ++j)
            if (UmicomFatStringEqual(entry->name, volume->directoryStage.entries[j].name))
                return UMICOM_DISK_CORRUPT;
        ++volume->directoryStage.count;
    }
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatPath(const char *path,
    char names[UMICOM_FAT16_DEPTH_LIMIT][13], UmicomSize *outDepth)
{
    if (!path || path[0] != '/') return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize length = 0U;
    while (length < UMICOM_FAT16_PATH_BYTES && path[length]) ++length;
    if (length == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    if (length == 1U) { *outDepth = 0U; return UMICOM_DISK_OK; }
    UmicomSize depth = 0U, start = 1U;
    while (start < length) {
        if (depth == UMICOM_FAT16_DEPTH_LIMIT) return UMICOM_DISK_LIMIT;
        UmicomSize end = start;
        while (end < length && path[end] != '/') ++end;
        if (end == start || end - start > 12U) return UMICOM_DISK_INVALID_ARGUMENT;
        UmicomSize base = 0U, extension = 0U, used = 0U;
        UmicomBoolean dotted = UMICOM_FALSE;
        for (UmicomSize i = start; i < end; ++i) {
            const UmicomU8 byte = (UmicomU8)path[i];
            if (byte == '.') {
                if (dotted || !base || i + 1U == end) return UMICOM_DISK_INVALID_ARGUMENT;
                dotted = UMICOM_TRUE;
            } else {
                if (!UmicomFatNameByte(byte)) return UMICOM_DISK_INVALID_ARGUMENT;
                if (dotted) ++extension; else ++base;
            }
            if (base > 8U || extension > 3U) return UMICOM_DISK_INVALID_ARGUMENT;
            names[depth][used++] = (char)UmicomFatUpper(byte);
        }
        names[depth][used] = '\0';
        ++depth;
        if (end == length) break;
        start = end + 1U;
        if (start == length) return UMICOM_DISK_INVALID_ARGUMENT;
    }
    *outDepth = depth;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatLookup(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Entry *outEntry)
{
    char names[UMICOM_FAT16_DEPTH_LIMIT][13];
    UmicomFatClear(names, sizeof(names));
    UmicomSize depth = 0U;
    UmicomKernelDiskStatus status = UmicomFatPath(path, names, &depth);
    if (status != UMICOM_DISK_OK) return status; /* Validate all components first. */
    UmicomKernelFat16Entry current;
    UmicomFatClear(&current, sizeof(current));
    current.name[0] = '/'; current.directory = UMICOM_TRUE;
    for (UmicomSize i = 0U; i < depth; ++i) {
        if (!current.directory) return UMICOM_DISK_NOT_DIRECTORY;
        status = UmicomFatDirectoryRead(volume, current.firstCluster);
        if (status != UMICOM_DISK_OK) return status;
        UmicomBoolean found = UMICOM_FALSE;
        for (UmicomSize j = 0U; j < volume->directoryStage.count; ++j) {
            if (UmicomFatStringEqual(names[i], volume->directoryStage.entries[j].name)) {
                current = volume->directoryStage.entries[j]; found = UMICOM_TRUE; break;
            }
        }
        if (!found) return UMICOM_DISK_NOT_FOUND;
    }
    *outEntry = current;
    return UMICOM_DISK_OK;
}
UmicomKernelDiskStatus UmicomKernelFat16Stat(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Entry *outEntry)
{
    if (!path || !outEntry) return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomKernelDiskStatus status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16Entry entry;
    UmicomFatClear(&entry, sizeof(entry));
    status = UmicomFatLookup(volume, path, &entry);
    if (status == UMICOM_DISK_OK) *outEntry = entry;
    return UmicomFatFinish(volume, status);
}
UmicomKernelDiskStatus UmicomKernelFat16List(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Directory *outDirectory)
{
    if (!path || !outDirectory) return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomKernelDiskStatus status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16Entry entry;
    UmicomFatClear(&entry, sizeof(entry));
    status = UmicomFatLookup(volume, path, &entry);
    if (status == UMICOM_DISK_OK && !entry.directory) status = UMICOM_DISK_NOT_DIRECTORY;
    if (status == UMICOM_DISK_OK) status = UmicomFatDirectoryRead(volume, entry.firstCluster);
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outDirectory, &volume->directoryStage, sizeof(*outDirectory));
    return UmicomFatFinish(volume, status);
}
UmicomKernelDiskStatus UmicomKernelFat16Read(UmicomKernelFat16 *volume,
    const char *path, UmicomU64 offset, void *output, UmicomSize capacity, UmicomSize *outBytes)
{
    if (!path || !output || !outBytes || !capacity || capacity > UMICOM_FAT16_READ_BYTES)
        return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomKernelDiskStatus status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16Entry entry;
    UmicomFatClear(&entry, sizeof(entry));
    status = UmicomFatLookup(volume, path, &entry);
    if (status == UMICOM_DISK_OK && entry.directory) status = UMICOM_DISK_IS_DIRECTORY;
    UmicomU16 chain[UMICOM_FAT16_CHAIN_LIMIT];
    UmicomFatClear(chain, sizeof(chain));
    UmicomSize chainCount = 0U, copied = 0U;
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    if (status == UMICOM_DISK_OK) {
        const UmicomU64 needed = ((UmicomU64)entry.bytes + clusterBytes - 1U) / clusterBytes;
        status = UmicomFatChain(volume, entry.firstCluster, UMICOM_TRUE, needed, chain, &chainCount);
    }
    if (status == UMICOM_DISK_OK && offset < entry.bytes) {
        const UmicomU64 left = (UmicomU64)entry.bytes - offset;
        const UmicomSize requested = left < capacity ? left : capacity;
        while (copied < requested) {
            const UmicomU64 position = offset + copied;
            const UmicomU64 index = position / clusterBytes;
            if (index >= chainCount) { status = UMICOM_DISK_CORRUPT; break; }
            const UmicomU64 sector = volume->dataStart + ((UmicomU64)chain[index] - 2U) *
                volume->info.sectorsPerCluster + (position % clusterBytes) / UMICOM_DISK_SECTOR_BYTES;
            status = UmicomFatReadSector(volume, sector, volume->dataSector);
            if (status != UMICOM_DISK_OK) break;
            const UmicomSize within = position % UMICOM_DISK_SECTOR_BYTES;
            UmicomSize take = UMICOM_DISK_SECTOR_BYTES - within;
            if (take > requested - copied) take = requested - copied;
            UmicomFatCopy(volume->readStage + copied, volume->dataSector + within, take);
            copied += take;
        }
    }
    if (status == UMICOM_DISK_OK) {
        /* The caller sees bytes only after the whole requested read succeeds.
         * There is no externally visible prefix after a late I/O failure. */
        UmicomFatCopy(output, volume->readStage, copied);
        *outBytes = copied;
    }
    return UmicomFatFinish(volume, status);
}

/*-----------------------------------------------------------------------------
 * Strict preparation for an existing-allocation data update.
 *
 * Keep the original read-only parser and its profile above intact. This
 * additional read-only entry point reuses those byte/chain/name decoders,
 * then proves the stronger allocation ownership required before a separate
 * transport owner can safely overwrite file data. No write callback exists
 * here, so a partially checked plan can never itself modify media.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_update_plan.h"

static UmicomBoolean UmicomFatPlanSpan(const void *pointer, UmicomSize bytes)
{
    const UmicomAddress start = (UmicomAddress)pointer;
    return pointer && bytes && bytes <= (UmicomU64)~(UmicomAddress)0U &&
        start <= ~(UmicomAddress)0U - (UmicomAddress)bytes ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomFatPlanOverlap(const void *left, UmicomSize leftBytes,
    const void *right, UmicomSize rightBytes)
{
    const UmicomAddress a = (UmicomAddress)left, b = (UmicomAddress)right;
    /* Extents were validated before this addition. Never form an overflowing
     * end address while deciding whether caller storage belongs to an owner. */
    return a < b + (UmicomAddress)rightBytes && b < a + (UmicomAddress)leftBytes ?
        UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomFatPlanZero(const void *pointer, UmicomSize bytes)
{
    const UmicomU8 *const source = (const UmicomU8 *)pointer;
    for (UmicomSize i = 0U; i < bytes; ++i) if (source[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomKernelDiskStatus UmicomFatPlanArguments(UmicomKernelFat16 *volume,
    const char *path, const void *input, UmicomSize bytes,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16UpdatePlan *outPlan)
{
    if (!bytes || bytes > UMICOM_FAT16_UPDATE_BYTES ||
        !UmicomFatPlanSpan(volume, sizeof(*volume)) ||
        !UmicomFatPlanSpan(workspace, sizeof(*workspace)) ||
        !UmicomFatPlanSpan(outPlan, sizeof(*outPlan)) ||
        !UmicomFatPlanSpan(input, bytes) ||
        !UmicomFatPlanSpan(path, UMICOM_FAT16_PATH_BYTES) ||
        (UmicomAddress)volume % _Alignof(UmicomKernelFat16) ||
        (UmicomAddress)workspace % _Alignof(UmicomKernelFat16UpdateWorkspace) ||
        (UmicomAddress)outPlan % _Alignof(UmicomKernelFat16UpdatePlan))
        return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    while (pathBytes < UMICOM_FAT16_PATH_BYTES) {
        /* Refuse each byte's ownership before reading it. A path pointing
         * into an owner/result must not first scan that storage for a NUL. */
        const char *const current = path + pathBytes;
        if (UmicomFatPlanOverlap(current, 1U, volume, sizeof(*volume)) ||
            UmicomFatPlanOverlap(current, 1U, workspace, sizeof(*workspace)) ||
            UmicomFatPlanOverlap(current, 1U, outPlan, sizeof(*outPlan)) ||
            UmicomFatPlanOverlap(current, 1U, input, bytes))
            return UMICOM_DISK_INVALID_ARGUMENT;
        if (!path[pathBytes]) break;
        ++pathBytes;
    }
    if (pathBytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    ++pathBytes; /* The terminator belongs to the path's protected input span. */
    const void *const pointers[] = {volume, workspace, outPlan, input, path};
    const UmicomSize lengths[] = {sizeof(*volume), sizeof(*workspace), sizeof(*outPlan), bytes, pathBytes};
    for (UmicomSize i = 0U; i < 5U; ++i)
        for (UmicomSize j = i + 1U; j < 5U; ++j)
            if (UmicomFatPlanOverlap(pointers[i], lengths[i], pointers[j], lengths[j]))
                return UMICOM_DISK_INVALID_ARGUMENT;
    if (workspace->self && workspace->self != workspace) return UMICOM_DISK_BAD_STATE;
    if (workspace->busy) return UMICOM_DISK_BUSY;
    if (!workspace->self && !UmicomFatPlanZero(workspace, sizeof(*workspace)))
        return UMICOM_DISK_BAD_STATE;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatPlanGeometry(const UmicomKernelFat16 *volume)
{
    /* The inspector already checked this geometry at Open. Recheck the small
     * arithmetic invariants before using public owner fields as divisors or
     * bitmap indices, so accidental owner damage cannot become an unsafe plan. */
    const UmicomKernelFat16Info *const info = &volume->info;
    if (!volume->reader.read || !volume->reader.sectors || !info->firstSector ||
        !info->volumeSectors || !volume->partitionSectors ||
        info->firstSector >= volume->reader.sectors ||
        volume->partitionSectors > volume->reader.sectors - info->firstSector ||
        info->volumeSectors > volume->partitionSectors ||
        !info->sectorsPerCluster || info->sectorsPerCluster > 64U ||
        (info->sectorsPerCluster & (info->sectorsPerCluster - 1U)) ||
        !info->sectorsPerFat || !info->rootEntries || info->rootEntries % 16U ||
        info->rootEntries > UMICOM_FAT16_SCAN_ENTRIES ||
        info->clusters < 4085U || info->clusters + 1U >= 0xfff0U ||
        !volume->fatStart || volume->fatStart >= info->volumeSectors)
        return UMICOM_DISK_BAD_STATE;
    if (info->sectorsPerFat > UMICOM_FAT16_UPDATE_FAT_SECTORS) return UMICOM_DISK_LIMIT;
    if ((UmicomU64)info->sectorsPerFat * 2U > info->volumeSectors - volume->fatStart)
        return UMICOM_DISK_BAD_STATE;
    const UmicomU64 root = volume->fatStart + (UmicomU64)info->sectorsPerFat * 2U;
    if (root >= info->volumeSectors || volume->rootStart != root ||
        (UmicomU64)info->rootEntries / 16U >= info->volumeSectors - root)
        return UMICOM_DISK_BAD_STATE;
    const UmicomU64 data = root + (UmicomU64)info->rootEntries / 16U;
    if (volume->dataStart != data ||
        (info->volumeSectors - data) / info->sectorsPerCluster != info->clusters ||
        ((UmicomU64)info->clusters + 2U) * 2U >
            (UmicomU64)info->sectorsPerFat * UMICOM_DISK_SECTOR_BYTES)
        return UMICOM_DISK_BAD_STATE;
    return UMICOM_DISK_OK;
}
static UmicomBoolean UmicomFatPlanOwned(const UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomU16 cluster)
{
    return workspace->owned[cluster / 8U] & (UmicomU8)(1U << (cluster % 8U)) ?
        UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelDiskStatus UmicomFatPlanOwnChain(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, const UmicomKernelFat16Entry *entry)
{
    UmicomSize count = 0U;
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    const UmicomU64 required = ((UmicomU64)entry->bytes + clusterBytes - 1U) / clusterBytes;
    UmicomKernelDiskStatus status = UmicomFatChain(volume, entry->firstCluster,
        entry->directory ? UMICOM_FALSE : UMICOM_TRUE, required, workspace->chain, &count);
    if (status != UMICOM_DISK_OK) return status;
    for (UmicomSize i = 0U; i < count; ++i) {
        const UmicomU16 cluster = workspace->chain[i];
        if (UmicomFatPlanOwned(workspace, cluster)) return UMICOM_DISK_CORRUPT;
        workspace->owned[cluster / 8U] |= (UmicomU8)(1U << (cluster % 8U));
    }
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatPlanDirectory(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 first, UmicomU16 parent)
{
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (first) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, first,
            UMICOM_FALSE, 0U, workspace->chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    UmicomKernelDiskStatus status = UmicomFatDirectoryRead(volume, first);
    if (status != UMICOM_DISK_OK) return status;
    if (volume->directoryStage.longNameRecords) return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    /* Reuse the original complete short-entry decoder, then examine only the
     * metadata it deliberately skips. Reads below do not overwrite its staged
     * listing. This avoids a second implementation of normal FAT names/files. */
    UmicomSize labels = 0U;
    for (UmicomU64 i = 0U; i < entries; ++i) {
        if (i % 16U == 0U) {
            UmicomU64 sector = volume->rootStart + i / 16U;
            if (first) {
                const UmicomU64 index = i / (16U * volume->info.sectorsPerCluster);
                if (index >= count) return UMICOM_DISK_CORRUPT;
                sector = volume->dataStart + ((UmicomU64)workspace->chain[index] - 2U) *
                    volume->info.sectorsPerCluster + (i / 16U) % volume->info.sectorsPerCluster;
            }
            status = UmicomFatReadSector(volume, sector, volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *const raw = volume->dataSector + (i % 16U) * 32U;
        if (first && i < 2U) {
            /* Dot records are references, not additional owners. Requiring
             * their canonical positions lets this profile reject missing,
             * duplicate and wrong-parent records without following them. */
            const UmicomSize dots = i == 0U ? 1U : 2U;
            for (UmicomSize j = 0U; j < dots; ++j) if (raw[j] != '.') return UMICOM_DISK_CORRUPT;
            for (UmicomSize j = dots; j < 11U; ++j) if (raw[j] != ' ') return UMICOM_DISK_CORRUPT;
            if (raw[11] != 0x10U || UmicomFat16Word(raw + 20U) || UmicomFat32Word(raw + 28U) ||
                UmicomFat16Word(raw + 26U) != (i == 0U ? first : parent))
                return UMICOM_DISK_CORRUPT;
            continue;
        }
        if (!raw[0]) break;
        if (raw[0] == 0xe5U) continue;
        if (raw[0] == '.') return UMICOM_DISK_CORRUPT;
        if (raw[11] == 0x0fU) return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
        if (raw[11] & 0x08U) {
            if (first || labels || raw[11] != 0x08U || UmicomFat16Word(raw + 20U) ||
                UmicomFat16Word(raw + 26U) || UmicomFat32Word(raw + 28U))
                return UMICOM_DISK_CORRUPT;
            ++labels;
        }
    }
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatPlanNamespace(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace)
{
    workspace->directoryCount = 1U; /* The zero-cluster queue entry is the fixed root. */
    workspace->objects = 1U;
    while (workspace->nextDirectory < workspace->directoryCount) {
        const UmicomKernelFat16UpdateDirectory *const directory =
            &workspace->directories[workspace->nextDirectory];
        const UmicomU16 first = directory->firstCluster;
        const UmicomU16 parent = directory->parentCluster;
        const UmicomSize depth = directory->depth;
        ++workspace->nextDirectory;
        UmicomKernelDiskStatus status = UmicomFatPlanDirectory(volume, workspace, first, parent);
        if (status != UMICOM_DISK_OK) return status;
        for (UmicomSize i = 0U; i < volume->directoryStage.count; ++i) {
            const UmicomKernelFat16Entry *const entry = &volume->directoryStage.entries[i];
            if (workspace->objects >= UMICOM_FAT16_UPDATE_OBJECT_LIMIT || depth >= UMICOM_FAT16_DEPTH_LIMIT)
                return UMICOM_DISK_LIMIT;
            ++workspace->objects;
            status = UmicomFatPlanOwnChain(volume, workspace, entry);
            if (status != UMICOM_DISK_OK) return status;
            if (entry->directory) {
                if (workspace->directoryCount >= UMICOM_FAT16_UPDATE_DIRECTORY_LIMIT)
                    return UMICOM_DISK_LIMIT;
                UmicomKernelFat16UpdateDirectory *const next =
                    &workspace->directories[workspace->directoryCount];
                next->firstCluster = entry->firstCluster;
                next->parentCluster = first;
                next->depth = depth + 1U;
                ++workspace->directoryCount;
            }
        }
    }
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatPlanAllocation(UmicomKernelFat16 *volume,
    const UmicomKernelFat16UpdateWorkspace *workspace)
{
    /* A reachable walk alone misses an orphan chain pointing into a live file.
     * Classify every usable FAT entry against the ownership bitmap. Compare
     * whole declared copies, but do not interpret padding as extra clusters. */
    for (UmicomU64 sector = 0U; sector < volume->info.sectorsPerFat; ++sector) {
        UmicomKernelDiskStatus status = UmicomFatReadSector(volume,
            volume->fatStart + sector, volume->fatSector);
        if (status != UMICOM_DISK_OK) return status;
        status = UmicomFatReadSector(volume,
            volume->fatStart + volume->info.sectorsPerFat + sector, volume->mirrorSector);
        if (status != UMICOM_DISK_OK) return status;
        if (!UmicomFatEqual(volume->fatSector, volume->mirrorSector, UMICOM_DISK_SECTOR_BYTES))
            return UMICOM_DISK_FAT_MISMATCH;
        if (sector == 0U) {
            const UmicomU16 media = UmicomFat16Word(volume->fatSector);
            const UmicomU16 flags = UmicomFat16Word(volume->fatSector + 2U);
            if ((media & 0xff00U) != 0xff00U ||
                ((media & 0xffU) != 0xf0U && (media & 0xffU) < 0xf8U) ||
                (flags & 0x3fffU) != 0x3fffU) return UMICOM_DISK_CORRUPT;
            if ((flags & 0xc000U) != 0xc000U) return UMICOM_DISK_DIRTY;
        }
        for (UmicomSize i = 0U; i < 256U; ++i) {
            const UmicomU64 cluster = sector * 256U + i;
            if (cluster < 2U || cluster > (UmicomU64)volume->info.clusters + 1U) continue;
            const UmicomU16 next = UmicomFat16Word(volume->fatSector + i * 2U);
            const UmicomBoolean owned = UmicomFatPlanOwned(workspace, (UmicomU16)cluster);
            if (!next) {
                if (owned) return UMICOM_DISK_CORRUPT;
                continue;
            }
            if (next == 0xfff7U) return UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
            if (next < 0xfff8U && (next < 2U || next >= 0xfff0U ||
                next > volume->info.clusters + 1U)) return UMICOM_DISK_CORRUPT;
            if (!owned) return UMICOM_DISK_CORRUPT;
        }
    }
    /* The streaming comparison reused the FAT cache buffers with another
     * sector. Invalidate the original single-sector cache before returning. */
    volume->fatCached = UMICOM_FALSE;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatPlanSectors(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomU64 offset, UmicomSize bytes)
{
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    UmicomSize copied = 0U;
    while (copied < bytes) {
        if (workspace->stage.count >= UMICOM_FAT16_UPDATE_SECTOR_LIMIT) return UMICOM_DISK_LIMIT;
        const UmicomU64 position = offset + copied;
        const UmicomU64 index = position / clusterBytes;
        if (index >= workspace->targetClusters) return UMICOM_DISK_CORRUPT;
        const UmicomU16 cluster = workspace->targetChain[index];
        if (cluster < 2U || cluster > volume->info.clusters + 1U ||
            !UmicomFatPlanOwned(workspace, cluster)) return UMICOM_DISK_CORRUPT;
        const UmicomU64 relative = volume->dataStart + ((UmicomU64)cluster - 2U) *
            volume->info.sectorsPerCluster + (position % clusterBytes) / UMICOM_DISK_SECTOR_BYTES;
        if (relative < volume->dataStart || relative >= volume->info.volumeSectors ||
            relative >= volume->partitionSectors) return UMICOM_DISK_RANGE;
        UmicomKernelFat16UpdateSector *const sector = &workspace->stage.sectors[workspace->stage.count];
        sector->sector = volume->info.firstSector + relative;
        sector->inputOffset = copied;
        sector->offset = position % UMICOM_DISK_SECTOR_BYTES;
        sector->bytes = UMICOM_DISK_SECTOR_BYTES - sector->offset;
        if (sector->bytes > bytes - copied) sector->bytes = bytes - copied;
        const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative, sector->data);
        if (status != UMICOM_DISK_OK) return status;
        UmicomFatCopy(sector->data + sector->offset, workspace->input + copied, sector->bytes);
        copied += sector->bytes;
        ++workspace->stage.count;
    }
    workspace->stage.offset = offset;
    workspace->stage.bytes = bytes;
    return UMICOM_DISK_OK;
}
UmicomKernelDiskStatus UmicomKernelFat16PlanUpdate(UmicomKernelFat16 *volume,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16UpdatePlan *outPlan)
{
    UmicomKernelDiskStatus status = UmicomFatPlanArguments(volume, path, input, bytes, workspace, outPlan);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    workspace->busy = UMICOM_TRUE;
    UmicomFatCopy(workspace->input, input, bytes);
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK) status = UmicomFatLookup(volume, path, &workspace->stage.entry);
    if (status == UMICOM_DISK_OK && workspace->stage.entry.directory) status = UMICOM_DISK_IS_DIRECTORY;
    if (status == UMICOM_DISK_OK && (workspace->stage.entry.attributes & 0x01U)) status = UMICOM_DISK_READ_ONLY;
    if (status == UMICOM_DISK_OK && !(workspace->stage.entry.attributes & 0x20U))
        status = UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    if (status == UMICOM_DISK_OK && (offset > workspace->stage.entry.bytes ||
        bytes > (UmicomU64)workspace->stage.entry.bytes - offset)) status = UMICOM_DISK_RANGE;
    if (status == UMICOM_DISK_OK) {
        const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
        const UmicomU64 required = ((UmicomU64)workspace->stage.entry.bytes + clusterBytes - 1U) / clusterBytes;
        status = UmicomFatChain(volume, workspace->stage.entry.firstCluster, UMICOM_TRUE,
            required, workspace->targetChain, &workspace->targetClusters);
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanSectors(volume, workspace, offset, bytes);
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outPlan, &workspace->stage, sizeof(*outPlan));
    /* Errors publish no partial plan. Scrub caller input and staged media
     * before releasing the guard, including after a failed final sector read. */
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}

/*-----------------------------------------------------------------------------
 * Existing-file write metadata preparation.
 *
 * Keep the original data-only planner and its stricter ARCHIVE requirement
 * intact. This companion shares its checked geometry, directory decoder,
 * allocation proof and staged data sectors, then derives one complete original
 * directory sector for a separate ordered file-commit owner. No disk mutation
 * is possible here. The caller supplies calendar fields explicitly.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_file_plan.h"

static UmicomKernelDiskStatus UmicomFatFileTimeFields(
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16FileTimeEncoding *encoded)
{
    if (time->year < 1980U || time->year > 2107U ||
        !time->month || time->month > 12U || !time->day ||
        time->hour > 23U || time->minute > 59U || time->second > 59U)
        return UMICOM_DISK_INVALID_ARGUMENT;
    const UmicomBoolean leap = time->year % 4U == 0U &&
        (time->year % 100U != 0U || time->year % 400U == 0U) ? UMICOM_TRUE : UMICOM_FALSE;
    UmicomU16 days = 31U;
    if (time->month == 2U) days = leap ? 29U : 28U;
    else if (time->month == 4U || time->month == 6U ||
        time->month == 9U || time->month == 11U) days = 30U;
    if (time->day > days) return UMICOM_DISK_INVALID_ARGUMENT;
    /* FAT stores seconds in two-second units. An odd second is deliberately
     * rounded down; no carry can silently alter the requested minute or date. */
    UmicomFatClear(encoded, sizeof(*encoded));
    encoded->writeTime = (UmicomU16)(((UmicomU32)time->hour << 11U) |
        ((UmicomU32)time->minute << 5U) | (time->second / 2U));
    encoded->writeDate = (UmicomU16)(((time->year - 1980U) << 9U) |
        ((UmicomU32)time->month << 5U) | time->day);
    encoded->storedSecond = (UmicomU8)((time->second / 2U) * 2U);
    return UMICOM_DISK_OK;
}

UmicomKernelDiskStatus UmicomKernelFat16FileTimeEncode(
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16FileTimeEncoding *outEncoded)
{
    if (!UmicomFatPlanSpan(time, sizeof(*time)) ||
        !UmicomFatPlanSpan(outEncoded, sizeof(*outEncoded)) ||
        (UmicomAddress)time % _Alignof(UmicomKernelFat16FileTime) ||
        (UmicomAddress)outEncoded % _Alignof(UmicomKernelFat16FileTimeEncoding) ||
        UmicomFatPlanOverlap(time, sizeof(*time), outEncoded, sizeof(*outEncoded)))
        return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomKernelFat16FileTimeEncoding encoded;
    const UmicomKernelDiskStatus status = UmicomFatFileTimeFields(time, &encoded);
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outEncoded, &encoded, sizeof(*outEncoded));
    return status;
}

static UmicomKernelDiskStatus UmicomFatFileArguments(UmicomKernelFat16 *volume,
    const char *path, const void *input, UmicomSize bytes,
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16FileUpdateWorkspace *fileWorkspace,
    UmicomKernelFat16UpdatePlan *outDataPlan, UmicomKernelFat16FileUpdatePlan *outFilePlan,
    UmicomSize *outPathBytes)
{
    if (!bytes || bytes > UMICOM_FAT16_UPDATE_BYTES ||
        !UmicomFatPlanSpan(volume, sizeof(*volume)) ||
        !UmicomFatPlanSpan(workspace, sizeof(*workspace)) ||
        !UmicomFatPlanSpan(fileWorkspace, sizeof(*fileWorkspace)) ||
        !UmicomFatPlanSpan(outDataPlan, sizeof(*outDataPlan)) ||
        !UmicomFatPlanSpan(outFilePlan, sizeof(*outFilePlan)) ||
        !UmicomFatPlanSpan(time, sizeof(*time)) ||
        !UmicomFatPlanSpan(input, bytes) ||
        !UmicomFatPlanSpan(path, UMICOM_FAT16_PATH_BYTES) ||
        (UmicomAddress)volume % _Alignof(UmicomKernelFat16) ||
        (UmicomAddress)workspace % _Alignof(UmicomKernelFat16UpdateWorkspace) ||
        (UmicomAddress)fileWorkspace % _Alignof(UmicomKernelFat16FileUpdateWorkspace) ||
        (UmicomAddress)outDataPlan % _Alignof(UmicomKernelFat16UpdatePlan) ||
        (UmicomAddress)outFilePlan % _Alignof(UmicomKernelFat16FileUpdatePlan) ||
        (UmicomAddress)time % _Alignof(UmicomKernelFat16FileTime))
        return UMICOM_DISK_INVALID_ARGUMENT;
    const void *const pointers[] = {volume, workspace, fileWorkspace, outDataPlan,
        outFilePlan, time, input};
    const UmicomSize lengths[] = {sizeof(*volume), sizeof(*workspace), sizeof(*fileWorkspace),
        sizeof(*outDataPlan), sizeof(*outFilePlan), sizeof(*time), bytes};
    for (UmicomSize i = 0U; i < 7U; ++i)
        for (UmicomSize j = i + 1U; j < 7U; ++j)
            if (UmicomFatPlanOverlap(pointers[i], lengths[i], pointers[j], lengths[j]))
                return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    while (pathBytes < UMICOM_FAT16_PATH_BYTES) {
        /* Ownership is checked before reading each byte, so even a path
         * beginning just outside an output cannot scan into that output. */
        for (UmicomSize i = 0U; i < 7U; ++i)
            if (UmicomFatPlanOverlap(path + pathBytes, 1U, pointers[i], lengths[i]))
                return UMICOM_DISK_INVALID_ARGUMENT;
        if (!path[pathBytes]) break;
        ++pathBytes;
    }
    if (pathBytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    if ((workspace->self && workspace->self != workspace) ||
        (fileWorkspace->self && fileWorkspace->self != fileWorkspace)) return UMICOM_DISK_BAD_STATE;
    if (workspace->busy || fileWorkspace->busy) return UMICOM_DISK_BUSY;
    if ((!workspace->self && !UmicomFatPlanZero(workspace, sizeof(*workspace))) ||
        (!fileWorkspace->self && !UmicomFatPlanZero(fileWorkspace, sizeof(*fileWorkspace))))
        return UMICOM_DISK_BAD_STATE;
    *outPathBytes = pathBytes + 1U;
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatFileLookup(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Entry *outEntry, UmicomU16 *outParent)
{
    char names[UMICOM_FAT16_DEPTH_LIMIT][13];
    UmicomFatClear(names, sizeof(names));
    UmicomSize depth = 0U;
    UmicomKernelDiskStatus status = UmicomFatPath(path, names, &depth);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16Entry current;
    UmicomFatClear(&current, sizeof(current));
    current.name[0] = '/'; current.directory = UMICOM_TRUE;
    UmicomU16 parent = 0U;
    for (UmicomSize i = 0U; i < depth; ++i) {
        if (!current.directory) return UMICOM_DISK_NOT_DIRECTORY;
        parent = current.firstCluster;
        status = UmicomFatDirectoryRead(volume, parent);
        if (status != UMICOM_DISK_OK) return status;
        UmicomBoolean found = UMICOM_FALSE;
        for (UmicomSize j = 0U; j < volume->directoryStage.count; ++j) {
            if (UmicomFatStringEqual(names[i], volume->directoryStage.entries[j].name)) {
                current = volume->directoryStage.entries[j]; found = UMICOM_TRUE; break;
            }
        }
        if (!found) return UMICOM_DISK_NOT_FOUND;
    }
    *outEntry = current;
    *outParent = parent;
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatFileDirectorySector(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 parent,
    UmicomKernelFat16FileUpdatePlan *stage)
{
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (parent) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, parent,
            UMICOM_FALSE, 0U, workspace->chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    UmicomU64 relative = 0U;
    for (UmicomU64 i = 0U; i < entries; ++i) {
        if (i % 16U == 0U) {
            relative = volume->rootStart + i / 16U;
            if (parent) {
                const UmicomU64 index = i / (16U * volume->info.sectorsPerCluster);
                if (index >= count) return UMICOM_DISK_CORRUPT;
                relative = volume->dataStart + ((UmicomU64)workspace->chain[index] - 2U) *
                    volume->info.sectorsPerCluster + (i / 16U) % volume->info.sectorsPerCluster;
            }
            const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative, volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *const raw = volume->dataSector + (i % 16U) * 32U;
        if (!raw[0]) break;
        if (raw[0] == 0xe5U || raw[0] == '.' || (raw[11] & 0x08U)) continue;
        char name[13];
        UmicomFatClear(name, sizeof(name));
        const UmicomKernelDiskStatus status = UmicomFatDecodeName(raw, name);
        if (status != UMICOM_DISK_OK) return status;
        if (!UmicomFatStringEqual(name, workspace->stage.entry.name)) continue;
        /* The complete directory was already decoded during lookup and the
         * whole-volume proof. Re-identify its checked target in the physical
         * chain and confirm its identity before retaining the original bytes. */
        if (raw[11] != workspace->stage.entry.attributes ||
            (raw[12] & ~0x18U) || UmicomFat16Word(raw + 20U) ||
            UmicomFat16Word(raw + 26U) != workspace->stage.entry.firstCluster ||
            UmicomFat32Word(raw + 28U) != workspace->stage.entry.bytes)
            return UMICOM_DISK_CORRUPT;
        stage->directorySector = volume->info.firstSector + relative;
        stage->entryOffset = (i % 16U) * 32U;
        UmicomFatCopy(stage->original, volume->dataSector, sizeof(stage->original));
        UmicomFatCopy(stage->data, stage->original, sizeof(stage->data));
        UmicomU8 *const target = stage->data + stage->entryOffset;
        target[11] |= 0x20U;
        target[22] = (UmicomU8)stage->encodedTime.writeTime;
        target[23] = (UmicomU8)(stage->encodedTime.writeTime >> 8U);
        target[24] = (UmicomU8)stage->encodedTime.writeDate;
        target[25] = (UmicomU8)(stage->encodedTime.writeDate >> 8U);
        return UMICOM_DISK_OK;
    }
    return UMICOM_DISK_CORRUPT; /* An already checked target cannot disappear. */
}

UmicomKernelDiskStatus UmicomKernelFat16PlanFileUpdate(UmicomKernelFat16 *volume,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16FileUpdateWorkspace *fileWorkspace,
    UmicomKernelFat16UpdatePlan *outDataPlan, UmicomKernelFat16FileUpdatePlan *outFilePlan)
{
    UmicomSize pathBytes = 0U;
    UmicomKernelDiskStatus status = UmicomFatFileArguments(volume, path, input, bytes,
        time, workspace, fileWorkspace, outDataPlan, outFilePlan, &pathBytes);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16FileTimeEncoding encoded;
    status = UmicomFatFileTimeFields(time, &encoded);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    workspace->busy = UMICOM_TRUE;
    UmicomFatClear(fileWorkspace, sizeof(*fileWorkspace));
    fileWorkspace->self = fileWorkspace;
    fileWorkspace->busy = UMICOM_TRUE;
    /* These copies finish before geometry, lookup or any other operation can
     * reach the reader. Callbacks cannot change this operation's input value. */
    UmicomFatCopy(workspace->input, input, bytes);
    UmicomFatCopy(fileWorkspace->path, path, pathBytes);
    UmicomFatCopy(&fileWorkspace->stage.requestedTime, time, sizeof(*time));
    UmicomFatCopy(&fileWorkspace->stage.encodedTime, &encoded, sizeof(encoded));
    UmicomU16 parent = 0U;
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK) status = UmicomFatFileLookup(volume,
        fileWorkspace->path, &workspace->stage.entry, &parent);
    if (status == UMICOM_DISK_OK && workspace->stage.entry.directory) status = UMICOM_DISK_IS_DIRECTORY;
    if (status == UMICOM_DISK_OK && (workspace->stage.entry.attributes & 0x01U)) status = UMICOM_DISK_READ_ONLY;
    if (status == UMICOM_DISK_OK && (offset > workspace->stage.entry.bytes ||
        bytes > (UmicomU64)workspace->stage.entry.bytes - offset)) status = UMICOM_DISK_RANGE;
    if (status == UMICOM_DISK_OK) {
        const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
        const UmicomU64 required = ((UmicomU64)workspace->stage.entry.bytes + clusterBytes - 1U) / clusterBytes;
        status = UmicomFatChain(volume, workspace->stage.entry.firstCluster, UMICOM_TRUE,
            required, workspace->targetChain, &workspace->targetClusters);
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatFileDirectorySector(volume,
        workspace, parent, &fileWorkspace->stage);
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanSectors(volume, workspace, offset, bytes);
    if (status == UMICOM_DISK_OK) {
        UmicomFatCopy(outDataPlan, &workspace->stage, sizeof(*outDataPlan));
        UmicomFatCopy(outFilePlan, &fileWorkspace->stage, sizeof(*outFilePlan));
    }
    /* Neither result is published after a failed read or validation. Scrub
     * staged caller data, calendar fields and directory bytes on every exit. */
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    UmicomFatClear(fileWorkspace, sizeof(*fileWorkspace));
    fileWorkspace->self = fileWorkspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}

/*-----------------------------------------------------------------------------
 * Persisted short-entry metadata snapshots.
 *
 * The earlier writer's retained result describes an operation. This read path
 * independently decodes the directory words which a later inspector actually
 * observes. Existing Entry/VFS layouts and ordinary read-only interpretation
 * remain intact; neither a malformed calendar nor this value snapshot grants
 * permission to write, repair or trust the allocation beyond the stated path.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_metadata.h"

UmicomKernelDiskStatus UmicomKernelFat16TimestampDecode(UmicomU16 rawTime,
    UmicomU16 rawDate, UmicomKernelFat16Timestamp *outTimestamp)
{
    if (!UmicomFatPlanSpan(outTimestamp, sizeof(*outTimestamp)) ||
        (UmicomAddress)outTimestamp % _Alignof(UmicomKernelFat16Timestamp))
        return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomKernelFat16Timestamp stage;
    UmicomFatClear(&stage, sizeof(stage));
    stage.rawTime = rawTime;
    stage.rawDate = rawDate;
    if (rawTime || rawDate) {
        stage.state = UMICOM_FAT16_TIMESTAMP_INVALID;
        UmicomKernelFat16FileTime value;
        UmicomFatClear(&value, sizeof(value));
        value.year = (UmicomU16)(1980U + (rawDate >> 9U));
        value.month = (UmicomU16)((rawDate >> 5U) & 0x0fU);
        value.day = (UmicomU16)(rawDate & 0x1fU);
        value.hour = (UmicomU16)(rawTime >> 11U);
        value.minute = (UmicomU16)((rawTime >> 5U) & 0x3fU);
        value.second = (UmicomU16)((rawTime & 0x1fU) * 2U);
        UmicomKernelFat16FileTimeEncoding encoded;
        /* Reuse the same Gregorian field checks as explicit write planning.
         * A nonzero time with a zero date and the two reserved second values
         * remain malformed observations, never silently normalised calendars. */
        if (UmicomFatFileTimeFields(&value, &encoded) == UMICOM_DISK_OK) {
            UmicomFatCopy(&stage.value, &value, sizeof(value));
            stage.state = UMICOM_FAT16_TIMESTAMP_VALID;
        }
    }
    UmicomFatCopy(outTimestamp, &stage, sizeof(stage));
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatMetadataArguments(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Metadata *outMetadata, UmicomSize *outPathBytes)
{
    if (!UmicomFatPlanSpan(volume, sizeof(*volume)) ||
        !UmicomFatPlanSpan(outMetadata, sizeof(*outMetadata)) ||
        !UmicomFatPlanSpan(path, UMICOM_FAT16_PATH_BYTES) ||
        (UmicomAddress)volume % _Alignof(UmicomKernelFat16) ||
        (UmicomAddress)outMetadata % _Alignof(UmicomKernelFat16Metadata) ||
        UmicomFatPlanOverlap(volume, sizeof(*volume), outMetadata, sizeof(*outMetadata)))
        return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize bytes = 0U;
    while (bytes < UMICOM_FAT16_PATH_BYTES) {
        /* Check ownership before dereferencing each path byte. A path which
         * starts just outside an output or owner must not scan into it while
         * searching for a terminator, including the terminator byte itself. */
        const char *const current = path + bytes;
        if (UmicomFatPlanOverlap(current, 1U, volume, sizeof(*volume)) ||
            UmicomFatPlanOverlap(current, 1U, outMetadata, sizeof(*outMetadata)))
            return UMICOM_DISK_INVALID_ARGUMENT;
        if (!path[bytes]) {
            *outPathBytes = bytes + 1U;
            return UMICOM_DISK_OK;
        }
        ++bytes;
    }
    return UMICOM_DISK_LIMIT;
}

static UmicomKernelDiskStatus UmicomFatMetadataGeometry(const UmicomKernelFat16 *volume)
{
    /* Open originally checked these arithmetic relationships. Recheck them
     * before public owner fields become divisors or sector offsets, so damaged
     * trusted storage produces an explicit refusal. Unlike writable planning,
     * ordinary inspection accepts an oversized but geometrically valid FAT. */
    const UmicomKernelFat16Info *const info = &volume->info;
    if (!volume->reader.read || !volume->reader.sectors || !info->firstSector ||
        !info->volumeSectors || !volume->partitionSectors ||
        info->firstSector >= volume->reader.sectors ||
        volume->partitionSectors > volume->reader.sectors - info->firstSector ||
        info->volumeSectors > volume->partitionSectors ||
        !info->sectorsPerCluster || info->sectorsPerCluster > 64U ||
        (info->sectorsPerCluster & (info->sectorsPerCluster - 1U)) ||
        !info->sectorsPerFat || !info->rootEntries || info->rootEntries % 16U ||
        info->rootEntries > UMICOM_FAT16_SCAN_ENTRIES ||
        info->clusters < 4085U || info->clusters >= 0xffefU ||
        !volume->fatStart || volume->fatStart >= info->volumeSectors)
        return UMICOM_DISK_BAD_STATE;
    if ((UmicomU64)info->sectorsPerFat * 2U > info->volumeSectors - volume->fatStart)
        return UMICOM_DISK_BAD_STATE;
    const UmicomU64 root = volume->fatStart + (UmicomU64)info->sectorsPerFat * 2U;
    if (root >= info->volumeSectors || volume->rootStart != root ||
        (UmicomU64)info->rootEntries / 16U >= info->volumeSectors - root)
        return UMICOM_DISK_BAD_STATE;
    const UmicomU64 data = root + (UmicomU64)info->rootEntries / 16U;
    if (volume->dataStart != data ||
        (info->volumeSectors - data) / info->sectorsPerCluster != info->clusters ||
        ((UmicomU64)info->clusters + 2U) * 2U >
            (UmicomU64)info->sectorsPerFat * UMICOM_DISK_SECTOR_BYTES)
        return UMICOM_DISK_BAD_STATE;
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatMetadataTimestamp(UmicomKernelFat16 *volume,
    UmicomU16 parent, const UmicomKernelFat16Entry *entry,
    UmicomKernelFat16Timestamp *outTimestamp)
{
    UmicomU16 chain[UMICOM_FAT16_CHAIN_LIMIT];
    UmicomFatClear(chain, sizeof(chain));
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (parent) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, parent,
            UMICOM_FALSE, 0U, chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    for (UmicomU64 i = 0U; i < entries; ++i) {
        if (i % 16U == 0U) {
            UmicomU64 relative = volume->rootStart + i / 16U;
            if (parent) {
                const UmicomU64 index = i / (16U * volume->info.sectorsPerCluster);
                if (index >= count) return UMICOM_DISK_CORRUPT;
                relative = volume->dataStart + ((UmicomU64)chain[index] - 2U) *
                    volume->info.sectorsPerCluster + (i / 16U) % volume->info.sectorsPerCluster;
            }
            const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative,
                volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *const raw = volume->dataSector + (i % 16U) * 32U;
        if (!raw[0]) break;
        /* These are the established read-only rules: deleted records, dot
         * entries, labels and long-name records do not become short entries. */
        if (raw[0] == 0xe5U || raw[0] == '.' || (raw[11] & 0x08U)) continue;
        char name[13];
        UmicomFatClear(name, sizeof(name));
        const UmicomKernelDiskStatus status = UmicomFatDecodeName(raw, name);
        if (status != UMICOM_DISK_OK) return status;
        if (!UmicomFatStringEqual(name, entry->name)) continue;
        /* Lookup decoded the complete parent directory before this physical
         * pass. Confirm the selected identity again before copying its time;
         * scratch bytes from a failed or different record are never published. */
        if (raw[11] != entry->attributes || (raw[12] & ~0x18U) ||
            UmicomFat16Word(raw + 20U) ||
            UmicomFat16Word(raw + 26U) != entry->firstCluster ||
            UmicomFat32Word(raw + 28U) != entry->bytes)
            return UMICOM_DISK_CORRUPT;
        return UmicomKernelFat16TimestampDecode(UmicomFat16Word(raw + 22U),
            UmicomFat16Word(raw + 24U), outTimestamp);
    }
    return UMICOM_DISK_CORRUPT; /* An already resolved short entry cannot vanish. */
}

UmicomKernelDiskStatus UmicomKernelFat16MetadataRead(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Metadata *outMetadata)
{
    UmicomSize pathBytes = 0U;
    UmicomKernelDiskStatus status = UmicomFatMetadataArguments(volume, path,
        outMetadata, &pathBytes);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    char selected[UMICOM_FAT16_PATH_BYTES];
    UmicomFatClear(selected, sizeof(selected));
    UmicomFatCopy(selected, path, pathBytes);
    UmicomKernelFat16Metadata stage;
    UmicomFatClear(&stage, sizeof(stage));
    UmicomU16 parent = 0U;
    status = UmicomFatMetadataGeometry(volume);
    if (status == UMICOM_DISK_OK)
        status = UmicomFatFileLookup(volume, selected, &stage.entry, &parent);
    if (status == UMICOM_DISK_OK && stage.entry.name[0] != '/') {
        status = UmicomFatMetadataTimestamp(volume, parent, &stage.entry,
            &stage.writeTimestamp);
        if (status == UMICOM_DISK_OK) stage.directoryEntryPresent = UMICOM_TRUE;
    }
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outMetadata, &stage, sizeof(stage));
    UmicomFatClear(selected, sizeof(selected));
    UmicomFatClear(&stage, sizeof(stage));
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}

/*-----------------------------------------------------------------------------
 * Existing-allocation append preparation.
 *
 * Appending within the current final cluster leaves the exact chain length
 * unchanged. Prove the original namespace and allocation before staging the
 * enlarged directory size, and reuse the checked sector planner at the old
 * EOF. Every prior byte and all unused slack outside the payload are retained.
 * No allocation, transport mutation or clean publication happens here.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_file_append.h"

UmicomKernelDiskStatus UmicomKernelFat16PlanFileAppend(
    UmicomKernelFat16 *volume, const char *path, const void *input,
    UmicomSize bytes, const UmicomKernelFat16FileTime *time,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16FileUpdateWorkspace *fileWorkspace,
    UmicomKernelFat16UpdatePlan *outDataPlan,
    UmicomKernelFat16FileUpdatePlan *outFilePlan)
{
    UmicomSize pathBytes = 0U;
    UmicomKernelDiskStatus status = UmicomFatFileArguments(volume, path, input,
        bytes, time, workspace, fileWorkspace, outDataPlan, outFilePlan, &pathBytes);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16FileTimeEncoding encoded;
    status = UmicomFatFileTimeFields(time, &encoded);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    workspace->busy = UMICOM_TRUE;
    UmicomFatClear(fileWorkspace, sizeof(*fileWorkspace));
    fileWorkspace->self = fileWorkspace;
    fileWorkspace->busy = UMICOM_TRUE;
    /* Callback-visible work begins only after these value snapshots. */
    UmicomFatCopy(workspace->input, input, bytes);
    UmicomFatCopy(fileWorkspace->path, path, pathBytes);
    UmicomFatCopy(&fileWorkspace->stage.requestedTime, time, sizeof(*time));
    UmicomFatCopy(&fileWorkspace->stage.encodedTime, &encoded, sizeof(encoded));
    UmicomU16 parent = 0U;
    UmicomU64 oldBytes = 0U;
    UmicomU64 newBytes = 0U;
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK) status = UmicomFatFileLookup(volume,
        fileWorkspace->path, &workspace->stage.entry, &parent);
    if (status == UMICOM_DISK_OK && workspace->stage.entry.directory)
        status = UMICOM_DISK_IS_DIRECTORY;
    if (status == UMICOM_DISK_OK && (workspace->stage.entry.attributes & 0x01U))
        status = UMICOM_DISK_READ_ONLY;
    if (status == UMICOM_DISK_OK) {
        oldBytes = workspace->stage.entry.bytes;
        const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster *
            UMICOM_DISK_SECTOR_BYTES;
        const UmicomU64 required = (oldBytes + clusterBytes - 1U) / clusterBytes;
        const UmicomU64 allocatedBytes = required * clusterBytes;
        newBytes = oldBytes + bytes;
        /* Geometry bounds clusterBytes and the entry size is U32. These U64
         * calculations cannot wrap. A boundary EOF has no final-cluster slack;
         * a valid empty file has no allocated cluster to admit an append. */
        if (!oldBytes || !workspace->stage.entry.firstCluster ||
            newBytes > 0xffffffffU || newBytes > allocatedBytes)
            status = UMICOM_DISK_RANGE;
        else status = UmicomFatChain(volume, workspace->stage.entry.firstCluster,
            UMICOM_TRUE, required, workspace->targetChain, &workspace->targetClusters);
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatFileDirectorySector(volume,
        workspace, parent, &fileWorkspace->stage);
    if (status == UMICOM_DISK_OK) {
        UmicomU8 *const target = fileWorkspace->stage.data + fileWorkspace->stage.entryOffset;
        target[28] = (UmicomU8)newBytes;
        target[29] = (UmicomU8)(newBytes >> 8U);
        target[30] = (UmicomU8)(newBytes >> 16U);
        target[31] = (UmicomU8)(newBytes >> 24U);
        status = UmicomFatPlanSectors(volume, workspace, oldBytes, bytes);
    }
    if (status == UMICOM_DISK_OK) {
        UmicomFatCopy(outDataPlan, &workspace->stage, sizeof(*outDataPlan));
        UmicomFatCopy(outFilePlan, &fileWorkspace->stage, sizeof(*outFilePlan));
    }
    /* Publish neither partial plan after a failed validation or read. The
     * original entry remains authoritative until both complete plans exist. */
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    UmicomFatClear(fileWorkspace, sizeof(*fileWorkspace));
    fileWorkspace->self = fileWorkspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}

/*-----------------------------------------------------------------------------
 * Same-parent short-name rename preparation.
 *
 * Namespace changes still require the writer's complete ownership proof, even
 * though no data or FAT link changes. A dedicated value plan avoids inventing
 * a file-data operation merely to transport one checked directory sector.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_rename.h"

static UmicomKernelDiskStatus UmicomFatRenameArguments(UmicomKernelFat16 *volume,
    const char *path, const char *newName, UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16RenameWorkspace *renameWorkspace, UmicomKernelFat16RenamePlan *outPlan,
    UmicomSize *outPathBytes, UmicomSize *outNameBytes)
{
    if (!UmicomFatPlanSpan(volume, sizeof(*volume)) ||
        !UmicomFatPlanSpan(workspace, sizeof(*workspace)) ||
        !UmicomFatPlanSpan(renameWorkspace, sizeof(*renameWorkspace)) ||
        !UmicomFatPlanSpan(outPlan, sizeof(*outPlan)) ||
        !UmicomFatPlanSpan(path, UMICOM_FAT16_PATH_BYTES) ||
        !UmicomFatPlanSpan(newName, 13U) ||
        (UmicomAddress)volume % _Alignof(UmicomKernelFat16) ||
        (UmicomAddress)workspace % _Alignof(UmicomKernelFat16UpdateWorkspace) ||
        (UmicomAddress)renameWorkspace % _Alignof(UmicomKernelFat16RenameWorkspace) ||
        (UmicomAddress)outPlan % _Alignof(UmicomKernelFat16RenamePlan))
        return UMICOM_DISK_INVALID_ARGUMENT;
    const void *const pointers[] = {volume, workspace, renameWorkspace, outPlan};
    const UmicomSize lengths[] = {sizeof(*volume), sizeof(*workspace),
        sizeof(*renameWorkspace), sizeof(*outPlan)};
    for (UmicomSize i = 0U; i < 4U; ++i)
        for (UmicomSize j = i + 1U; j < 4U; ++j)
            if (UmicomFatPlanOverlap(pointers[i], lengths[i], pointers[j], lengths[j]))
                return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    while (pathBytes < UMICOM_FAT16_PATH_BYTES) {
        for (UmicomSize i = 0U; i < 4U; ++i)
            if (UmicomFatPlanOverlap(path + pathBytes, 1U, pointers[i], lengths[i]))
                return UMICOM_DISK_INVALID_ARGUMENT;
        /* Protect the replacement's start before discovering its full extent.
         * The second scan then checks against this complete terminated path. */
        if (UmicomFatPlanOverlap(path + pathBytes, 1U, newName, 1U))
            return UMICOM_DISK_INVALID_ARGUMENT;
        if (!path[pathBytes]) break;
        ++pathBytes;
    }
    if (pathBytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    ++pathBytes;
    UmicomSize nameBytes = 0U;
    while (nameBytes < 13U) {
        for (UmicomSize i = 0U; i < 4U; ++i)
            if (UmicomFatPlanOverlap(newName + nameBytes, 1U, pointers[i], lengths[i]))
                return UMICOM_DISK_INVALID_ARGUMENT;
        if (UmicomFatPlanOverlap(newName + nameBytes, 1U, path, pathBytes))
            return UMICOM_DISK_INVALID_ARGUMENT;
        if (!newName[nameBytes]) break;
        ++nameBytes;
    }
    if (nameBytes == 13U) return UMICOM_DISK_INVALID_ARGUMENT;
    ++nameBytes;
    if ((workspace->self && workspace->self != workspace) ||
        (renameWorkspace->self && renameWorkspace->self != renameWorkspace))
        return UMICOM_DISK_BAD_STATE;
    if (workspace->busy || renameWorkspace->busy) return UMICOM_DISK_BUSY;
    if ((!workspace->self && !UmicomFatPlanZero(workspace, sizeof(*workspace))) ||
        (!renameWorkspace->self && !UmicomFatPlanZero(renameWorkspace, sizeof(*renameWorkspace))))
        return UMICOM_DISK_BAD_STATE;
    *outPathBytes = pathBytes;
    *outNameBytes = nameBytes;
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatRenameName(const char *newName,
    UmicomSize nameBytes, char *canonical, UmicomU8 *raw)
{
    char path[14];
    char names[UMICOM_FAT16_DEPTH_LIMIT][13];
    UmicomFatClear(path, sizeof(path));
    UmicomFatClear(names, sizeof(names));
    path[0] = '/';
    UmicomFatCopy(path + 1U, newName, nameBytes);
    UmicomSize depth = 0U;
    const UmicomKernelDiskStatus status = UmicomFatPath(path, names, &depth);
    if (status != UMICOM_DISK_OK || depth != 1U) return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomFatCopy(canonical, names[0], 13U);
    for (UmicomSize i = 0U; i < 11U; ++i) raw[i] = ' ';
    UmicomSize position = 0U;
    for (UmicomSize i = 0U; canonical[i]; ++i) {
        if (canonical[i] == '.') position = 8U;
        else raw[position++] = (UmicomU8)canonical[i];
    }
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatRenameDirectorySector(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 parent,
    const UmicomU8 *newRawName, UmicomKernelFat16RenamePlan *stage)
{
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (parent) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, parent,
            UMICOM_FALSE, 0U, workspace->chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    UmicomU64 relative = 0U;
    for (UmicomU64 i = 0U; i < entries; ++i) {
        if (i % 16U == 0U) {
            relative = volume->rootStart + i / 16U;
            if (parent) {
                const UmicomU64 index = i / (16U * volume->info.sectorsPerCluster);
                if (index >= count) return UMICOM_DISK_CORRUPT;
                relative = volume->dataStart + ((UmicomU64)workspace->chain[index] - 2U) *
                    volume->info.sectorsPerCluster + (i / 16U) % volume->info.sectorsPerCluster;
            }
            const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative, volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *const raw = volume->dataSector + (i % 16U) * 32U;
        if (!raw[0]) break;
        if (raw[0] == 0xe5U || raw[0] == '.' || (raw[11] & 0x08U)) continue;
        char name[13];
        UmicomFatClear(name, sizeof(name));
        const UmicomKernelDiskStatus status = UmicomFatDecodeName(raw, name);
        if (status != UMICOM_DISK_OK) return status;
        if (!UmicomFatStringEqual(name, stage->originalEntry.name)) continue;
        /* Lookup and the namespace proof checked the whole parent. Re-identify
         * the selected record physically before retaining its complete sector;
         * preserve its calendar and attributes even when ARCHIVE is clear. */
        if (raw[11] != stage->originalEntry.attributes || (raw[12] & ~0x18U) ||
            UmicomFat16Word(raw + 20U) ||
            UmicomFat16Word(raw + 26U) != stage->originalEntry.firstCluster ||
            UmicomFat32Word(raw + 28U) != stage->originalEntry.bytes)
            return UMICOM_DISK_CORRUPT;
        stage->directorySector = volume->info.firstSector + relative;
        stage->entryOffset = (i % 16U) * 32U;
        UmicomFatCopy(stage->original, volume->dataSector, sizeof(stage->original));
        UmicomFatCopy(stage->data, stage->original, sizeof(stage->data));
        UmicomU8 *const target = stage->data + stage->entryOffset;
        UmicomFatCopy(target, newRawName, 11U);
        target[12] &= (UmicomU8)~0x18U;
        return UMICOM_DISK_OK;
    }
    return UMICOM_DISK_CORRUPT;
}

UmicomKernelDiskStatus UmicomKernelFat16PlanRename(UmicomKernelFat16 *volume,
    const char *path, const char *newName, UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16RenameWorkspace *renameWorkspace, UmicomKernelFat16RenamePlan *outPlan)
{
    UmicomSize pathBytes = 0U, nameBytes = 0U;
    UmicomKernelDiskStatus status = UmicomFatRenameArguments(volume, path, newName,
        workspace, renameWorkspace, outPlan, &pathBytes, &nameBytes);
    if (status != UMICOM_DISK_OK) return status;
    char canonical[13];
    UmicomU8 rawName[11];
    UmicomFatClear(canonical, sizeof(canonical));
    UmicomFatClear(rawName, sizeof(rawName));
    status = UmicomFatRenameName(newName, nameBytes, canonical, rawName);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    workspace->busy = UMICOM_TRUE;
    UmicomFatClear(renameWorkspace, sizeof(*renameWorkspace));
    renameWorkspace->self = renameWorkspace;
    renameWorkspace->busy = UMICOM_TRUE;
    /* All input values are owned snapshots before any reader callback. */
    UmicomFatCopy(renameWorkspace->path, path, pathBytes);
    UmicomFatCopy(renameWorkspace->newName, canonical, sizeof(canonical));
    UmicomFatCopy(renameWorkspace->stage.updatedName, canonical, sizeof(canonical));
    UmicomKernelFat16Entry *const entry = &renameWorkspace->stage.originalEntry;
    UmicomU16 parent = 0U;
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK)
        status = UmicomFatFileLookup(volume, renameWorkspace->path, entry, &parent);
    if (status == UMICOM_DISK_OK && entry->directory) status = UMICOM_DISK_IS_DIRECTORY;
    if (status == UMICOM_DISK_OK && (entry->attributes & 0x01U)) status = UMICOM_DISK_READ_ONLY;
    if (status == UMICOM_DISK_OK) {
        /* The final lookup leaves the complete source parent in this staging
         * listing. Include the source itself: case-only renames are explicit
         * collisions, never a no-op which acquires mutation authority. */
        for (UmicomSize i = 0U; i < volume->directoryStage.count; ++i) {
            if (UmicomFatStringEqual(volume->directoryStage.entries[i].name,
                renameWorkspace->newName)) {
                status = UMICOM_DISK_EXISTS;
                break;
            }
        }
    }
    if (status == UMICOM_DISK_OK) {
        const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
        const UmicomU64 required = ((UmicomU64)entry->bytes + clusterBytes - 1U) / clusterBytes;
        status = UmicomFatChain(volume, entry->firstCluster, UMICOM_TRUE, required,
            workspace->targetChain, &workspace->targetClusters);
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatRenameDirectorySector(volume,
        workspace, parent, rawName, &renameWorkspace->stage);
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outPlan, &renameWorkspace->stage, sizeof(*outPlan));
    UmicomFatClear(canonical, sizeof(canonical));
    UmicomFatClear(rawName, sizeof(rawName));
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace;
    UmicomFatClear(renameWorkspace, sizeof(*renameWorkspace));
    renameWorkspace->self = renameWorkspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}

/*-----------------------------------------------------------------------------
 * Bounded regular-file lifecycle preparation.
 *
 * Keep all established APIs above intact. Allocation changes have their own
 * complete-sector plan and scratch, including final header images, so an
 * exclusive transport can distinguish allocation from data and directory work.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_lifecycle.h"

static void UmicomFatLifecyclePut16(UmicomU8 *target, UmicomU16 value)
{
    target[0] = (UmicomU8)value;
    target[1] = (UmicomU8)(value >> 8U);
}
static void UmicomFatLifecyclePut32(UmicomU8 *target, UmicomU32 value)
{
    for (UmicomSize i = 0U; i < 4U; ++i) target[i] = (UmicomU8)(value >> (i * 8U));
}
/* Directory requests now pass the same disjoint-buffer and calendar admission as file requests. The preceding implementation is retained below for
 * source review; only the implementation after this disabled block executes. */
#if 0
static UmicomKernelDiskStatus UmicomFatLifecycleArguments(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan, UmicomSize *outPathBytes,
    UmicomKernelFat16FileTimeEncoding *encoded)
{
    const void *const owners[] = {volume, request, workspace, lifecycleWorkspace, outPlan};
    const UmicomSize lengths[] = {sizeof(*volume), sizeof(*request), sizeof(*workspace),
        sizeof(*lifecycleWorkspace), sizeof(*outPlan)};
    const UmicomSize alignments[] = {_Alignof(UmicomKernelFat16),
        _Alignof(UmicomKernelFat16LifecycleRequest), _Alignof(UmicomKernelFat16UpdateWorkspace),
        _Alignof(UmicomKernelFat16LifecycleWorkspace), _Alignof(UmicomKernelFat16LifecyclePlan)};
    for (UmicomSize i = 0U; i < 5U; ++i) {
        if (!UmicomFatPlanSpan(owners[i], lengths[i]) || (UmicomAddress)owners[i] % alignments[i])
            return UMICOM_DISK_INVALID_ARGUMENT;
        for (UmicomSize j = 0U; j < i; ++j)
            if (UmicomFatPlanOverlap(owners[i], lengths[i], owners[j], lengths[j]))
                return UMICOM_DISK_INVALID_ARGUMENT;
    }
    /* Only after validating the request's independent storage may its fields
     * become addresses or lengths. No protected output byte is probed first. */
    if (!UmicomFatPlanSpan(request->path, UMICOM_FAT16_PATH_BYTES) ||
        request->bytes > UMICOM_FAT16_UPDATE_BYTES ||
        (request->bytes ? !UmicomFatPlanSpan(request->input, request->bytes) : request->input != 0))
        return UMICOM_DISK_INVALID_ARGUMENT;
    switch (request->operation) {
    case UMICOM_FAT16_LIFECYCLE_CREATE:
        if (request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_APPEND:
        if (!request->bytes || request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_TRUNCATE:
        if (request->bytes) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_DELETE:
        if (request->bytes || request->size || request->time.year || request->time.month ||
            request->time.day || request->time.hour || request->time.minute || request->time.second)
            return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    default: return UMICOM_DISK_INVALID_ARGUMENT;
    }
    for (UmicomSize i = 0U; request->bytes && i < 5U; ++i)
        if (UmicomFatPlanOverlap(request->input, request->bytes, owners[i], lengths[i]))
            return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    while (pathBytes < UMICOM_FAT16_PATH_BYTES) {
        for (UmicomSize i = 0U; i < 5U; ++i)
            if (UmicomFatPlanOverlap(request->path + pathBytes, 1U, owners[i], lengths[i]))
                return UMICOM_DISK_INVALID_ARGUMENT;
        if (request->bytes && UmicomFatPlanOverlap(request->path + pathBytes, 1U,
            request->input, request->bytes)) return UMICOM_DISK_INVALID_ARGUMENT;
        if (!request->path[pathBytes]) break;
        ++pathBytes;
    }
    if (pathBytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    if ((workspace->self && workspace->self != workspace) ||
        (lifecycleWorkspace->self && lifecycleWorkspace->self != lifecycleWorkspace))
        return UMICOM_DISK_BAD_STATE;
    if (workspace->busy || lifecycleWorkspace->busy) return UMICOM_DISK_BUSY;
    if ((!workspace->self && !UmicomFatPlanZero(workspace, sizeof(*workspace))) ||
        (!lifecycleWorkspace->self && !UmicomFatPlanZero(lifecycleWorkspace, sizeof(*lifecycleWorkspace))))
        return UMICOM_DISK_BAD_STATE;
    UmicomFatClear(encoded, sizeof(*encoded));
    if (request->operation != UMICOM_FAT16_LIFECYCLE_DELETE) {
        const UmicomKernelDiskStatus status = UmicomFatFileTimeFields(&request->time, encoded);
        if (status != UMICOM_DISK_OK) return status;
    }
    *outPathBytes = pathBytes + 1U;
    return UMICOM_DISK_OK;
}
#endif

/* Move preparation validates a second independent path before any callbacks.
 * The preceding implementation is retained for engineering review. */
#if 0
static UmicomKernelDiskStatus UmicomFatLifecycleArguments(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan, UmicomSize *outPathBytes,
    UmicomKernelFat16FileTimeEncoding *encoded)
{
    const void *const owners[] = {volume, request, workspace, lifecycleWorkspace, outPlan};
    const UmicomSize lengths[] = {sizeof(*volume), sizeof(*request), sizeof(*workspace),
        sizeof(*lifecycleWorkspace), sizeof(*outPlan)};
    const UmicomSize alignments[] = {_Alignof(UmicomKernelFat16),
        _Alignof(UmicomKernelFat16LifecycleRequest), _Alignof(UmicomKernelFat16UpdateWorkspace),
        _Alignof(UmicomKernelFat16LifecycleWorkspace), _Alignof(UmicomKernelFat16LifecyclePlan)};
    for (UmicomSize i = 0U; i < 5U; ++i) {
        if (!UmicomFatPlanSpan(owners[i], lengths[i]) || (UmicomAddress)owners[i] % alignments[i])
            return UMICOM_DISK_INVALID_ARGUMENT;
        for (UmicomSize j = 0U; j < i; ++j)
            if (UmicomFatPlanOverlap(owners[i], lengths[i], owners[j], lengths[j]))
                return UMICOM_DISK_INVALID_ARGUMENT;
    }
    /* Only after validating the request's independent storage may its fields
     * become addresses or lengths. No protected output byte is probed first. */
    if (!UmicomFatPlanSpan(request->path, UMICOM_FAT16_PATH_BYTES) ||
        request->bytes > UMICOM_FAT16_UPDATE_BYTES ||
        (request->bytes ? !UmicomFatPlanSpan(request->input, request->bytes) : request->input != 0))
        return UMICOM_DISK_INVALID_ARGUMENT;
    switch (request->operation) {
    case UMICOM_FAT16_LIFECYCLE_CREATE:
        if (request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_APPEND:
        if (!request->bytes || request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_TRUNCATE:
        if (request->bytes) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY:
        /* Directory bytes belong to the filesystem, never to caller payload. */
        if (request->bytes || request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY:
    case UMICOM_FAT16_LIFECYCLE_DELETE:
        if (request->bytes || request->size || request->time.year || request->time.month ||
            request->time.day || request->time.hour || request->time.minute || request->time.second)
            return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    default: return UMICOM_DISK_INVALID_ARGUMENT;
    }
    for (UmicomSize i = 0U; request->bytes && i < 5U; ++i)
        if (UmicomFatPlanOverlap(request->input, request->bytes, owners[i], lengths[i]))
            return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    while (pathBytes < UMICOM_FAT16_PATH_BYTES) {
        for (UmicomSize i = 0U; i < 5U; ++i)
            if (UmicomFatPlanOverlap(request->path + pathBytes, 1U, owners[i], lengths[i]))
                return UMICOM_DISK_INVALID_ARGUMENT;
        if (request->bytes && UmicomFatPlanOverlap(request->path + pathBytes, 1U,
            request->input, request->bytes)) return UMICOM_DISK_INVALID_ARGUMENT;
        if (!request->path[pathBytes]) break;
        ++pathBytes;
    }
    if (pathBytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    if ((workspace->self && workspace->self != workspace) ||
        (lifecycleWorkspace->self && lifecycleWorkspace->self != lifecycleWorkspace))
        return UMICOM_DISK_BAD_STATE;
    if (workspace->busy || lifecycleWorkspace->busy) return UMICOM_DISK_BUSY;
    if ((!workspace->self && !UmicomFatPlanZero(workspace, sizeof(*workspace))) ||
        (!lifecycleWorkspace->self && !UmicomFatPlanZero(lifecycleWorkspace, sizeof(*lifecycleWorkspace))))
        return UMICOM_DISK_BAD_STATE;
    UmicomFatClear(encoded, sizeof(*encoded));
    if (request->operation != UMICOM_FAT16_LIFECYCLE_DELETE &&
        request->operation != UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY) {
        const UmicomKernelDiskStatus status = UmicomFatFileTimeFields(&request->time, encoded);
        if (status != UMICOM_DISK_OK) return status;
    }
    *outPathBytes = pathBytes + 1U;
    return UMICOM_DISK_OK;
}
#endif

static UmicomKernelDiskStatus UmicomFatLifecycleArguments(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan, UmicomSize *outPathBytes,
    UmicomKernelFat16FileTimeEncoding *encoded, UmicomSize *outDestinationBytes)
{
    const void *const owners[] = {volume, request, workspace, lifecycleWorkspace, outPlan};
    const UmicomSize lengths[] = {sizeof(*volume), sizeof(*request), sizeof(*workspace),
        sizeof(*lifecycleWorkspace), sizeof(*outPlan)};
    const UmicomSize alignments[] = {_Alignof(UmicomKernelFat16),
        _Alignof(UmicomKernelFat16LifecycleRequest), _Alignof(UmicomKernelFat16UpdateWorkspace),
        _Alignof(UmicomKernelFat16LifecycleWorkspace), _Alignof(UmicomKernelFat16LifecyclePlan)};
    for (UmicomSize i = 0U; i < 5U; ++i) {
        if (!UmicomFatPlanSpan(owners[i], lengths[i]) || (UmicomAddress)owners[i] % alignments[i])
            return UMICOM_DISK_INVALID_ARGUMENT;
        for (UmicomSize j = 0U; j < i; ++j)
            if (UmicomFatPlanOverlap(owners[i], lengths[i], owners[j], lengths[j]))
                return UMICOM_DISK_INVALID_ARGUMENT;
    }
    /* Only after validating the request's independent storage may its fields
     * become addresses or lengths. No protected output byte is probed first. */
    if (!UmicomFatPlanSpan(request->path, UMICOM_FAT16_PATH_BYTES) ||
        request->bytes > UMICOM_FAT16_UPDATE_BYTES ||
        (request->bytes ? !UmicomFatPlanSpan(request->input, request->bytes) : request->input != 0))
        return UMICOM_DISK_INVALID_ARGUMENT;
    switch (request->operation) {
    case UMICOM_FAT16_LIFECYCLE_CREATE:
        if (request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_APPEND:
        if (!request->bytes || request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_TRUNCATE:
        if (request->bytes) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY:
        /* Directory bytes belong to the filesystem, never to caller payload. */
        if (request->bytes || request->size) return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    case UMICOM_FAT16_LIFECYCLE_MOVE:
    case UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY:
    case UMICOM_FAT16_LIFECYCLE_DELETE:
        if (request->bytes || request->size || request->time.year || request->time.month ||
            request->time.day || request->time.hour || request->time.minute || request->time.second)
            return UMICOM_DISK_INVALID_ARGUMENT;
        break;
    default: return UMICOM_DISK_INVALID_ARGUMENT;
    }
    for (UmicomSize i = 0U; request->bytes && i < 5U; ++i)
        if (UmicomFatPlanOverlap(request->input, request->bytes, owners[i], lengths[i]))
            return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    while (pathBytes < UMICOM_FAT16_PATH_BYTES) {
        for (UmicomSize i = 0U; i < 5U; ++i)
            if (UmicomFatPlanOverlap(request->path + pathBytes, 1U, owners[i], lengths[i]))
                return UMICOM_DISK_INVALID_ARGUMENT;
        if (request->bytes && UmicomFatPlanOverlap(request->path + pathBytes, 1U,
            request->input, request->bytes)) return UMICOM_DISK_INVALID_ARGUMENT;
        if (!request->path[pathBytes]) break;
        ++pathBytes;
    }
    if (pathBytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
    *outDestinationBytes = 0U;
    if (request->operation == UMICOM_FAT16_LIFECYCLE_MOVE) {
        if (!UmicomFatPlanSpan(request->destination, UMICOM_FAT16_PATH_BYTES))
            return UMICOM_DISK_INVALID_ARGUMENT;
        UmicomSize bytes = 0U;
        for (; bytes < UMICOM_FAT16_PATH_BYTES; ++bytes) {
            for (UmicomSize i = 0U; i < 5U; ++i)
                if (UmicomFatPlanOverlap(request->destination + bytes, 1U, owners[i], lengths[i]))
                    return UMICOM_DISK_INVALID_ARGUMENT;
            if (UmicomFatPlanOverlap(request->destination + bytes, 1U, request->path, pathBytes + 1U))
                return UMICOM_DISK_INVALID_ARGUMENT;
            if (!request->destination[bytes]) break;
        }
        if (bytes == UMICOM_FAT16_PATH_BYTES) return UMICOM_DISK_LIMIT;
        *outDestinationBytes = bytes + 1U;
    }
    if ((workspace->self && workspace->self != workspace) ||
        (lifecycleWorkspace->self && lifecycleWorkspace->self != lifecycleWorkspace))
        return UMICOM_DISK_BAD_STATE;
    if (workspace->busy || lifecycleWorkspace->busy) return UMICOM_DISK_BUSY;
    if ((!workspace->self && !UmicomFatPlanZero(workspace, sizeof(*workspace))) ||
        (!lifecycleWorkspace->self && !UmicomFatPlanZero(lifecycleWorkspace, sizeof(*lifecycleWorkspace))))
        return UMICOM_DISK_BAD_STATE;
    UmicomFatClear(encoded, sizeof(*encoded));
    if (request->operation != UMICOM_FAT16_LIFECYCLE_DELETE &&
        request->operation != UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY &&
        request->operation != UMICOM_FAT16_LIFECYCLE_MOVE) {
        const UmicomKernelDiskStatus status = UmicomFatFileTimeFields(&request->time, encoded);
        if (status != UMICOM_DISK_OK) return status;
    }
    *outPathBytes = pathBytes + 1U;
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatLifecycleCreateLookup(UmicomKernelFat16 *volume,
    const char *path, UmicomKernelFat16Entry *entry, UmicomU16 *outParent)
{
    char names[UMICOM_FAT16_DEPTH_LIMIT][13];
    UmicomFatClear(names, sizeof(names));
    UmicomSize depth = 0U;
    UmicomKernelDiskStatus status = UmicomFatPath(path, names, &depth);
    if (status != UMICOM_DISK_OK) return status;
    if (!depth) return UMICOM_DISK_IS_DIRECTORY;
    UmicomKernelFat16Entry parent;
    UmicomFatClear(&parent, sizeof(parent));
    parent.directory = UMICOM_TRUE;
    for (UmicomSize i = 0U; i < depth; ++i) {
        if (!parent.directory) return UMICOM_DISK_NOT_DIRECTORY;
        status = UmicomFatDirectoryRead(volume, parent.firstCluster);
        if (status != UMICOM_DISK_OK) return status;
        UmicomBoolean found = UMICOM_FALSE;
        UmicomKernelFat16Entry selected;
        UmicomFatClear(&selected, sizeof(selected));
        for (UmicomSize j = 0U; j < volume->directoryStage.count; ++j)
            if (UmicomFatStringEqual(names[i], volume->directoryStage.entries[j].name)) {
                selected = volume->directoryStage.entries[j]; found = UMICOM_TRUE; break;
            }
        if (i + 1U == depth) {
            if (found) return UMICOM_DISK_EXISTS;
            if (parent.attributes & 0x01U) return UMICOM_DISK_READ_ONLY;
            if (volume->directoryStage.count >= UMICOM_FAT16_ENTRY_LIMIT) return UMICOM_DISK_LIMIT;
            UmicomFatCopy(entry->name, names[i], sizeof(entry->name));
            *outParent = parent.firstCluster;
            return UMICOM_DISK_OK;
        }
        if (!found) return UMICOM_DISK_NOT_FOUND;
        parent = selected;
    }
    return UMICOM_DISK_CORRUPT;
}

static UmicomKernelDiskStatus UmicomFatLifecycleFatSector(UmicomKernelFat16 *volume,
    UmicomKernelFat16LifecyclePlan *stage, UmicomSize relative, UmicomSize *outIndex)
{
    if (relative >= volume->info.sectorsPerFat) return UMICOM_DISK_CORRUPT;
    const UmicomU64 primary = volume->info.firstSector + volume->fatStart + relative;
    for (UmicomSize i = 0U; i < stage->fatSectorCount; ++i)
        if (stage->fatSectors[i].primarySector == primary) { *outIndex = i; return UMICOM_DISK_OK; }
    if (stage->fatSectorCount >= UMICOM_FAT16_LIFECYCLE_FAT_SECTORS) return UMICOM_DISK_LIMIT;
    UmicomKernelFat16LifecycleFatSector *const sector = &stage->fatSectors[stage->fatSectorCount];
    UmicomKernelDiskStatus status = UmicomFatReadSector(volume, volume->fatStart + relative, sector->original);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatReadSector(volume, volume->fatStart + volume->info.sectorsPerFat + relative,
        volume->mirrorSector);
    if (status != UMICOM_DISK_OK) return status;
    if (!UmicomFatEqual(sector->original, volume->mirrorSector, sizeof(sector->original)))
        return UMICOM_DISK_FAT_MISMATCH;
    sector->primarySector = primary;
    sector->mirrorSector = primary + volume->info.sectorsPerFat;
    UmicomFatCopy(sector->data, sector->original, sizeof(sector->data));
    *outIndex = stage->fatSectorCount++;
    return UMICOM_DISK_OK;
}
static UmicomKernelDiskStatus UmicomFatLifecycleLink(UmicomKernelFat16 *volume,
    UmicomKernelFat16LifecyclePlan *stage, UmicomU16 cluster, UmicomU16 next,
    UmicomBoolean boundary)
{
    if (cluster < 2U || cluster > volume->info.clusters + 1U) return UMICOM_DISK_CORRUPT;
    UmicomSize index = 0U;
    const UmicomKernelDiskStatus status = UmicomFatLifecycleFatSector(volume, stage,
        (UmicomSize)cluster / 256U, &index);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16LifecycleFatSector *const sector = &stage->fatSectors[index];
    UmicomFatLifecyclePut16(sector->data + ((UmicomSize)cluster % 256U) * 2U, next);
    sector->changed = !UmicomFatEqual(sector->original, sector->data, sizeof(sector->data));
    if (boundary) { stage->chainBoundaryPresent = UMICOM_TRUE; stage->chainBoundaryFatIndex = index; }
    return UMICOM_DISK_OK;
}

/* A new directory needs one cluster even though FAT directory byte size is zero. The preceding implementation is retained below for
 * source review; only the implementation after this disabled block executes. */
#if 0
static UmicomKernelDiskStatus UmicomFatLifecycleAllocation(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace)
{
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    const UmicomU64 required = stage->updatedEntryPresent ?
        ((UmicomU64)stage->updatedEntry.bytes + clusterBytes - 1U) / clusterBytes : 0U;
    if (required > UMICOM_FAT16_CHAIN_LIMIT) return UMICOM_DISK_LIMIT;
    UmicomSize header = 0U;
    UmicomKernelDiskStatus status = UmicomFatLifecycleFatSector(volume, stage, 0U, &header);
    if (status != UMICOM_DISK_OK) return status;
    const UmicomSize oldCount = workspace->targetClusters;
    const UmicomSize newCount = (UmicomSize)required;
    if (newCount > oldCount) {
        stage->allocatedClusters = newCount - oldCount;
        if (stage->allocatedClusters > UMICOM_FAT16_LIFECYCLE_NEW_CLUSTERS) return UMICOM_DISK_LIMIT;
        UmicomSize selected = 0U;
        /* The completed allocation proof established unowned iff FREE for
         * every usable identifier. Select deterministically without rereading
         * all FAT sectors or ever treating reserved entries as candidates. */
        for (UmicomU32 cluster = 2U; cluster <= volume->info.clusters + 1U &&
            selected < stage->allocatedClusters; ++cluster)
            if (!UmicomFatPlanOwned(workspace, (UmicomU16)cluster))
                lifecycleWorkspace->newChain[selected++] = (UmicomU16)cluster;
        if (selected != stage->allocatedClusters) return UMICOM_DISK_NO_SPACE;
        for (UmicomSize i = 0U; i < selected; ++i) {
            status = UmicomFatLifecycleLink(volume, stage, lifecycleWorkspace->newChain[i],
                i + 1U < selected ? lifecycleWorkspace->newChain[i + 1U] : (UmicomU16)0xffffU,
                UMICOM_FALSE);
            if (status != UMICOM_DISK_OK) return status;
        }
        if (oldCount) {
            status = UmicomFatLifecycleLink(volume, stage, workspace->targetChain[oldCount - 1U],
                lifecycleWorkspace->newChain[0], UMICOM_TRUE);
            if (status != UMICOM_DISK_OK) return status;
        } else stage->updatedEntry.firstCluster = lifecycleWorkspace->newChain[0];
    } else if (newCount < oldCount) {
        stage->freedClusters = oldCount - newCount;
        if (newCount) {
            status = UmicomFatLifecycleLink(volume, stage, workspace->targetChain[newCount - 1U],
                0xffffU, UMICOM_TRUE);
            if (status != UMICOM_DISK_OK) return status;
        } else stage->updatedEntry.firstCluster = 0U;
        for (UmicomSize i = newCount; i < oldCount; ++i) {
            status = UmicomFatLifecycleLink(volume, stage, workspace->targetChain[i], 0U, UMICOM_FALSE);
            if (status != UMICOM_DISK_OK) return status;
        }
    }
    return UMICOM_DISK_OK;
}
#endif

static UmicomKernelDiskStatus UmicomFatLifecycleAllocation(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace)
{
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    const UmicomU64 required = stage->updatedEntryPresent ?
        (stage->updatedEntry.directory ? 1U :
         ((UmicomU64)stage->updatedEntry.bytes + clusterBytes - 1U) / clusterBytes) : 0U;
    if (required > UMICOM_FAT16_CHAIN_LIMIT) return UMICOM_DISK_LIMIT;
    UmicomSize header = 0U;
    UmicomKernelDiskStatus status = UmicomFatLifecycleFatSector(volume, stage, 0U, &header);
    if (status != UMICOM_DISK_OK) return status;
    const UmicomSize oldCount = workspace->targetClusters;
    const UmicomSize newCount = (UmicomSize)required;
    if (newCount > oldCount) {
        stage->allocatedClusters = newCount - oldCount;
        if (stage->allocatedClusters > UMICOM_FAT16_LIFECYCLE_NEW_CLUSTERS) return UMICOM_DISK_LIMIT;
        UmicomSize selected = 0U;
        /* The completed allocation proof established unowned iff FREE for
         * every usable identifier. Select deterministically without rereading
         * all FAT sectors or ever treating reserved entries as candidates. */
        for (UmicomU32 cluster = 2U; cluster <= volume->info.clusters + 1U &&
            selected < stage->allocatedClusters; ++cluster)
            if (!UmicomFatPlanOwned(workspace, (UmicomU16)cluster))
                lifecycleWorkspace->newChain[selected++] = (UmicomU16)cluster;
        if (selected != stage->allocatedClusters) return UMICOM_DISK_NO_SPACE;
        for (UmicomSize i = 0U; i < selected; ++i) {
            status = UmicomFatLifecycleLink(volume, stage, lifecycleWorkspace->newChain[i],
                i + 1U < selected ? lifecycleWorkspace->newChain[i + 1U] : (UmicomU16)0xffffU,
                UMICOM_FALSE);
            if (status != UMICOM_DISK_OK) return status;
        }
        if (oldCount) {
            status = UmicomFatLifecycleLink(volume, stage, workspace->targetChain[oldCount - 1U],
                lifecycleWorkspace->newChain[0], UMICOM_TRUE);
            if (status != UMICOM_DISK_OK) return status;
        } else stage->updatedEntry.firstCluster = lifecycleWorkspace->newChain[0];
    } else if (newCount < oldCount) {
        stage->freedClusters = oldCount - newCount;
        if (newCount) {
            status = UmicomFatLifecycleLink(volume, stage, workspace->targetChain[newCount - 1U],
                0xffffU, UMICOM_TRUE);
            if (status != UMICOM_DISK_OK) return status;
        } else stage->updatedEntry.firstCluster = 0U;
        for (UmicomSize i = newCount; i < oldCount; ++i) {
            status = UmicomFatLifecycleLink(volume, stage, workspace->targetChain[i], 0U, UMICOM_FALSE);
            if (status != UMICOM_DISK_OK) return status;
        }
    }
    return UMICOM_DISK_OK;
}

static UmicomKernelDiskStatus UmicomFatLifecycleDataSector(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16LifecyclePlan *stage,
    UmicomU16 cluster, UmicomSize clusterSector, UmicomU64 logical, UmicomBoolean initialise)
{
    if (stage->dataSectorCount >= UMICOM_FAT16_LIFECYCLE_DATA_SECTORS) return UMICOM_DISK_LIMIT;
    UmicomKernelFat16UpdateSector *const sector = &stage->dataSectors[stage->dataSectorCount];
    const UmicomU64 relative = volume->dataStart + ((UmicomU64)cluster - 2U) *
        volume->info.sectorsPerCluster + clusterSector;
    if (relative >= volume->info.volumeSectors) return UMICOM_DISK_RANGE;
    sector->sector = volume->info.firstSector + relative;
    if (!initialise) {
        const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative, sector->data);
        if (status != UMICOM_DISK_OK) return status;
    }
    /* New sector arrays started zero-filled. Existing sectors were captured
     * completely; only the overlap with caller bytes is subsequently patched. */
    const UmicomU64 end = stage->offset + stage->requestedBytes;
    const UmicomU64 first = logical > stage->offset ? logical : stage->offset;
    const UmicomU64 last = logical + UMICOM_DISK_SECTOR_BYTES < end ?
        logical + UMICOM_DISK_SECTOR_BYTES : end;
    if (first < last) {
        sector->offset = (UmicomSize)(first - logical);
        sector->inputOffset = (UmicomSize)(first - stage->offset);
        sector->bytes = (UmicomSize)(last - first);
        UmicomFatCopy(sector->data + sector->offset, workspace->input + sector->inputOffset, sector->bytes);
    }
    ++stage->dataSectorCount;
    return UMICOM_DISK_OK;
}
/* Directory storage now shares full-cluster initialization with allocated file data. The preceding implementation is retained below for
 * source review; only the implementation after this disabled block executes. */
#if 0
static UmicomKernelDiskStatus UmicomFatLifecycleData(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace)
{
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    if (!stage->requestedBytes) return UMICOM_DISK_OK;
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    const UmicomU64 oldCapacity = (UmicomU64)workspace->targetClusters * clusterBytes;
    const UmicomU64 end = stage->offset + stage->requestedBytes;
    for (UmicomU64 logical = stage->offset / UMICOM_DISK_SECTOR_BYTES * UMICOM_DISK_SECTOR_BYTES;
        logical < end && logical < oldCapacity; logical += UMICOM_DISK_SECTOR_BYTES) {
        const UmicomSize index = (UmicomSize)(logical / clusterBytes);
        const UmicomKernelDiskStatus status = UmicomFatLifecycleDataSector(volume, workspace, stage,
            workspace->targetChain[index], (UmicomSize)((logical % clusterBytes) / UMICOM_DISK_SECTOR_BYTES),
            logical, UMICOM_FALSE);
        if (status != UMICOM_DISK_OK) return status;
    }
    for (UmicomSize i = 0U; i < stage->allocatedClusters; ++i)
        for (UmicomSize sector = 0U; sector < volume->info.sectorsPerCluster; ++sector) {
            const UmicomU64 logical = oldCapacity + (UmicomU64)i * clusterBytes + sector * UMICOM_DISK_SECTOR_BYTES;
            const UmicomKernelDiskStatus status = UmicomFatLifecycleDataSector(volume, workspace, stage,
                lifecycleWorkspace->newChain[i], sector, logical, UMICOM_TRUE);
            if (status != UMICOM_DISK_OK) return status;
        }
    return UMICOM_DISK_OK;
}
#endif

static UmicomKernelDiskStatus UmicomFatLifecycleData(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace)
{
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    /* Directory initialization has no caller payload but must clear the
     * complete allocated cluster before either FAT copy exposes it. */
    if (!stage->requestedBytes && !stage->updatedEntry.directory) return UMICOM_DISK_OK;
    const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    const UmicomU64 oldCapacity = (UmicomU64)workspace->targetClusters * clusterBytes;
    const UmicomU64 end = stage->offset + stage->requestedBytes;
    for (UmicomU64 logical = stage->offset / UMICOM_DISK_SECTOR_BYTES * UMICOM_DISK_SECTOR_BYTES;
        logical < end && logical < oldCapacity; logical += UMICOM_DISK_SECTOR_BYTES) {
        const UmicomSize index = (UmicomSize)(logical / clusterBytes);
        const UmicomKernelDiskStatus status = UmicomFatLifecycleDataSector(volume, workspace, stage,
            workspace->targetChain[index], (UmicomSize)((logical % clusterBytes) / UMICOM_DISK_SECTOR_BYTES),
            logical, UMICOM_FALSE);
        if (status != UMICOM_DISK_OK) return status;
    }
    for (UmicomSize i = 0U; i < stage->allocatedClusters; ++i)
        for (UmicomSize sector = 0U; sector < volume->info.sectorsPerCluster; ++sector) {
            const UmicomU64 logical = oldCapacity + (UmicomU64)i * clusterBytes + sector * UMICOM_DISK_SECTOR_BYTES;
            const UmicomKernelDiskStatus status = UmicomFatLifecycleDataSector(volume, workspace, stage,
                lifecycleWorkspace->newChain[i], sector, logical, UMICOM_TRUE);
            if (status != UMICOM_DISK_OK) return status;
        }
    return UMICOM_DISK_OK;
}

static UmicomU64 UmicomFatLifecycleDirectoryRelative(const UmicomKernelFat16 *volume,
    const UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 parent, UmicomU64 entry)
{
    if (!parent) return volume->rootStart + entry / 16U;
    const UmicomU64 index = entry / ((UmicomU64)volume->info.sectorsPerCluster * 16U);
    return volume->dataStart + ((UmicomU64)workspace->chain[index] - 2U) *
        volume->info.sectorsPerCluster + (entry / 16U) % volume->info.sectorsPerCluster;
}
/* Directory entries now use the same timestamp encoder and tombstone publication as regular files. The preceding implementation is retained below for
 * source review; only the implementation after this disabled block executes. */
#if 0
static void UmicomFatLifecycleDirectoryPatch(UmicomKernelFat16LifecyclePlan *stage)
{
    UmicomU8 *const raw = stage->directorySectors[0].data + stage->entryOffset;
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_DELETE) { raw[0] = 0xe5U; return; }
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE) {
        UmicomFatClear(raw, 32U);
        for (UmicomSize i = 0U; i < 11U; ++i) raw[i] = ' ';
        UmicomSize position = 0U;
        for (UmicomSize i = 0U; stage->updatedEntry.name[i]; ++i) {
            if (stage->updatedEntry.name[i] == '.') position = 8U;
            else raw[position++] = (UmicomU8)stage->updatedEntry.name[i];
        }
        UmicomFatLifecyclePut16(raw + 14U, stage->encodedTime.writeTime);
        UmicomFatLifecyclePut16(raw + 16U, stage->encodedTime.writeDate);
        UmicomFatLifecyclePut16(raw + 18U, stage->encodedTime.writeDate);
    }
    raw[11] = stage->updatedEntry.attributes;
    UmicomFatLifecyclePut16(raw + 22U, stage->encodedTime.writeTime);
    UmicomFatLifecyclePut16(raw + 24U, stage->encodedTime.writeDate);
    UmicomFatLifecyclePut16(raw + 26U, stage->updatedEntry.firstCluster);
    UmicomFatLifecyclePut32(raw + 28U, stage->updatedEntry.bytes);
}
#endif

/* Moves need an unchanged source entry before metadata publication.
 * The preceding implementation is retained for engineering review. */
#if 0
static void UmicomFatLifecycleDirectoryPatch(UmicomKernelFat16LifecyclePlan *stage)
{
    UmicomU8 *const raw = stage->directorySectors[0].data + stage->entryOffset;
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_DELETE ||
        stage->operation == UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY) { raw[0] = 0xe5U; return; }
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE ||
        stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY) {
        UmicomFatClear(raw, 32U);
        for (UmicomSize i = 0U; i < 11U; ++i) raw[i] = ' ';
        UmicomSize position = 0U;
        for (UmicomSize i = 0U; stage->updatedEntry.name[i]; ++i) {
            if (stage->updatedEntry.name[i] == '.') position = 8U;
            else raw[position++] = (UmicomU8)stage->updatedEntry.name[i];
        }
        UmicomFatLifecyclePut16(raw + 14U, stage->encodedTime.writeTime);
        UmicomFatLifecyclePut16(raw + 16U, stage->encodedTime.writeDate);
        UmicomFatLifecyclePut16(raw + 18U, stage->encodedTime.writeDate);
    }
    raw[11] = stage->updatedEntry.attributes;
    UmicomFatLifecyclePut16(raw + 22U, stage->encodedTime.writeTime);
    UmicomFatLifecyclePut16(raw + 24U, stage->encodedTime.writeDate);
    UmicomFatLifecyclePut16(raw + 26U, stage->updatedEntry.firstCluster);
    UmicomFatLifecyclePut32(raw + 28U, stage->updatedEntry.bytes);
}
#endif

static void UmicomFatLifecycleDirectoryPatch(UmicomKernelFat16LifecyclePlan *stage)
{
    /* Capture a move source without changing its calendar or attributes. The
     * move planner later patches only the explicitly permitted bytes. */
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_MOVE) return;
    UmicomU8 *const raw = stage->directorySectors[0].data + stage->entryOffset;
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_DELETE ||
        stage->operation == UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY) { raw[0] = 0xe5U; return; }
    if (stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE ||
        stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY) {
        UmicomFatClear(raw, 32U);
        for (UmicomSize i = 0U; i < 11U; ++i) raw[i] = ' ';
        UmicomSize position = 0U;
        for (UmicomSize i = 0U; stage->updatedEntry.name[i]; ++i) {
            if (stage->updatedEntry.name[i] == '.') position = 8U;
            else raw[position++] = (UmicomU8)stage->updatedEntry.name[i];
        }
        UmicomFatLifecyclePut16(raw + 14U, stage->encodedTime.writeTime);
        UmicomFatLifecyclePut16(raw + 16U, stage->encodedTime.writeDate);
        UmicomFatLifecyclePut16(raw + 18U, stage->encodedTime.writeDate);
    }
    raw[11] = stage->updatedEntry.attributes;
    UmicomFatLifecyclePut16(raw + 22U, stage->encodedTime.writeTime);
    UmicomFatLifecyclePut16(raw + 24U, stage->encodedTime.writeDate);
    UmicomFatLifecyclePut16(raw + 26U, stage->updatedEntry.firstCluster);
    UmicomFatLifecyclePut32(raw + 28U, stage->updatedEntry.bytes);
}
/* Slot selection now accepts directory creation while retaining fixed-root capacity checks. The preceding implementation is retained below for
 * source review; only the implementation after this disabled block executes. */
#if 0
static UmicomKernelDiskStatus UmicomFatLifecycleDirectory(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 parent,
    UmicomKernelFat16LifecyclePlan *stage)
{
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (parent) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, parent,
            UMICOM_FALSE, 0U, workspace->chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    const UmicomBoolean create = stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE;
    for (UmicomU64 i = 0U; i < entries; ++i) {
        const UmicomU64 relative = UmicomFatLifecycleDirectoryRelative(volume, workspace, parent, i);
        if (i % 16U == 0U) {
            const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative, volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *const raw = volume->dataSector + (i % 16U) * 32U;
        const UmicomBoolean endMarker = !raw[0];
        if (create) {
            if (raw[0] && raw[0] != 0xe5U) continue;
        } else {
            if (endMarker) break;
            if (raw[0] == 0xe5U || raw[0] == '.' || (raw[11] & 0x08U)) continue;
            char name[13];
            UmicomFatClear(name, sizeof(name));
            const UmicomKernelDiskStatus status = UmicomFatDecodeName(raw, name);
            if (status != UMICOM_DISK_OK) return status;
            if (!UmicomFatStringEqual(name, stage->originalEntry.name)) continue;
            if (raw[11] != stage->originalEntry.attributes || (raw[12] & ~0x18U) ||
                UmicomFat16Word(raw + 20U) ||
                UmicomFat16Word(raw + 26U) != stage->originalEntry.firstCluster ||
                UmicomFat32Word(raw + 28U) != stage->originalEntry.bytes) return UMICOM_DISK_CORRUPT;
        }
        UmicomKernelFat16LifecycleDirectorySector *const sector = &stage->directorySectors[0];
        sector->sector = volume->info.firstSector + relative;
        UmicomFatCopy(sector->original, volume->dataSector, sizeof(sector->original));
        UmicomFatCopy(sector->data, sector->original, sizeof(sector->data));
        stage->directorySectorCount = 1U;
        stage->entryOffset = (UmicomSize)(i % 16U) * 32U;
        UmicomFatLifecycleDirectoryPatch(stage);
        if (create && endMarker && i + 1U < entries) {
            const UmicomU64 next = UmicomFatLifecycleDirectoryRelative(volume, workspace, parent, i + 1U);
            const UmicomSize offset = (UmicomSize)((i + 1U) % 16U) * 32U;
            if (next == relative) sector->data[offset] = 0U;
            else {
                const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, next, volume->dataSector);
                if (status != UMICOM_DISK_OK) return status;
                if (volume->dataSector[offset]) {
                    UmicomKernelFat16LifecycleDirectorySector *const continuation = &stage->directorySectors[1];
                    continuation->sector = volume->info.firstSector + next;
                    UmicomFatCopy(continuation->original, volume->dataSector, sizeof(continuation->original));
                    UmicomFatCopy(continuation->data, continuation->original, sizeof(continuation->data));
                    continuation->data[offset] = 0U;
                    stage->directorySectorCount = 2U;
                }
            }
        }
        return UMICOM_DISK_OK;
    }
    return create ? UMICOM_DISK_NO_SPACE : UMICOM_DISK_CORRUPT;
}
#endif

static UmicomKernelDiskStatus UmicomFatLifecycleDirectory(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 parent,
    UmicomKernelFat16LifecyclePlan *stage)
{
    UmicomSize count = 0U;
    UmicomU64 entries = volume->info.rootEntries;
    if (parent) {
        const UmicomKernelDiskStatus status = UmicomFatChain(volume, parent,
            UMICOM_FALSE, 0U, workspace->chain, &count);
        if (status != UMICOM_DISK_OK) return status;
        entries = (UmicomU64)count * volume->info.sectorsPerCluster * 16U;
    }
    if (entries > UMICOM_FAT16_SCAN_ENTRIES) return UMICOM_DISK_LIMIT;
    const UmicomBoolean create = stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE ||
        stage->operation == UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY;
    for (UmicomU64 i = 0U; i < entries; ++i) {
        const UmicomU64 relative = UmicomFatLifecycleDirectoryRelative(volume, workspace, parent, i);
        if (i % 16U == 0U) {
            const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, relative, volume->dataSector);
            if (status != UMICOM_DISK_OK) return status;
        }
        const UmicomU8 *const raw = volume->dataSector + (i % 16U) * 32U;
        const UmicomBoolean endMarker = !raw[0];
        if (create) {
            if (raw[0] && raw[0] != 0xe5U) continue;
        } else {
            if (endMarker) break;
            if (raw[0] == 0xe5U || raw[0] == '.' || (raw[11] & 0x08U)) continue;
            char name[13];
            UmicomFatClear(name, sizeof(name));
            const UmicomKernelDiskStatus status = UmicomFatDecodeName(raw, name);
            if (status != UMICOM_DISK_OK) return status;
            if (!UmicomFatStringEqual(name, stage->originalEntry.name)) continue;
            if (raw[11] != stage->originalEntry.attributes || (raw[12] & ~0x18U) ||
                UmicomFat16Word(raw + 20U) ||
                UmicomFat16Word(raw + 26U) != stage->originalEntry.firstCluster ||
                UmicomFat32Word(raw + 28U) != stage->originalEntry.bytes) return UMICOM_DISK_CORRUPT;
        }
        UmicomKernelFat16LifecycleDirectorySector *const sector = &stage->directorySectors[0];
        sector->sector = volume->info.firstSector + relative;
        UmicomFatCopy(sector->original, volume->dataSector, sizeof(sector->original));
        UmicomFatCopy(sector->data, sector->original, sizeof(sector->data));
        stage->directorySectorCount = 1U;
        stage->entryOffset = (UmicomSize)(i % 16U) * 32U;
        UmicomFatLifecycleDirectoryPatch(stage);
        if (create && endMarker && i + 1U < entries) {
            const UmicomU64 next = UmicomFatLifecycleDirectoryRelative(volume, workspace, parent, i + 1U);
            const UmicomSize offset = (UmicomSize)((i + 1U) % 16U) * 32U;
            if (next == relative) sector->data[offset] = 0U;
            else {
                const UmicomKernelDiskStatus status = UmicomFatReadSector(volume, next, volume->dataSector);
                if (status != UMICOM_DISK_OK) return status;
                if (volume->dataSector[offset]) {
                    UmicomKernelFat16LifecycleDirectorySector *const continuation = &stage->directorySectors[1];
                    continuation->sector = volume->info.firstSector + next;
                    UmicomFatCopy(continuation->original, volume->dataSector, sizeof(continuation->original));
                    UmicomFatCopy(continuation->data, continuation->original, sizeof(continuation->data));
                    continuation->data[offset] = 0U;
                    stage->directorySectorCount = 2U;
                }
            }
        }
        return UMICOM_DISK_OK;
    }
    return create ? UMICOM_DISK_NO_SPACE : UMICOM_DISK_CORRUPT;
}


/* Dot records describe namespace ownership, rather than ordinary child files.
 * All timestamps match the containing entry. FAT16 uses cluster zero for the
 * fixed root parent, even though the root itself occupies reserved sectors. */
static void UmicomFatLifecycleDotEntries(UmicomKernelFat16LifecyclePlan *stage,
    UmicomU16 parent)
{
    UmicomU8 *const data = stage->dataSectors[0].data;
    for (UmicomSize record = 0U; record < 2U; ++record) {
        UmicomU8 *const raw = data + record * 32U;
        for (UmicomSize i = 0U; i < 11U; ++i) raw[i] = ' ';
        raw[0] = '.';
        if (record) raw[1] = '.';
        raw[11] = 0x10U;
        UmicomFatLifecyclePut16(raw + 14U, stage->encodedTime.writeTime);
        UmicomFatLifecyclePut16(raw + 16U, stage->encodedTime.writeDate);
        UmicomFatLifecyclePut16(raw + 18U, stage->encodedTime.writeDate);
        UmicomFatLifecyclePut16(raw + 22U, stage->encodedTime.writeTime);
        UmicomFatLifecyclePut16(raw + 24U, stage->encodedTime.writeDate);
        UmicomFatLifecyclePut16(raw + 26U, record ? parent : stage->updatedEntry.firstCluster);
    }
}

/* Extend only an allocated parent whose existing slots are exhausted. The
 * namespace proof has already accounted for every old cluster. Excluding this
 * operation's newly selected clusters prevents the parent and child sharing
 * storage before those allocations appear in the on-disk FAT. */
static UmicomKernelDiskStatus UmicomFatLifecycleGrowParent(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace, UmicomU16 parent)
{
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    if (!parent) return UMICOM_DISK_NO_SPACE;
    UmicomSize count = 0U;
    UmicomKernelDiskStatus status = UmicomFatChain(volume, parent, UMICOM_FALSE,
        0U, workspace->chain, &count);
    if (status != UMICOM_DISK_OK) return status;
    if (!count) return UMICOM_DISK_CORRUPT;
    if (count >= UMICOM_FAT16_CHAIN_LIMIT ||
        (UmicomU64)(count + 1U) * volume->info.sectorsPerCluster * 16U > UMICOM_FAT16_SCAN_ENTRIES ||
        stage->allocatedClusters >= UMICOM_FAT16_LIFECYCLE_NEW_CLUSTERS ||
        volume->info.sectorsPerCluster > UMICOM_FAT16_LIFECYCLE_DATA_SECTORS - stage->dataSectorCount)
        return UMICOM_DISK_LIMIT;
    UmicomU16 added = 0U;
    for (UmicomU32 candidate = 2U; candidate <= volume->info.clusters + 1U; ++candidate) {
        if (UmicomFatPlanOwned(workspace, (UmicomU16)candidate)) continue;
        UmicomBoolean selected = UMICOM_FALSE;
        for (UmicomSize i = 0U; i < stage->allocatedClusters; ++i)
            if (lifecycleWorkspace->newChain[i] == candidate) selected = UMICOM_TRUE;
        if (!selected) { added = (UmicomU16)candidate; break; }
    }
    if (!added) return UMICOM_DISK_NO_SPACE;
    const UmicomU16 tail = workspace->chain[count - 1U];
    status = UmicomFatLifecycleLink(volume, stage, added, 0xffffU, UMICOM_FALSE);
    if (status == UMICOM_DISK_OK)
        status = UmicomFatLifecycleLink(volume, stage, tail, added, UMICOM_TRUE);
    if (status != UMICOM_DISK_OK) return status;
    UmicomKernelFat16LifecycleDirectorySector *const directory = &stage->directorySectors[0];
    const UmicomU64 relative = volume->dataStart +
        ((UmicomU64)added - 2U) * volume->info.sectorsPerCluster;
    directory->sector = volume->info.firstSector + relative;
    status = UmicomFatReadSector(volume, relative, directory->original);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(directory->data, sizeof(directory->data));
    stage->directorySectorCount = 1U;
    stage->entryOffset = 0U;
    UmicomFatLifecycleDirectoryPatch(stage);
    const UmicomSize firstData = stage->dataSectorCount;
    for (UmicomSize sector = 0U; sector < volume->info.sectorsPerCluster; ++sector) {
        /* A logical position beyond the payload requests initialization only.
         * The parent extension must never receive the child's file contents. */
        status = UmicomFatLifecycleDataSector(volume, workspace, stage, added, sector,
            stage->offset + stage->requestedBytes + sector * UMICOM_DISK_SECTOR_BYTES, UMICOM_TRUE);
        if (status != UMICOM_DISK_OK) return status;
    }
    /* Initialize the entry while this cluster is still unreachable. Keeping
     * both plan images identical allows Finish to reverify every data sector
     * after directory publication without mistaking our own write for damage. */
    UmicomFatCopy(stage->dataSectors[firstData].data, directory->data, sizeof(directory->data));
    lifecycleWorkspace->newChain[stage->allocatedClusters++] = added;
    stage->parentGrown = UMICOM_TRUE;
    stage->parentAddedCluster = added;
    stage->parentTailCluster = tail;
    return UMICOM_DISK_OK;
}

/* Directory allocation and removal now reuse the complete namespace proof and immutable plan publication. The preceding implementation is retained below for
 * source review; only the implementation after this disabled block executes. */
#if 0
UmicomKernelDiskStatus UmicomKernelFat16PlanLifecycle(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan)
{
    UmicomSize pathBytes = 0U;
    UmicomKernelFat16FileTimeEncoding encoded;
    UmicomKernelDiskStatus status = UmicomFatLifecycleArguments(volume, request, workspace,
        lifecycleWorkspace, outPlan, &pathBytes, &encoded);
    if (status != UMICOM_DISK_OK) return status;
    /* Copy scalar intent before admission; the request itself is never read
     * after a callback. Its borrowed pointers are used only for these copies. */
    const UmicomKernelFat16LifecycleRequest selected = *request;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace; workspace->busy = UMICOM_TRUE;
    UmicomFatClear(lifecycleWorkspace, sizeof(*lifecycleWorkspace));
    lifecycleWorkspace->self = lifecycleWorkspace; lifecycleWorkspace->busy = UMICOM_TRUE;
    UmicomFatCopy(lifecycleWorkspace->path, selected.path, pathBytes);
    if (selected.bytes) UmicomFatCopy(workspace->input, selected.input, selected.bytes);
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    stage->operation = selected.operation;
    stage->requestedTime = selected.time;
    stage->encodedTime = encoded;
    stage->requestedBytes = selected.bytes;
    UmicomU16 parent = 0U;
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK && selected.operation == UMICOM_FAT16_LIFECYCLE_CREATE) {
        status = UmicomFatLifecycleCreateLookup(volume, lifecycleWorkspace->path, &stage->updatedEntry, &parent);
        if (status == UMICOM_DISK_OK) {
            stage->updatedEntryPresent = UMICOM_TRUE;
            stage->updatedEntry.bytes = (UmicomU32)selected.bytes;
            stage->updatedEntry.attributes = 0x20U;
        }
    } else if (status == UMICOM_DISK_OK) {
        status = UmicomFatFileLookup(volume, lifecycleWorkspace->path, &stage->originalEntry, &parent);
        if (status == UMICOM_DISK_OK && stage->originalEntry.directory) status = UMICOM_DISK_IS_DIRECTORY;
        if (status == UMICOM_DISK_OK && (stage->originalEntry.attributes & 0x01U)) status = UMICOM_DISK_READ_ONLY;
        if (status == UMICOM_DISK_OK) {
            stage->originalEntryPresent = UMICOM_TRUE;
            if (selected.operation != UMICOM_FAT16_LIFECYCLE_DELETE) {
                stage->updatedEntryPresent = UMICOM_TRUE;
                stage->updatedEntry = stage->originalEntry;
                stage->updatedEntry.attributes |= 0x20U;
            }
            if (selected.operation == UMICOM_FAT16_LIFECYCLE_APPEND) {
                stage->offset = stage->originalEntry.bytes;
                if (selected.bytes > (UmicomU64)0xffffffffU - stage->offset) status = UMICOM_DISK_RANGE;
                else stage->updatedEntry.bytes = (UmicomU32)(stage->offset + selected.bytes);
            } else if (selected.operation == UMICOM_FAT16_LIFECYCLE_TRUNCATE) {
                if (selected.size > stage->originalEntry.bytes) status = UMICOM_DISK_RANGE;
                else stage->updatedEntry.bytes = selected.size;
            }
        }
        if (status == UMICOM_DISK_OK) {
            const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
            const UmicomU64 required = ((UmicomU64)stage->originalEntry.bytes + clusterBytes - 1U) / clusterBytes;
            status = UmicomFatChain(volume, stage->originalEntry.firstCluster, UMICOM_TRUE,
                required, workspace->targetChain, &workspace->targetClusters);
        }
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK && selected.operation == UMICOM_FAT16_LIFECYCLE_CREATE &&
        workspace->objects >= UMICOM_FAT16_UPDATE_OBJECT_LIMIT) status = UMICOM_DISK_LIMIT;
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatLifecycleAllocation(volume, workspace, lifecycleWorkspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatLifecycleData(volume, workspace, lifecycleWorkspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatLifecycleDirectory(volume, workspace, parent, stage);
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outPlan, stage, sizeof(*outPlan));
    UmicomFatClear(workspace, sizeof(*workspace)); workspace->self = workspace;
    UmicomFatClear(lifecycleWorkspace, sizeof(*lifecycleWorkspace)); lifecycleWorkspace->self = lifecycleWorkspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}
#endif

/* Metadata-only moves follow the same complete ownership proof as allocation operations.
 * The preceding implementation is retained for engineering review. */
#if 0
UmicomKernelDiskStatus UmicomKernelFat16PlanLifecycle(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan)
{
    UmicomSize pathBytes = 0U;
    UmicomKernelFat16FileTimeEncoding encoded;
    UmicomKernelDiskStatus status = UmicomFatLifecycleArguments(volume, request, workspace,
        lifecycleWorkspace, outPlan, &pathBytes, &encoded);
    if (status != UMICOM_DISK_OK) return status;
    /* Copy scalar intent before admission; the request itself is never read
     * after a callback. Its borrowed pointers are used only for these copies. */
    const UmicomKernelFat16LifecycleRequest selected = *request;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace; workspace->busy = UMICOM_TRUE;
    UmicomFatClear(lifecycleWorkspace, sizeof(*lifecycleWorkspace));
    lifecycleWorkspace->self = lifecycleWorkspace; lifecycleWorkspace->busy = UMICOM_TRUE;
    UmicomFatCopy(lifecycleWorkspace->path, selected.path, pathBytes);
    if (selected.bytes) UmicomFatCopy(workspace->input, selected.input, selected.bytes);
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    stage->operation = selected.operation;
    stage->requestedTime = selected.time;
    stage->encodedTime = encoded;
    stage->requestedBytes = selected.bytes;
    const UmicomBoolean makeDirectory = selected.operation == UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY;
    const UmicomBoolean removeDirectory = selected.operation == UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY;
    const UmicomBoolean create = selected.operation == UMICOM_FAT16_LIFECYCLE_CREATE || makeDirectory;
    const UmicomBoolean remove = selected.operation == UMICOM_FAT16_LIFECYCLE_DELETE || removeDirectory;
    UmicomU16 parent = 0U;
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK && create) {
        status = UmicomFatLifecycleCreateLookup(volume, lifecycleWorkspace->path, &stage->updatedEntry, &parent);
        if (status == UMICOM_DISK_OK) {
            stage->updatedEntryPresent = UMICOM_TRUE;
            stage->updatedEntry.bytes = (UmicomU32)selected.bytes;
            stage->updatedEntry.attributes = makeDirectory ? 0x10U : 0x20U;
            stage->updatedEntry.directory = makeDirectory;
        }
    } else if (status == UMICOM_DISK_OK) {
        status = UmicomFatFileLookup(volume, lifecycleWorkspace->path, &stage->originalEntry, &parent);
        if (status == UMICOM_DISK_OK && removeDirectory) {
            if (!stage->originalEntry.directory) status = UMICOM_DISK_NOT_DIRECTORY;
            else if (!stage->originalEntry.firstCluster) status = UMICOM_DISK_IS_DIRECTORY;
            else {
                /* Ignore canonical dot records, but never erase live children.
                 * The full namespace proof below still validates dot ownership. */
                status = UmicomFatDirectoryRead(volume, stage->originalEntry.firstCluster);
                if (status == UMICOM_DISK_OK && volume->directoryStage.count)
                    status = UMICOM_DISK_NOT_EMPTY;
            }
        } else if (status == UMICOM_DISK_OK && stage->originalEntry.directory)
            status = UMICOM_DISK_IS_DIRECTORY;
        if (status == UMICOM_DISK_OK && (stage->originalEntry.attributes & 0x01U)) status = UMICOM_DISK_READ_ONLY;
        if (status == UMICOM_DISK_OK) {
            stage->originalEntryPresent = UMICOM_TRUE;
            if (!remove) {
                stage->updatedEntryPresent = UMICOM_TRUE;
                stage->updatedEntry = stage->originalEntry;
                stage->updatedEntry.attributes |= 0x20U;
            }
            if (selected.operation == UMICOM_FAT16_LIFECYCLE_APPEND) {
                stage->offset = stage->originalEntry.bytes;
                if (selected.bytes > (UmicomU64)0xffffffffU - stage->offset) status = UMICOM_DISK_RANGE;
                else stage->updatedEntry.bytes = (UmicomU32)(stage->offset + selected.bytes);
            } else if (selected.operation == UMICOM_FAT16_LIFECYCLE_TRUNCATE) {
                if (selected.size > stage->originalEntry.bytes) status = UMICOM_DISK_RANGE;
                else stage->updatedEntry.bytes = selected.size;
            }
        }
        if (status == UMICOM_DISK_OK) {
            const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
            const UmicomU64 required = ((UmicomU64)stage->originalEntry.bytes + clusterBytes - 1U) / clusterBytes;
            status = UmicomFatChain(volume, stage->originalEntry.firstCluster, !removeDirectory,
                required, workspace->targetChain, &workspace->targetClusters);
        }
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK && create &&
        workspace->objects >= UMICOM_FAT16_UPDATE_OBJECT_LIMIT) status = UMICOM_DISK_LIMIT;
    /* A newly published directory must fit the same bounded reader used after
     * reboot. Large FAT16 clusters are legal, but this implementation cannot
     * promise access to more raw directory entries than it can inspect. */
    if (status == UMICOM_DISK_OK && makeDirectory &&
        (UmicomU64)volume->info.sectorsPerCluster * 16U > UMICOM_FAT16_SCAN_ENTRIES)
        status = UMICOM_DISK_LIMIT;
    if (status == UMICOM_DISK_OK && makeDirectory &&
        workspace->directoryCount >= UMICOM_FAT16_UPDATE_DIRECTORY_LIMIT) status = UMICOM_DISK_LIMIT;
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatLifecycleAllocation(volume, workspace, lifecycleWorkspace);
    if (status == UMICOM_DISK_OK) status = UmicomFatLifecycleData(volume, workspace, lifecycleWorkspace);
    if (status == UMICOM_DISK_OK && makeDirectory) UmicomFatLifecycleDotEntries(stage, parent);
    if (status == UMICOM_DISK_OK) {
        status = UmicomFatLifecycleDirectory(volume, workspace, parent, stage);
        if (status == UMICOM_DISK_NO_SPACE && create)
            status = UmicomFatLifecycleGrowParent(volume, workspace, lifecycleWorkspace, parent);
    }
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outPlan, stage, sizeof(*outPlan));
    UmicomFatClear(workspace, sizeof(*workspace)); workspace->self = workspace;
    UmicomFatClear(lifecycleWorkspace, sizeof(*lifecycleWorkspace)); lifecycleWorkspace->self = lifecycleWorkspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}
#endif

/* Read-only move helpers share the existing namespace proof. */
#include "fat16_move_plan.inc"

UmicomKernelDiskStatus UmicomKernelFat16PlanLifecycle(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan)
{
    UmicomSize pathBytes = 0U;
    UmicomSize destinationBytes = 0U;
    UmicomKernelFat16FileTimeEncoding encoded;
    UmicomKernelDiskStatus status = UmicomFatLifecycleArguments(volume, request, workspace,
        lifecycleWorkspace, outPlan, &pathBytes, &encoded, &destinationBytes);
    if (status != UMICOM_DISK_OK) return status;
    /* Copy scalar intent before admission; the request itself is never read
     * after a callback. Its borrowed pointers are used only for these copies. */
    const UmicomKernelFat16LifecycleRequest selected = *request;
    status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    UmicomFatClear(workspace, sizeof(*workspace));
    workspace->self = workspace; workspace->busy = UMICOM_TRUE;
    UmicomFatClear(lifecycleWorkspace, sizeof(*lifecycleWorkspace));
    lifecycleWorkspace->self = lifecycleWorkspace; lifecycleWorkspace->busy = UMICOM_TRUE;
    UmicomFatCopy(lifecycleWorkspace->path, selected.path, pathBytes);
    if (destinationBytes) UmicomFatCopy(lifecycleWorkspace->destination, selected.destination, destinationBytes);
    if (selected.bytes) UmicomFatCopy(workspace->input, selected.input, selected.bytes);
    UmicomKernelFat16LifecyclePlan *const stage = &lifecycleWorkspace->stage;
    stage->operation = selected.operation;
    stage->requestedTime = selected.time;
    stage->encodedTime = encoded;
    stage->requestedBytes = selected.bytes;
    const UmicomBoolean move = selected.operation == UMICOM_FAT16_LIFECYCLE_MOVE;
    const UmicomBoolean makeDirectory = selected.operation == UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY;
    const UmicomBoolean removeDirectory = selected.operation == UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY;
    const UmicomBoolean create = selected.operation == UMICOM_FAT16_LIFECYCLE_CREATE || makeDirectory;
    const UmicomBoolean remove = selected.operation == UMICOM_FAT16_LIFECYCLE_DELETE || removeDirectory;
    UmicomU16 parent = 0U;
    status = UmicomFatPlanGeometry(volume);
    if (status == UMICOM_DISK_OK && create) {
        status = UmicomFatLifecycleCreateLookup(volume, lifecycleWorkspace->path, &stage->updatedEntry, &parent);
        if (status == UMICOM_DISK_OK) {
            stage->updatedEntryPresent = UMICOM_TRUE;
            stage->updatedEntry.bytes = (UmicomU32)selected.bytes;
            stage->updatedEntry.attributes = makeDirectory ? 0x10U : 0x20U;
            stage->updatedEntry.directory = makeDirectory;
        }
    } else if (status == UMICOM_DISK_OK) {
        status = UmicomFatFileLookup(volume, lifecycleWorkspace->path, &stage->originalEntry, &parent);
        if (status == UMICOM_DISK_OK && removeDirectory) {
            if (!stage->originalEntry.directory) status = UMICOM_DISK_NOT_DIRECTORY;
            else if (!stage->originalEntry.firstCluster) status = UMICOM_DISK_IS_DIRECTORY;
            else {
                /* Ignore canonical dot records, but never erase live children.
                 * The full namespace proof below still validates dot ownership. */
                status = UmicomFatDirectoryRead(volume, stage->originalEntry.firstCluster);
                if (status == UMICOM_DISK_OK && volume->directoryStage.count)
                    status = UMICOM_DISK_NOT_EMPTY;
            }
        } else if (status == UMICOM_DISK_OK && stage->originalEntry.directory && !move)
            status = UMICOM_DISK_IS_DIRECTORY;
        if (status == UMICOM_DISK_OK && move && stage->originalEntry.directory &&
            !stage->originalEntry.firstCluster) status = UMICOM_DISK_IS_DIRECTORY;
        if (status == UMICOM_DISK_OK && (stage->originalEntry.attributes & 0x01U)) status = UMICOM_DISK_READ_ONLY;
        if (status == UMICOM_DISK_OK) {
            stage->originalEntryPresent = UMICOM_TRUE;
            if (!remove) {
                stage->updatedEntryPresent = UMICOM_TRUE;
                stage->updatedEntry = stage->originalEntry;
                if (!move) stage->updatedEntry.attributes |= 0x20U;
            }
            if (selected.operation == UMICOM_FAT16_LIFECYCLE_APPEND) {
                stage->offset = stage->originalEntry.bytes;
                if (selected.bytes > (UmicomU64)0xffffffffU - stage->offset) status = UMICOM_DISK_RANGE;
                else stage->updatedEntry.bytes = (UmicomU32)(stage->offset + selected.bytes);
            } else if (selected.operation == UMICOM_FAT16_LIFECYCLE_TRUNCATE) {
                if (selected.size > stage->originalEntry.bytes) status = UMICOM_DISK_RANGE;
                else stage->updatedEntry.bytes = selected.size;
            }
        }
        if (status == UMICOM_DISK_OK) {
            const UmicomU64 clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
            const UmicomU64 required = ((UmicomU64)stage->originalEntry.bytes + clusterBytes - 1U) / clusterBytes;
            status = UmicomFatChain(volume, stage->originalEntry.firstCluster, !(removeDirectory || (move && stage->originalEntry.directory)),
                required, workspace->targetChain, &workspace->targetClusters);
        }
    }
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanNamespace(volume, workspace);
    if (status == UMICOM_DISK_OK && create &&
        workspace->objects >= UMICOM_FAT16_UPDATE_OBJECT_LIMIT) status = UMICOM_DISK_LIMIT;
    /* A newly published directory must fit the same bounded reader used after
     * reboot. Large FAT16 clusters are legal, but this implementation cannot
     * promise access to more raw directory entries than it can inspect. */
    if (status == UMICOM_DISK_OK && makeDirectory &&
        (UmicomU64)volume->info.sectorsPerCluster * 16U > UMICOM_FAT16_SCAN_ENTRIES)
        status = UMICOM_DISK_LIMIT;
    if (status == UMICOM_DISK_OK && makeDirectory &&
        workspace->directoryCount >= UMICOM_FAT16_UPDATE_DIRECTORY_LIMIT) status = UMICOM_DISK_LIMIT;
    if (status == UMICOM_DISK_OK) status = UmicomFatPlanAllocation(volume, workspace);
    if (status == UMICOM_DISK_OK && move)
        status = UmicomFatLifecycleMove(volume, workspace, lifecycleWorkspace, parent);
    if (status == UMICOM_DISK_OK && !move) status = UmicomFatLifecycleAllocation(volume, workspace, lifecycleWorkspace);
    if (status == UMICOM_DISK_OK && !move) status = UmicomFatLifecycleData(volume, workspace, lifecycleWorkspace);
    if (status == UMICOM_DISK_OK && makeDirectory) UmicomFatLifecycleDotEntries(stage, parent);
    if (status == UMICOM_DISK_OK && !move) {
        status = UmicomFatLifecycleDirectory(volume, workspace, parent, stage);
        if (status == UMICOM_DISK_NO_SPACE && create)
            status = UmicomFatLifecycleGrowParent(volume, workspace, lifecycleWorkspace, parent);
    }
    if (status == UMICOM_DISK_OK) UmicomFatCopy(outPlan, stage, sizeof(*outPlan));
    UmicomFatClear(workspace, sizeof(*workspace)); workspace->self = workspace;
    UmicomFatClear(lifecycleWorkspace, sizeof(*lifecycleWorkspace)); lifecycleWorkspace->self = lifecycleWorkspace;
    volume->fatCached = UMICOM_FALSE;
    return UmicomFatFinish(volume, status);
}
