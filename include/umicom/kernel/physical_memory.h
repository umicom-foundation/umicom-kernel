/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/physical_memory.h
 *
 * PURPOSE:
 *   Publish K3's first physical page-frame allocator and accounting contract.
 *
 * EDUCATIONAL OVERVIEW:
 *   Physical memory is the RAM hardware actually provides.  K3 does NOT create
 *   virtual address spaces or page tables.  It divides one RAM range into
 *   fixed 4 KiB frames and records whether each frame is:
 *
 *     - reserved by the Kernel/platform; or
 *     - allocated to a caller; or
 *     - currently free.
 *
 *   Two separate bitmaps are used so "reserved" and "allocated" never become
 *   ambiguous.  This allows K3 to refuse attempts to free Kernel/DTB memory and
 *   to detect a caller freeing the same allocated frame twice.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_PHYSICAL_MEMORY_H
#define UMICOM_KERNEL_PHYSICAL_MEMORY_H

/* Import the full Umicom low-level types. */
#include "umicom/kernel/types.h"

/* K3 selects the conventional 4 KiB physical frame size.
 *
 * The later virtual-memory milestone may map these frames in different ways,
 * but it will not silently redefine what K3 calls one physical frame. */
#define UMICOM_KERNEL_PAGE_SIZE ((UmicomSize)4096U)

/* Keep the first allocator deliberately bounded and statically allocated.
 *
 * 65,536 frames * 4 KiB = 256 MiB of representable RAM.  The K3 QEMU profile
 * uses 128 MiB, so this provides headroom without dynamic metadata allocation. */
#define UMICOM_KERNEL_PHYSICAL_MAX_FRAMES ((UmicomSize)65536U)

/* List every result the first physical allocator can report explicitly. */
typedef enum UmicomKernelMemoryStatus {
    /* Operation completed and its output/state is valid. */
    UMICOM_KERNEL_MEMORY_OK = 0,

    /* A required pointer/value was invalid. */
    UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT = 1,

    /* RAM/page input did not meet the required alignment contract. */
    UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT = 2,

    /* Address arithmetic would wrap the target address type. */
    UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW = 3,

    /* A requested byte/frame range lies outside the configured RAM range. */
    UMICOM_KERNEL_MEMORY_OUTSIDE_RAM = 4,

    /* The statically bounded K3 bitmap cannot describe that many frames. */
    UMICOM_KERNEL_MEMORY_CAPACITY_EXCEEDED = 5,

    /* Reservation overlaps a frame that is already reserved or allocated. */
    UMICOM_KERNEL_MEMORY_OVERLAP = 6,

    /* Release requested one or more frames that are not currently reserved. */
    UMICOM_KERNEL_MEMORY_NOT_RESERVED = 7,

    /* A caller tried to free a frame that belongs to a reserved range. */
    UMICOM_KERNEL_MEMORY_RESERVED_FRAME = 8,

    /* A caller tried to free a frame that is not currently allocated. */
    UMICOM_KERNEL_MEMORY_NOT_ALLOCATED = 9,

    /* No free physical frame remains in the managed range. */
    UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY = 10,

    /* Internal bitmap/accounting state violated an allocator invariant. */
    UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE = 11,

    /* The physical-memory manager has not yet been initialised. */
    UMICOM_KERNEL_MEMORY_NOT_INITIALISED = 12
} UmicomKernelMemoryStatus;

/* Snapshot counters/addresses without exposing the allocator's private bitmap. */
typedef struct UmicomKernelPhysicalMemorySnapshot {
    /* First byte of the configured physical RAM range. */
    UmicomAddress ramBase;

    /* Total byte count represented by the configured RAM range. */
    UmicomSize ramBytes;

    /* Fixed K3 page/frame size, currently 4096 bytes. */
    UmicomSize pageBytes;

    /* Number of frames represented by the configured RAM range. */
    UmicomSize totalFrames;

    /* Frames unavailable because the Kernel/platform explicitly reserved them. */
    UmicomSize reservedFrames;

    /* Frames temporarily owned by successful allocate calls. */
    UmicomSize allocatedFrames;

    /* Frames available to a future successful allocate call. */
    UmicomSize freeFrames;
} UmicomKernelPhysicalMemorySnapshot;

/* Initialise the bounded physical allocator for one contiguous RAM range.
 *
 * `ramBase` and `ramBytes` must both be page aligned.  A successful call resets
 * the allocator to an all-free state; callers then reserve Kernel/DTB ranges
 * before making ordinary frame allocations. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryInitialize(
    UmicomAddress ramBase,
    UmicomSize ramBytes
);

/* Reserve every page touched by the supplied byte range.
 *
 * The operation is transactional: K3 first proves that the complete range is
 * free of existing reservations/allocations, then changes the bitmap. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryReserveRange(
    UmicomAddress base,
    UmicomSize bytes
);

/* Release every page touched by a previously reserved byte range.
 *
 * The complete range must currently be reserved.  K3 refuses partial release
 * if any page in the range is not reserved. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryReleaseReservedRange(
    UmicomAddress base,
    UmicomSize bytes
);

/* Allocate the lowest-addressed free page and return its physical address. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(
    UmicomAddress *outFrameAddress
);

/* Free one previously allocated, page-aligned frame.
 *
 * Reserved and already-free frames are reported distinctly. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(
    UmicomAddress frameAddress
);

/* Copy current allocator counters/geometry to caller-owned storage. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemorySnapshotRead(
    UmicomKernelPhysicalMemorySnapshot *outSnapshot
);

/* Recount the private bitmaps and prove all public accounting invariants still
 * match the allocator's stored counters. */
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryValidate(void);

/* Return a stable human-readable name for one memory status value. */
const char *UmicomKernelMemoryStatusName(UmicomKernelMemoryStatus status);

#endif /* UMICOM_KERNEL_PHYSICAL_MEMORY_H */
