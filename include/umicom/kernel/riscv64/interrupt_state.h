/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/interrupt_state.h
 *
 * PURPOSE:
 *   Keep exact machine CSR operations separate from the C ownership rules.
 *   Native tests provide an explicit model of this boundary, not a fake hart.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_RISCV64_INTERRUPT_STATE_H
#define UMICOM_KERNEL_RISCV64_INTERRUPT_STATE_H
#include "umicom/kernel/types.h"

/* Read only while MIE is masked. The pending field is a device observation;
 * it need not be identical across two snapshots even while delivery is masked. */
typedef struct UmicomRiscvInterruptState {
    UmicomU64 hart;
    UmicomU64 status;
    UmicomU64 sources;
    UmicomU64 pending;
    UmicomAddress vector;
    UmicomAddress scratch;
    UmicomU64 translation;
    UmicomU64 delegation;
} UmicomRiscvInterruptState;
_Static_assert(sizeof(UmicomRiscvInterruptState) == 64U, "CSR snapshot must match Assembly stores");
_Static_assert(__builtin_offsetof(UmicomRiscvInterruptState, delegation) == 56U,
    "CSR snapshot delegation offset must match Assembly");

/* Return exactly the former MIE bit (zero or eight), clearing it atomically.
 * Neither this primitive nor Restore acknowledges a pending device source. */
UmicomU64 UmicomRiscvInterruptMaskSave(void);
void UmicomRiscvInterruptMaskRestore(UmicomU64 mieBit);
void UmicomRiscvInterruptStateRead(UmicomRiscvInterruptState *outState);
/* Caller already owns a masked section. These change named mie bits only. */
void UmicomRiscvInterruptSourcesEnable(UmicomU64 sources);
void UmicomRiscvInterruptSourcesDisable(UmicomU64 sources);
/* Board-side release/admission check. A disabled source can still have a
 * future device deadline, so mie/mip alone cannot establish quiescence. */
UmicomBoolean UmicomPlatformInterruptSourceQuiescent(UmicomU64 sources);
#endif /* UMICOM_KERNEL_RISCV64_INTERRUPT_STATE_H */
