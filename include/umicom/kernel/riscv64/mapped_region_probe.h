/* Umicom Kernel protected-stack acceptance contract.
 * These payload operations are trusted diagnostics, not native syscall numbers.
 * Everything they execute is collected into its own mapped text pages.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_KERNEL_RISCV64_MAPPED_REGION_PROBE_H
#define UMICOM_KERNEL_RISCV64_MAPPED_REGION_PROBE_H
#define UMICOM_REGION_PROBE_COOKIE 0x554d49434f4d
#ifndef __ASSEMBLER__
#include "umicom/kernel/types.h"
typedef struct UmicomKernelRegionObservation {
    UmicomU64 operation;
    UmicomAddress lowerGuard;
    UmicomAddress upperGuard;
    UmicomAddress stackBase;
    UmicomU64 entered;
    UmicomAddress observedStack;
    UmicomU64 observedSatp;
    UmicomU64 result;
    UmicomU64 completed;
} UmicomKernelRegionObservation;
void UmicomRiscvRegionPayloadEntry(void);
UmicomU64 UmicomKernelRegionPayload(volatile UmicomKernelRegionObservation *observation);
UmicomU64 UmicomRiscvRegionLowerGuard(UmicomAddress address);
void UmicomRiscvRegionUpperGuard(UmicomAddress address);
void UmicomRiscvRegionExhaustStack(UmicomSize steps);
void UmicomRiscvRegionExecuteStack(UmicomAddress address);
extern UmicomU8 UmicomRiscvRegionCompletion[];
extern UmicomU8 UmicomRiscvRegionLowerInstruction[];
extern UmicomU8 UmicomRiscvRegionUpperInstruction[];
extern UmicomU8 UmicomRiscvRegionStackInstruction[];
extern UmicomU8 __umicom_region_text_start[];
extern UmicomU8 __umicom_region_text_end[];
#endif
#endif
