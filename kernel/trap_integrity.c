/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/trap_integrity.c
 *
 * PURPOSE:
 *   Validate machine trap inputs and the narrowly permitted changes on return.
 *
 * EDUCATIONAL OVERVIEW:
 *   A saved status word is not harmless diagnostic data once it is written back
 *   to mstatus. It can select privilege, address translation and interrupt state.
 *   The return check therefore accepts an explicit policy, not an arbitrary
 *   replacement frame. It also protects every saved integer register.
 *
 *   This file reads ordinary Kernel-owned memory only. Hardware entry, stack
 *   switching, instruction inspection and fatal handling have separate owners.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/trap_integrity.h"

/* MPP=3 describes an interrupted machine instruction. MPRV can change that
 * instruction's data permissions, but does not change its execution privilege. */
#define UMICOM_TRAP_MPP_MASK ((UmicomU64)0x1800U)
#define UMICOM_TRAP_MPRV ((UmicomU64)0x20000U)
#define UMICOM_TRAP_UNSAVED_STATE ((UmicomU64)0x6600U) /* FS and VS fields. */

static UmicomBoolean UmicomTrapPcContains(UmicomAddress begin,
    UmicomAddress end, UmicomU64 pc, UmicomU64 bytes)
{
    /* Subtract only after ordering is established. A wrapped pc+length must
     * not turn an out-of-image instruction into an apparently valid range. */
    if (begin >= end || pc < begin || pc >= end || (pc & 1U) != 0U) {
        return UMICOM_FALSE;
    }
    return bytes <= (UmicomU64)end - pc ? UMICOM_TRUE : UMICOM_FALSE;
}

UmicomKernelTrapIntegrityStatus UmicomKernelTrapFrameCheck(
    const UmicomRiscvTrapFrame *frame, UmicomKernelTrapRoute route,
    UmicomAddress textBegin, UmicomAddress textEnd)
{
    if (frame == (const UmicomRiscvTrapFrame *)0 || textBegin >= textEnd ||
        route < UMICOM_TRAP_ROUTE_MACHINE_ECALL || route > UMICOM_TRAP_ROUTE_PROBE_STORE) {
        return UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT;
    }
    /* Lower-privilege traps have their own entry contracts. Treating one as a
     * machine continuation would let MRET interpret the wrong privilege state. */
    if ((frame->mstatus & UMICOM_TRAP_MPP_MASK) != UMICOM_TRAP_MPP_MASK) {
        return UMICOM_TRAP_INTEGRITY_WRONG_PRIVILEGE;
    }
    /* Hardware clears MIE on entry. Floating-point/vector state is not saved
     * by this integer-only frame; a caller enabling it needs a different ABI. */
    if ((frame->mstatus & (UMICOM_RISCV_MSTATUS_MIE | UMICOM_TRAP_UNSAVED_STATE)) != 0U) {
        return UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    }
    const UmicomBoolean probe = route == UMICOM_TRAP_ROUTE_PROBE_LOAD ||
        route == UMICOM_TRAP_ROUTE_PROBE_STORE ? UMICOM_TRUE : UMICOM_FALSE;
    /* Ordinary machine returns do not guess an effective-privilege state lost
     * when hardware overwrites MPP. Only an armed probe has a known recovery. */
    if (((frame->mstatus & UMICOM_TRAP_MPRV) != 0U) != (probe != UMICOM_FALSE)) {
        return UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS;
    }
    const UmicomU64 expectedCause = route == UMICOM_TRAP_ROUTE_MACHINE_ECALL ? 11U :
        route == UMICOM_TRAP_ROUTE_MACHINE_TIMER ? (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U) :
        route == UMICOM_TRAP_ROUTE_PROBE_LOAD ? 13U : 15U;
    if (frame->mcause != expectedCause) {
        return UMICOM_TRAP_INTEGRITY_WRONG_CAUSE;
    }
    /* A timer resumes at the interrupted PC. ECALL and both controlled memory
     * probes are four-byte instructions; their following instruction must fit. */
    const UmicomU64 extent = route == UMICOM_TRAP_ROUTE_MACHINE_TIMER ? 2U : 6U;
    if (UmicomTrapPcContains(textBegin, textEnd, frame->mepc, extent) == UMICOM_FALSE) {
        return UMICOM_TRAP_INTEGRITY_INVALID_PC;
    }
    return UMICOM_TRAP_INTEGRITY_OK;
}

