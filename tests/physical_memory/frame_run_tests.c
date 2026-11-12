/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/physical_memory/frame_run_tests.c
 *
 * PURPOSE:
 *   Exercise aligned physical runs, fragmentation, ownership and failure-atomic release through the real allocator.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/physical_memory.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) {     fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)
#define OK UMICOM_KERNEL_MEMORY_OK
#define PAGE UMICOM_KERNEL_PAGE_SIZE
#define BASE ((UmicomAddress)0x1000U)
#define FRAMES ((UmicomSize)32U)
#define MARKER ((UmicomAddress)0x12345678U)

/* A snapshot is checked field by field: structure padding is not semantic
 * allocator state. Per-frame queries detect mutations hidden by equal totals. */
typedef struct Observation {
    UmicomKernelPhysicalMemorySnapshot snapshot;
    UmicomKernelPhysicalFrameState states[32];
} Observation;

static int Observe(Observation *out)
{
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&out->snapshot) == OK);
    CHECK(out->snapshot.totalFrames <= 32U);
    for (UmicomSize i = 0; i < out->snapshot.totalFrames; ++i)
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(out->snapshot.ramBase +
            (UmicomAddress)(i * PAGE), &out->states[i]) == OK);
    return 0;
}
static int Same(const Observation *before)
{
    Observation after;
    CHECK(Observe(&after) == 0);
    CHECK(before->snapshot.ramBase == after.snapshot.ramBase);
    CHECK(before->snapshot.ramBytes == after.snapshot.ramBytes);
    CHECK(before->snapshot.pageBytes == after.snapshot.pageBytes);
    CHECK(before->snapshot.totalFrames == after.snapshot.totalFrames);
    CHECK(before->snapshot.reservedFrames == after.snapshot.reservedFrames);
    CHECK(before->snapshot.allocatedFrames == after.snapshot.allocatedFrames);
    CHECK(before->snapshot.freeFrames == after.snapshot.freeFrames);
    for (UmicomSize i = 0; i < before->snapshot.totalFrames; ++i)
        CHECK(before->states[i] == after.states[i]);
    CHECK(UmicomKernelPhysicalMemoryValidate() == OK);
    return 0;
}
static int Accounting(UmicomSize reserved, UmicomSize allocated)
{
    UmicomKernelPhysicalMemorySnapshot observed;
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&observed) == OK);
    CHECK(observed.reservedFrames == reserved && observed.allocatedFrames == allocated);
    CHECK(observed.freeFrames == observed.totalFrames - reserved - allocated);
    CHECK(UmicomKernelPhysicalMemoryValidate() == OK);
    return 0;
}
static int RejectAllocate(UmicomSize count, UmicomSize alignment,
    UmicomKernelMemoryStatus expected, int nullOutput)
{
    Observation before;
    CHECK(Observe(&before) == 0);
    UmicomAddress output = MARKER;
    CHECK(UmicomKernelPhysicalMemoryAllocateFrames(count, alignment,
        nullOutput ? NULL : &output) == expected);
    CHECK(output == MARKER);
    return Same(&before);
}
static int RejectFree(UmicomAddress first, UmicomSize count,
    UmicomKernelMemoryStatus expected)
{
    Observation before;
    CHECK(Observe(&before) == 0);
    CHECK(UmicomKernelPhysicalMemoryFreeFrames(first, count) == expected);
    return Same(&before);
}
static int AllocateExpected(UmicomSize count, UmicomSize alignment, UmicomAddress expected)
{
    UmicomAddress output = MARKER;
    CHECK(UmicomKernelPhysicalMemoryAllocateFrames(count, alignment, &output) == OK);
    CHECK(output == expected);
    for (UmicomSize i = 0; i < count; ++i) {
        UmicomKernelPhysicalFrameState state;
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(output + (UmicomAddress)(i * PAGE), &state) == OK);
        CHECK(state == UMICOM_PHYSICAL_FRAME_ALLOCATED);
    }
    CHECK(UmicomKernelPhysicalMemoryValidate() == OK);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *name = argv[1];
    UmicomAddress frame = MARKER;
    if (strcmp(name, "uninitialised-allocate") == 0) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrames(2U, 1U, &frame) == UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
        CHECK(frame == MARKER);
        return 0;
    }
    if (strcmp(name, "uninitialised-free") == 0) {
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE, 2U) == UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
        return 0;
    }
    CHECK(UmicomKernelPhysicalMemoryInitialize(BASE, FRAMES * PAGE) == OK);
    if (strcmp(name, "null-output") == 0)
        return RejectAllocate(1U, 1U, UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT, 1);
    if (strcmp(name, "zero-count") == 0)
        return RejectAllocate(0U, 1U, UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT, 0);
    if (strcmp(name, "zero-alignment") == 0)
        return RejectAllocate(1U, 0U, UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT, 0);
    if (strcmp(name, "nonpower-alignment") == 0)
        return RejectAllocate(1U, 3U, UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT, 0);
    if (strcmp(name, "maximum-count") == 0)
        return RejectAllocate(~(UmicomSize)0U, 1U, UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY, 0);
    if (strcmp(name, "largest-alignment") == 0)
        return RejectAllocate(1U, (UmicomSize)1U << 63U, UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY, 0);
    if (strcmp(name, "largest-alignment-zero") == 0) {
        CHECK(UmicomKernelPhysicalMemoryInitialize(0U, FRAMES * PAGE) == OK);
        CHECK(AllocateExpected(2U, (UmicomSize)1U << 63U, 0U) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(0U, 2U) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "single") == 0 || strcmp(name, "pair") == 0 ||
        strcmp(name, "whole-range") == 0) {
        UmicomSize count = strcmp(name, "single") == 0 ? 1U :
            (strcmp(name, "pair") == 0 ? 2U : FRAMES);
        CHECK(AllocateExpected(count, 1U, BASE) == 0);
        CHECK(Accounting(0U, count) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE, count) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "last-frame") == 0) {
        CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE, (FRAMES - 1U) * PAGE) == OK);
        CHECK(AllocateExpected(1U, 1U, BASE + (UmicomAddress)((FRAMES - 1U) * PAGE)) == 0);
        return Accounting(FRAMES - 1U, 1U);
    }
    if (strcmp(name, "physical-alignment") == 0) {
        /* RAM starts at page number one; relative-index alignment would wrongly
         * return 0x1000. Absolute four-page alignment first admits 0x4000. */
        CHECK(AllocateExpected(3U, 4U, (UmicomAddress)0x4000U) == 0);
        return Accounting(0U, 3U);
    }
    if (strcmp(name, "reservation-barrier") == 0 || strcmp(name, "allocation-barrier") == 0) {
        if (strcmp(name, "reservation-barrier") == 0) {
            CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)0x5000U, PAGE) == OK);
        } else {
            CHECK(AllocateExpected(1U, 4U, (UmicomAddress)0x4000U) == 0);
        }
        CHECK(AllocateExpected(4U, 4U, (UmicomAddress)0x8000U) == 0);
        return Accounting(strcmp(name, "reservation-barrier") == 0 ? 1U : 0U,
            strcmp(name, "allocation-barrier") == 0 ? 5U : 4U);
    }
    if (strcmp(name, "fragmentation") == 0 || strcmp(name, "refused-output") == 0) {
        for (UmicomSize i = 1U; i < FRAMES; i += 2U)
            CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE + (UmicomAddress)(i * PAGE), PAGE) == OK);
        CHECK(Accounting(FRAMES / 2U, 0U) == 0);
        return RejectAllocate(strcmp(name, "fragmentation") == 0 ? 2U : FRAMES,
            1U, UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY, 0);
    }
    if (strcmp(name, "free-zero") == 0)
        return RejectFree(BASE, 0U, UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT);
    if (strcmp(name, "free-unaligned") == 0)
        return RejectFree(BASE + 1U, 1U, UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT);
    if (strcmp(name, "free-below") == 0)
        return RejectFree(BASE - (UmicomAddress)PAGE, 1U, UMICOM_KERNEL_MEMORY_OUTSIDE_RAM);
    if (strcmp(name, "free-end") == 0)
        return RejectFree(BASE + (UmicomAddress)(FRAMES * PAGE), 1U, UMICOM_KERNEL_MEMORY_OUTSIDE_RAM);
    if (strcmp(name, "free-overrun") == 0)
        return RejectFree(BASE + (UmicomAddress)((FRAMES - 1U) * PAGE), 2U, UMICOM_KERNEL_MEMORY_OUTSIDE_RAM);
    if (strcmp(name, "free-largest-count") == 0)
        return RejectFree(BASE, ~(UmicomSize)0U, UMICOM_KERNEL_MEMORY_OUTSIDE_RAM);
    if (strcmp(name, "free-unallocated") == 0)
        return RejectFree(BASE, 4U, UMICOM_KERNEL_MEMORY_NOT_ALLOCATED);
    if (strcmp(name, "free-reserved-tail") == 0) {
        CHECK(AllocateExpected(3U, 1U, BASE) == 0);
        CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE + (UmicomAddress)(3U * PAGE), PAGE) == OK);
        return RejectFree(BASE, 4U, UMICOM_KERNEL_MEMORY_RESERVED_FRAME);
    }
    if (strcmp(name, "free-hole-tail") == 0 || strcmp(name, "free-hole-middle") == 0) {
        CHECK(AllocateExpected(4U, 1U, BASE) == 0);
        const UmicomSize hole = strcmp(name, "free-hole-tail") == 0 ? 3U : 1U;
        CHECK(UmicomKernelPhysicalMemoryFreeFrame(BASE + (UmicomAddress)(hole * PAGE)) == OK);
        return RejectFree(BASE, 4U, UMICOM_KERNEL_MEMORY_NOT_ALLOCATED);
    }
    if (strcmp(name, "free-repeat") == 0) {
        CHECK(AllocateExpected(3U, 1U, BASE) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE, 3U) == OK);
        return RejectFree(BASE, 3U, UMICOM_KERNEL_MEMORY_NOT_ALLOCATED);
    }
    if (strcmp(name, "partial-release") == 0) {
        CHECK(AllocateExpected(5U, 1U, BASE) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE + (UmicomAddress)PAGE, 3U) == OK);
        CHECK(AllocateExpected(3U, 1U, BASE + (UmicomAddress)PAGE) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE, 5U) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "mixed-single-run") == 0) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&frame) == OK && frame == BASE);
        CHECK(AllocateExpected(3U, 1U, BASE + (UmicomAddress)PAGE) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrame(BASE + (UmicomAddress)PAGE) == OK);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE + (UmicomAddress)(2U * PAGE), 2U) == OK);
        CHECK(UmicomKernelPhysicalMemoryFreeFrame(BASE) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "alignment-one") == 0 || strcmp(name, "alignment-two") == 0 ||
        strcmp(name, "alignment-eight") == 0 || strcmp(name, "alignment-large") == 0) {
        UmicomSize align = strcmp(name, "alignment-one") == 0 ? 1U :
            (strcmp(name, "alignment-two") == 0 ? 2U :
            (strcmp(name, "alignment-eight") == 0 ? 8U : 32U));
        CHECK(AllocateExpected(1U, align, (UmicomAddress)(align * PAGE)) == 0);
        return Accounting(0U, 1U);
    }
    if (strcmp(name, "top-address") == 0) {
        const UmicomAddress top = (~(UmicomAddress)0U) & ~((UmicomAddress)PAGE - 1U);
        const UmicomAddress start = top - (UmicomAddress)(8U * PAGE);
        CHECK(UmicomKernelPhysicalMemoryInitialize(start, 8U * PAGE) == OK);
        CHECK(AllocateExpected(8U, 1U, start) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(start, 8U) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "maximum-capacity") == 0) {
        CHECK(UmicomKernelPhysicalMemoryInitialize(0U, UMICOM_KERNEL_PHYSICAL_MAX_FRAMES * PAGE) == OK);
        CHECK(AllocateExpected(UMICOM_KERNEL_PHYSICAL_MAX_FRAMES, 1U, 0U) == 0);
        CHECK(Accounting(0U, UMICOM_KERNEL_PHYSICAL_MAX_FRAMES) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(0U, UMICOM_KERNEL_PHYSICAL_MAX_FRAMES) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "reuse-lowest") == 0) {
        CHECK(AllocateExpected(4U, 1U, BASE) == 0);
        CHECK(AllocateExpected(4U, 1U, BASE + (UmicomAddress)(4U * PAGE)) == 0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE, 4U) == OK);
        CHECK(AllocateExpected(4U, 1U, BASE) == 0);
        return Accounting(0U, 8U);
    }
    if (strcmp(name, "reinitialise") == 0) {
        CHECK(AllocateExpected(4U, 1U, BASE) == 0);
        CHECK(UmicomKernelPhysicalMemoryInitialize(BASE, FRAMES * PAGE) == OK);
        return Accounting(0U, 0U);
    }
    if (strcmp(name, "invalid-reinitialise") == 0) {
        CHECK(AllocateExpected(4U, 1U, BASE) == 0);
        Observation before;
        CHECK(Observe(&before) == 0);
        CHECK(UmicomKernelPhysicalMemoryInitialize(BASE + 1U, FRAMES * PAGE) == UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT);
        return Same(&before);
    }
    fprintf(stderr, "Unknown frame-run case: %s\n", name);
    return 2;
}
