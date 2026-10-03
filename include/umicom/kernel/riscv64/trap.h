/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/trap.h
 *
 * PURPOSE:
 *   Define the RV64 machine-mode trap frame and the small K2 trap API shared
 *   between Assembly entry code, C23 trap policy and the K2 teaching sequence.
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
 *   calls UmiRiscvTrapDispatch().
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
#define UMI_RISCV_MCAUSE_INTERRUPT_BIT ((UmiU64)1ULL << 63U)

/* mcause exception code 11 means "environment call from M-mode". */
#define UMI_RISCV_EXCEPTION_ECALL_M_MODE ((UmiU64)11U)

/* mcause interrupt code 7 means "machine timer interrupt". */
#define UMI_RISCV_INTERRUPT_MACHINE_TIMER ((UmiU64)7U)

/* mie bit 7 enables delivery of machine-timer interrupts. */
#define UMI_RISCV_MIE_MTIE ((UmiU64)1ULL << 7U)

/* mstatus bit 3 globally enables interrupts while currently in M-mode. */
#define UMI_RISCV_MSTATUS_MIE ((UmiU64)1ULL << 3U)

/* mstatus bit 7 is MPIE: the previous M-mode interrupt-enable state that MRET
 * uses to restore MIE after a trap handler returns. */
#define UMI_RISCV_MSTATUS_MPIE ((UmiU64)1ULL << 7U)

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
typedef struct UmiRiscvTrapFrame {
    UmiU64 x1_ra;
    UmiU64 x2_sp;
    UmiU64 x3_gp;
    UmiU64 x4_tp;
    UmiU64 x5_t0;
    UmiU64 x6_t1;
    UmiU64 x7_t2;
    UmiU64 x8_s0;
    UmiU64 x9_s1;
    UmiU64 x10_a0;
    UmiU64 x11_a1;
    UmiU64 x12_a2;
    UmiU64 x13_a3;
    UmiU64 x14_a4;
    UmiU64 x15_a5;
    UmiU64 x16_a6;
    UmiU64 x17_a7;
    UmiU64 x18_s2;
    UmiU64 x19_s3;
    UmiU64 x20_s4;
    UmiU64 x21_s5;
    UmiU64 x22_s6;
    UmiU64 x23_s7;
    UmiU64 x24_s8;
    UmiU64 x25_s9;
    UmiU64 x26_s10;
    UmiU64 x27_s11;
    UmiU64 x28_t3;
    UmiU64 x29_t4;
    UmiU64 x30_t5;
    UmiU64 x31_t6;

    /* Program counter to which MRET will return. */
    UmiU64 mepc;

    /* Machine status captured immediately after hardware entered the trap. */
    UmiU64 mstatus;

    /* Encodes whether this was an interrupt and its exception/interrupt code. */
    UmiU64 mcause;

    /* Trap-specific value, for example a faulting address/instruction detail. */
    UmiU64 mtval;

    /* Explicit padding keeps the whole frame 16-byte aligned for the RV64 ABI. */
    UmiU64 reserved;
} UmiRiscvTrapFrame;

/* Verify the C structure's total size matches trap.S FRAME_SIZE exactly. */
_Static_assert(
    sizeof(UmiRiscvTrapFrame) == 288U,
    "RV64 trap frame must remain exactly 288 bytes"
);

/* Verify important offsets against the Assembly constants.
 *
 * __builtin_offsetof is supplied by Clang and does not require a hosted libc
 * header, which keeps this freestanding milestone self-contained.
 */
_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, x1_ra) == 0U,
    "RV64 trap frame ra offset changed"
);

_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, x2_sp) == 8U,
    "RV64 trap frame sp offset changed"
);

_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, x31_t6) == 240U,
    "RV64 trap frame t6 offset changed"
);

_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, mepc) == 248U,
    "RV64 trap frame mepc offset changed"
);

_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, mstatus) == 256U,
    "RV64 trap frame mstatus offset changed"
);

_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, mcause) == 264U,
    "RV64 trap frame mcause offset changed"
);

_Static_assert(
    __builtin_offsetof(UmiRiscvTrapFrame, mtval) == 272U,
    "RV64 trap frame mtval offset changed"
);