UmicomKernelTrapIntegrityStatus UmicomKernelTrapReturnCheck(
    const UmicomRiscvTrapFrame *before, const UmicomRiscvTrapFrame *after,
    UmicomKernelTrapRoute route, UmicomAddress textBegin, UmicomAddress textEnd)
{
    if (after == (const UmicomRiscvTrapFrame *)0) {
        return UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT;
    }
    const UmicomKernelTrapIntegrityStatus admission =
        UmicomKernelTrapFrameCheck(before, route, textBegin, textEnd);
    if (admission != UMICOM_TRAP_INTEGRITY_OK) {
        return admission;
    }
    /* Character access can compare an object's representation without aliasing
     * one struct member as an array of a different type. All first 248 bytes
     * are saved integer registers, including the interrupted stack pointer. */
    const UmicomU8 *const left = (const UmicomU8 *)before;
    const UmicomU8 *const right = (const UmicomU8 *)after;
    for (UmicomSize index = 0U; index < UMICOM_TRAP_INTEGRITY_PC; ++index) {
        if (left[index] != right[index]) {
            return UMICOM_TRAP_INTEGRITY_CHANGED_REGISTERS;
        }
    }
    /* Cause and trap value are evidence, not fields a handler may rewrite to
     * disguise the exception which actually arrived. */
    if (before->mcause != after->mcause || before->mtval != after->mtval ||
        before->reserved != after->reserved) {
        return UMICOM_TRAP_INTEGRITY_CHANGED_DIAGNOSTICS;
    }
    const UmicomU64 expectedPc = before->mepc +
        (route == UMICOM_TRAP_ROUTE_MACHINE_TIMER ? 0U : 4U);
    if (after->mepc != expectedPc) {
        return UMICOM_TRAP_INTEGRITY_BAD_RETURN_PC;
    }
    /* The established one-shot timer policy clears MPIE. A controlled MPRV
     * fault clears MPRV before restoring the machine registers. No other bit
     * can be changed by these routes. ECALL preserves the saved status word. */
    const UmicomU64 clearMask = route == UMICOM_TRAP_ROUTE_MACHINE_TIMER ?
        UMICOM_RISCV_MSTATUS_MPIE : route == UMICOM_TRAP_ROUTE_MACHINE_ECALL ? 0U : UMICOM_TRAP_MPRV;
    if (after->mstatus != (before->mstatus & ~clearMask)) {
        return UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS;
    }
    return UMICOM_TRAP_INTEGRITY_OK;
}

const char *UmicomKernelTrapIntegrityStatusName(UmicomKernelTrapIntegrityStatus status)
{
    switch (status) {
    case UMICOM_TRAP_INTEGRITY_OK: return "ok";
    case UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT: return "invalid-argument";
    case UMICOM_TRAP_INTEGRITY_WRONG_PRIVILEGE: return "wrong-privilege";
    case UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS: return "unsafe-status";
    case UMICOM_TRAP_INTEGRITY_INVALID_PC: return "invalid-pc";
    case UMICOM_TRAP_INTEGRITY_WRONG_CAUSE: return "wrong-cause";
    case UMICOM_TRAP_INTEGRITY_CHANGED_REGISTERS: return "changed-registers";
    case UMICOM_TRAP_INTEGRITY_CHANGED_DIAGNOSTICS: return "changed-diagnostics";
    case UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS: return "bad-return-status";
    case UMICOM_TRAP_INTEGRITY_BAD_RETURN_PC: return "bad-return-pc";
    default: return "unknown-integrity-status";
    }
}
