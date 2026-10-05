/* Umicom Kernel supervisor payload for owned, guarded stacks.
 * No strings, Kernel globals or hosted calls may escape these text pages.
 * The shared observation is data; nested volatile locals prove stack use.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/riscv64/mapped_region_probe.h"
#define UMICOM_REGION_TEXT __attribute__((section(".text.umicom_region_payload"), noinline))

static UMICOM_REGION_TEXT UmicomU64 UmicomRegionNested(UmicomU64 depth)
{
    volatile UmicomU64 values[8];
    for (UmicomSize index = 0U; index < 8U; ++index) values[index] = depth + index;
    /* Return through multiple actual C frames on the new translated stack. */
    const UmicomU64 nested = depth != 0U ? UmicomRegionNested(depth - 1U) : 0U;
    UmicomU64 result = nested;
    for (UmicomSize index = 0U; index < 8U; ++index) result += values[index];
    return result;
}
UMICOM_REGION_TEXT UmicomU64 UmicomKernelRegionPayload(volatile UmicomKernelRegionObservation *observation)
{
    observation->entered = 1U;
    UmicomAddress stack = 0U;
    UmicomU64 satp = 0U;
    __asm__ volatile("mv %0, sp" : "=r"(stack));
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    observation->observedStack = stack;
    observation->observedSatp = satp;
    observation->result = UmicomRegionNested(3U); /* 28 + 36 + 44 + 52 = 160. */
    if (observation->operation == 1U) {
        (void)UmicomRiscvRegionLowerGuard(observation->lowerGuard);
    } else if (observation->operation == 2U) {
        UmicomRiscvRegionUpperGuard(observation->upperGuard);
    } else if (observation->operation == 3U) {
        UmicomRiscvRegionExhaustStack(1024U); /* More than the two-page mapped stack can hold. */
    } else if (observation->operation == 4U) {
        UmicomRiscvRegionExecuteStack(observation->stackBase);
    }
    observation->completed = 1U;
    return observation->result;
}
