/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_provider.c
 *
 * PURPOSE:
 *   Give existing VFS clients bounded read-only FAT16 access while preserving
 *   the inspector as the only interpreter of partition and filesystem bytes.
 *
 * EDUCATIONAL OVERVIEW:
 *   Names are canonical paths and IDs are monotonic capabilities within this
 *   provider. A cached name owns no disk allocation. A pin only prevents its
 *   cache record from being evicted; the transport remains borrowed until the
 *   mount and all descriptions release their pins and the provider closes.
 *
 *   Cleanup inspects local ownership only. A failed sector read must not make
 *   closing a descriptor depend on the same unavailable sector. Immutable
 *   metadata snapshots therefore serve stat, validation and pin accounting.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_provider.h"
#include "vfs_internal.h"

static void UmicomFatProviderCopy(void *target, const void *source, UmicomSize bytes)
{
    /* Freestanding compilation alone does not prevent the compiler from
     * lowering a large structure assignment to an external memcpy call. The
     * volatile destination keeps these bounded byte stores explicit, so this
     * provider links without importing a hosted memory runtime at any level
     * of optimisation. The same rule applies to staged result publication. */
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}

static UmicomKernelVfsStatus UmicomFatProviderDiskResult(UmicomKernelDiskStatus status)
{
    /* A broken disk is different from damaged Kernel ownership. Preserve the
     * exact inspector status in the owner as well as this public VFS category. */
    switch (status) {
    case UMICOM_DISK_OK: return UMICOM_VFS_OK;
    case UMICOM_DISK_INVALID_ARGUMENT: return UMICOM_VFS_INVALID_ARGUMENT;
    case UMICOM_DISK_BAD_STATE: return UMICOM_VFS_BAD_STATE;
    case UMICOM_DISK_IO_ERROR: return UMICOM_VFS_IO_ERROR;
    case UMICOM_DISK_UNSUPPORTED_TABLE:
    case UMICOM_DISK_UNSUPPORTED_FILESYSTEM: return UMICOM_VFS_UNSUPPORTED;
    case UMICOM_DISK_NOT_FOUND: return UMICOM_VFS_NOT_FOUND;
    case UMICOM_DISK_NOT_DIRECTORY: return UMICOM_VFS_NOT_DIRECTORY;
    case UMICOM_DISK_IS_DIRECTORY: return UMICOM_VFS_NOT_FILE;
    case UMICOM_DISK_LIMIT: return UMICOM_VFS_INSPECTION_LIMIT;
    case UMICOM_DISK_BUSY: return UMICOM_VFS_BUSY;
    case UMICOM_DISK_SIGNATURE:
    case UMICOM_DISK_CORRUPT:
    case UMICOM_DISK_RANGE:
    case UMICOM_DISK_OVERLAP:
    case UMICOM_DISK_FAT_MISMATCH:
    case UMICOM_DISK_CHAIN_CYCLE:
    case UMICOM_DISK_DIRTY: return UMICOM_VFS_CORRUPT_FILESYSTEM;
    default: return UMICOM_VFS_CORRUPT_STATE;
    }
}

