/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_writable_provider.h
 *
 * A bounded mutable FAT16 namespace for the existing typed VFS. This provider
 * borrows one exclusive lifecycle owner. Each successful mutation includes an
 * accepted Stage and Finish; closing a descriptor never commits pending work.
 * The read-only FAT16 provider retains its separate immutable-media contract.
 *
 * Cache slots are storage, not identities. IDs are never reused, independent
 * descriptions pin their records, and successful mutations update the last
 * accepted metadata snapshot without changing those identities. A failed media
 * operation blocks further media access while local release remains possible.
 * These are ordered persistent writes, not a journal or atomic transactions.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_H
#define UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_H
#include "umicom/kernel/fat16_lifecycle_query.h"
#include "umicom/kernel/vfs.h"

#define UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT 64U
#define UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_ROOT_ID ((UmicomKernelVfsNodeId)1U)

typedef struct UmicomKernelFat16WritableProviderNode {
    UmicomKernelVfsNodeId id;
    char path[UMICOM_FAT16_PATH_BYTES];
    UmicomKernelFat16Entry entry;
    UmicomSize pins;
    UmicomBoolean occupied;
} UmicomKernelFat16WritableProviderNode;

/* Public layout permits static Kernel storage and deliberate fault tests.
 * All fields are private ownership state. Never copy, reset or directly edit
 * an admitted provider, or use the borrowed lifecycle API until Close succeeds.
 * The caller serialises this entire lifetime, including idle intervals, and
 * excludes external writers to the backing medium. No SMP lock is implied. */
typedef struct UmicomKernelFat16WritableProvider {
    const struct UmicomKernelFat16WritableProvider *self;
    UmicomKernelVfsLifetime state;
    UmicomKernelFat16LifecycleCommitter *lifecycle;
    UmicomKernelFat16FileTime time;
    UmicomBoolean busy;
    UmicomBoolean mediaFailed;
    UmicomSize pins;
    UmicomKernelVfsNodeId nextNodeId;
    UmicomU64 directoryEpoch;
    UmicomU64 committedOperations;
    UmicomU32 clusters;
    UmicomSize maximumFileBytes;
    UmicomKernelVfsStatus lastStatus;
    UmicomKernelFat16UpdateStatus lastUpdateStatus;
    UmicomKernelFat16WritableProviderNode nodes[UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_NODE_LIMIT];
    UmicomKernelFat16QueryResult queryStage;
    UmicomU8 zeroStage[UMICOM_FAT16_UPDATE_BYTES];
} UmicomKernelFat16WritableProvider;

/* Stable, initially zero-filled storage, separate from the lifecycle owner,
 * block domain and DMA pages. The lifecycle must be READY or COMMITTED and
 * remain exclusively borrowed until provider Close. Admission reads root
 * metadata through that same lease; a failed Open leaves the provider zero.
 * time is an explicit Gregorian FAT calendar, never a monotonic clock value.
 * Its convention is chosen by the caller and is retained until SetTime. */
UmicomKernelVfsStatus UmicomKernelFat16WritableProviderOpen(
    UmicomKernelFat16WritableProvider *provider,
    UmicomKernelFat16LifecycleCommitter *lifecycle,
    const UmicomKernelFat16FileTime *time);
UmicomKernelVfsStatus UmicomKernelFat16WritableProviderSetTime(
    UmicomKernelFat16WritableProvider *provider,
    const UmicomKernelFat16FileTime *time);

/* Close is local, idempotent after success, and BUSY while any pin remains.
 * Unmount every VFS first. It neither closes the borrowed lifecycle lease nor
 * flushes, repairs, retries or changes retained mutation evidence. */
UmicomKernelVfsStatus UmicomKernelFat16WritableProviderClose(
    UmicomKernelFat16WritableProvider *provider);

/* Writes overwrite or extend at a position no later than EOF, with at most
 * 4096 supplied bytes per accepted commit. Sparse holes are refused. Resize
 * can shrink or zero-extend by at most 4096 bytes, within the admitted bounded
 * chain capacity. Zero-byte writes and equal-size resize are local no-ops only
 * while the media owner remains usable. A failure reports zero accepted bytes;
 * it is not proof that no media bytes changed. Consult lifecycle.lastResult.
 *
 * Unlink refuses a pinned target with BUSY. This profile has no persistent
 * orphan storage; successful name removal therefore never invalidates an open
 * description. Close the target descriptors before removing its name.
 *
 * Every accepted create/remove advances a mount-wide directory epoch. Earlier
 * streams return CHANGED without changing their entry or cursor until rewound.
 * Local stat reports the last accepted snapshot, including after media failure;
 * it cannot determine what a failed write left on disk. Data operations refuse
 * after failure or unexpected lifecycle count/state changes. Validate, unpin
 * and Close remain local, so media failure cannot prevent resource release.
 *
 * Typed outputs require natural alignment. Caller spans are trusted Kernel
 * memory, non-overflowing and separate from each other, the entire provider,
 * lifecycle, block domain and DMA pages. Invalid storage is never dereferenced
 * for output. These checks are not a user-memory probing ABI. */
const UmicomKernelVfsOperations *UmicomKernelFat16WritableProviderOperationsGet(void);

#endif /* UMICOM_KERNEL_FAT16_WRITABLE_PROVIDER_H */
