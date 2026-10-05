/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/physical_memory.c
 *
 * PURPOSE:
 *   Implement the physical-memory foundation's first bounded physical page-frame allocator.
 *
 * EDUCATIONAL OVERVIEW:
 *   This allocator is intentionally simple enough to inspect completely:
 *
 *     - one contiguous physical RAM range;
 *     - one fixed 4 KiB page/frame size;
 *     - one bitmap recording reserved frames;
 *     - one bitmap recording caller-allocated frames;
 *     - deterministic lowest-address-first allocation;
 *     - explicit accounting and invariant checks.
 *
 *   No dynamic memory is used to manage memory.  The allocator metadata lives
 *   in the Kernel's own BSS and is therefore itself covered by the reserved
 *   Kernel range before ordinary allocations begin.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import checked address alignment/addition primitives. */
#include "umicom/kernel/address.h"

/* Import this module's public allocator contract. */
#include "umicom/kernel/physical_memory.h"

/* One bit represents one frame.  Round the maximum frame count upward to a
 * whole-byte bitmap size. */
#define UMICOM_KERNEL_PHYSICAL_BITMAP_BYTES \
    ((UMICOM_KERNEL_PHYSICAL_MAX_FRAMES + (UmicomSize)7U) / (UmicomSize)8U)

/* Record frames that may never be returned by the ordinary allocator until an
 * explicit matching release operation succeeds. */
static UmicomU8 gReservedBitmap[UMICOM_KERNEL_PHYSICAL_BITMAP_BYTES];

/* Record frames currently owned by callers of AllocateFrame. */
static UmicomU8 gAllocatedBitmap[UMICOM_KERNEL_PHYSICAL_BITMAP_BYTES];

/* Remember whether initialization completed successfully. */
static UmicomBoolean gInitialised;

/* First physical byte managed by this allocator instance. */
static UmicomAddress gRamBase;

/* Total physical bytes managed by this allocator instance. */
static UmicomSize gRamBytes;

/* Number of 4 KiB frames represented by `gRamBytes`. */
static UmicomSize gFrameCount;

/* Running count of frames marked in the reserved bitmap. */
static UmicomSize gReservedCount;

/* Running count of frames marked in the allocated bitmap. */
static UmicomSize gAllocatedCount;

/* Describe an internal contiguous frame span using a first index plus count. */
typedef struct UmicomKernelFrameSpan {
    /* Zero-based frame index relative to `gRamBase`. */
    UmicomSize firstFrame;

    /* Number of consecutive frames touched by the requested byte range. */
    UmicomSize frameCount;
} UmicomKernelFrameSpan;

/* Return the byte position inside a bitmap for one frame index. */
static UmicomSize BitmapByteIndex(UmicomSize frameIndex)
{
    /* Eight frame-state bits fit in one byte. */
    return frameIndex / (UmicomSize)8U;
}

/* Return the bit position (0..7) inside the selected bitmap byte. */
static UmicomU8 BitmapBitIndex(UmicomSize frameIndex)
{
    /* Modulo eight isolates the frame's position inside its bitmap byte. */
    return (UmicomU8)(frameIndex % (UmicomSize)8U);
}

/* Construct the one-bit mask for a frame index. */
static UmicomU8 BitmapMask(UmicomSize frameIndex)
{
    /* Determine which bit inside the byte belongs to this frame. */
    const UmicomU8 bitIndex = BitmapBitIndex(frameIndex);

    /* Shift the integer value 1 to that bit position, then narrow the known
     * 0..255 result to the bitmap's byte type. */
    return (UmicomU8)((UmicomU32)1U << (UmicomU32)bitIndex);
}

/* Read one boolean bit from a private bitmap. */
static UmicomBoolean BitmapRead(const UmicomU8 *bitmap, UmicomSize frameIndex)
{
    /* Find the byte containing the requested frame's state. */
    const UmicomSize byteIndex = BitmapByteIndex(frameIndex);

    /* Construct the mask identifying the requested bit inside that byte. */
    const UmicomU8 mask = BitmapMask(frameIndex);

    /* A non-zero masked value means the bit is set. */
    return (
        (bitmap[byteIndex] & mask) != (UmicomU8)0U
    ) ? UMICOM_TRUE : UMICOM_FALSE;
}

