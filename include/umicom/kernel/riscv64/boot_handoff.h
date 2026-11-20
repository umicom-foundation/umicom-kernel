/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/boot_handoff.h
 *
 * PURPOSE:
 *   Describe the checked RAM, Kernel and device-tree placement at RISC-V boot.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_RISCV64_BOOT_HANDOFF_H
#define UMICOM_KERNEL_RISCV64_BOOT_HANDOFF_H

#include "umicom/kernel/boot_handoff.h"
#include "umicom/kernel/boot_reservations.h"

typedef struct UmicomRiscvBootHandoffInput {
    UmicomU64 hart;
    UmicomBootSpan ram;
    UmicomBootSpan kernel;
    UmicomBootSpan deviceTree;
} UmicomRiscvBootHandoffInput;

/* Project the actual startup spans and the existing reservation owner's plan
 * through the version-1 writer and reader. Claim only the supplied single RAM
 * range and the FDT. The plan must be the complete result of boot preflight.
 * All objects are accessible and stable during the call. No address in the
 * record is dereferenced and no page is reserved. Every refusal preserves out.
 * A valid plan with more reservations than version 1 can carry returns LIMIT.
 */
UmicomBootHandoffStatus UmicomRiscvBootHandoffProject(const UmicomRiscvBootHandoffInput *input,
    const UmicomKernelBootReservationPlan *plan, UmicomKernelBootHandoff *out);

#endif
