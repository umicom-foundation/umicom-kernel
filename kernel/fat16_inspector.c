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