/* Set one frame-state bit without disturbing neighbouring frame bits. */
static void BitmapSet(UmicomU8 *bitmap, UmicomSize frameIndex)
{
    /* Locate the byte containing the frame's state. */
    const UmicomSize byteIndex = BitmapByteIndex(frameIndex);

    /* Construct the frame's one-bit mask. */
    const UmicomU8 mask = BitmapMask(frameIndex);

    /* OR sets the selected bit while preserving every other bit in the byte. */
    bitmap[byteIndex] = (UmicomU8)(bitmap[byteIndex] | mask);
}

/* Clear one frame-state bit without disturbing neighbouring frame bits. */
static void BitmapClear(UmicomU8 *bitmap, UmicomSize frameIndex)
{
    /* Locate the byte containing the frame's state. */
    const UmicomSize byteIndex = BitmapByteIndex(frameIndex);

    /* Construct the frame's one-bit mask. */
    const UmicomU8 mask = BitmapMask(frameIndex);

    /* AND with the inverted mask clears only the selected bit. */
    bitmap[byteIndex] = (UmicomU8)(bitmap[byteIndex] & (UmicomU8)~mask);
}

/* Clear every byte of both static frame-state bitmaps. */
static void BitmapsClear(void)
{
    /* Visit each metadata byte exactly once. */
    for (
        UmicomSize index = (UmicomSize)0U;
        index < UMICOM_KERNEL_PHYSICAL_BITMAP_BYTES;
        ++index
    ) {
        /* No frame is reserved immediately after initialization. */
        gReservedBitmap[index] = (UmicomU8)0U;

        /* No frame is allocated immediately after initialization. */
        gAllocatedBitmap[index] = (UmicomU8)0U;
    }
}

/* Convert one validated frame index back into its physical page address. */
static UmicomAddress FrameAddress(UmicomSize frameIndex)
{
    /* Each preceding frame contributes exactly one page of byte offset. */
    const UmicomSize byteOffset = frameIndex * UMICOM_KERNEL_PAGE_SIZE;

    /* Initialization validated that the complete represented range fits the
     * target address space, so this bounded offset cannot overflow here. */
    return gRamBase + (UmicomAddress)byteOffset;
}