/* Copy of the small trap state K2 exposes back to its teaching main routine.
 *
 * The interrupt handler owns the live volatile state.  Callers receive a normal
 * value snapshot after each controlled test has completed.
 */
typedef struct UmiRiscvTrapSnapshot {
    UmiU64 exceptionCount;
    UmiU64 timerInterruptCount;
    UmiU64 lastCauseCode;
    UmiU64 lastMepc;
    UmiU64 lastMtval;
    UmiU64 lastWasInterrupt;
} UmiRiscvTrapSnapshot;

/* Install the direct-mode machine trap vector and leave interrupts disabled. */
void UmiRiscvTrapInstall(void);

/* Return the address written to mtvec so K2 can expose it in serial evidence. */
UmiAddress UmiRiscvTrapVectorAddress(void);

/* Deliberately execute one M-mode ECALL instruction for the exception test. */
void UmiRiscvTriggerMachineEcall(void);

/* Enable only the machine-timer interrupt plus M-mode global interrupt bit. */
void UmiRiscvMachineTimerInterruptEnable(void);

/* Disable the machine-timer source and M-mode global interrupt bit. */
void UmiRiscvMachineTimerInterruptDisable(void);

/* Pause until the architecture observes an enabled interrupt/event. */
void UmiRiscvWaitForInterrupt(void);

/* Read the current hardware-thread identifier from mhartid. */
UmiU64 UmiRiscvReadHartId(void);

/* C23 policy function called by the Assembly trap entry after register save. */
void UmiRiscvTrapDispatch(UmiRiscvTrapFrame *frame);

/* Copy the currently recorded K2 trap counters/details to caller-owned memory. */
void UmiRiscvTrapSnapshotRead(UmiRiscvTrapSnapshot *outSnapshot);


/*-------------------------------------------------------------------------
 * K3 FULL-NAME SOURCE ALIASES
 *
 * Preserve the already committed K2 type/function symbols while all new
 * Umicom Kernel source uses the full project name.
 *-------------------------------------------------------------------------*/

/* Full-name type alias for the complete machine trap frame. */
typedef UmiRiscvTrapFrame UmicomRiscvTrapFrame;

/* Full-name type alias for copied trap evidence. */
typedef UmiRiscvTrapSnapshot UmicomRiscvTrapSnapshot;

/* Full-name constant aliases for architectural trap fields. */
#define UMICOM_RISCV_MCAUSE_INTERRUPT_BIT UMI_RISCV_MCAUSE_INTERRUPT_BIT
#define UMICOM_RISCV_EXCEPTION_ECALL_M_MODE UMI_RISCV_EXCEPTION_ECALL_M_MODE
#define UMICOM_RISCV_INTERRUPT_MACHINE_TIMER UMI_RISCV_INTERRUPT_MACHINE_TIMER
#define UMICOM_RISCV_MIE_MTIE UMI_RISCV_MIE_MTIE
#define UMICOM_RISCV_MSTATUS_MIE UMI_RISCV_MSTATUS_MIE
#define UMICOM_RISCV_MSTATUS_MPIE UMI_RISCV_MSTATUS_MPIE

/* Full-name source aliases for the K2 trap primitives. */
#define UmicomRiscvTrapInstall UmiRiscvTrapInstall
#define UmicomRiscvTrapVectorAddress UmiRiscvTrapVectorAddress
#define UmicomRiscvTriggerMachineEcall UmiRiscvTriggerMachineEcall
#define UmicomRiscvMachineTimerInterruptEnable UmiRiscvMachineTimerInterruptEnable
#define UmicomRiscvMachineTimerInterruptDisable UmiRiscvMachineTimerInterruptDisable
#define UmicomRiscvWaitForInterrupt UmiRiscvWaitForInterrupt
#define UmicomRiscvReadHartId UmiRiscvReadHartId
#define UmicomRiscvTrapDispatch UmiRiscvTrapDispatch
#define UmicomRiscvTrapSnapshotRead UmiRiscvTrapSnapshotRead

#endif /* UMICOM_KERNEL_RISCV64_TRAP_H */
