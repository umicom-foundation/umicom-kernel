/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/ramfs.c
 *
 * PURPOSE:
 *   Store a directory tree in object-cache nodes and file contents in owned
 *   physical pages, behind the provider contract in vfs.h.
 *
 * EDUCATIONAL OVERVIEW:
 *   Nothing here borrows a file buffer after a call returns. A write copies its
 *   bytes; a read fills caller-owned storage. Missing pages represent zeroes.
 *   The namespace has no hard links, so each linked node has exactly one parent.
 *   An unlinked open file survives until its last description releases its pin.
 *
 *   Allocation can fail after an earlier page was written. That prefix remains
 *   committed and is reported honestly; no pretend all-or-nothing write hides
 *   it. Deallocation is different: unlink first removes the name, and Reap later
 *   scrubs/releases unreachable nodes with retryable ownership records.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/ramfs.h"
#include "vfs_internal.h"

static UmicomKernelVfsStatus UmicomRamfsObjectResult(UmicomKernelObjectStatus status)
{
    /* The backend does not turn a corrupted allocator into an empty directory. */
    if (status == UMICOM_OBJECT_OK) return UMICOM_VFS_OK;
    if (status == UMICOM_OBJECT_OUT_OF_MEMORY) return UMICOM_VFS_NO_MEMORY;
    if (status == UMICOM_OBJECT_CAPACITY) return UMICOM_VFS_CAPACITY;
    if (status == UMICOM_OBJECT_UNSAFE_CONTEXT) return UMICOM_VFS_UNSAFE_CONTEXT;
    if (status == UMICOM_OBJECT_TICKET_EXHAUSTED) return UMICOM_VFS_EXHAUSTED;
    if (status == UMICOM_OBJECT_RELEASE_FAILED) return UMICOM_VFS_RELEASE_FAILED;
    return UMICOM_VFS_CORRUPT_STATE;
}
static UmicomKernelRamfsNode *UmicomRamfsNodeAt(UmicomKernelRamfs *fs, UmicomSize index)
{
    if (index == 0U) return &fs->root;
    void *node = (void *)0;
    if (UmicomKernelObjectCacheResolve(&fs->nodeCache, fs->records[index].object, &node) != UMICOM_OBJECT_OK) return (UmicomKernelRamfsNode *)0;
    return (UmicomKernelRamfsNode *)node;
}
static UmicomSize UmicomRamfsIndex(UmicomKernelRamfs *fs, UmicomKernelVfsNodeId id)
{
    /* IDs are not array indexes or addresses. A small bounded search also makes
     * an old ID harmless when a record slot is reused for a different node. */
    if (id == 0U) return UMICOM_RAMFS_NODE_LIMIT;
    for (UmicomSize i = 0U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) if (fs->records[i].id == id) return i;
    return UMICOM_RAMFS_NODE_LIMIT;
}
static UmicomKernelVfsStatus UmicomRamfsPoison(UmicomKernelRamfs *fs)
{
    /* Retain every ownership record. Freeing through suspect addresses would
     * turn a diagnosable inconsistency into damage to some other service. */
    fs->state = UMICOM_VFS_POISONED;
    return UMICOM_VFS_CORRUPT_STATE;
}
UmicomKernelVfsStatus UmicomKernelRamfsValidate(UmicomKernelRamfs *fs)
{
    if (fs == (UmicomKernelRamfs *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    if (UmicomKernelObjectCacheAccessAllowed() == UMICOM_FALSE) return UMICOM_VFS_UNSAFE_CONTEXT;
    if (fs->self != fs) return UMICOM_VFS_BAD_STATE;
    if (fs->state == UMICOM_VFS_POISONED) return UMICOM_VFS_CORRUPT_STATE;
    if (fs->state != UMICOM_VFS_OPEN && fs->state != UMICOM_VFS_CLOSING) return UMICOM_VFS_BAD_STATE;
    if (fs->records[0].id != 1U || fs->lastIdentity < 1U || fs->epoch == 0U ||
        fs->root.kind != UMICOM_VFS_DIRECTORY || fs->root.parent != 0U ||
        fs->root.linked != UMICOM_TRUE || fs->root.bytes != 0U ||
        UmicomKernelObjectCacheValidate(&fs->nodeCache) != UMICOM_OBJECT_OK) return UmicomRamfsPoison(fs);

    /* Resolve object-cache tickets before inspecting node contents. Then
     * collect backing frames separately so aliases between files cannot hide
     * behind otherwise plausible per-file page lists. */
    UmicomKernelRamfsNode *nodes[UMICOM_RAMFS_NODE_LIMIT];
    UmicomAddress frames[UMICOM_RAMFS_DATA_PAGES];
    UmicomSize nodeCount = 0U;
    UmicomSize frameCount = 0U;
    UmicomVfsClear(nodes, sizeof(nodes));
    for (UmicomSize i = 0U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) {
            if (UmicomVfsZero(&fs->records[i].object, sizeof(fs->records[i].object)) == UMICOM_FALSE) return UmicomRamfsPoison(fs);
            continue;
        }
        if (fs->records[i].id > fs->lastIdentity) return UmicomRamfsPoison(fs);
        for (UmicomSize earlier = 0U; earlier < i; ++earlier) {
            if (fs->records[earlier].id == fs->records[i].id) return UmicomRamfsPoison(fs);
        }
        nodes[i] = UmicomRamfsNodeAt(fs, i);
        if (nodes[i] == (UmicomKernelRamfsNode *)0) return UmicomRamfsPoison(fs);
        UmicomKernelRamfsNode *const node = nodes[i];
        if ((node->kind != UMICOM_VFS_FILE && node->kind != UMICOM_VFS_DIRECTORY) ||
            (node->linked != UMICOM_FALSE && node->linked != UMICOM_TRUE) ||
            node->bytes > UMICOM_RAMFS_FILE_BYTES ||
            (node->kind == UMICOM_VFS_DIRECTORY && node->bytes != 0U) ||
            (i != 0U && UmicomVfsNameValid(node->name) == UMICOM_FALSE)) return UmicomRamfsPoison(fs);
        ++nodeCount;
        for (UmicomSize page = 0U; page < UMICOM_RAMFS_FILE_PAGES; ++page) {
            const UmicomAddress address = node->pages[page];
            if (address == 0U) continue;
            UmicomKernelPhysicalFrameState state = UMICOM_PHYSICAL_FRAME_FREE;
            if (node->kind != UMICOM_VFS_FILE || frameCount == UMICOM_RAMFS_DATA_PAGES ||
                UmicomKernelPhysicalMemoryFrameQuery(address, &state) != UMICOM_KERNEL_MEMORY_OK ||
                state != UMICOM_PHYSICAL_FRAME_ALLOCATED) return UmicomRamfsPoison(fs);
            /* A byte frame may not alias another file or a cache metadata frame. */
            for (UmicomSize earlier = 0U; earlier < frameCount; ++earlier) if (frames[earlier] == address) return UmicomRamfsPoison(fs);
            for (UmicomSize cachePage = 0U; cachePage < UMICOM_OBJECT_CACHE_FRAME_LIMIT; ++cachePage) {
                if (fs->nodeCache.pages[cachePage].owned && fs->nodeCache.pages[cachePage].frame == address) return UmicomRamfsPoison(fs);
            }
            frames[frameCount++] = address;
        }
    }
    /* Check parents only after all node references have been resolved safely. */
    for (UmicomSize i = 1U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (nodes[i] == (UmicomKernelRamfsNode *)0 || !nodes[i]->linked) continue;
        const UmicomSize parent = UmicomRamfsIndex(fs, nodes[i]->parent);
        if (parent == UMICOM_RAMFS_NODE_LIMIT || nodes[parent] == (UmicomKernelRamfsNode *)0 ||
            nodes[parent]->kind != UMICOM_VFS_DIRECTORY || !nodes[parent]->linked ||
            nodes[i]->parent >= fs->records[i].id) return UmicomRamfsPoison(fs);
        for (UmicomSize earlier = 1U; earlier < i; ++earlier) {
            if (nodes[earlier] != (UmicomKernelRamfsNode *)0 && nodes[earlier]->linked &&
                nodes[earlier]->parent == nodes[i]->parent && UmicomVfsNameEqual(nodes[earlier]->name, nodes[i]->name)) return UmicomRamfsPoison(fs);
        }
    }
    if (nodeCount != fs->nodes || frameCount != fs->dataPages ||
        fs->nodeCache.liveObjects != nodeCount - 1U) return UmicomRamfsPoison(fs);
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsReady(void *context)
{
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    UmicomKernelVfsStatus status = UmicomKernelRamfsValidate(fs);
    if (status != UMICOM_VFS_OK) return status;
    return fs->state == UMICOM_VFS_OPEN ? UMICOM_VFS_OK : UMICOM_VFS_BAD_STATE;
}
static UmicomKernelVfsStatus UmicomRamfsGet(void *context, UmicomKernelVfsNodeId id,
    UmicomKernelRamfsNode **outNode)
{
    UmicomKernelVfsStatus status = UmicomRamfsReady(context);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    const UmicomSize index = UmicomRamfsIndex(fs, id);
    if (index == UMICOM_RAMFS_NODE_LIMIT) return UMICOM_VFS_NOT_FOUND;
    *outNode = UmicomRamfsNodeAt(fs, index);
    return *outNode != (UmicomKernelRamfsNode *)0 ? UMICOM_VFS_OK : UmicomRamfsPoison(fs);
}
UmicomKernelVfsStatus UmicomKernelRamfsInitialize(UmicomKernelRamfs *fs)
{
    if (fs == (UmicomKernelRamfs *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    if (UmicomKernelObjectCacheAccessAllowed() == UMICOM_FALSE) return UMICOM_VFS_UNSAFE_CONTEXT;
    if (!UmicomVfsZero(fs, sizeof(*fs))) return UMICOM_VFS_BAD_STATE;
    UmicomKernelObjectStatus status = UmicomKernelObjectCacheInitialize(&fs->nodeCache,
        sizeof(UmicomKernelRamfsNode), 16U, UMICOM_OBJECT_CACHE_FRAME_LIMIT);
    if (status != UMICOM_OBJECT_OK) return UmicomRamfsObjectResult(status);
    /* Root lives inside the owner; an empty mounted filesystem needs no frame. */
    fs->self = fs;
    fs->state = UMICOM_VFS_OPEN;
    fs->lastIdentity = 1U;
    fs->epoch = 1U;
    fs->nodes = 1U;
    fs->records[0].id = 1U;
    fs->root.kind = UMICOM_VFS_DIRECTORY;
    fs->root.linked = UMICOM_TRUE;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsRoot(void *context, UmicomKernelVfsNodeId *outNode)
{
    if (outNode == (UmicomKernelVfsNodeId *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsStatus status = UmicomRamfsReady(context);
    if (status == UMICOM_VFS_OK) *outNode = 1U;
    return status;
}
static UmicomKernelVfsStatus UmicomRamfsLookup(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsNodeId *outNode)
{
    if (!UmicomVfsNameValid(name) || outNode == (UmicomKernelVfsNodeId *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelRamfsNode *parent = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, directory, &parent);
    if (status != UMICOM_VFS_OK) return status;
    if (parent->kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_NOT_DIRECTORY;
    if (!parent->linked) return UMICOM_VFS_NOT_FOUND;
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    for (UmicomSize i = 1U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) continue;
        UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, i);
        if (node->linked && node->parent == directory && UmicomVfsNameEqual(node->name, name)) {
            *outNode = fs->records[i].id;
            return UMICOM_VFS_OK;
        }
    }
    return UMICOM_VFS_NOT_FOUND;
}
static UmicomKernelVfsStatus UmicomRamfsCreate(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsKind kind)
{
    if (kind != UMICOM_VFS_FILE && kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsNodeId existing = 0U;
    UmicomKernelVfsStatus status = UmicomRamfsLookup(context, directory, name, &existing);
    if (status == UMICOM_VFS_OK) return UMICOM_VFS_EXISTS;
    if (status != UMICOM_VFS_NOT_FOUND) return status;
    /* Lookup's NOT_FOUND must mean a missing child, not a missing parent. */
    UmicomKernelRamfsNode *parent = (UmicomKernelRamfsNode *)0;
    status = UmicomRamfsGet(context, directory, &parent);
    if (status != UMICOM_VFS_OK || !parent->linked) return status == UMICOM_VFS_OK ? UMICOM_VFS_NOT_FOUND : status;
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    if (fs->lastIdentity == ~(UmicomU64)0U || fs->epoch == ~(UmicomU64)0U) return UMICOM_VFS_EXHAUSTED;
    UmicomSize slot = 1U;
    while (slot < UMICOM_RAMFS_NODE_LIMIT && fs->records[slot].id != 0U) ++slot;
    if (slot == UMICOM_RAMFS_NODE_LIMIT) return UMICOM_VFS_CAPACITY;
    UmicomKernelObjectReference reference;
    UmicomVfsClear(&reference, sizeof(reference));
    UmicomKernelObjectStatus allocated = UmicomKernelObjectCacheAllocate(&fs->nodeCache, &reference);
    if (allocated != UMICOM_OBJECT_OK) return UmicomRamfsObjectResult(allocated);
    /* Allocate already zeroed the complete node. Publish the record only after
     * its kind, parent and bounded name are ready for subsequent lookup. */
    UmicomKernelRamfsNode *const node = (UmicomKernelRamfsNode *)reference.address;
    node->parent = directory;
    node->kind = kind;
    node->linked = UMICOM_TRUE;
    UmicomSize length = 0U;
    while (name[length] != '\0') ++length;
    UmicomVfsCopy(node->name, name, length + 1U);
    fs->records[slot].object = reference;
    fs->records[slot].id = ++fs->lastIdentity;
    ++fs->nodes;
    ++fs->epoch;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsUnlink(void *context, UmicomKernelVfsNodeId directory,
    const char *name, UmicomKernelVfsKind kind)
{
    if (kind != UMICOM_VFS_FILE && kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsNodeId id = 0U;
    UmicomKernelVfsStatus status = UmicomRamfsLookup(context, directory, name, &id);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, UmicomRamfsIndex(fs, id));
    if (kind != node->kind) return kind == UMICOM_VFS_FILE ? UMICOM_VFS_NOT_FILE : UMICOM_VFS_NOT_DIRECTORY;
    if (fs->epoch == ~(UmicomU64)0U) return UMICOM_VFS_EXHAUSTED;
    if (kind == UMICOM_VFS_DIRECTORY) {
        for (UmicomSize i = 1U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
            if (fs->records[i].id == 0U) continue;
            UmicomKernelRamfsNode *const child = UmicomRamfsNodeAt(fs, i);
            if (child->linked && child->parent == id) return UMICOM_VFS_NOT_EMPTY;
        }
        /* This profile deliberately refuses open-directory removal. It does
         * support open-file unlink, whose pins keep the old bytes reachable. */
        if (node->pins != 0U) return UMICOM_VFS_BUSY;
    }
    node->linked = UMICOM_FALSE;
    ++fs->epoch;
    /* Name removal has committed. Memory reclamation is explicit in Reap, so
     * a later frame-release refusal cannot ambiguously undo the namespace. */
    return UMICOM_VFS_OK;
}
static void UmicomRamfsInfo(UmicomKernelVfsNodeId id, UmicomKernelRamfsNode *node,
    UmicomKernelVfsNodeInfo *outInfo)
{
    UmicomVfsClear(outInfo, sizeof(*outInfo));
    outInfo->id = id;
    outInfo->kind = node->kind;
    outInfo->bytes = node->bytes;
    outInfo->maximumBytes = node->kind == UMICOM_VFS_FILE ? UMICOM_RAMFS_FILE_BYTES : 0U;
}
static UmicomKernelVfsStatus UmicomRamfsStat(void *context, UmicomKernelVfsNodeId id,
    UmicomKernelVfsNodeInfo *outInfo)
{
    if (outInfo == (UmicomKernelVfsNodeInfo *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelRamfsNode *node = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &node);
    if (status == UMICOM_VFS_OK) UmicomRamfsInfo(id, node, outInfo);
    return status;
}
static UmicomKernelVfsStatus UmicomRamfsPin(void *context, UmicomKernelVfsNodeId id)
{
    UmicomKernelRamfsNode *node = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (!node->linked || node->pins == ~(UmicomSize)0U) return UMICOM_VFS_BAD_STATE;
    ++node->pins;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsUnpin(void *context, UmicomKernelVfsNodeId id)
{
    UmicomKernelRamfsNode *node = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->pins == 0U) return UMICOM_VFS_BAD_STATE;
    --node->pins; /* Reap, not an implicit destructor here, owns physical frees. */
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsRead(void *context, UmicomKernelVfsNodeId id,
    UmicomSize offset, void *destination, UmicomSize bytes, UmicomSize *outRead)
{
    if (outRead == (UmicomSize *)0 || (bytes != 0U && destination == (void *)0)) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelRamfsNode *node = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->kind != UMICOM_VFS_FILE) return UMICOM_VFS_NOT_FILE;
    *outRead = 0U;
    if (offset >= node->bytes) return UMICOM_VFS_OK;
    const UmicomSize count = bytes < node->bytes - offset ? bytes : node->bytes - offset;
    UmicomU8 *const out = (UmicomU8 *)destination;
    for (UmicomSize done = 0U; done < count;) {
        const UmicomSize position = offset + done;
        const UmicomSize inPage = position % UMICOM_KERNEL_PAGE_SIZE;
        const UmicomSize available = UMICOM_KERNEL_PAGE_SIZE - inPage;
        const UmicomSize part = count - done < available ? count - done : available;
        const UmicomAddress frame = node->pages[position / UMICOM_KERNEL_PAGE_SIZE];
        if (frame == 0U) UmicomVfsClear(out + done, part); /* Sparse holes never disclose old RAM. */
        else UmicomVfsCopy(out + done, (const void *)(frame + inPage), part);
        done += part;
    }
    *outRead = count;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsWrite(void *context, UmicomKernelVfsNodeId id,
    UmicomSize offset, const void *source, UmicomSize bytes, UmicomSize *outWritten)
{
    if (outWritten == (UmicomSize *)0 || (bytes != 0U && source == (const void *)0)) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelRamfsNode *node = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->kind != UMICOM_VFS_FILE) return UMICOM_VFS_NOT_FILE;
    *outWritten = 0U;
    if (offset > UMICOM_RAMFS_FILE_BYTES || bytes > UMICOM_RAMFS_FILE_BYTES - offset) return UMICOM_VFS_RANGE;
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    const UmicomU8 *const in = (const UmicomU8 *)source;
    while (*outWritten < bytes) {
        const UmicomSize position = offset + *outWritten;
        const UmicomSize page = position / UMICOM_KERNEL_PAGE_SIZE;
        const UmicomSize inPage = position % UMICOM_KERNEL_PAGE_SIZE;
        if (node->pages[page] == 0U) {
            if (fs->dataPages == UMICOM_RAMFS_DATA_PAGES) return UMICOM_VFS_CAPACITY;
            UmicomAddress frame = 0U;
            if (UmicomKernelPhysicalMemoryAllocateFrame(&frame) != UMICOM_KERNEL_MEMORY_OK) return UMICOM_VFS_NO_MEMORY;
            /* Clear the whole new page, not just the written portion. Later
             * reads of a seek gap or an expanded file must see defined zeroes. */
            UmicomVfsClear((void *)frame, UMICOM_KERNEL_PAGE_SIZE);
            node->pages[page] = frame;
            ++fs->dataPages;
        }
        const UmicomSize available = UMICOM_KERNEL_PAGE_SIZE - inPage;
        const UmicomSize remaining = bytes - *outWritten;
        const UmicomSize part = remaining < available ? remaining : available;
        UmicomVfsCopy((void *)(node->pages[page] + inPage), in + *outWritten, part);
        *outWritten += part;
        if (offset + *outWritten > node->bytes) node->bytes = offset + *outWritten;
        /* Publish progress after each copied portion. If the next allocation
         * fails, this prefix and the corresponding new length are still real. */
    }
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsResize(void *context, UmicomKernelVfsNodeId id, UmicomSize bytes)
{
    UmicomKernelRamfsNode *node = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &node);
    if (status != UMICOM_VFS_OK) return status;
    if (node->kind != UMICOM_VFS_FILE) return UMICOM_VFS_NOT_FILE;
    if (bytes > UMICOM_RAMFS_FILE_BYTES) return UMICOM_VFS_RANGE;
    if (bytes < node->bytes) {
        /* Shrink clears truncated bytes but retains capacity. A later growth
         * cannot reveal them. Explicit unlink/reap or Close releases the pages;
         * no half-completed free changes the meaning of this resize operation. */
        for (UmicomSize page = 0U; page < UMICOM_RAMFS_FILE_PAGES; ++page) {
            if (node->pages[page] == 0U) continue;
            const UmicomSize start = page * UMICOM_KERNEL_PAGE_SIZE;
            if (start + UMICOM_KERNEL_PAGE_SIZE <= bytes) continue;
            const UmicomSize keep = bytes > start ? bytes - start : 0U;
            UmicomVfsClear((void *)(node->pages[page] + keep), UMICOM_KERNEL_PAGE_SIZE - keep);
        }
    }
    node->bytes = bytes; /* Growth is sparse and needs no eager allocation. */
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomRamfsEnumerate(void *context, UmicomKernelVfsNodeId id,
    UmicomU64 epoch, UmicomSize cursor, UmicomKernelVfsDirectoryEntry *outEntry,
    UmicomU64 *outEpoch, UmicomSize *outNext)
{
    if (outEntry == (UmicomKernelVfsDirectoryEntry *)0 || outEpoch == (UmicomU64 *)0 || outNext == (UmicomSize *)0 ||
        cursor > UMICOM_RAMFS_NODE_LIMIT) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelRamfsNode *directory = (UmicomKernelRamfsNode *)0;
    UmicomKernelVfsStatus status = UmicomRamfsGet(context, id, &directory);
    if (status != UMICOM_VFS_OK) return status;
    if (directory->kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_NOT_DIRECTORY;
    UmicomKernelRamfs *const fs = (UmicomKernelRamfs *)context;
    /* One conservative epoch covers the whole namespace. Even a change in a
     * different directory makes a previously started cursor explicit about
     * restarting; this is not an immutable directory snapshot. */
    if (epoch != 0U && epoch != fs->epoch) return UMICOM_VFS_CHANGED;
    for (UmicomSize i = cursor < 1U ? 1U : cursor; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) continue;
        UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, i);
        if (!node->linked || node->parent != id) continue;
        UmicomVfsClear(outEntry, sizeof(*outEntry));
        UmicomVfsCopy(outEntry->name, node->name, UMICOM_VFS_NAME_BYTES);
        UmicomRamfsInfo(fs->records[i].id, node, &outEntry->info);
        *outEpoch = fs->epoch;
        *outNext = i + 1U;
        return UMICOM_VFS_OK;
    }
    *outEpoch = fs->epoch;
    *outNext = UMICOM_RAMFS_NODE_LIMIT;
    return UMICOM_VFS_END;
}
UmicomKernelVfsStatus UmicomKernelRamfsSnapshot(UmicomKernelRamfs *fs, UmicomKernelRamfsInfo *outInfo)
{
    if (outInfo == (UmicomKernelRamfsInfo *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsStatus status = UmicomKernelRamfsValidate(fs);
    if (status != UMICOM_VFS_OK) return status;
    UmicomVfsClear(outInfo, sizeof(*outInfo));
    outInfo->nodes = fs->nodes;
    outInfo->dataPages = fs->dataPages;
    outInfo->metadataPages = fs->nodeCache.frames;
    outInfo->namespaceEpoch = fs->epoch;
    for (UmicomSize i = 0U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) continue;
        UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, i);
        outInfo->pins += node->pins;
        if (!node->linked) ++outInfo->unlinkedNodes;
    }
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelRamfsReap(UmicomKernelRamfs *fs, UmicomSize *outReaped)
{
    if (outReaped == (UmicomSize *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsStatus status = UmicomKernelRamfsValidate(fs);
    if (status != UMICOM_VFS_OK) return status;
    *outReaped = 0U;
    for (UmicomSize i = 1U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) continue;
        UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, i);
        /* A name or an open description is sufficient to keep this node alive.
         * Reap is not allowed to infer that an unlinked file has no readers. */
        if (node->linked || node->pins != 0U) continue;
        for (UmicomSize page = 0U; page < UMICOM_RAMFS_FILE_PAGES; ++page) {
            if (node->pages[page] == 0U) continue;
            UmicomVfsClear((void *)node->pages[page], UMICOM_KERNEL_PAGE_SIZE);
            if (UmicomKernelPhysicalMemoryFreeFrame(node->pages[page]) != UMICOM_KERNEL_MEMORY_OK) return UMICOM_VFS_RELEASE_FAILED;
            node->pages[page] = 0U; /* Forget ownership only after successful release. */
            --fs->dataPages;
        }
        status = UmicomRamfsObjectResult(UmicomKernelObjectCacheFree(&fs->nodeCache, fs->records[i].object));
        if (status != UMICOM_VFS_OK) return status;
        UmicomVfsClear(&fs->records[i], sizeof(fs->records[i]));
        --fs->nodes;
        ++*outReaped;
    }
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelRamfsClose(UmicomKernelRamfs *fs)
{
    UmicomKernelVfsStatus status = UmicomKernelRamfsValidate(fs);
    if (status != UMICOM_VFS_OK) return status;
    /* Check every pin before removing any name. A mount or open description
     * still borrowing this provider must never point into reclaimed metadata. */
    for (UmicomSize i = 0U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id != 0U && UmicomRamfsNodeAt(fs, i)->pins != 0U) return UMICOM_VFS_BUSY;
    }
    /* Admission closes before any namespace is dismantled. Once destruction
     * begins, a retry may finish it, but an ordinary file call cannot revive
     * objects between two successful frame releases. */
    fs->state = UMICOM_VFS_CLOSING;
    for (UmicomSize i = 1U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id != 0U) UmicomRamfsNodeAt(fs, i)->linked = UMICOM_FALSE;
    }
    UmicomSize reaped = 0U;
    status = UmicomKernelRamfsReap(fs, &reaped);
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomRamfsObjectResult(UmicomKernelObjectCacheClose(&fs->nodeCache));
    if (status != UMICOM_VFS_OK) return status;
    fs->state = UMICOM_VFS_CLOSED;
    return UMICOM_VFS_OK;
}
const UmicomKernelVfsOperations *UmicomKernelRamfsOperations(void)
{
    /* One immutable provider description; no function pointer comes from file
     * contents or a user program. The mount borrows it for its entire lifetime. */
    static const UmicomKernelVfsOperations operations = {
        .validate = UmicomRamfsReady, .root = UmicomRamfsRoot,
        .lookup = UmicomRamfsLookup, .create = UmicomRamfsCreate,
        .unlink = UmicomRamfsUnlink, .stat = UmicomRamfsStat,
        .pin = UmicomRamfsPin, .unpin = UmicomRamfsUnpin,
        .read = UmicomRamfsRead, .write = UmicomRamfsWrite,
        .resize = UmicomRamfsResize, .enumerate = UmicomRamfsEnumerate
    };
    return &operations;
}
