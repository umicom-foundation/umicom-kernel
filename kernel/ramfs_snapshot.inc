/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/ramfs_snapshot.inc
 *
 * PURPOSE:
 *   Keep RAM filesystem statistics separate from live owner, metadata and file pages.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
static UmicomBoolean UmicomRamfsSnapshotSpan(const void *pointer, UmicomSize bytes)
{
    const UmicomAddress address = (UmicomAddress)pointer;
    return pointer && bytes && address <= ~(UmicomAddress)0U - bytes ?
        UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomRamfsSnapshotOverlap(UmicomAddress left, UmicomSize leftBytes,
    UmicomAddress right, UmicomSize rightBytes)
{
    return left <= right ? right - left < leftBytes : left - right < rightBytes;
}
/* Snapshot publication and reclamation counters share one ownership boundary.
 * Neither may overwrite a page whose lifetime the filesystem controls. Check
 * all pages before mutation so an invalid cleanup result cannot free anything. */
static UmicomKernelVfsStatus UmicomRamfsOutputAdmission(UmicomKernelRamfs *fs,
    const void *outInfo, UmicomSize bytes, UmicomSize alignment)
{
    if (!UmicomRamfsSnapshotSpan(fs, sizeof(*fs)) ||
        !UmicomRamfsSnapshotSpan(outInfo, bytes) ||
        (UmicomAddress)fs % alignof(UmicomKernelRamfs) ||
        (UmicomAddress)outInfo % alignment ||
        UmicomRamfsSnapshotOverlap((UmicomAddress)fs, sizeof(*fs),
            (UmicomAddress)outInfo, bytes))
        return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsStatus status = UmicomKernelRamfsValidate(fs);
    if (status != UMICOM_VFS_OK) return status;
    const UmicomAddress output = (UmicomAddress)outInfo;
    /* A result inside any owned page is destructive even when it misses the
     * current node: object guards, padding and future file bytes still belong
     * to the filesystem. Reject the whole page, not just visible data length. */
    for (UmicomSize i = 0U; i < UMICOM_OBJECT_CACHE_FRAME_LIMIT; ++i)
        if (fs->nodeCache.pages[i].owned &&
            UmicomRamfsSnapshotOverlap(output, bytes,
                fs->nodeCache.pages[i].frame, UMICOM_KERNEL_PAGE_SIZE))
            return UMICOM_VFS_INVALID_ARGUMENT;
    for (UmicomSize i = 0U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) continue;
        const UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, i);
        if (!node) return UmicomRamfsPoison(fs);
        for (UmicomSize page = 0U; page < UMICOM_RAMFS_FILE_PAGES; ++page)
            if (node->pages[page] &&
                UmicomRamfsSnapshotOverlap(output, bytes,
                    node->pages[page], UMICOM_KERNEL_PAGE_SIZE))
                return UMICOM_VFS_INVALID_ARGUMENT;
    }
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelRamfsSnapshot(UmicomKernelRamfs *fs, UmicomKernelRamfsInfo *outInfo)
{
    const UmicomKernelVfsStatus status = UmicomRamfsOutputAdmission(fs,
        outInfo, sizeof(*outInfo), alignof(UmicomKernelRamfsInfo));
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelRamfsInfo staged;
    UmicomVfsClear(&staged, sizeof(staged));
    staged.nodes = fs->nodes;
    staged.dataPages = fs->dataPages;
    staged.metadataPages = fs->nodeCache.frames;
    staged.namespaceEpoch = fs->epoch;
    for (UmicomSize i = 0U; i < UMICOM_RAMFS_NODE_LIMIT; ++i) {
        if (fs->records[i].id == 0U) continue;
        const UmicomKernelRamfsNode *const node = UmicomRamfsNodeAt(fs, i);
        if (!node) return UmicomRamfsPoison(fs);
        if (node->pins > ~(UmicomSize)0U - staged.pins) return UMICOM_VFS_RANGE;
        staged.pins += node->pins;
        if (!node->linked) ++staged.unlinkedNodes;
    }
    /* Freestanding byte publication cannot introduce a hidden hosted memcpy.
     * Nothing has touched caller output before every ownership check succeeds. */
    volatile UmicomU8 *const destination = (volatile UmicomU8 *)outInfo;
    const UmicomU8 *const source = (const UmicomU8 *)&staged;
    for (UmicomSize i = 0U; i < sizeof(staged); ++i) destination[i] = source[i];
    return UMICOM_VFS_OK;
}
