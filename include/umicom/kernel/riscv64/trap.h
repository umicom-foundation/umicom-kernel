/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/trap.h
 *
 * PURPOSE:
 *   Define the RV64 machine-mode trap frame and the machine-mode trap API shared
 *   between Assembly entry code, C23 trap policy and the controlled trap and timer teaching sequence.
 *
 * EDUCATIONAL OVERVIEW:
 *   A RISC-V "trap" is the common architectural mechanism used for:
 *
 *     - synchronous exceptions, such as ECALL or an illegal instruction; and
 *     - asynchronous interrupts, such as a machine timer interrupt.
 *
 *   When a trap reaches machine mode, hardware records key state in CSRs such
 *   as mepc, mcause, mtval and mstatus, then jumps to the address in mtvec.
 *
 *   Hardware does NOT automatically save all general-purpose registers for our
 *   C code.  arch/riscv64/trap.S therefore builds the frame below before it
 *   calls UmicomRiscvTrapDispatch().
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_RISCV64_TRAP_H
#define UMICOM_KERNEL_RISCV64_TRAP_H

/* Import fixed-width RV64 integer/address types. */
#include "umicom/kernel/types.h"

/* The high bit of RV64 mcause distinguishes an interrupt from an exception. */
#define UMICOM_RISCV_MCAUSE_INTERRUPT_BIT ((UmicomU64)1ULL << 63U)

/* mcause exception code 11 means "environment call from M-mode". */
#define UMICOM_RISCV_EXCEPTION_ECALL_M_MODE ((UmicomU64)11U)

/* mcause interrupt code 7 means "machine timer interrupt". */
#define UMICOM_RISCV_INTERRUPT_MACHINE_TIMER ((UmicomU64)7U)

/* mie bit 7 enables delivery of machine-timer interrupts. */
#define UMICOM_RISCV_MIE_MTIE ((UmicomU64)1ULL << 7U)

/* mstatus bit 3 globally enables interrupts while currently in M-mode. */
#define UMICOM_RISCV_MSTATUS_MIE ((UmicomU64)1ULL << 3U)

/* mstatus bit 7 is MPIE: the previous M-mode interrupt-enable state that MRET
 * uses to restore MIE after a trap handler returns. */
#define UMICOM_RISCV_MSTATUS_MPIE ((UmicomU64)1ULL << 7U)

/* Save every integer register that may contain interrupted program state.
 *
 * x0 is omitted because the RISC-V zero register is architecturally fixed at
 * zero and therefore contains no state to preserve.
 *
 * The field order deliberately matches the byte offsets used by trap.S.
 * Keep the compile-time offset assertions below whenever this structure is
 * extended so a C/Assembly disagreement fails the build instead of corrupting
 * register state at runtime.
 */
typedef struct UmicomRiscvTrapFrame {
    UmicomU64 x1_ra;
    UmicomU64 x2_sp;
    UmicomU64 x3_gp;
    UmicomU64 x4_tp;
    UmicomU64 x5_t0;
    UmicomU64 x6_t1;
    UmicomU64 x7_t2;
    UmicomU64 x8_s0;
    UmicomU64 x9_s1;
    UmicomU64 x10_a0;
    UmicomU64 x11_a1;
    UmicomU64 x12_a2;
    UmicomU64 x13_a3;
    UmicomU64 x14_a4;
    UmicomU64 x15_a5;
    UmicomU64 x16_a6;
    UmicomU64 x17_a7;
    UmicomU64 x18_s2;
    UmicomU64 x19_s3;
    UmicomU64 x20_s4;
    UmicomU64 x21_s5;
    UmicomU64 x22_s6;
    UmicomU64 x23_s7;
    UmicomU64 x24_s8;
    UmicomU64 x25_s9;
    UmicomU64 x26_s10;
    UmicomU64 x27_s11;
    UmicomU64 x28_t3;
    UmicomU64 x29_t4;
    UmicomU64 x30_t5;
    UmicomU64 x31_t6;

    /* Program counter to which MRET will return. */
    UmicomU64 mepc;

    /* Machine status captured immediately after hardware entered the trap. */
    UmicomU64 mstatus;

    /* Encodes whether this was an interrupt and its exception/interrupt code. */
    UmicomU64 mcause;

    /* Trap-specific value, for example a faulting address/instruction detail. */
    UmicomU64 mtval;

    /* Explicit padding keeps the whole frame 16-byte aligned for the RV64 ABI. */
    UmicomU64 reserved;
} UmicomRiscvTrapFrame;

/* Verify the C structure's total size matches trap.S FRAME_SIZE exactly. */
_Static_assert(
    sizeof(UmicomRiscvTrapFrame) == 288U,
    "RV64 trap frame must remain exactly 288 bytes"
);

/* Verify important offsets against the Assembly constants.
 *
 * __builtin_offsetof is supplied by Clang and does not require a hosted libc
 * header, which keeps this freestanding milestone self-contained.
 */
_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, x1_ra) == 0U,
    "RV64 trap frame ra offset changed"
);

_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, x2_sp) == 8U,
    "RV64 trap frame sp offset changed"
);

_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, x31_t6) == 240U,
    "RV64 trap frame t6 offset changed"
);

