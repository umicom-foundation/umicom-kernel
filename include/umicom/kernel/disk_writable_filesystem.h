/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/disk_writable_filesystem.h
 *
 * Own an exclusive FAT16 lifecycle lease, mutable provider and VFS root as one
 * checked dependency chain. A client with zero descriptors still retains its
 * attached namespace. Release clients before closing this single-use mount.
 * No Framework, user-space service implementation or automatic mount policy
 * enters the native Kernel through this adapter.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_H
#define UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_H
#include "umicom/kernel/fat16_writable_provider.h"

typedef struct UmicomKernelDiskWritableMount {
    const struct UmicomKernelDiskWritableMount *self;
    UmicomKernelVfsLifetime state;
    UmicomKernelFat16LifecycleCommitter lifecycle;
    UmicomKernelFat16WritableProvider provider;
    UmicomKernelVfs vfs;
    UmicomKernelVfsStatus lastStatus;
    UmicomBoolean busy;
    UmicomBoolean admitted;
} UmicomKernelDiskWritableMount;

/* Use zero-filled, stable storage disjoint from domain and all DMA frames.
 * Failed Open may retain resources: keep the owner alive and call Close until
 * it succeeds. An attempted mount lifetime is single-use, including failed
 * admission; fresh storage is required for another attempt. Calendar fields
 * must be explicitly supplied and validated before any transport admission.
 * This mounts a separate VFS domain rooted at /, not a RAMFS overlay. */
UmicomKernelVfsStatus UmicomKernelDiskWritableMountOpen(
    UmicomKernelDiskWritableMount *mount, UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks,
    const UmicomKernelFat16FileTime *time);

/* Grant only the caller-selected subset of ALL rights. The existing VFS and
 * process file services enforce those rights before provider operations. A
 * trusted process-file owner can attach mount.vfs through UserFilesAttach. */
UmicomKernelVfsStatus UmicomKernelDiskWritableMountClientOpen(
    UmicomKernelDiskWritableMount *mount, UmicomKernelVfsClient *client,
    UmicomU64 principal, UmicomKernelVfsRights rights);
UmicomKernelVfsStatus UmicomKernelDiskWritableMountSetTime(
    UmicomKernelDiskWritableMount *mount,
    const UmicomKernelFat16FileTime *time);

/* BUSY preserves an active namespace. Once clients and extra provider pins
 * are gone, retire the VFS root, close the provider locally, then release the
 * lifecycle lease. Transport reset/release failure retains CLOSING for retry.
 * Close never commits. Exact mutation and cleanup evidence stays available in
 * lifecycle.lastResult and lifecycle.commit.updater.lastCleanupStatus. */
UmicomKernelVfsStatus UmicomKernelDiskWritableMountClose(
    UmicomKernelDiskWritableMount *mount);

#endif /* UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_H */
