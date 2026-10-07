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
