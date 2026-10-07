/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/disk_filesystem.h
 *
 * Own one checked read-only disk filesystem from transport acquisition through
 * final reset and DMA release. A VFS root pin and its attached clients keep the
 * FAT16 provider alive; the provider never owns the device independently.
 *
 * Supply stable, initially zero-filled Kernel storage and serialise calls.
 * An admitted provider/VFS lifetime is single-use, matching the existing VFS
 * contract. Admission may be retried only while no provider was admitted and
 * all earlier transport ownership has been released. A retained failed open
 * is a cleanup obligation, not permission to overwrite the handle.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_FILESYSTEM_H
#define UMICOM_KERNEL_DISK_FILESYSTEM_H
#include "umicom/kernel/fat16_provider.h"
#include "umicom/kernel/virtio_block.h"

#define UMICOM_DISK_MOUNT_READ_RIGHTS (UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY | \
    UMICOM_VFS_RIGHT_ENUMERATE | UMICOM_VFS_RIGHT_DUPLICATE)
/* Qualified QEMU uses a 10 MHz clock: this bounds acceptance of each inspector
 * operation, independently of how long the mount was idle. A VFS path lookup
 * may make several inspector calls. An in-flight sector can take its separate
 * request timeout beyond this deadline; completion is checked before bytes are
 * published. Individual requests retain the finite polling bound too. */
#define UMICOM_DISK_MOUNT_OPERATION_TICKS 100000000U

typedef struct UmicomKernelDiskMount {
    const struct UmicomKernelDiskMount *self;
    UmicomKernelVfsLifetime state;
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
    UmicomSize slot;
    UmicomSize partition;
    UmicomU64 timeoutTicks;
    UmicomKernelFat16Provider provider;
    UmicomKernelVfs vfs;
    UmicomKernelBlockStatus lastBlockStatus;
    UmicomKernelBlockStatus lastCleanupStatus;
    UmicomKernelVfsStatus lastStatus;
    UmicomBoolean busy;
    UmicomBoolean admitted;
    UmicomU64 operationStarted;
    UmicomU64 operationClock;
    UmicomSize operationReads;
} UmicomKernelDiskMount;

/* A non-OK result can retain a handle. Always keep the owner alive and call
 * Close until it reports OK. Diagnostics distinguish the primary operation,
 * the exact parser result (provider.lastDiskStatus), and transport cleanup.
 * No automatic mount, host disk selection, write or repair is performed. */
UmicomKernelVfsStatus UmicomKernelDiskMountOpen(UmicomKernelDiskMount *mount,
    UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize partition,
    UmicomU64 timeoutTicks);
/* Any attached client, including one with no descriptors, makes Close BUSY.
 * Additional trusted provider pins or mounts likewise keep this domain alive.
 * BUSY leaves the active filesystem and transport usable. After VFS unmount,
 * a reset/release failure leaves CLOSING state with the original block handle;
 * subsequent Close calls retry only outstanding cleanup. */
UmicomKernelVfsStatus UmicomKernelDiskMountClose(UmicomKernelDiskMount *mount);
/* This is the normal grant boundary for disk clients. The namespace is a
 * separate VFS domain rooted at /; it is not a /disk overlay inside RAMFS.
 * No requested WRITE, CREATE or REMOVE right can be granted here. */
UmicomKernelVfsStatus UmicomKernelDiskMountClientOpen(UmicomKernelDiskMount *mount,
    UmicomKernelVfsClient *client, UmicomU64 principal, UmicomKernelVfsRights rights);

void UmicomKernelDiskFilesystemValidate(void);
_Noreturn void UmicomKernelDiskFilesystemBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_DISK_FILESYSTEM_H */