_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, mepc) == 248U,
    "RV64 trap frame mepc offset changed"
);

_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, mstatus) == 256U,
    "RV64 trap frame mstatus offset changed"
);

_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, mcause) == 264U,
    "RV64 trap frame mcause offset changed"
);

_Static_assert(
    __builtin_offsetof(UmicomRiscvTrapFrame, mtval) == 272U,
    "RV64 trap frame mtval offset changed"
);

/* Copy of the small trap state exposed back to its teaching main routine.
 *
 * The interrupt handler owns the live volatile state.  Callers receive a normal
 * value snapshot after each controlled test has completed.
 */
typedef struct UmicomRiscvTrapSnapshot {
    UmicomU64 exceptionCount;
    UmicomU64 timerInterruptCount;
    UmicomU64 lastCauseCode;
    UmicomU64 lastMepc;
    UmicomU64 lastMtval;
    UmicomU64 lastWasInterrupt;
} UmicomRiscvTrapSnapshot;

/* Install the direct-mode machine trap vector and leave interrupts disabled. */
void UmicomRiscvTrapInstall(void);

/* Return the address written to mtvec so diagnostics can expose it in serial evidence. */
UmicomAddress UmicomRiscvTrapVectorAddress(void);

/* Deliberately execute one M-mode ECALL instruction for the exception test. */
void UmicomRiscvTriggerMachineEcall(void);

/* Enable only the machine-timer interrupt plus M-mode global interrupt bit. */
void UmicomRiscvMachineTimerInterruptEnable(void);

/* Disable the machine-timer source and M-mode global interrupt bit. */
void UmicomRiscvMachineTimerInterruptDisable(void);

/* Pause until the architecture observes an enabled interrupt/event. */
void UmicomRiscvWaitForInterrupt(void);

/* Read the current hardware-thread identifier from mhartid. */
UmicomU64 UmicomRiscvReadHartId(void);

/* C23 policy function called by the Assembly trap entry after register save. */
void UmicomRiscvTrapDispatch(UmicomRiscvTrapFrame *frame);

/* Copy the currently recorded trap counters/details to caller-owned memory. */
void UmicomRiscvTrapSnapshotRead(UmicomRiscvTrapSnapshot *outSnapshot);


/*-----------------------------------------------------------------------------
 * HISTORICAL SHORT RISC-V NAMES — RETAINED FOR REVIEW, NOT COMPILED
 *
 * The trap subsystem originally used `UmiRiscv...` names.  The actual types,
 * constants and functions above now use `UmicomRiscv...` and
 * `UMICOM_RISCV_...` directly.
 *
 * The old aliases below are intentionally preserved verbatim, but disabled.
 * This keeps the naming history visible and makes comparison with earlier
 * commits straightforward without allowing the abbreviated names to influence
 * the current program.
 *---------------------------------------------------------------------------*/
#if 0
/*-------------------------------------------------------------------------
 * LEGACY SOURCE-COMPATIBILITY ALIASES
 *
 * Preserve the earlier short type/function spellings while the canonical
 * Umicom Kernel source uses the full project name.
 *-------------------------------------------------------------------------*/

/* Legacy short type alias for the complete machine trap frame. */
typedef UmicomRiscvTrapFrame UmiRiscvTrapFrame;

/* Legacy short type alias for copied trap evidence. */
typedef UmicomRiscvTrapSnapshot UmiRiscvTrapSnapshot;

/* Legacy short constant aliases for architectural trap fields. */
#define UMI_RISCV_MCAUSE_INTERRUPT_BIT UMICOM_RISCV_MCAUSE_INTERRUPT_BIT
#define UMI_RISCV_EXCEPTION_ECALL_M_MODE UMICOM_RISCV_EXCEPTION_ECALL_M_MODE
#define UMI_RISCV_INTERRUPT_MACHINE_TIMER UMICOM_RISCV_INTERRUPT_MACHINE_TIMER
#define UMI_RISCV_MIE_MTIE UMICOM_RISCV_MIE_MTIE
#define UMI_RISCV_MSTATUS_MIE UMICOM_RISCV_MSTATUS_MIE
#define UMI_RISCV_MSTATUS_MPIE UMICOM_RISCV_MSTATUS_MPIE

/* Legacy short source aliases for the trap primitives. */
#define UmiRiscvTrapInstall UmicomRiscvTrapInstall
#define UmiRiscvTrapVectorAddress UmicomRiscvTrapVectorAddress
#define UmiRiscvTriggerMachineEcall UmicomRiscvTriggerMachineEcall
#define UmiRiscvMachineTimerInterruptEnable UmicomRiscvMachineTimerInterruptEnable
#define UmiRiscvMachineTimerInterruptDisable UmicomRiscvMachineTimerInterruptDisable
#define UmiRiscvWaitForInterrupt UmicomRiscvWaitForInterrupt
#define UmiRiscvReadHartId UmicomRiscvReadHartId
#define UmiRiscvTrapDispatch UmicomRiscvTrapDispatch
#define UmiRiscvTrapSnapshotRead UmicomRiscvTrapSnapshotRead

#endif

#endif /* UMICOM_KERNEL_RISCV64_TRAP_H */