/* Return the exclusive end of configured RAM through checked arithmetic. */
static UmicomKernelMemoryStatus RamEnd(UmicomAddress *outRamEnd)
{
    /* Caller-owned output storage is required. */
    if (outRamEnd == (UmicomAddress *)0) {
        /* Report a programming error rather than dereferencing null. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* The allocator cannot describe RAM before successful initialization. */
    if (gInitialised == UMICOM_FALSE) {
        /* Distinguish missing initialization from a malformed range. */
        return UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
    }

    /* Reuse the shared checked-add primitive for base + byte length. */
    if (
        UmicomKernelAddressAddChecked(gRamBase, gRamBytes, outRamEnd) ==
        UMICOM_FALSE
    ) {
        /* This should be impossible after successful initialization, so expose
         * it as an invariant-level range failure rather than wrapping. */
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    }

    /* `outRamEnd` now identifies the first byte outside managed RAM. */
    return UMICOM_KERNEL_MEMORY_OK;
}

/* Convert an arbitrary byte range to the full physical pages it touches. */
static UmicomKernelMemoryStatus RangeToFrameSpan(
    UmicomAddress base,
    UmicomSize bytes,
    UmicomKernelFrameSpan *outSpan
)
{
    /* Caller-owned span storage is mandatory. */
    if (outSpan == (UmicomKernelFrameSpan *)0) {
        /* No trustworthy output can be published. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* Zero bytes do not identify a meaningful reservation/release range. */
    if (bytes == (UmicomSize)0U) {
        /* Reject an accidental no-op that could hide a caller bug. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* The allocator needs a configured RAM geometry before translating ranges. */
    if (gInitialised == UMICOM_FALSE) {
        /* Tell the caller initialization is the missing prerequisite. */
        return UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
    }

    /* Compute the exclusive RAM end using the same checked geometry. */
    UmicomAddress ramEnd = (UmicomAddress)0U;

    /* Stop immediately if the internal RAM geometry cannot be reconstructed. */
    const UmicomKernelMemoryStatus ramStatus = RamEnd(&ramEnd);

    /* Only a successful RAM-end calculation permits range validation. */
    if (ramStatus != UMICOM_KERNEL_MEMORY_OK) {
        /* Preserve the exact reason reported by the geometry helper. */
        return ramStatus;
    }

    /* A requested range may not begin below the managed RAM base. */
    if (base < gRamBase) {
        /* This would describe ROM/MMIO/unknown physical space. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Calculate the caller's exclusive byte-range end without wrap-around. */
    UmicomAddress rangeEnd = (UmicomAddress)0U;

    /* Refuse a range whose base + length cannot be represented. */
    if (
        UmicomKernelAddressAddChecked(base, bytes, &rangeEnd) ==
        UMICOM_FALSE
    ) {
        /* Distinguish arithmetic overflow from an ordinary out-of-RAM request. */
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    }

    /* The complete byte range must remain inside managed RAM. */
    if (rangeEnd > ramEnd) {
        /* Do not silently clip reservations to the allocator boundary. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Round the starting byte down so the first partly touched page is included. */
    UmicomAddress alignedStart = (UmicomAddress)0U;

    /* Page size is a compile-time power of two, but keep the checked helper so
     * the invariant remains visible if that constant is ever edited. */
    if (
        UmicomKernelAddressAlignDown(
            base,
            (UmicomU64)UMICOM_KERNEL_PAGE_SIZE,
            &alignedStart
        ) == UMICOM_FALSE
    ) {
        /* An invalid page-size definition is an allocator invariant failure. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Round the exclusive range end upward so the final partly touched page is
     * included in the span. */
    UmicomAddress alignedEnd = (UmicomAddress)0U;

    /* Refuse an alignment operation if it would overflow. */
    if (
        UmicomKernelAddressAlignUpChecked(
            rangeEnd,
            (UmicomU64)UMICOM_KERNEL_PAGE_SIZE,
            &alignedEnd
        ) == UMICOM_FALSE
    ) {
        /* Expose upward-alignment overflow explicitly. */
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    }

    /* Upward rounding at the final RAM byte must not move beyond configured RAM. */
    if (alignedEnd > ramEnd) {
        /* The caller's original range was inside RAM, but the containing page
         * is not fully representable by this allocator profile. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Calculate the byte distance from RAM base to the aligned first page. */
    const UmicomSize startOffset =
        (UmicomSize)(alignedStart - gRamBase);

    /* Translate that page-aligned byte offset to a zero-based frame index. */
    const UmicomSize firstFrame =
        startOffset / UMICOM_KERNEL_PAGE_SIZE;

    /* Calculate how many aligned bytes the complete reservation touches. */
    const UmicomSize alignedBytes =
        (UmicomSize)(alignedEnd - alignedStart);

    /* Translate the aligned byte count to an exact page count. */
    const UmicomSize frameCount =
        alignedBytes / UMICOM_KERNEL_PAGE_SIZE;

    /* A non-zero input range must always touch at least one physical frame. */
    if (frameCount == (UmicomSize)0U) {
        /* Treat an impossible zero-page result as an invariant failure. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Defensive bound: the first frame must exist in the configured geometry. */
    if (firstFrame >= gFrameCount) {
        /* Reject the inconsistent translation before touching the bitmap. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Calculate the exclusive ending frame index using ordinary bounded frame
     * counts.  The prior RAM-range checks guarantee no address wrap occurred. */
    const UmicomSize exclusiveFrame = firstFrame + frameCount;

    /* The rounded span must not extend past the represented frame count. */
    if (exclusiveFrame > gFrameCount) {
        /* Refuse a bitmap index outside active allocator metadata. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Publish the first frame only after every validation has passed. */
    outSpan->firstFrame = firstFrame;

    /* Publish the complete number of touched frames. */
    outSpan->frameCount = frameCount;

    /* The caller may now safely inspect or modify these bitmap entries. */
    return UMICOM_KERNEL_MEMORY_OK;
}

/* Validate a single page-aligned physical address and translate it to an index. */
static UmicomKernelMemoryStatus FrameAddressToIndex(
    UmicomAddress frameAddress,
    UmicomSize *outFrameIndex
)
{
    /* Caller-owned index storage is mandatory. */
    if (outFrameIndex == (UmicomSize *)0) {
        /* Do not attempt a translation with nowhere to store it. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* The manager needs configured RAM before validating a physical page. */
    if (gInitialised == UMICOM_FALSE) {
        /* Report the missing initialization state. */
        return UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
    }

    /* Every freeable frame address must be exactly page aligned. */
    if (
        (frameAddress & (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - 1U)) !=
        (UmicomAddress)0U
    ) {
        /* A byte inside a page is not a valid frame handle. */
        return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    }

    /* The frame address may not begin before managed RAM. */
    if (frameAddress < gRamBase) {
        /* Reject unknown lower physical space. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Recover the zero-based byte offset within managed RAM. */
    const UmicomSize offset = (UmicomSize)(frameAddress - gRamBase);

    /* Translate the aligned byte offset into a frame index. */
    const UmicomSize frameIndex = offset / UMICOM_KERNEL_PAGE_SIZE;

    /* The translated index must exist in the current RAM geometry. */
    if (frameIndex >= gFrameCount) {
        /* The address is at or beyond the exclusive RAM end. */
        return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    }

    /* Publish the validated index. */
    *outFrameIndex = frameIndex;

    /* The caller can now safely read the corresponding bitmap bits. */
    return UMICOM_KERNEL_MEMORY_OK;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryInitialize(
    UmicomAddress ramBase,
    UmicomSize ramBytes
)
{
    /* A memory manager with no bytes could never satisfy an allocation. */
    if (ramBytes == (UmicomSize)0U) {
        /* Report malformed initialization input. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* Physical RAM base must begin on a page boundary. */
    if (
        (ramBase & (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - 1U)) !=
        (UmicomAddress)0U
    ) {
        /* Misaligned RAM would make frame indexing ambiguous. */
        return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    }

    /* RAM byte length must contain an exact whole number of pages. */
    if (
        (ramBytes % UMICOM_KERNEL_PAGE_SIZE) != (UmicomSize)0U
    ) {
        /* the physical-memory foundation refuses an incomplete tail page rather than silently dropping it. */
        return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    }

    /* Prove the exclusive RAM end can be represented before publishing state. */
    UmicomAddress checkedEnd = (UmicomAddress)0U;

    /* A wrapping physical range is never a valid allocator geometry. */
    if (
        UmicomKernelAddressAddChecked(ramBase, ramBytes, &checkedEnd) ==
        UMICOM_FALSE
    ) {
        /* Keep previous allocator state untouched on invalid initialization. */
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    }

    /* The value is intentionally used only to prove the checked addition.
     * Suppress an otherwise misleading unused-variable concern by comparing it
     * to the base, which must be strictly smaller for a non-zero RAM length. */
    if (checkedEnd <= ramBase) {
        /* This can only occur if the geometry violated an arithmetic invariant. */
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    }

    /* Determine how many fixed-size physical frames the RAM range contains. */
    const UmicomSize frameCount = ramBytes / UMICOM_KERNEL_PAGE_SIZE;

    /* The first implementation deliberately uses bounded static metadata. */
    if (frameCount > UMICOM_KERNEL_PHYSICAL_MAX_FRAMES) {
        /* Do not partially manage a RAM range larger than the bitmap capacity. */
        return UMICOM_KERNEL_MEMORY_CAPACITY_EXCEEDED;
    }

    /* Clear old bitmap state only after every input validation has succeeded. */
    BitmapsClear();

    /* Publish the new RAM base. */
    gRamBase = ramBase;

    /* Publish the new RAM byte length. */
    gRamBytes = ramBytes;

    /* Publish the validated frame count. */
    gFrameCount = frameCount;

    /* No frame has been reserved yet. */
    gReservedCount = (UmicomSize)0U;

    /* No frame has been allocated yet. */
    gAllocatedCount = (UmicomSize)0U;

    /* Mark initialization complete only after all related state is coherent. */
    gInitialised = UMICOM_TRUE;

    /* The caller must reserve Kernel/platform ranges before ordinary allocation. */
    return UMICOM_KERNEL_MEMORY_OK;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryReserveRange(
    UmicomAddress base,
    UmicomSize bytes
)
{
    /* Translate the caller's byte range to complete touched physical pages. */
    UmicomKernelFrameSpan span;

    /* Validate and build the frame span before reading any bitmap state. */
    const UmicomKernelMemoryStatus spanStatus =
        RangeToFrameSpan(base, bytes, &span);

    /* Preserve the exact validation result if translation failed. */
    if (spanStatus != UMICOM_KERNEL_MEMORY_OK) {
        /* Nothing has changed, so the operation is failure-atomic. */
        return spanStatus;
    }

    /* First pass: prove every target page is completely free.
     *
     * Performing validation before mutation prevents a later overlap from
     * leaving the first half of the range accidentally reserved. */
    for (
        UmicomSize offset = (UmicomSize)0U;
        offset < span.frameCount;
        ++offset
    ) {
        /* Translate the local span offset to the allocator-wide frame index. */
        const UmicomSize frameIndex = span.firstFrame + offset;

        /* An existing reservation makes the requested range overlap. */
        if (BitmapRead(gReservedBitmap, frameIndex) != UMICOM_FALSE) {
            /* Leave every bitmap bit unchanged. */
            return UMICOM_KERNEL_MEMORY_OVERLAP;
        }

        /* An allocated frame also cannot be converted silently into reserved state. */
        if (BitmapRead(gAllocatedBitmap, frameIndex) != UMICOM_FALSE) {
            /* Preserve the caller's existing allocation ownership. */
            return UMICOM_KERNEL_MEMORY_OVERLAP;
        }
    }

    /* Second pass: now that the complete range is known to be free, commit all
     * reservation bits. */
    for (
        UmicomSize offset = (UmicomSize)0U;
        offset < span.frameCount;
        ++offset
    ) {
        /* Translate the local span offset to the global frame index. */
        const UmicomSize frameIndex = span.firstFrame + offset;

        /* Mark this frame unavailable to ordinary allocation. */
        BitmapSet(gReservedBitmap, frameIndex);
    }

    /* Account for the complete successfully reserved span once. */
    gReservedCount += span.frameCount;

    /* The range is now fully reserved. */
    return UMICOM_KERNEL_MEMORY_OK;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryReleaseReservedRange(
    UmicomAddress base,
    UmicomSize bytes
)
{
    /* Translate the caller's byte range to its containing physical frames. */
    UmicomKernelFrameSpan span;

    /* Validate the requested range before inspecting ownership bits. */
    const UmicomKernelMemoryStatus spanStatus =
        RangeToFrameSpan(base, bytes, &span);

    /* Preserve the exact range-validation failure. */
    if (spanStatus != UMICOM_KERNEL_MEMORY_OK) {
        /* No bitmap state has changed. */
        return spanStatus;
    }

    /* First pass: every frame must currently belong to the reserved set. */
    for (
        UmicomSize offset = (UmicomSize)0U;
        offset < span.frameCount;
        ++offset
    ) {
        /* Translate the local offset to the global frame index. */
        const UmicomSize frameIndex = span.firstFrame + offset;

        /* A missing reserved bit means this is not the exact state requested. */
        if (BitmapRead(gReservedBitmap, frameIndex) == UMICOM_FALSE) {
            /* Failure remains transactional: no earlier bit is cleared. */
            return UMICOM_KERNEL_MEMORY_NOT_RESERVED;
        }

        /* Reserved and allocated simultaneously would violate the physical-memory foundation's core model. */
        if (BitmapRead(gAllocatedBitmap, frameIndex) != UMICOM_FALSE) {
            /* Expose corruption instead of trying to guess which owner wins. */
            return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
        }
    }

    /* Second pass: the complete span was proven reserved, so release it. */
    for (
        UmicomSize offset = (UmicomSize)0U;
        offset < span.frameCount;
        ++offset
    ) {
        /* Translate the local offset to the global frame index. */
        const UmicomSize frameIndex = span.firstFrame + offset;

        /* Clear the reservation bit and make the page available again. */
        BitmapClear(gReservedBitmap, frameIndex);
    }

    /* The prior validation proves subtraction cannot underflow this counter. */
    gReservedCount -= span.frameCount;

    /* The complete range was released successfully. */
    return UMICOM_KERNEL_MEMORY_OK;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(
    UmicomAddress *outFrameAddress
)
{
    /* Allocation needs caller-owned storage for the selected physical address. */
    if (outFrameAddress == (UmicomAddress *)0) {
        /* Reject a request that would allocate a page nobody can identify. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* Allocation is undefined until RAM geometry has been configured. */
    if (gInitialised == UMICOM_FALSE) {
        /* Report the missing initialization step explicitly. */
        return UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
    }

    /* Deterministically search from the lowest frame to the highest. */
    for (
        UmicomSize frameIndex = (UmicomSize)0U;
        frameIndex < gFrameCount;
        ++frameIndex
    ) {
        /* Reserved pages are owned by the Kernel/platform and must be skipped. */
        const UmicomBoolean isReserved =
            BitmapRead(gReservedBitmap, frameIndex);

        /* Allocated pages are already owned by another caller and must be skipped. */
        const UmicomBoolean isAllocated =
            BitmapRead(gAllocatedBitmap, frameIndex);

        /* A frame is allocatable only when neither ownership bitmap is set. */
        if (isReserved == UMICOM_FALSE && isAllocated == UMICOM_FALSE) {
            /* Claim the page before publishing its address. */
            BitmapSet(gAllocatedBitmap, frameIndex);

            /* Update allocation accounting in the same successful operation. */
            ++gAllocatedCount;

            /* Convert the claimed frame index back into a physical page address. */
            *outFrameAddress = FrameAddress(frameIndex);

            /* The caller now owns exactly one physical page frame. */
            return UMICOM_KERNEL_MEMORY_OK;
        }
    }

    /* Every represented page is either reserved or already allocated. */
    return UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(
    UmicomAddress frameAddress
)
{
    /* Translate/validate the supplied page address before reading bitmaps. */
    UmicomSize frameIndex = (UmicomSize)0U;

    /* Reject misaligned or out-of-range addresses explicitly. */
    const UmicomKernelMemoryStatus indexStatus =
        FrameAddressToIndex(frameAddress, &frameIndex);

    /* Preserve the exact address-validation failure. */
    if (indexStatus != UMICOM_KERNEL_MEMORY_OK) {
        /* No ownership state has changed. */
        return indexStatus;
    }

    /* A reserved page belongs to the Kernel/platform, not an allocator caller. */
    if (BitmapRead(gReservedBitmap, frameIndex) != UMICOM_FALSE) {
        /* Distinguish this from an ordinary double-free. */
        return UMICOM_KERNEL_MEMORY_RESERVED_FRAME;
    }

    /* No allocated bit means nobody currently owns this page through AllocateFrame. */
    if (BitmapRead(gAllocatedBitmap, frameIndex) == UMICOM_FALSE) {
        /* A second free of the same page reaches this explicit result. */
        return UMICOM_KERNEL_MEMORY_NOT_ALLOCATED;
    }

    /* Release caller ownership of the page. */
    BitmapClear(gAllocatedBitmap, frameIndex);

    /* The successful allocation corresponding to this page is no longer live. */
    --gAllocatedCount;

    /* The page is now free for deterministic reuse. */
    return UMICOM_KERNEL_MEMORY_OK;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemorySnapshotRead(
    UmicomKernelPhysicalMemorySnapshot *outSnapshot
)
{
    /* Caller-owned output storage is mandatory. */
    if (outSnapshot == (UmicomKernelPhysicalMemorySnapshot *)0) {
        /* Do not dereference a null snapshot pointer. */
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }

    /* Counters have no defined meaning until initialization completes. */
    if (gInitialised == UMICOM_FALSE) {
        /* Report missing initialization explicitly. */
        return UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
    }

    /* Publish the configured first RAM byte. */
    outSnapshot->ramBase = gRamBase;

    /* Publish the complete RAM byte length. */
    outSnapshot->ramBytes = gRamBytes;

    /* Publish the frame-size contract used by all counts. */
    outSnapshot->pageBytes = UMICOM_KERNEL_PAGE_SIZE;

    /* Publish the total number of represented physical frames. */
    outSnapshot->totalFrames = gFrameCount;

    /* Publish frames permanently/temporarily reserved by explicit reserve calls. */
    outSnapshot->reservedFrames = gReservedCount;

    /* Publish frames currently owned by allocation callers. */
    outSnapshot->allocatedFrames = gAllocatedCount;

    /* Free frames are exactly those in neither ownership category.
     *
     * Validate first-order arithmetic before subtracting to avoid hiding a
     * corrupted counter as an unsigned wrap. */
    if (gReservedCount > gFrameCount) {
        /* A reserved count larger than all frames is impossible. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Determine how many frames remain after reservations. */
    const UmicomSize afterReserved = gFrameCount - gReservedCount;

    /* Allocations cannot exceed the frames left after reservations. */
    if (gAllocatedCount > afterReserved) {
        /* Refuse to publish a wrapped free-frame count. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Publish exact remaining free-frame accounting. */
    outSnapshot->freeFrames = afterReserved - gAllocatedCount;

    /* The caller now owns a coherent snapshot. */
    return UMICOM_KERNEL_MEMORY_OK;
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryValidate(void)
{
    /* Validation requires an initialized allocator geometry. */
    if (gInitialised == UMICOM_FALSE) {
        /* Report missing initialization rather than scanning inactive metadata. */
        return UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
    }

    /* Recount reservation bits independently of the stored counter. */
    UmicomSize observedReserved = (UmicomSize)0U;

    /* Recount allocation bits independently of the stored counter. */
    UmicomSize observedAllocated = (UmicomSize)0U;

    /* Inspect every active frame exactly once. */
    for (
        UmicomSize frameIndex = (UmicomSize)0U;
        frameIndex < gFrameCount;
        ++frameIndex
    ) {
        /* Read reservation state for this physical frame. */
        const UmicomBoolean isReserved =
            BitmapRead(gReservedBitmap, frameIndex);

        /* Read caller-allocation state for this physical frame. */
        const UmicomBoolean isAllocated =
            BitmapRead(gAllocatedBitmap, frameIndex);

        /* A page may never have two owners in the physical-memory foundation's model. */
        if (isReserved != UMICOM_FALSE && isAllocated != UMICOM_FALSE) {
            /* This is direct allocator metadata corruption. */
            return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
        }

        /* Count one observed reserved page. */
        if (isReserved != UMICOM_FALSE) {
            /* Increment only after observing a set reservation bit. */
            ++observedReserved;
        }

        /* Count one observed allocated page. */
        if (isAllocated != UMICOM_FALSE) {
            /* Increment only after observing a set allocation bit. */
            ++observedAllocated;
        }
    }

    /* Stored reservation accounting must equal the independent bitmap recount. */
    if (observedReserved != gReservedCount) {
        /* Do not allow counter drift to remain hidden. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Stored allocation accounting must equal the independent bitmap recount. */
    if (observedAllocated != gAllocatedCount) {
        /* Do not allow counter drift to remain hidden. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Combined ownership may never exceed the total number of frames. */
    if (gReservedCount > gFrameCount - gAllocatedCount) {
        /* This catches impossible counter combinations even if bitmap recounts matched. */
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }

    /* Every the physical-memory foundation ownership/accounting invariant currently holds. */
    return UMICOM_KERNEL_MEMORY_OK;
}

const char *UmicomKernelMemoryStatusName(UmicomKernelMemoryStatus status)
{
    /* Map each public enum value to stable teaching/diagnostic text. */
    switch (status) {
        /* Successful operation. */
        case UMICOM_KERNEL_MEMORY_OK:
            return "ok";

        /* Required input/output was invalid. */
        case UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT:
            return "invalid-argument";

        /* Page/RAM alignment was invalid. */
        case UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT:
            return "invalid-alignment";

        /* Checked address arithmetic refused unsigned wrap. */
        case UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW:
            return "range-overflow";

        /* Range/address does not belong to configured RAM. */
        case UMICOM_KERNEL_MEMORY_OUTSIDE_RAM:
            return "outside-ram";

        /* Static metadata cannot represent the requested geometry. */
        case UMICOM_KERNEL_MEMORY_CAPACITY_EXCEEDED:
            return "capacity-exceeded";

        /* Reservation touched an already-owned frame. */
        case UMICOM_KERNEL_MEMORY_OVERLAP:
            return "overlap";

        /* Release included a frame without a reservation. */
        case UMICOM_KERNEL_MEMORY_NOT_RESERVED:
            return "not-reserved";

        /* Free targeted a platform/Kernel-reserved frame. */
        case UMICOM_KERNEL_MEMORY_RESERVED_FRAME:
            return "reserved-frame";

        /* Free targeted a frame not owned by an allocation. */
        case UMICOM_KERNEL_MEMORY_NOT_ALLOCATED:
            return "not-allocated";

        /* No free page frame remains. */
        case UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY:
            return "out-of-memory";

        /* Internal ownership/accounting state is inconsistent. */
        case UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE:
            return "invariant-failure";

        /* Allocator operations began before initialization. */
        case UMICOM_KERNEL_MEMORY_NOT_INITIALISED:
            return "not-initialised";

        /* A value outside the public enum is itself invalid diagnostic input. */
        default:
            return "unknown-status";
    }
}

UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFrameQuery(
    UmicomAddress frameAddress, UmicomKernelPhysicalFrameState *outState)
{
    /* Validate the destination before consulting allocator-private metadata. */
    if (outState == (UmicomKernelPhysicalFrameState *)0) {
        return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    }
    /* Reuse the same alignment, initialisation and range checks as FreeFrame.
     * This is an observation only: no reservation or allocation bit is changed. */
    UmicomSize frameIndex = 0U;
    const UmicomKernelMemoryStatus status = FrameAddressToIndex(frameAddress, &frameIndex);
    if (status != UMICOM_KERNEL_MEMORY_OK) {
        return status;
    }
    const UmicomBoolean reserved = BitmapRead(gReservedBitmap, frameIndex);
    const UmicomBoolean allocated = BitmapRead(gAllocatedBitmap, frameIndex);
    /* A frame cannot belong to both classes. Do not publish an arbitrary winner
     * if corruption made both bits true. The caller receives no new state. */
    if (reserved != UMICOM_FALSE && allocated != UMICOM_FALSE) {
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    }
    *outState = reserved != UMICOM_FALSE ? UMICOM_PHYSICAL_FRAME_RESERVED :
        (allocated != UMICOM_FALSE ? UMICOM_PHYSICAL_FRAME_ALLOCATED : UMICOM_PHYSICAL_FRAME_FREE);
    return UMICOM_KERNEL_MEMORY_OK;
}