static UmicomU8 UmicomFatProviderUpper(UmicomU8 byte)
{
    return byte >= 'a' && byte <= 'z' ? (UmicomU8)(byte - 'a' + 'A') : byte;
}
static UmicomBoolean UmicomFatProviderNameByte(UmicomU8 byte)
{
    byte = UmicomFatProviderUpper(byte);
    if ((byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')) return UMICOM_TRUE;
    /* This admission alphabet matches the inspector's documented portable
     * short aliases. Disk decoding and FAT validation remain in that module. */
    switch (byte) {
    case '_': case '-': case '$': case '~': case '!': case '#': case '%':
    case '&': case '(': case ')': case '@': case '^': case '{': case '}':
    case '\'': return UMICOM_TRUE;
    default: return UMICOM_FALSE;
    }
}
static UmicomKernelVfsStatus UmicomFatProviderAlias(const char *name, char *outName)
{
    if (!name || !name[0]) return UMICOM_VFS_INVALID_PATH;
    UmicomSize base = 0U, extension = 0U;
    UmicomBoolean dotted = UMICOM_FALSE;
    for (UmicomSize i = 0U; i < 13U; ++i) {
        const UmicomU8 byte = (UmicomU8)name[i];
        if (!byte) {
            if (!base || (dotted && !extension)) return UMICOM_VFS_INVALID_PATH;
            outName[i] = '\0';
            return UMICOM_VFS_OK;
        }
        if (i == 12U) return UMICOM_VFS_INVALID_PATH;
        if (byte == '.') {
            if (dotted || !base) return UMICOM_VFS_INVALID_PATH;
            dotted = UMICOM_TRUE;
        } else {
            if (!UmicomFatProviderNameByte(byte)) return UMICOM_VFS_INVALID_PATH;
            if (dotted) ++extension; else ++base;
            if (base > 8U || extension > 3U) return UMICOM_VFS_INVALID_PATH;
        }
        outName[i] = (char)UmicomFatProviderUpper(byte);
    }
    return UMICOM_VFS_INVALID_PATH;
}
static UmicomBoolean UmicomFatProviderStringEqual(const char *left, const char *right,
    UmicomSize bound)
{
    for (UmicomSize i = 0U; i < bound; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (!left[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomFatProviderPathShape(const char *path,
    UmicomSize *outLength, UmicomSize *outDepth, UmicomSize *outLast)
{
    if (!path || path[0] != '/') return UMICOM_VFS_INVALID_PATH;
    UmicomSize length = 0U;
    while (length < UMICOM_FAT16_PATH_BYTES && path[length]) ++length;
    if (length == UMICOM_FAT16_PATH_BYTES) return UMICOM_VFS_INSPECTION_LIMIT;
    if (length == 1U) {
        *outLength = length; *outDepth = 0U; *outLast = 0U;
        return UMICOM_VFS_OK;
    }
    UmicomSize depth = 0U, start = 1U, last = 0U;
    while (start < length) {
        if (depth == UMICOM_FAT16_DEPTH_LIMIT) return UMICOM_VFS_INSPECTION_LIMIT;
        UmicomSize end = start;
        while (end < length && path[end] != '/') ++end;
        if (end == start || end - start > 12U) return UMICOM_VFS_INVALID_PATH;
        char alias[13], canonical[13];
        UmicomVfsClear(alias, sizeof(alias));
        UmicomVfsClear(canonical, sizeof(canonical));
        UmicomFatProviderCopy(alias, path + start, end - start);
        if (UmicomFatProviderAlias(alias, canonical) != UMICOM_VFS_OK ||
            !UmicomFatProviderStringEqual(alias, canonical, sizeof(alias)))
            return UMICOM_VFS_INVALID_PATH;
        ++depth;
        last = start;
        if (end == length) break;
        start = end + 1U;
        if (start == length) return UMICOM_VFS_INVALID_PATH;
    }
    *outLength = length; *outDepth = depth; *outLast = last;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomFatProviderChildPath(const char *parent,
    const char *name, char *outPath)
{
    char alias[13];
    UmicomVfsClear(alias, sizeof(alias));
    UmicomKernelVfsStatus status = UmicomFatProviderAlias(name, alias);
    if (status != UMICOM_VFS_OK) return status;
    UmicomSize parentBytes = 0U, depth = 0U, last = 0U;
    status = UmicomFatProviderPathShape(parent, &parentBytes, &depth, &last);
    if (status != UMICOM_VFS_OK) return status;
    if (depth == UMICOM_FAT16_DEPTH_LIMIT) return UMICOM_VFS_INSPECTION_LIMIT;
    UmicomSize nameBytes = 0U;
    while (alias[nameBytes]) ++nameBytes;
    const UmicomSize separator = parentBytes == 1U ? 0U : 1U;
    if (parentBytes + separator + nameBytes >= UMICOM_FAT16_PATH_BYTES)
        return UMICOM_VFS_INSPECTION_LIMIT;
    UmicomVfsClear(outPath, UMICOM_FAT16_PATH_BYTES);
    UmicomFatProviderCopy(outPath, parent, parentBytes);
    if (separator) outPath[parentBytes] = '/';
    UmicomFatProviderCopy(outPath + parentBytes + separator, alias, nameBytes + 1U);
    return UMICOM_VFS_OK;
}

static UmicomKernelVfsStatus UmicomFatProviderPoison(UmicomKernelFat16Provider *provider)
{
    provider->state = UMICOM_VFS_POISONED;
    return UMICOM_VFS_CORRUPT_STATE;
}
static UmicomBoolean UmicomFatProviderEntryEqual(const UmicomKernelFat16Entry *left,
    const UmicomKernelFat16Entry *right)
{
    /* Compare semantic fields, not padding bytes in a compiler-owned layout. */
    return UmicomFatProviderStringEqual(left->name, right->name, sizeof(left->name)) &&
        left->directory == right->directory && left->bytes == right->bytes &&
        left->firstCluster == right->firstCluster && left->attributes == right->attributes ?
        UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomFatProviderEntryValid(const UmicomKernelFat16Provider *provider,
    const UmicomKernelFat16Entry *entry)
{
    char canonical[13];
    UmicomVfsClear(canonical, sizeof(canonical));
    if ((entry->directory != UMICOM_FALSE && entry->directory != UMICOM_TRUE) ||
        UmicomFatProviderAlias(entry->name, canonical) != UMICOM_VFS_OK ||
        !UmicomFatProviderStringEqual(entry->name, canonical, sizeof(canonical)) ||
        (entry->attributes & 0xc8U) ||
        ((entry->attributes & 0x10U) != 0U) != (entry->directory == UMICOM_TRUE) ||
        (entry->directory && entry->bytes)) return UMICOM_FALSE;
    if (entry->directory || entry->bytes) {
        return entry->firstCluster >= 2U && entry->firstCluster < 0xfff0U &&
            entry->firstCluster <= provider->volume.info.clusters + 1U ? UMICOM_TRUE : UMICOM_FALSE;
    }
    return entry->firstCluster == 0U ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomFatProviderValidate(void *context)
{
    UmicomKernelFat16Provider *const provider = (UmicomKernelFat16Provider *)context;
    if (!provider) return UMICOM_VFS_INVALID_ARGUMENT;
    /* Check the guard before self so a begin callback cannot re-enter even an
     * Open whose geometry has not yet been admitted or published. */
    if (provider->busy == UMICOM_TRUE) return UMICOM_VFS_BUSY;
    if (provider->self != provider) return UMICOM_VFS_BAD_STATE;
    if (provider->state == UMICOM_VFS_POISONED) return UMICOM_VFS_CORRUPT_STATE;
    if (provider->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    if (provider->busy != UMICOM_FALSE || provider->volume.self != &provider->volume ||
        provider->volume.open != UMICOM_TRUE || provider->volume.busy != UMICOM_FALSE ||
        !provider->volume.reader.read || !provider->volume.reader.sectors ||
        provider->nextNodeId == UMICOM_FAT16_PROVIDER_ROOT_ID)
        return UmicomFatProviderPoison(provider);

    UmicomSize pins = 0U;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_PROVIDER_NODE_LIMIT; ++i) {
        const UmicomKernelFat16ProviderNode *const node = &provider->nodes[i];
        if (node->occupied == UMICOM_FALSE) {
            if (i == 0U || !UmicomVfsZero(node, sizeof(*node))) return UmicomFatProviderPoison(provider);
            continue;
        }
        if (node->occupied != UMICOM_TRUE || !node->id ||
            (provider->nextNodeId && node->id >= provider->nextNodeId) ||
            pins > ~(UmicomSize)0U - node->pins) return UmicomFatProviderPoison(provider);
        UmicomSize length = 0U, depth = 0U, last = 0U;
        if (UmicomFatProviderPathShape(node->path, &length, &depth, &last) != UMICOM_VFS_OK)
            return UmicomFatProviderPoison(provider);
        if (i == 0U) {
            if (node->id != UMICOM_FAT16_PROVIDER_ROOT_ID || depth != 0U || length != 1U ||
                node->entry.name[0] != '/' || node->entry.name[1] != '\0' ||
                node->entry.directory != UMICOM_TRUE || node->entry.bytes ||
                node->entry.firstCluster || node->entry.attributes) return UmicomFatProviderPoison(provider);
        } else if (!depth || !UmicomFatProviderEntryValid(provider, &node->entry) ||
            !UmicomFatProviderStringEqual(node->path + last, node->entry.name, sizeof(node->entry.name))) {
            return UmicomFatProviderPoison(provider);
        }
        for (UmicomSize earlier = 0U; earlier < i; ++earlier) {
            const UmicomKernelFat16ProviderNode *const other = &provider->nodes[earlier];
            if (other->occupied && (other->id == node->id ||
                UmicomFatProviderStringEqual(other->path, node->path, sizeof(node->path))))
                return UmicomFatProviderPoison(provider);
        }
        pins += node->pins;
    }
    return pins == provider->pins ? UMICOM_VFS_OK : UmicomFatProviderPoison(provider);
}
static UmicomSize UmicomFatProviderNodeIndex(const UmicomKernelFat16Provider *provider,
    UmicomKernelVfsNodeId id)
{
    if (id) {
        for (UmicomSize i = 0U; i < UMICOM_FAT16_PROVIDER_NODE_LIMIT; ++i)
            if (provider->nodes[i].occupied && provider->nodes[i].id == id) return i;
    }
    return UMICOM_FAT16_PROVIDER_NODE_LIMIT;
}
static UmicomKernelVfsStatus UmicomFatProviderGet(void *context, UmicomKernelVfsNodeId id,
    UmicomKernelFat16ProviderNode **outNode)
{
    UmicomKernelVfsStatus status = UmicomFatProviderValidate(context);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelFat16Provider *const provider = (UmicomKernelFat16Provider *)context;
    const UmicomSize index = UmicomFatProviderNodeIndex(provider, id);
    if (index == UMICOM_FAT16_PROVIDER_NODE_LIMIT) return UMICOM_VFS_NOT_FOUND;
    *outNode = &provider->nodes[index];
    return UMICOM_VFS_OK;
}
static UmicomSize UmicomFatProviderPathIndex(const UmicomKernelFat16Provider *provider,
    const char *path)
{
    for (UmicomSize i = 0U; i < UMICOM_FAT16_PROVIDER_NODE_LIMIT; ++i) {
        if (provider->nodes[i].occupied &&
            UmicomFatProviderStringEqual(provider->nodes[i].path, path, UMICOM_FAT16_PATH_BYTES)) return i;
    }
    return UMICOM_FAT16_PROVIDER_NODE_LIMIT;
}
static UmicomKernelVfsStatus UmicomFatProviderReserve(const UmicomKernelFat16Provider *provider,
    UmicomSize *outSlot)
{
    if (!provider->nextNodeId) return UMICOM_VFS_EXHAUSTED;
    UmicomSize oldest = UMICOM_FAT16_PROVIDER_NODE_LIMIT;
    for (UmicomSize i = 1U; i < UMICOM_FAT16_PROVIDER_NODE_LIMIT; ++i) {
        const UmicomKernelFat16ProviderNode *const node = &provider->nodes[i];
        if (!node->occupied) { *outSlot = i; return UMICOM_VFS_OK; }
        if (!node->pins && (oldest == UMICOM_FAT16_PROVIDER_NODE_LIMIT ||
            node->id < provider->nodes[oldest].id)) oldest = i;
    }
    if (oldest == UMICOM_FAT16_PROVIDER_NODE_LIMIT) return UMICOM_VFS_CAPACITY;
    *outSlot = oldest;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsNodeId UmicomFatProviderPublish(UmicomKernelFat16Provider *provider,
    UmicomSize slot, const char *path, const UmicomKernelFat16Entry *entry)
{
    /* Admission has already reserved an unpinned slot and validated the entire
     * inspector result. Reusing storage does not resurrect its previous ID.
     * The last integer ID may be issued once; zero then permanently refuses
     * further identities instead of allowing unsigned wraparound. */
    UmicomKernelFat16ProviderNode *const node = &provider->nodes[slot];
    UmicomVfsClear(node, sizeof(*node));
    UmicomFatProviderCopy(node->path, path, sizeof(node->path));
    UmicomFatProviderCopy(&node->entry, entry, sizeof(node->entry));
    node->id = provider->nextNodeId;
    node->occupied = UMICOM_TRUE;
    provider->nextNodeId = node->id == ~(UmicomKernelVfsNodeId)0U ? 0U : node->id + 1U;
    return node->id;
}
static void UmicomFatProviderInfo(const UmicomKernelFat16ProviderNode *node,
    UmicomKernelVfsNodeInfo *outInfo)
{
    UmicomVfsClear(outInfo, sizeof(*outInfo));
    outInfo->id = node->id;
    outInfo->kind = node->entry.directory ? UMICOM_VFS_DIRECTORY : UMICOM_VFS_FILE;
    outInfo->bytes = node->entry.bytes;
    /* FAT16's directory entry stores a 32-bit logical file size. This bound
     * permits seeking past EOF; it does not grant permission to resize/write. */
    outInfo->maximumBytes = node->entry.directory ? 0U : (UmicomSize)~(UmicomU32)0U;
}
static UmicomKernelVfsStatus UmicomFatProviderBeginIo(UmicomKernelFat16Provider *provider)
{
    provider->busy = UMICOM_TRUE;
    if (provider->ioPolicy.begin) {
        const UmicomKernelVfsStatus status = provider->ioPolicy.begin(provider->ioPolicy.context);
        if (status != UMICOM_VFS_OK) { provider->busy = UMICOM_FALSE; return status; }
    }
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomFatProviderFinishIo(UmicomKernelFat16Provider *provider,
    UmicomKernelVfsStatus status)
{
    UmicomVfsClear(&provider->listStage, sizeof(provider->listStage));
    provider->busy = UMICOM_FALSE;
    return status;
}
static UmicomBoolean UmicomFatProviderUnused(const UmicomKernelFat16Provider *provider)
{
    /* Failed admission can remember its exact disk diagnosis without acquiring
     * identity or a transport lease. Every other byte must still be zero. */
    const UmicomU8 *const bytes = (const UmicomU8 *)provider;
    const UmicomU8 *const diagnostic = (const UmicomU8 *)&provider->lastDiskStatus;
    const UmicomSize start = (UmicomSize)(diagnostic - bytes);
    const UmicomSize end = start + sizeof(provider->lastDiskStatus);
    if ((UmicomU32)provider->lastDiskStatus > (UmicomU32)UMICOM_DISK_BUSY) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < sizeof(*provider); ++i)
        if ((i < start || i >= end) && bytes[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}

UmicomKernelVfsStatus UmicomKernelFat16ProviderOpen(UmicomKernelFat16Provider *provider,
    const UmicomKernelDiskReader *reader, UmicomSize partition,
    const UmicomKernelFat16ProviderIoPolicy *policy)
{
    if (!provider || !reader || !reader->read || !reader->sectors ||
        partition >= UMICOM_DISK_PRIMARY_PARTITIONS) return UMICOM_VFS_INVALID_ARGUMENT;
    if (provider->busy == UMICOM_TRUE) return UMICOM_VFS_BUSY;
    if (!UmicomFatProviderUnused(provider)) return UMICOM_VFS_BAD_STATE;
    UmicomKernelFat16ProviderIoPolicy copiedPolicy;
    UmicomVfsClear(&copiedPolicy, sizeof(copiedPolicy));
    if (policy) UmicomFatProviderCopy(&copiedPolicy, policy, sizeof(copiedPolicy));
    UmicomKernelDiskReader copiedReader;
    UmicomFatProviderCopy(&copiedReader, reader, sizeof(copiedReader));
    provider->busy = UMICOM_TRUE;
    if (copiedPolicy.begin) {
        const UmicomKernelVfsStatus status = copiedPolicy.begin(copiedPolicy.context);
        if (status != UMICOM_VFS_OK) { provider->busy = UMICOM_FALSE; return status; }
    }
    /* Inspector Open is transactional: its embedded owner stays zero after a
     * failure. Only successful geometry admission publishes our VFS identity. */
    const UmicomKernelDiskStatus diskStatus = UmicomKernelFat16Open(&provider->volume, &copiedReader, partition);
    provider->lastDiskStatus = diskStatus;
    if (diskStatus != UMICOM_DISK_OK) {
        provider->busy = UMICOM_FALSE;
        return UmicomFatProviderDiskResult(diskStatus);
    }
    provider->self = provider;
    provider->state = UMICOM_VFS_OPEN;
    UmicomFatProviderCopy(&provider->ioPolicy, &copiedPolicy, sizeof(provider->ioPolicy));
    provider->nextNodeId = UMICOM_FAT16_PROVIDER_ROOT_ID + 1U;
    UmicomKernelFat16ProviderNode *const root = &provider->nodes[0];
    root->id = UMICOM_FAT16_PROVIDER_ROOT_ID;
    root->path[0] = '/';
    root->entry.name[0] = '/';
    root->entry.directory = UMICOM_TRUE;
    root->occupied = UMICOM_TRUE;
    provider->busy = UMICOM_FALSE;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelFat16ProviderClose(UmicomKernelFat16Provider *provider)
{
    UmicomKernelVfsStatus status = UmicomFatProviderValidate(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (provider->pins) return UMICOM_VFS_BUSY;
    provider->busy = UMICOM_TRUE;
    /* Inspector Close clears scratch and geometry only; it never reads media
     * or resets the borrowed transport. Retain the last media diagnostic on
     * successful cleanup so a previous I/O failure remains explainable. */
    const UmicomKernelDiskStatus diskStatus = UmicomKernelFat16Close(&provider->volume);
    if (diskStatus != UMICOM_DISK_OK) {
        provider->lastDiskStatus = diskStatus;
        provider->busy = UMICOM_FALSE;
        return UmicomFatProviderDiskResult(diskStatus);
    }
    UmicomVfsClear(provider->nodes, sizeof(provider->nodes));
    UmicomVfsClear(&provider->listStage, sizeof(provider->listStage));
    UmicomVfsClear(&provider->ioPolicy, sizeof(provider->ioPolicy));
    provider->state = UMICOM_VFS_CLOSED;
    provider->busy = UMICOM_FALSE;
    return UMICOM_VFS_OK;
}

static UmicomKernelVfsStatus UmicomFatProviderRoot(void *context, UmicomKernelVfsNodeId *outNode)
{
    if (!outNode) return UMICOM_VFS_INVALID_ARGUMENT;
    const UmicomKernelVfsStatus status = UmicomFatProviderValidate(context);
    if (status == UMICOM_VFS_OK) *outNode = UMICOM_FAT16_PROVIDER_ROOT_ID;
    return status;
}
static UmicomKernelVfsStatus UmicomFatProviderLookup(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsNodeId *outNode)
{
    if (!outNode) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelFat16ProviderNode *parent = (UmicomKernelFat16ProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomFatProviderGet(context, directory, &parent);
    if (status != UMICOM_VFS_OK) return status;
    if (!parent->entry.directory) return UMICOM_VFS_NOT_DIRECTORY;
    char path[UMICOM_FAT16_PATH_BYTES];
    status = UmicomFatProviderChildPath(parent->path, name, path);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelFat16Provider *const provider = (UmicomKernelFat16Provider *)context;
    UmicomSize slot = UmicomFatProviderPathIndex(provider, path);
    if (slot != UMICOM_FAT16_PROVIDER_NODE_LIMIT) {
        *outNode = provider->nodes[slot].id;
        return UMICOM_VFS_OK;
    }
    status = UmicomFatProviderReserve(provider, &slot);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomFatProviderBeginIo(provider);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelFat16Entry entry;
    UmicomVfsClear(&entry, sizeof(entry));
    provider->lastDiskStatus = UmicomKernelFat16Stat(&provider->volume, path, &entry);
    status = UmicomFatProviderDiskResult(provider->lastDiskStatus);
    if (status == UMICOM_VFS_OK) {
        if (!UmicomFatProviderEntryValid(provider, &entry)) status = UMICOM_VFS_CORRUPT_STATE;
        else *outNode = UmicomFatProviderPublish(provider, slot, path, &entry);
    }
    return UmicomFatProviderFinishIo(provider, status);
}
static UmicomKernelVfsStatus UmicomFatProviderStat(void *context, UmicomKernelVfsNodeId id,
    UmicomKernelVfsNodeInfo *outInfo)
{
    if (!outInfo) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelFat16ProviderNode *node = (UmicomKernelFat16ProviderNode *)0;
    const UmicomKernelVfsStatus status = UmicomFatProviderGet(context, id, &node);
    if (status == UMICOM_VFS_OK) UmicomFatProviderInfo(node, outInfo);
    return status;
}
static UmicomKernelVfsStatus UmicomFatProviderPin(void *context, UmicomKernelVfsNodeId id)
{
    UmicomKernelFat16ProviderNode *node = (UmicomKernelFat16ProviderNode *)0;
    const UmicomKernelVfsStatus status = UmicomFatProviderGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelFat16Provider *const provider = (UmicomKernelFat16Provider *)context;
    if (provider->pins == ~(UmicomSize)0U || node->pins == ~(UmicomSize)0U) return UMICOM_VFS_EXHAUSTED;
    ++node->pins;
    ++provider->pins;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomFatProviderUnpin(void *context, UmicomKernelVfsNodeId id)
{
    UmicomKernelFat16ProviderNode *node = (UmicomKernelFat16ProviderNode *)0;
    const UmicomKernelVfsStatus status = UmicomFatProviderGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (!node->pins) return UMICOM_VFS_BAD_STATE;
    --node->pins;
    --((UmicomKernelFat16Provider *)context)->pins;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomFatProviderRead(void *context, UmicomKernelVfsNodeId id,
    UmicomSize offset, void *destination, UmicomSize bytes, UmicomSize *outRead)
{
    if (outRead) *outRead = 0U;
    if (!outRead || (bytes && !destination)) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelFat16ProviderNode *node = (UmicomKernelFat16ProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomFatProviderGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->entry.directory) return UMICOM_VFS_NOT_FILE;
    if (bytes > UMICOM_FAT16_READ_BYTES) return UMICOM_VFS_INSPECTION_LIMIT;
    if (!bytes) return UMICOM_VFS_OK;
    UmicomKernelFat16Provider *const provider = (UmicomKernelFat16Provider *)context;
    status = UmicomFatProviderBeginIo(provider);
    if (status != UMICOM_VFS_OK) return status;
    UmicomSize transferred = 0U;
    /* The inspector already stages the entire requested read. Passing a local
     * count translates its unchanged-on-error convention into VFS's zero-count
     * convention without a second 4096-byte buffer or a partial publication. */
    provider->lastDiskStatus = UmicomKernelFat16Read(&provider->volume, node->path,
        offset, destination, bytes, &transferred);
    status = UmicomFatProviderDiskResult(provider->lastDiskStatus);
    if (status == UMICOM_VFS_OK) *outRead = transferred;
    return UmicomFatProviderFinishIo(provider, status);
}
static UmicomKernelVfsStatus UmicomFatProviderCreate(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsKind kind)
{
    (void)context; (void)directory; (void)name; (void)kind;
    return UMICOM_VFS_READ_ONLY;
}
static UmicomKernelVfsStatus UmicomFatProviderUnlink(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsKind kind)
{
    (void)context; (void)directory; (void)name; (void)kind;
    return UMICOM_VFS_READ_ONLY;
}
static UmicomKernelVfsStatus UmicomFatProviderWrite(void *context, UmicomKernelVfsNodeId id,
    UmicomSize offset, const void *source, UmicomSize bytes, UmicomSize *outWritten)
{
    (void)context; (void)id; (void)offset; (void)source; (void)bytes;
    if (outWritten) *outWritten = 0U;
    return UMICOM_VFS_READ_ONLY;
}
static UmicomKernelVfsStatus UmicomFatProviderResize(void *context, UmicomKernelVfsNodeId id,
    UmicomSize bytes)
{
    (void)context; (void)id; (void)bytes;
    return UMICOM_VFS_READ_ONLY;
}
static UmicomKernelVfsStatus UmicomFatProviderEnumerate(void *context, UmicomKernelVfsNodeId id,
    UmicomU64 epoch, UmicomSize cursor, UmicomKernelVfsDirectoryEntry *outEntry,
    UmicomU64 *outEpoch, UmicomSize *outNext)
{
    if (!outEntry || !outEpoch || !outNext) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelFat16ProviderNode *directory = (UmicomKernelFat16ProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomFatProviderGet(context, id, &directory);
    if (status != UMICOM_VFS_OK) return status;
    if (!directory->entry.directory) return UMICOM_VFS_NOT_DIRECTORY;
    if (epoch && epoch != UMICOM_FAT16_PROVIDER_DIRECTORY_EPOCH) return UMICOM_VFS_CHANGED;
    if (cursor > UMICOM_FAT16_ENTRY_LIMIT) return UMICOM_VFS_RANGE;
    UmicomKernelFat16Provider *const provider = (UmicomKernelFat16Provider *)context;
    status = UmicomFatProviderBeginIo(provider);
    if (status != UMICOM_VFS_OK) return status;
    provider->lastDiskStatus = UmicomKernelFat16List(&provider->volume, directory->path, &provider->listStage);
    status = UmicomFatProviderDiskResult(provider->lastDiskStatus);
    if (status != UMICOM_VFS_OK) return UmicomFatProviderFinishIo(provider, status);
    if (cursor >= provider->listStage.count) {
        *outEpoch = UMICOM_FAT16_PROVIDER_DIRECTORY_EPOCH;
        *outNext = cursor;
        return UmicomFatProviderFinishIo(provider, UMICOM_VFS_END);
    }
    const UmicomKernelFat16Entry *const entry = &provider->listStage.entries[cursor];
    if (!UmicomFatProviderEntryValid(provider, entry))
        return UmicomFatProviderFinishIo(provider, UMICOM_VFS_CORRUPT_STATE);
    char path[UMICOM_FAT16_PATH_BYTES];
    status = UmicomFatProviderChildPath(directory->path, entry->name, path);
    if (status != UMICOM_VFS_OK) return UmicomFatProviderFinishIo(provider, status);
    UmicomSize slot = UmicomFatProviderPathIndex(provider, path);
    if (slot == UMICOM_FAT16_PROVIDER_NODE_LIMIT) {
        status = UmicomFatProviderReserve(provider, &slot);
        if (status != UMICOM_VFS_OK) return UmicomFatProviderFinishIo(provider, status);
        (void)UmicomFatProviderPublish(provider, slot, path, entry);
    } else if (!UmicomFatProviderEntryEqual(&provider->nodes[slot].entry, entry)) {
        /* An observed contradiction violates the immutable-medium promise.
         * Refuse it without overwriting a pinned snapshot or moving iteration. */
        return UmicomFatProviderFinishIo(provider, UMICOM_VFS_CHANGED);
    }
    UmicomKernelVfsDirectoryEntry staged;
    UmicomVfsClear(&staged, sizeof(staged));
    UmicomFatProviderCopy(staged.name, entry->name, sizeof(entry->name));
    UmicomFatProviderInfo(&provider->nodes[slot], &staged.info);
    /* One complete value and both cursor fields are published together only
     * after the complete directory, path and cache admission have succeeded. */
    UmicomFatProviderCopy(outEntry, &staged, sizeof(*outEntry));
    *outEpoch = UMICOM_FAT16_PROVIDER_DIRECTORY_EPOCH;
    *outNext = cursor + 1U;
    return UmicomFatProviderFinishIo(provider, UMICOM_VFS_OK);
}

const UmicomKernelVfsOperations *UmicomKernelFat16ProviderOperationsGet(void)
{
    static const UmicomKernelVfsOperations operations = {
        .validate = UmicomFatProviderValidate,
        .root = UmicomFatProviderRoot,
        .lookup = UmicomFatProviderLookup,
        .create = UmicomFatProviderCreate,
        .unlink = UmicomFatProviderUnlink,
        .stat = UmicomFatProviderStat,
        .pin = UmicomFatProviderPin,
        .unpin = UmicomFatProviderUnpin,
        .read = UmicomFatProviderRead,
        .write = UmicomFatProviderWrite,
        .resize = UmicomFatProviderResize,
        .enumerate = UmicomFatProviderEnumerate
    };
    return &operations;
}
