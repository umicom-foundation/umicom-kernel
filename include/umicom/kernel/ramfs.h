/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/ramfs.h
 *
 * PURPOSE:
 *   Own a bounded directory tree and sparse RAM-backed file bytes for the VFS.
 *
 * EDUCATIONAL OVERVIEW:
 *   The root is embedded in this stable owner; other nodes use the existing
 *   object cache. File pages come independently from the existing physical
 *   allocator. A pathname owns a namespace link; an open description owns a
 *   pin. Only an unlinked, unpinned node can be reaped.
 *
 *   Removing a name and reclaiming memory are separate operations. Reap is
 *   explicit and retryable, so a refused frame release never loses the record
 *   of that frame. This is volatile storage, not a persistent filesystem.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_RAMFS_H
#define UMICOM_KERNEL_RAMFS_H
#include "umicom/kernel/vfs.h"
#include "umicom/kernel/object_cache.h"
#define UMICOM_RAMFS_NODE_LIMIT 64U
#define UMICOM_RAMFS_FILE_PAGES 32U
#define UMICOM_RAMFS_DATA_PAGES 128U
#define UMICOM_RAMFS_FILE_BYTES ((UmicomSize)UMICOM_RAMFS_FILE_PAGES * UMICOM_KERNEL_PAGE_SIZE)

/* Exposed only for static storage and deliberate fault tests. Do not mutate,
 * copy or reset a live owner, cache reference, node or page list. */
typedef struct UmicomKernelRamfsNode {
    UmicomKernelVfsNodeId parent;
    UmicomKernelVfsKind kind;
    UmicomBoolean linked;
    UmicomSize pins;
    UmicomSize bytes;
    char name[UMICOM_VFS_NAME_BYTES];
    UmicomAddress pages[UMICOM_RAMFS_FILE_PAGES]; /* Zero entries read as holes. */
} UmicomKernelRamfsNode;
typedef struct UmicomKernelRamfsRecord {
    UmicomKernelVfsNodeId id; /* Monotonic within this domain, never recycled. */
    UmicomKernelObjectReference object;
} UmicomKernelRamfsRecord;
typedef struct UmicomKernelRamfs {
    const struct UmicomKernelRamfs *self;
    UmicomKernelVfsLifetime state;
    UmicomU64 lastIdentity;
    UmicomU64 epoch;
    UmicomSize nodes;
    UmicomSize dataPages;
    UmicomKernelRamfsNode root;
    UmicomKernelObjectCache nodeCache;
    UmicomKernelRamfsRecord records[UMICOM_RAMFS_NODE_LIMIT];
} UmicomKernelRamfs;
typedef struct UmicomKernelRamfsInfo {
    UmicomSize nodes; /* Includes the root while the domain is open. */
    UmicomSize unlinkedNodes;
    UmicomSize pins;
    UmicomSize dataPages;
    UmicomSize metadataPages;
    UmicomU64 namespaceEpoch;
} UmicomKernelRamfsInfo;

/* Initialisation is lazy: the embedded root and cache metadata need no frame.
 * The same execution gate as object caches applies to every operation. */
UmicomKernelVfsStatus UmicomKernelRamfsInitialize(UmicomKernelRamfs *fs);
const UmicomKernelVfsOperations *UmicomKernelRamfsOperations(void);
UmicomKernelVfsStatus UmicomKernelRamfsValidate(UmicomKernelRamfs *fs);
UmicomKernelVfsStatus UmicomKernelRamfsSnapshot(UmicomKernelRamfs *fs, UmicomKernelRamfsInfo *outInfo);
/* Reap frees only unlinked, unpinned nodes. On release failure outReaped reports
 * completed nodes; remaining page addresses stay owned and the next call retries.
 * Empty cache frames are released when the domain closes, not at every unlink. */
UmicomKernelVfsStatus UmicomKernelRamfsReap(UmicomKernelRamfs *fs, UmicomSize *outReaped);
/* Close requires no pins, including the mount's root pin. It closes admission
 * before tearing down the namespace. Retrying a failed close is supported;
 * reopening/resetting the same owner would resurrect remembered identities. */
UmicomKernelVfsStatus UmicomKernelRamfsClose(UmicomKernelRamfs *fs);
#endif /* UMICOM_KERNEL_RAMFS_H */
