/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/object_cache.h
 *
 * PURPOSE:
 *   Allocate aligned, fixed-size Kernel records from independently owned frames.
 *
 * EDUCATIONAL OVERVIEW:
 *   A directory entry or work item is usually smaller than a physical page.
 *   Giving each record a whole page wastes memory; putting bookkeeping inside
 *   freed records lets an old pointer corrupt the allocator's next operation.
 *   This cache divides frames into slots and keeps all metadata separately.
 *
 *   A returned reference includes the address and a non-repeating ticket.
 *   Free checks both, so an old reference cannot release a new object which
 *   happens to reuse the same address. This does not revoke raw C pointers or
 *   make a trusted Kernel caller into an isolated process.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_OBJECT_CACHE_H
#define UMICOM_KERNEL_OBJECT_CACHE_H
#include "umicom/kernel/physical_memory.h"

/* These limits bound metadata, validation work and worst-case retained memory.
 * Large buffers and stacks belong to a future mapped-region owner, not a
 * guessed contiguous run of physical frames returned by this cache. */
#define UMICOM_OBJECT_CACHE_FRAME_LIMIT 16U
#define UMICOM_OBJECT_CACHE_SLOT_LIMIT 128U
#define UMICOM_OBJECT_CACHE_GUARD_BYTES 16U
#define UMICOM_OBJECT_CACHE_MAX_BYTES 4080U

typedef enum UmicomKernelObjectCacheState {
    UMICOM_OBJECT_CACHE_UNINITIALISED,
    UMICOM_OBJECT_CACHE_OPEN,
    UMICOM_OBJECT_CACHE_CLOSING,
    UMICOM_OBJECT_CACHE_CLOSED,
    UMICOM_OBJECT_CACHE_POISONED
} UmicomKernelObjectCacheState;
typedef enum UmicomKernelObjectStatus {
    UMICOM_OBJECT_OK,
    UMICOM_OBJECT_INVALID_ARGUMENT,
    UMICOM_OBJECT_BAD_STATE,
    UMICOM_OBJECT_UNSAFE_CONTEXT,
    UMICOM_OBJECT_INVALID_REFERENCE,
    UMICOM_OBJECT_CAPACITY,
    UMICOM_OBJECT_OUT_OF_MEMORY,
    UMICOM_OBJECT_TICKET_EXHAUSTED,
    UMICOM_OBJECT_BUSY,
    UMICOM_OBJECT_BACKEND_ERROR,
    UMICOM_OBJECT_RELEASE_FAILED,
    UMICOM_OBJECT_CORRUPT_STATE
} UmicomKernelObjectStatus;

/* Visible for static allocation and deliberate fault-injection tests only.
 * Consumers must not copy a live cache or edit its records. A zero ticket means
 * a free slot; released object bytes never contain free-list links. */
typedef struct UmicomKernelObjectPage {
    UmicomAddress frame;
    UmicomBoolean owned;
    UmicomSize live;
    UmicomU64 tickets[UMICOM_OBJECT_CACHE_SLOT_LIMIT];
} UmicomKernelObjectPage;
typedef struct UmicomKernelObjectCache {
    const struct UmicomKernelObjectCache *self;
    UmicomKernelObjectCacheState state;
    UmicomSize objectBytes;
    UmicomSize alignment;
    UmicomSize stride;
    UmicomSize slotsPerFrame;
    UmicomSize frameLimit;
    UmicomSize frames;
    UmicomSize liveObjects;
    UmicomSize peakObjects;
    UmicomU64 issued; /* Maximum ticket is issued once; the next request refuses. */
    UmicomKernelObjectPage pages[UMICOM_OBJECT_CACHE_FRAME_LIMIT];
} UmicomKernelObjectCache;

typedef struct UmicomKernelObjectReference {
    const UmicomKernelObjectCache *cache;
    void *address;
    UmicomU64 ticket;
} UmicomKernelObjectReference;
typedef struct UmicomKernelObjectCacheInfo {
    UmicomKernelObjectCacheState state;
    UmicomSize objectBytes;
    UmicomSize alignment;
    UmicomSize stride;
    UmicomSize slotsPerFrame;
    UmicomSize frames;
    UmicomSize liveObjects;
    UmicomSize emptyFrames;
    UmicomSize liveBytes;
    UmicomSize reservedBytes;
    UmicomSize peakObjects;
    UmicomU64 issued;
} UmicomKernelObjectCacheInfo;

/* Initialise stable, entirely zero-filled Kernel storage once. Physical memory
 * must already be initialised and Kernel/metadata storage reserved. Every frame
 * is directly accessible in the current Bare M-mode profile. alignment is a
 * power of two in 1..4096; objectBytes is 1..4080; frameLimit is 1..16.
 * No frame is acquired by initialisation. A closed cache cannot be reset. */
UmicomKernelObjectStatus UmicomKernelObjectCacheInitialize(UmicomKernelObjectCache *cache,
    UmicomSize objectBytes, UmicomSize alignment, UmicomSize frameLimit);
/* Allocate lazily and return zeroed object bytes. Outputs are unchanged on
 * refusal. Use the reference to Resolve or Free; pointer-only Free is absent. */
UmicomKernelObjectStatus UmicomKernelObjectCacheAllocate(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference *outReference);
UmicomKernelObjectStatus UmicomKernelObjectCacheResolve(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference reference, void **outObject);
/* The object, padding and guard are scrubbed before the slot becomes reusable.
 * Free does not return its frame; that separate decision belongs to Trim. */
UmicomKernelObjectStatus UmicomKernelObjectCacheFree(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference reference);
/* Release only empty frames. Stops on the first failed release, retaining that
 * frame and all later ones for retry. outReleased reports actual progress even
 * on a release error; it is untouched for invalid arguments or unsafe context. */
UmicomKernelObjectStatus UmicomKernelObjectCacheTrim(UmicomKernelObjectCache *cache,
    UmicomSize *outReleased);
/* Close refuses live objects. Once closing starts, admission stays closed even
 * if frame release fails. Retrying Close releases only what is still owned. */
UmicomKernelObjectStatus UmicomKernelObjectCacheClose(UmicomKernelObjectCache *cache);
UmicomKernelObjectStatus UmicomKernelObjectCacheSnapshot(UmicomKernelObjectCache *cache,
    UmicomKernelObjectCacheInfo *outInfo);
/* Validate metadata, backing-frame state and every live tail guard. A corrupt
 * cache is poisoned, retains its frames and refuses allocation/free/close. This
 * is diagnostic containment, not recovery from arbitrary Kernel-memory damage. */
UmicomKernelObjectStatus UmicomKernelObjectCacheValidate(UmicomKernelObjectCache *cache);
const char *UmicomKernelObjectStatusName(UmicomKernelObjectStatus status);
void UmicomKernelObjectCachesValidateExecution(void);

/* Narrow architecture gate. All APIs require serial Kernel calls on hart zero,
 * interrupts masked, Bare addressing, and no unfinished managed ownership.
 * They do not mask interrupts themselves and are not IRQ-safe or SMP-safe.
 * Pointer arguments must be valid Kernel storage, not alias cache metadata or
 * its backing frames. Raw object pointers expire at successful Free. */
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void);
#endif /* UMICOM_KERNEL_OBJECT_CACHE_H */
