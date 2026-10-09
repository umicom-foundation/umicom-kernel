/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_writable_provider.c
 *
 * A mutable VFS facade over one exclusively borrowed lifecycle owner. Namespace
 * and data updates are acknowledged to VFS only after accepted clean Finish.
 * Failure is retained separately from local lifetime: media cannot be trusted
 * after uncertainty, but descriptors must still release their cached pins.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_writable_provider.h"
#include "umicom/kernel/physical_memory.h"

static void UmicomKernelFat16WritableClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static void UmicomKernelFat16WritableCopy(void *target, const void *source, UmicomSize bytes)
{
    /* Keep large value publication freestanding at every optimisation level. */
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static UmicomBoolean UmicomKernelFat16WritableZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *const input = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (input[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomKernelFat16WritableSpan(UmicomAddress address, UmicomSize bytes)
{
    return address && bytes && address <= ~(UmicomAddress)0U - bytes ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelFat16WritableOverlap(UmicomAddress left, UmicomSize leftBytes,
    UmicomAddress right, UmicomSize rightBytes)
{
    if (!leftBytes || !rightBytes) return UMICOM_FALSE;
    return left <= right ? right - left < leftBytes : left - right < rightBytes;
}
static UmicomBoolean UmicomKernelFat16WritableDomainValid(const UmicomKernelBlockDomain *domain)
{
    return domain && !((UmicomAddress)domain % alignof(UmicomKernelBlockDomain)) &&
        UmicomKernelFat16WritableSpan((UmicomAddress)domain, sizeof(*domain)) &&
        domain->self == domain && domain->ready == UMICOM_TRUE && domain->count &&
        domain->count <= UMICOM_BLOCK_SLOT_LIMIT ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelFat16WritableDomainIndependent(
    const UmicomKernelBlockDomain *domain, UmicomAddress address, UmicomSize bytes)
{
    if (!UmicomKernelFat16WritableSpan(address, bytes) ||
        UmicomKernelFat16WritableOverlap(address, bytes, (UmicomAddress)domain, sizeof(*domain)))
        return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        const UmicomKernelBlockSlot *const slot = &domain->slots[i];
        if ((slot->queueFrame && UmicomKernelFat16WritableOverlap(address, bytes,
                slot->queueFrame, UMICOM_KERNEL_PAGE_SIZE)) ||
            (slot->dataFrame && UmicomKernelFat16WritableOverlap(address, bytes,
                slot->dataFrame, UMICOM_KERNEL_PAGE_SIZE))) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomKernelFat16WritableIndependent(
    const UmicomKernelFat16WritableProvider *provider, UmicomAddress address, UmicomSize bytes)
{
    return UmicomKernelFat16WritableDomainIndependent(provider->lifecycle->commit.updater.domain,
            address, bytes) &&
        !UmicomKernelFat16WritableOverlap(address, bytes, (UmicomAddress)provider, sizeof(*provider)) &&
        !UmicomKernelFat16WritableOverlap(address, bytes, (UmicomAddress)provider->lifecycle,
            sizeof(*provider->lifecycle)) ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableDiskStatus(UmicomKernelDiskStatus status)
{
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
    case UMICOM_DISK_READ_ONLY: return UMICOM_VFS_READ_ONLY;
    case UMICOM_DISK_EXISTS: return UMICOM_VFS_EXISTS;
    case UMICOM_DISK_NO_SPACE: return UMICOM_VFS_CAPACITY;
    case UMICOM_DISK_NOT_EMPTY: return UMICOM_VFS_NOT_EMPTY;
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
static UmicomKernelVfsStatus UmicomKernelFat16WritableUpdateStatus(
    const UmicomKernelFat16LifecycleCommitter *owner, UmicomKernelFat16UpdateStatus status)
{
    switch (status) {
    case UMICOM_FAT16_UPDATE_OK: return UMICOM_VFS_OK;
    case UMICOM_FAT16_UPDATE_INVALID_ARGUMENT: return UMICOM_VFS_INVALID_ARGUMENT;
    case UMICOM_FAT16_UPDATE_BAD_STATE: return UMICOM_VFS_BAD_STATE;
    case UMICOM_FAT16_UPDATE_BUSY: return UMICOM_VFS_BUSY;
    case UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT: return UMICOM_VFS_UNSAFE_CONTEXT;
    case UMICOM_FAT16_UPDATE_READ_ONLY: return UMICOM_VFS_READ_ONLY;
    case UMICOM_FAT16_UPDATE_RANGE: return UMICOM_VFS_RANGE;
    case UMICOM_FAT16_UPDATE_INSPECTION_LIMIT: return UMICOM_VFS_INSPECTION_LIMIT;
    case UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR:
        return UmicomKernelFat16WritableDiskStatus(owner->commit.updater.lastDiskStatus);
    case UMICOM_FAT16_UPDATE_RELEASE_FAILED: return UMICOM_VFS_RELEASE_FAILED;
    case UMICOM_FAT16_UPDATE_TRANSPORT_ERROR:
    case UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN: return UMICOM_VFS_IO_ERROR;
    default: return UMICOM_VFS_CORRUPT_STATE;
    }
}

static UmicomU8 UmicomKernelFat16WritableUpper(UmicomU8 byte)
{
    return byte >= 'a' && byte <= 'z' ? (UmicomU8)(byte - 'a' + 'A') : byte;
}
static UmicomBoolean UmicomKernelFat16WritableNameByte(UmicomU8 byte)
{
    byte = UmicomKernelFat16WritableUpper(byte);
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
static UmicomKernelVfsStatus UmicomKernelFat16WritableAlias(const char *name, char *outName)
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
            if (!UmicomKernelFat16WritableNameByte(byte)) return UMICOM_VFS_INVALID_PATH;
            if (dotted) ++extension; else ++base;
            if (base > 8U || extension > 3U) return UMICOM_VFS_INVALID_PATH;
        }
        outName[i] = (char)UmicomKernelFat16WritableUpper(byte);
    }
    return UMICOM_VFS_INVALID_PATH;
}
static UmicomBoolean UmicomKernelFat16WritableStringEqual(const char *left, const char *right,
    UmicomSize bound)
{
    for (UmicomSize i = 0U; i < bound; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (!left[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritablePathShape(const char *path,
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
        UmicomKernelFat16WritableClear(alias, sizeof(alias));
        UmicomKernelFat16WritableClear(canonical, sizeof(canonical));
        UmicomKernelFat16WritableCopy(alias, path + start, end - start);
        if (UmicomKernelFat16WritableAlias(alias, canonical) != UMICOM_VFS_OK ||
            !UmicomKernelFat16WritableStringEqual(alias, canonical, sizeof(alias)))
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
static UmicomKernelVfsStatus UmicomKernelFat16WritableChildPath(const char *parent,
    const char *name, char *outPath)
{
    char alias[13];
    UmicomKernelFat16WritableClear(alias, sizeof(alias));
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableAlias(name, alias);
    if (status != UMICOM_VFS_OK) return status;
    UmicomSize parentBytes = 0U, depth = 0U, last = 0U;
    status = UmicomKernelFat16WritablePathShape(parent, &parentBytes, &depth, &last);
    if (status != UMICOM_VFS_OK) return status;
    if (depth == UMICOM_FAT16_DEPTH_LIMIT) return UMICOM_VFS_INSPECTION_LIMIT;
    UmicomSize nameBytes = 0U;
    while (alias[nameBytes]) ++nameBytes;
    const UmicomSize separator = parentBytes == 1U ? 0U : 1U;
    if (parentBytes + separator + nameBytes >= UMICOM_FAT16_PATH_BYTES)
        return UMICOM_VFS_INSPECTION_LIMIT;
    UmicomKernelFat16WritableClear(outPath, UMICOM_FAT16_PATH_BYTES);
    UmicomKernelFat16WritableCopy(outPath, parent, parentBytes);
    if (separator) outPath[parentBytes] = '/';
    UmicomKernelFat16WritableCopy(outPath + parentBytes + separator, alias, nameBytes + 1U);
    return UMICOM_VFS_OK;
}

static UmicomBoolean UmicomKernelFat16WritableStorage(const UmicomKernelFat16WritableProvider *provider)
{
    return provider && !((UmicomAddress)provider % alignof(UmicomKernelFat16WritableProvider)) &&
        UmicomKernelFat16WritableSpan((UmicomAddress)provider, sizeof(*provider)) ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelFat16WritableEntryEqual(const UmicomKernelFat16Entry *left,
    const UmicomKernelFat16Entry *right)
{
    return UmicomKernelFat16WritableStringEqual(left->name, right->name, sizeof(left->name)) &&
        left->directory == right->directory && left->bytes == right->bytes &&
        left->firstCluster == right->firstCluster && left->attributes == right->attributes ?
        UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelFat16WritableEntryValid(const UmicomKernelFat16WritableProvider *provider,
    const UmicomKernelFat16Entry *entry)
{
    char canonical[13];
    UmicomKernelFat16WritableClear(canonical, sizeof(canonical));
    if ((entry->directory != UMICOM_FALSE && entry->directory != UMICOM_TRUE) ||
        UmicomKernelFat16WritableAlias(entry->name, canonical) != UMICOM_VFS_OK ||
        !UmicomKernelFat16WritableStringEqual(entry->name, canonical, sizeof(canonical)) ||
        (entry->attributes & 0xc8U) ||
        ((entry->attributes & 0x10U) != 0U) != (entry->directory == UMICOM_TRUE) ||
        (entry->directory && entry->bytes)) return UMICOM_FALSE;
    if (entry->directory || entry->bytes) return entry->firstCluster >= 2U &&
        entry->firstCluster < 0xfff0U && entry->firstCluster <= provider->clusters + 1U ?
        UMICOM_TRUE : UMICOM_FALSE;
    return entry->firstCluster == 0U ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableValidate(void *context)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    if (!UmicomKernelFat16WritableStorage(provider)) return UMICOM_VFS_INVALID_ARGUMENT;
    if (provider->busy == UMICOM_TRUE) return UMICOM_VFS_BUSY;
    if (provider->self != provider || provider->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    /* No media state is consulted here. VFS validates before closing each
     * description, so a dirty or unavailable disk must not poison this path. */
    if (provider->busy != UMICOM_FALSE ||
        (provider->mediaFailed != UMICOM_FALSE && provider->mediaFailed != UMICOM_TRUE) ||
        !provider->lifecycle ||
        (UmicomAddress)provider->lifecycle % alignof(UmicomKernelFat16LifecycleCommitter) ||
        !UmicomKernelFat16WritableSpan((UmicomAddress)provider->lifecycle, sizeof(*provider->lifecycle)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)provider, sizeof(*provider),
            (UmicomAddress)provider->lifecycle, sizeof(*provider->lifecycle)) ||
        !provider->directoryEpoch || !provider->clusters || !provider->maximumFileBytes ||
        provider->nextNodeId == UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_ROOT_ID)
        return UMICOM_VFS_CORRUPT_STATE;
    const UmicomKernelBlockDomain *const domain = provider->lifecycle->commit.updater.domain;
    if (!UmicomKernelFat16WritableDomainValid(domain) ||
        !UmicomKernelFat16WritableDomainIndependent(domain, (UmicomAddress)provider, sizeof(*provider)) ||
        !UmicomKernelFat16WritableDomainIndependent(domain, (UmicomAddress)provider->lifecycle,
            sizeof(*provider->lifecycle))) return UMICOM_VFS_CORRUPT_STATE;
    UmicomSize pins = 0U;
    for (UmicomSize i = 0U; i < UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT; ++i) {
        const UmicomKernelFat16WritableProviderNode *const node = &provider->nodes[i];
        if (node->occupied == UMICOM_FALSE) {
            if (!i || !UmicomKernelFat16WritableZero(node, sizeof(*node))) return UMICOM_VFS_CORRUPT_STATE;
            continue;
        }
        if (node->occupied != UMICOM_TRUE || !node->id ||
            (provider->nextNodeId && node->id >= provider->nextNodeId) ||
            pins > ~(UmicomSize)0U - node->pins) return UMICOM_VFS_CORRUPT_STATE;
        UmicomSize length = 0U, depth = 0U, last = 0U;
        if (UmicomKernelFat16WritablePathShape(node->path, &length, &depth, &last) != UMICOM_VFS_OK)
            return UMICOM_VFS_CORRUPT_STATE;
        if (!i) {
            if (node->id != UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_ROOT_ID || depth || length != 1U ||
                node->entry.name[0] != '/' || node->entry.name[1] || node->entry.directory != UMICOM_TRUE ||
                node->entry.bytes || node->entry.firstCluster || node->entry.attributes)
                return UMICOM_VFS_CORRUPT_STATE;
        } else if (!depth || !UmicomKernelFat16WritableEntryValid(provider, &node->entry) ||
            node->entry.bytes > provider->maximumFileBytes ||
            !UmicomKernelFat16WritableStringEqual(node->path + last, node->entry.name,
                sizeof(node->entry.name))) return UMICOM_VFS_CORRUPT_STATE;
        for (UmicomSize earlier = 0U; earlier < i; ++earlier) {
            const UmicomKernelFat16WritableProviderNode *const other = &provider->nodes[earlier];
            if (other->occupied && (other->id == node->id ||
                UmicomKernelFat16WritableStringEqual(other->path, node->path, sizeof(node->path))))
                return UMICOM_VFS_CORRUPT_STATE;
        }
        pins += node->pins;
    }
    return pins == provider->pins ? UMICOM_VFS_OK : UMICOM_VFS_CORRUPT_STATE;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableMediaReady(UmicomKernelFat16WritableProvider *provider)
{
    if (provider->mediaFailed) return UMICOM_VFS_IO_ERROR;
    UmicomKernelFat16LifecycleCommitter *const owner = provider->lifecycle;
    UmicomKernelFat16Updater *const updater = &owner->commit.updater;
    if (owner->busy || owner->commit.busy || updater->busy || owner->workspace.busy ||
        updater->workspace.busy || updater->volume.busy || updater->domain->busy)
        return UMICOM_VFS_BUSY;
    if (owner->self != owner || owner->commit.self != &owner->commit ||
        updater->self != updater || updater->state != UMICOM_FAT16_UPDATER_OPEN ||
        !updater->handle || updater->admitted != UMICOM_TRUE || updater->slot >= updater->domain->count ||
        (owner->commit.state != UMICOM_FAT16_COMMIT_READY &&
            owner->commit.state != UMICOM_FAT16_COMMIT_COMMITTED)) {
        provider->mediaFailed = UMICOM_TRUE;
        return UMICOM_VFS_BAD_STATE;
    }
    const UmicomKernelBlockSlot *const slot = &updater->domain->slots[updater->slot];
    if (slot->state != UMICOM_BLOCK_READY || slot->claimed != UMICOM_TRUE ||
        slot->writable != UMICOM_TRUE || !slot->queueFrame || !slot->dataFrame) {
        provider->mediaFailed = UMICOM_TRUE;
        return UMICOM_VFS_IO_ERROR;
    }
    if (owner->committedOperations != provider->committedOperations) {
        /* External use of the borrowed lifecycle cannot be reconciled with
         * cached identities by guessing which namespace mutation occurred. */
        provider->mediaFailed = UMICOM_TRUE;
        return UMICOM_VFS_CHANGED;
    }
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableBegin(UmicomKernelFat16WritableProvider *provider)
{
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableMediaReady(provider);
    if (status == UMICOM_VFS_OK) provider->busy = UMICOM_TRUE;
    return status;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableEnd(UmicomKernelFat16WritableProvider *provider,
    UmicomKernelVfsStatus status)
{
    UmicomKernelFat16WritableClear(&provider->queryStage, sizeof(provider->queryStage));
    UmicomKernelFat16WritableClear(provider->zeroStage, sizeof(provider->zeroStage));
    provider->lastStatus = status;
    provider->busy = UMICOM_FALSE;
    return status;
}
static UmicomSize UmicomKernelFat16WritableNodeIndex(const UmicomKernelFat16WritableProvider *provider,
    UmicomKernelVfsNodeId id)
{
    if (id) for (UmicomSize i = 0U; i < UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT; ++i)
        if (provider->nodes[i].occupied && provider->nodes[i].id == id) return i;
    return UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT;
}
static UmicomSize UmicomKernelFat16WritablePathIndex(const UmicomKernelFat16WritableProvider *provider,
    const char *path)
{
    for (UmicomSize i = 0U; i < UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT; ++i)
        if (provider->nodes[i].occupied &&
            UmicomKernelFat16WritableStringEqual(provider->nodes[i].path, path, UMICOM_FAT16_PATH_BYTES)) return i;
    return UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableGet(UmicomKernelFat16WritableProvider *provider,
    UmicomKernelVfsNodeId id, UmicomKernelFat16WritableProviderNode **outNode)
{
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableValidate(provider);
    if (status != UMICOM_VFS_OK) return status;
    const UmicomSize index = UmicomKernelFat16WritableNodeIndex(provider, id);
    if (index == UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT) return UMICOM_VFS_NOT_FOUND;
    *outNode = &provider->nodes[index];
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableReserve(const UmicomKernelFat16WritableProvider *provider,
    UmicomSize *outSlot)
{
    if (!provider->nextNodeId) return UMICOM_VFS_EXHAUSTED;
    UmicomSize oldest = UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT;
    for (UmicomSize i = 1U; i < UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT; ++i) {
        const UmicomKernelFat16WritableProviderNode *const node = &provider->nodes[i];
        if (!node->occupied) { *outSlot = i; return UMICOM_VFS_OK; }
        if (!node->pins && (oldest == UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT ||
            node->id < provider->nodes[oldest].id)) oldest = i;
    }
    if (oldest == UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT) return UMICOM_VFS_CAPACITY;
    *outSlot = oldest;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsNodeId UmicomKernelFat16WritablePublish(UmicomKernelFat16WritableProvider *provider,
    UmicomSize slot, const char *path, const UmicomKernelFat16Entry *entry)
{
    UmicomKernelFat16WritableProviderNode *const node = &provider->nodes[slot];
    UmicomKernelFat16WritableClear(node, sizeof(*node));
    UmicomKernelFat16WritableCopy(node->path, path, sizeof(node->path));
    UmicomKernelFat16WritableCopy(&node->entry, entry, sizeof(node->entry));
    node->id = provider->nextNodeId;
    node->occupied = UMICOM_TRUE;
    provider->nextNodeId = node->id == ~(UmicomKernelVfsNodeId)0U ? 0U : node->id + 1U;
    return node->id;
}
static void UmicomKernelFat16WritableInfo(const UmicomKernelFat16WritableProvider *provider,
    const UmicomKernelFat16WritableProviderNode *node, UmicomKernelVfsNodeInfo *outInfo)
{
    UmicomKernelFat16WritableClear(outInfo, sizeof(*outInfo));
    outInfo->id = node->id;
    outInfo->kind = node->entry.directory ? UMICOM_VFS_DIRECTORY : UMICOM_VFS_FILE;
    outInfo->bytes = node->entry.bytes;
    outInfo->maximumBytes = node->entry.directory ? 0U : provider->maximumFileBytes;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableName(
    const UmicomKernelFat16WritableProvider *provider, const char *name, char *outName,
    const void *callerOutput, UmicomSize callerOutputBytes)
{
    if (!name) return UMICOM_VFS_INVALID_ARGUMENT;
    const UmicomAddress address = (UmicomAddress)name;
    for (UmicomSize i = 0U; i < 13U; ++i) {
        if (address > ~(UmicomAddress)0U - i ||
            !UmicomKernelFat16WritableIndependent(provider, address + i, 1U) ||
            UmicomKernelFat16WritableOverlap(address + i, 1U,
                (UmicomAddress)callerOutput, callerOutputBytes)) return UMICOM_VFS_INVALID_ARGUMENT;
        outName[i] = name[i];
        if (!outName[i]) return UMICOM_VFS_OK;
    }
    return UMICOM_VFS_INVALID_PATH;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableQuery(UmicomKernelFat16WritableProvider *provider,
    UmicomKernelFat16QueryKind kind, const char *path, UmicomSize offset, UmicomSize bytes)
{
    const UmicomKernelFat16Query query = {kind, path, offset, bytes};
    provider->lastUpdateStatus = UmicomKernelFat16LifecycleQuery(provider->lifecycle,
        &query, &provider->queryStage);
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableUpdateStatus(provider->lifecycle,
        provider->lastUpdateStatus);
    if (status == UMICOM_VFS_OK && provider->queryStage.committedOperations != provider->committedOperations)
        status = UMICOM_VFS_CHANGED;
    if (status == UMICOM_VFS_IO_ERROR || status == UMICOM_VFS_CORRUPT_FILESYSTEM ||
        status == UMICOM_VFS_CORRUPT_STATE || status == UMICOM_VFS_CHANGED ||
        status == UMICOM_VFS_BAD_STATE || status == UMICOM_VFS_RELEASE_FAILED ||
        provider->lifecycle->commit.state == UMICOM_FAT16_COMMIT_FAILED)
        provider->mediaFailed = UMICOM_TRUE;
    return status;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableMutation(
    UmicomKernelFat16WritableProvider *provider, const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16WritableProviderNode *changedNode, UmicomBoolean namespaceChanged)
{
    if (provider->committedOperations == ~(UmicomU64)0U ||
        (namespaceChanged && provider->directoryEpoch == ~(UmicomU64)0U)) return UMICOM_VFS_EXHAUSTED;
    UmicomKernelFat16LifecycleResult result;
    UmicomKernelFat16WritableClear(&result, sizeof(result));
    provider->lastUpdateStatus = UmicomKernelFat16LifecycleStage(provider->lifecycle, request, &result);
    if (provider->lastUpdateStatus == UMICOM_FAT16_UPDATE_OK)
        provider->lastUpdateStatus = UmicomKernelFat16LifecycleFinish(provider->lifecycle, &result);
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableUpdateStatus(provider->lifecycle,
        provider->lastUpdateStatus);
    if (status == UMICOM_VFS_OK) {
        if (!result.commit.commitAccepted || result.committedOperations != provider->committedOperations + 1U ||
            provider->lifecycle->committedOperations != result.committedOperations ||
            provider->lifecycle->commit.state != UMICOM_FAT16_COMMIT_COMMITTED ||
            (changedNode && (!result.updatedEntryPresent ||
                !result.originalEntryPresent ||
                !UmicomKernelFat16WritableEntryEqual(&changedNode->entry, &result.originalEntry) ||
                !UmicomKernelFat16WritableEntryValid(provider, &result.updatedEntry) ||
                result.updatedEntry.directory != changedNode->entry.directory ||
                !UmicomKernelFat16WritableStringEqual(result.updatedEntry.name, changedNode->entry.name,
                    sizeof(changedNode->entry.name)) ||
                result.updatedEntry.bytes > provider->maximumFileBytes))) {
            provider->mediaFailed = UMICOM_TRUE;
            status = UMICOM_VFS_CORRUPT_STATE;
        } else {
            if (changedNode) UmicomKernelFat16WritableCopy(&changedNode->entry,
                &result.updatedEntry, sizeof(changedNode->entry));
            provider->committedOperations = result.committedOperations;
            if (namespaceChanged) ++provider->directoryEpoch;
        }
    } else if (provider->lifecycle->commit.state == UMICOM_FAT16_COMMIT_FAILED ||
        status == UMICOM_VFS_IO_ERROR || status == UMICOM_VFS_CORRUPT_FILESYSTEM ||
        status == UMICOM_VFS_CORRUPT_STATE || status == UMICOM_VFS_BAD_STATE ||
        status == UMICOM_VFS_RELEASE_FAILED) {
        /* Never translate a submitted but unaccepted prefix into VFS bytes.
         * The lifecycle result remains the exact evidence for media inspection. */
        provider->mediaFailed = UMICOM_TRUE;
    }
    UmicomKernelFat16WritableClear(&result, sizeof(result));
    return status;
}

UmicomKernelVfsStatus UmicomKernelFat16WritableProviderOpen(
    UmicomKernelFat16WritableProvider *provider, UmicomKernelFat16LifecycleCommitter *lifecycle,
    const UmicomKernelFat16FileTime *time)
{
    if (!UmicomKernelFat16WritableStorage(provider) || !lifecycle ||
        (UmicomAddress)lifecycle % alignof(UmicomKernelFat16LifecycleCommitter) ||
        !UmicomKernelFat16WritableSpan((UmicomAddress)lifecycle, sizeof(*lifecycle)) ||
        !time || (UmicomAddress)time % alignof(UmicomKernelFat16FileTime) ||
        !UmicomKernelFat16WritableSpan((UmicomAddress)time, sizeof(*time)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)provider, sizeof(*provider),
            (UmicomAddress)lifecycle, sizeof(*lifecycle))) return UMICOM_VFS_INVALID_ARGUMENT;
    if (provider->busy == UMICOM_TRUE) return UMICOM_VFS_BUSY;
    if (!UmicomKernelFat16WritableZero(provider, sizeof(*provider))) return UMICOM_VFS_BAD_STATE;
    if (lifecycle->self != lifecycle || lifecycle->commit.self != &lifecycle->commit ||
        (lifecycle->commit.state != UMICOM_FAT16_COMMIT_READY &&
            lifecycle->commit.state != UMICOM_FAT16_COMMIT_COMMITTED)) return UMICOM_VFS_BAD_STATE;
    if (lifecycle->busy || lifecycle->commit.busy || lifecycle->commit.updater.busy || lifecycle->workspace.busy)
        return UMICOM_VFS_BUSY;
    const UmicomKernelBlockDomain *const domain = lifecycle->commit.updater.domain;
    if (!UmicomKernelFat16WritableDomainValid(domain)) return UMICOM_VFS_BAD_STATE;
    if (!UmicomKernelFat16WritableDomainIndependent(domain, (UmicomAddress)provider, sizeof(*provider)) ||
        !UmicomKernelFat16WritableDomainIndependent(domain, (UmicomAddress)lifecycle, sizeof(*lifecycle)) ||
        !UmicomKernelFat16WritableDomainIndependent(domain, (UmicomAddress)time, sizeof(*time)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)time, sizeof(*time),
            (UmicomAddress)provider, sizeof(*provider)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)time, sizeof(*time),
            (UmicomAddress)lifecycle, sizeof(*lifecycle))) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelFat16FileTimeEncoding encoded;
    if (UmicomKernelFat16FileTimeEncode(time, &encoded) != UMICOM_DISK_OK) return UMICOM_VFS_INVALID_ARGUMENT;
    const UmicomKernelFat16FileTime copiedTime = *time;
    const UmicomU64 expected = lifecycle->committedOperations;
    const UmicomKernelFat16Query query = {UMICOM_FAT16_QUERY_STAT, "/", 0U, 0U};
    provider->busy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus updateStatus =
        UmicomKernelFat16LifecycleQuery(lifecycle, &query, &provider->queryStage);
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableUpdateStatus(lifecycle, updateStatus);
    if (status == UMICOM_VFS_OK && (provider->queryStage.committedOperations != expected ||
        lifecycle->committedOperations != expected || provider->queryStage.metadata.directoryEntryPresent ||
        !provider->queryStage.metadata.entry.directory || provider->queryStage.metadata.entry.bytes ||
        !lifecycle->commit.updater.info.clusters || !lifecycle->commit.updater.info.sectorsPerCluster))
        status = UMICOM_VFS_CORRUPT_STATE;
    if (status != UMICOM_VFS_OK) {
        UmicomKernelFat16WritableClear(provider, sizeof(*provider));
        return status;
    }
    provider->self = provider;
    provider->state = UMICOM_VFS_OPEN;
    provider->lifecycle = lifecycle;
    provider->time = copiedTime;
    provider->committedOperations = expected;
    provider->directoryEpoch = 1U;
    provider->nextNodeId = UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_ROOT_ID + 1U;
    provider->clusters = lifecycle->commit.updater.info.clusters;
    const UmicomU64 capacity = (UmicomU64)UMICOM_FAT16_CHAIN_LIMIT * UMICOM_DISK_SECTOR_BYTES *
        lifecycle->commit.updater.info.sectorsPerCluster;
    provider->maximumFileBytes = (UmicomSize)(capacity > (UmicomU64)~(UmicomU32)0U ?
        (UmicomU64)~(UmicomU32)0U : capacity);
    provider->lastUpdateStatus = updateStatus;
    UmicomKernelFat16WritableProviderNode *const root = &provider->nodes[0];
    root->id = UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_ROOT_ID;
    root->path[0] = '/';
    root->entry.name[0] = '/';
    root->entry.directory = UMICOM_TRUE;
    root->occupied = UMICOM_TRUE;
    return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_OK);
}
UmicomKernelVfsStatus UmicomKernelFat16WritableProviderSetTime(
    UmicomKernelFat16WritableProvider *provider, const UmicomKernelFat16FileTime *time)
{
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableValidate(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (!time || (UmicomAddress)time % alignof(UmicomKernelFat16FileTime) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)time, sizeof(*time)))
        return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelFat16FileTimeEncoding encoded;
    if (UmicomKernelFat16FileTimeEncode(time, &encoded) != UMICOM_DISK_OK) return UMICOM_VFS_INVALID_ARGUMENT;
    status = UmicomKernelFat16WritableMediaReady(provider);
    if (status != UMICOM_VFS_OK) return status;
    provider->time = *time;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelFat16WritableProviderClose(UmicomKernelFat16WritableProvider *provider)
{
    if (!UmicomKernelFat16WritableStorage(provider)) return UMICOM_VFS_INVALID_ARGUMENT;
    if (provider->busy == UMICOM_TRUE) return UMICOM_VFS_BUSY;
    if (!provider->self) return UmicomKernelFat16WritableZero(provider, sizeof(*provider)) ?
        UMICOM_VFS_OK : UMICOM_VFS_BAD_STATE;
    if (provider->self == provider && provider->state == UMICOM_VFS_CLOSED)
        return provider->pins ? UMICOM_VFS_CORRUPT_STATE : UMICOM_VFS_OK;
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableValidate(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (provider->pins) return UMICOM_VFS_BUSY;
    UmicomKernelFat16WritableClear(provider->nodes, sizeof(provider->nodes));
    UmicomKernelFat16WritableClear(&provider->queryStage, sizeof(provider->queryStage));
    UmicomKernelFat16WritableClear(provider->zeroStage, sizeof(provider->zeroStage));
    UmicomKernelFat16WritableClear(&provider->time, sizeof(provider->time));
    provider->state = UMICOM_VFS_CLOSED;
    return UMICOM_VFS_OK;
}

static UmicomKernelVfsStatus UmicomKernelFat16WritableRoot(void *context, UmicomKernelVfsNodeId *outNode)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableValidate(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (!outNode || (UmicomAddress)outNode % alignof(UmicomKernelVfsNodeId) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outNode, sizeof(*outNode)))
        return UMICOM_VFS_INVALID_ARGUMENT;
    *outNode = UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_ROOT_ID;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableLookup(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsNodeId *outNode)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *parent = (UmicomKernelFat16WritableProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, directory, &parent);
    if (status != UMICOM_VFS_OK) return status;
    if (!outNode || (UmicomAddress)outNode % alignof(UmicomKernelVfsNodeId) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outNode, sizeof(*outNode)))
        return UMICOM_VFS_INVALID_ARGUMENT;
    if (!parent->entry.directory) return UMICOM_VFS_NOT_DIRECTORY;
    char copiedName[13], path[UMICOM_FAT16_PATH_BYTES];
    status = UmicomKernelFat16WritableName(provider, name, copiedName, outNode, sizeof(*outNode));
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelFat16WritableChildPath(parent->path, copiedName, path);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    UmicomSize slot = UmicomKernelFat16WritablePathIndex(provider, path);
    if (slot != UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT) {
        *outNode = provider->nodes[slot].id;
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_OK);
    }
    status = UmicomKernelFat16WritableReserve(provider, &slot);
    if (status == UMICOM_VFS_OK)
        status = UmicomKernelFat16WritableQuery(provider, UMICOM_FAT16_QUERY_STAT, path, 0U, 0U);
    if (status == UMICOM_VFS_OK) {
        const UmicomKernelFat16Entry *const entry = &provider->queryStage.metadata.entry;
        if (!UmicomKernelFat16WritableEntryValid(provider, entry)) {
            provider->mediaFailed = UMICOM_TRUE;
            status = UMICOM_VFS_CORRUPT_STATE;
        } else if (entry->bytes > provider->maximumFileBytes) status = UMICOM_VFS_INSPECTION_LIMIT;
        else *outNode = UmicomKernelFat16WritablePublish(provider, slot, path, entry);
    }
    return UmicomKernelFat16WritableEnd(provider, status);
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableStat(void *context, UmicomKernelVfsNodeId id,
    UmicomKernelVfsNodeInfo *outInfo)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *node = (UmicomKernelFat16WritableProviderNode *)0;
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (!outInfo || (UmicomAddress)outInfo % alignof(UmicomKernelVfsNodeInfo) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outInfo, sizeof(*outInfo)))
        return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsNodeInfo staged;
    UmicomKernelFat16WritableInfo(provider, node, &staged);
    UmicomKernelFat16WritableCopy(outInfo, &staged, sizeof(*outInfo));
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritablePin(void *context, UmicomKernelVfsNodeId id)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *node = (UmicomKernelFat16WritableProviderNode *)0;
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (provider->pins == ~(UmicomSize)0U || node->pins == ~(UmicomSize)0U) return UMICOM_VFS_EXHAUSTED;
    ++node->pins;
    ++provider->pins;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableUnpin(void *context, UmicomKernelVfsNodeId id)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *node = (UmicomKernelFat16WritableProviderNode *)0;
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (!node->pins) return UMICOM_VFS_BAD_STATE;
    --node->pins;
    --provider->pins;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableTransferStorage(
    UmicomKernelFat16WritableProvider *provider, const void *buffer, UmicomSize bytes, UmicomSize *outBytes)
{
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableValidate(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (!outBytes || (UmicomAddress)outBytes % alignof(UmicomSize) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outBytes, sizeof(*outBytes)) ||
        (bytes && (!UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)buffer, bytes) ||
            UmicomKernelFat16WritableOverlap((UmicomAddress)buffer, bytes,
                (UmicomAddress)outBytes, sizeof(*outBytes))))) return UMICOM_VFS_INVALID_ARGUMENT;
    *outBytes = 0U;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableRead(void *context, UmicomKernelVfsNodeId id,
    UmicomSize offset, void *destination, UmicomSize bytes, UmicomSize *outRead)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableTransferStorage(provider, destination, bytes, outRead);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelFat16WritableProviderNode *node = (UmicomKernelFat16WritableProviderNode *)0;
    status = UmicomKernelFat16WritableGet(provider, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->entry.directory) return UMICOM_VFS_NOT_FILE;
    if (bytes > UMICOM_FAT16_READ_BYTES) return UMICOM_VFS_INSPECTION_LIMIT;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (!bytes) return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_OK);
    status = UmicomKernelFat16WritableQuery(provider, UMICOM_FAT16_QUERY_READ, node->path, offset, bytes);
    if (status == UMICOM_VFS_OK) {
        if (!UmicomKernelFat16WritableEntryEqual(&node->entry, &provider->queryStage.metadata.entry) ||
            provider->queryStage.bytes > bytes) {
            provider->mediaFailed = UMICOM_TRUE;
            status = UMICOM_VFS_CHANGED;
        } else {
            UmicomKernelFat16WritableCopy(destination, provider->queryStage.data, provider->queryStage.bytes);
            *outRead = provider->queryStage.bytes;
        }
    }
    return UmicomKernelFat16WritableEnd(provider, status);
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableCreate(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsKind kind)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *parent = (UmicomKernelFat16WritableProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, directory, &parent);
    if (status != UMICOM_VFS_OK) return status;
    if (kind != UMICOM_VFS_FILE && kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!parent->entry.directory) return UMICOM_VFS_NOT_DIRECTORY;
    char copiedName[13], path[UMICOM_FAT16_PATH_BYTES];
    status = UmicomKernelFat16WritableName(provider, name, copiedName, (void *)0, 0U);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelFat16WritableChildPath(parent->path, copiedName, path);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (UmicomKernelFat16WritablePathIndex(provider, path) != UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT)
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_EXISTS);
    const UmicomKernelFat16LifecycleRequest request = {
        .operation = kind == UMICOM_VFS_DIRECTORY ? UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY :
            UMICOM_FAT16_LIFECYCLE_CREATE,
        .path = path, .time = provider->time
    };
    status = UmicomKernelFat16WritableMutation(provider, &request,
        (UmicomKernelFat16WritableProviderNode *)0, UMICOM_TRUE);
    return UmicomKernelFat16WritableEnd(provider, status);
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableUnlink(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsKind kind)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *parent = (UmicomKernelFat16WritableProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, directory, &parent);
    if (status != UMICOM_VFS_OK) return status;
    if (kind != UMICOM_VFS_FILE && kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!parent->entry.directory) return UMICOM_VFS_NOT_DIRECTORY;
    char copiedName[13], path[UMICOM_FAT16_PATH_BYTES];
    status = UmicomKernelFat16WritableName(provider, name, copiedName, (void *)0, 0U);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelFat16WritableChildPath(parent->path, copiedName, path);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    const UmicomSize slot = UmicomKernelFat16WritablePathIndex(provider, path);
    if (slot != UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT) {
        const UmicomKernelFat16WritableProviderNode *const node = &provider->nodes[slot];
        if ((kind == UMICOM_VFS_DIRECTORY) != (node->entry.directory == UMICOM_TRUE))
            return UmicomKernelFat16WritableEnd(provider,
                node->entry.directory ? UMICOM_VFS_NOT_FILE : UMICOM_VFS_NOT_DIRECTORY);
        if (node->pins) return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_BUSY);
    }
    const UmicomKernelFat16LifecycleRequest request = {
        .operation = kind == UMICOM_VFS_DIRECTORY ? UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY :
            UMICOM_FAT16_LIFECYCLE_DELETE,
        .path = path
    };
    status = UmicomKernelFat16WritableMutation(provider, &request,
        (UmicomKernelFat16WritableProviderNode *)0, UMICOM_TRUE);
    if (status == UMICOM_VFS_OK && slot != UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT)
        UmicomKernelFat16WritableClear(&provider->nodes[slot], sizeof(provider->nodes[slot]));
    return UmicomKernelFat16WritableEnd(provider, status);
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableWrite(void *context, UmicomKernelVfsNodeId id,
    UmicomSize offset, const void *source, UmicomSize bytes, UmicomSize *outWritten)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableTransferStorage(provider, source, bytes, outWritten);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelFat16WritableProviderNode *node = (UmicomKernelFat16WritableProviderNode *)0;
    status = UmicomKernelFat16WritableGet(provider, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->entry.directory) return UMICOM_VFS_NOT_FILE;
    if (bytes > UMICOM_FAT16_UPDATE_BYTES) return UMICOM_VFS_INSPECTION_LIMIT;
    if (offset > provider->maximumFileBytes || bytes > provider->maximumFileBytes - offset ||
        (bytes && offset > node->entry.bytes)) return UMICOM_VFS_RANGE;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (node->entry.attributes & 1U) return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_READ_ONLY);
    if (!bytes) return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_OK);
    const UmicomKernelFat16LifecycleRequest request = {
        .operation = UMICOM_FAT16_LIFECYCLE_WRITE, .path = node->path,
        .input = source, .bytes = bytes, .time = provider->time, .offset = offset
    };
    status = UmicomKernelFat16WritableMutation(provider, &request, node, UMICOM_FALSE);
    if (status == UMICOM_VFS_OK) *outWritten = bytes;
    return UmicomKernelFat16WritableEnd(provider, status);
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableResize(void *context, UmicomKernelVfsNodeId id,
    UmicomSize bytes)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *node = (UmicomKernelFat16WritableProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->entry.directory) return UMICOM_VFS_NOT_FILE;
    if (bytes > provider->maximumFileBytes) return UMICOM_VFS_RANGE;
    if (bytes > node->entry.bytes && bytes - node->entry.bytes > UMICOM_FAT16_UPDATE_BYTES)
        return UMICOM_VFS_INSPECTION_LIMIT;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (node->entry.attributes & 1U) return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_READ_ONLY);
    if (bytes == node->entry.bytes) return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_OK);
    UmicomKernelFat16LifecycleRequest request;
    UmicomKernelFat16WritableClear(&request, sizeof(request));
    request.path = node->path;
    request.time = provider->time;
    if (bytes < node->entry.bytes) {
        request.operation = UMICOM_FAT16_LIFECYCLE_TRUNCATE;
        request.size = (UmicomU32)bytes;
    } else {
        /* Zero extension uses the same checked WRITE planner as application
         * bytes. It is one bounded accepted commit, never several hidden ones. */
        UmicomKernelFat16WritableClear(provider->zeroStage, sizeof(provider->zeroStage));
        request.operation = UMICOM_FAT16_LIFECYCLE_WRITE;
        request.offset = node->entry.bytes;
        request.bytes = bytes - node->entry.bytes;
        request.input = provider->zeroStage;
    }
    status = UmicomKernelFat16WritableMutation(provider, &request, node, UMICOM_FALSE);
    UmicomKernelFat16WritableClear(&request, sizeof(request));
    return UmicomKernelFat16WritableEnd(provider, status);
}
static UmicomKernelVfsStatus UmicomKernelFat16WritableEnumerate(void *context, UmicomKernelVfsNodeId id,
    UmicomU64 epoch, UmicomSize cursor, UmicomKernelVfsDirectoryEntry *outEntry,
    UmicomU64 *outEpoch, UmicomSize *outNext)
{
    UmicomKernelFat16WritableProvider *const provider = (UmicomKernelFat16WritableProvider *)context;
    UmicomKernelFat16WritableProviderNode *directory = (UmicomKernelFat16WritableProviderNode *)0;
    UmicomKernelVfsStatus status = UmicomKernelFat16WritableGet(provider, id, &directory);
    if (status != UMICOM_VFS_OK) return status;
    if (!outEntry || !outEpoch || !outNext ||
        (UmicomAddress)outEntry % alignof(UmicomKernelVfsDirectoryEntry) ||
        (UmicomAddress)outEpoch % alignof(UmicomU64) || (UmicomAddress)outNext % alignof(UmicomSize) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outEntry, sizeof(*outEntry)) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outEpoch, sizeof(*outEpoch)) ||
        !UmicomKernelFat16WritableIndependent(provider, (UmicomAddress)outNext, sizeof(*outNext)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)outEntry, sizeof(*outEntry),
            (UmicomAddress)outEpoch, sizeof(*outEpoch)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)outEntry, sizeof(*outEntry),
            (UmicomAddress)outNext, sizeof(*outNext)) ||
        UmicomKernelFat16WritableOverlap((UmicomAddress)outEpoch, sizeof(*outEpoch),
            (UmicomAddress)outNext, sizeof(*outNext))) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!directory->entry.directory) return UMICOM_VFS_NOT_DIRECTORY;
    if (cursor > UMICOM_FAT16_ENTRY_LIMIT) return UMICOM_VFS_RANGE;
    status = UmicomKernelFat16WritableBegin(provider);
    if (status != UMICOM_VFS_OK) return status;
    if (epoch && epoch != provider->directoryEpoch)
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_CHANGED);
    status = UmicomKernelFat16WritableQuery(provider, UMICOM_FAT16_QUERY_LIST, directory->path, 0U, 0U);
    if (status != UMICOM_VFS_OK) return UmicomKernelFat16WritableEnd(provider, status);
    if (!UmicomKernelFat16WritableEntryEqual(&directory->entry, &provider->queryStage.metadata.entry) ||
        provider->queryStage.directory.count > UMICOM_FAT16_ENTRY_LIMIT) {
        provider->mediaFailed = UMICOM_TRUE;
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_CHANGED);
    }
    if (cursor >= provider->queryStage.directory.count) {
        *outEpoch = provider->directoryEpoch;
        *outNext = cursor;
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_END);
    }
    const UmicomKernelFat16Entry *const entry = &provider->queryStage.directory.entries[cursor];
    if (!UmicomKernelFat16WritableEntryValid(provider, entry)) {
        provider->mediaFailed = UMICOM_TRUE;
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_CORRUPT_STATE);
    }
    if (entry->bytes > provider->maximumFileBytes)
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_INSPECTION_LIMIT);
    char path[UMICOM_FAT16_PATH_BYTES];
    status = UmicomKernelFat16WritableChildPath(directory->path, entry->name, path);
    if (status != UMICOM_VFS_OK) return UmicomKernelFat16WritableEnd(provider, status);
    UmicomSize slot = UmicomKernelFat16WritablePathIndex(provider, path);
    if (slot == UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT) {
        status = UmicomKernelFat16WritableReserve(provider, &slot);
        if (status != UMICOM_VFS_OK) return UmicomKernelFat16WritableEnd(provider, status);
        (void)UmicomKernelFat16WritablePublish(provider, slot, path, entry);
    } else if (!UmicomKernelFat16WritableEntryEqual(&provider->nodes[slot].entry, entry)) {
        provider->mediaFailed = UMICOM_TRUE;
        return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_CHANGED);
    }
    UmicomKernelVfsDirectoryEntry staged;
    UmicomKernelFat16WritableClear(&staged, sizeof(staged));
    UmicomKernelFat16WritableCopy(staged.name, entry->name, sizeof(entry->name));
    UmicomKernelFat16WritableInfo(provider, &provider->nodes[slot], &staged.info);
    UmicomKernelFat16WritableCopy(outEntry, &staged, sizeof(*outEntry));
    *outEpoch = provider->directoryEpoch;
    *outNext = cursor + 1U;
    return UmicomKernelFat16WritableEnd(provider, UMICOM_VFS_OK);
}

const UmicomKernelVfsOperations *UmicomKernelFat16WritableProviderOperationsGet(void)
{
    static const UmicomKernelVfsOperations operations = {
        .validate = UmicomKernelFat16WritableValidate,
        .root = UmicomKernelFat16WritableRoot,
        .lookup = UmicomKernelFat16WritableLookup,
        .create = UmicomKernelFat16WritableCreate,
        .unlink = UmicomKernelFat16WritableUnlink,
        .stat = UmicomKernelFat16WritableStat,
        .pin = UmicomKernelFat16WritablePin,
        .unpin = UmicomKernelFat16WritableUnpin,
        .read = UmicomKernelFat16WritableRead,
        .write = UmicomKernelFat16WritableWrite,
        .resize = UmicomKernelFat16WritableResize,
        .enumerate = UmicomKernelFat16WritableEnumerate
    };
    return &operations;
}

