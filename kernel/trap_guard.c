/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/trap_guard.c
 *
 * PURPOSE:
 *   Surround the existing machine trap policy with landing-stack and return
 *   checks. Only explicitly armed validation instructions may recover a fault.
 *
 * EDUCATIONAL OVERVIEW:
 *   The global vector uses one owned stack and rejects nesting. This permits a
 *   complete before/after comparison without changing the old cause dispatcher.
 *   A failure in this wrapper goes to a separate emergency vector; it must not
 *   try to restart a partially completed Kernel operation.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/trap_integrity.h"

/* Only one hart enters this non-nesting vector. Volatile makes the diagnostic
 * observations visible across traps; it does not turn them into SMP locks. */
static volatile UmicomKernelTrapIntegritySnapshot umicomTrapObservations;
static volatile UmicomBoolean umicomTrapActive;
static volatile UmicomBoolean umicomTrapProbeArmed;
static volatile UmicomBoolean umicomTrapProbeCompleted;
static UmicomKernelTrapRoute umicomTrapProbeRoute;

static UmicomBoolean UmicomTrapMarginsIntact(void)
{
    /* These symbols denote aligned Kernel-owned RAM, not caller-selected data. */
    const volatile UmicomU64 *const low =
        (const volatile UmicomU64 *)UmicomRiscvMachineTrapStackLow;
    const volatile UmicomU64 *const high =
        (const volatile UmicomU64 *)UmicomRiscvMachineTrapStackTop;
    const UmicomU64 canary = (UmicomU64)UMICOM_TRAP_INTEGRITY_CANARY;
    return low[0] == canary && low[1] == ~canary &&
        high[0] == canary && high[1] == ~canary ? UMICOM_TRUE : UMICOM_FALSE;
}

static _Noreturn void UmicomTrapIntegrityStop(const UmicomRiscvTrapFrame *frame)
{
    /* The emergency platform path is bounded and bypasses the ordinary UART
     * polling loop. A damaged control frame is never silently repaired. */
    UmicomPlatformTrapEmergencyFinish(frame->mcause, frame->mepc,
        frame->mtval, frame->mstatus, UMICOM_FALSE);
}

UmicomBoolean UmicomKernelTrapProbeArm(UmicomKernelTrapRoute route)
{
    /* These fixed probes are Kernel tests, not a fault-resume service. A caller
     * cannot supply a trap PC, continuation address or privilege word. */
    if (umicomTrapActive != UMICOM_FALSE || umicomTrapProbeArmed != UMICOM_FALSE ||
        (route != UMICOM_TRAP_ROUTE_PROBE_LOAD && route != UMICOM_TRAP_ROUTE_PROBE_STORE)) {
        return UMICOM_FALSE;
    }
    umicomTrapProbeRoute = route;
    umicomTrapProbeCompleted = UMICOM_FALSE;
    umicomTrapProbeArmed = UMICOM_TRUE;
    return UMICOM_TRUE;
}

UmicomBoolean UmicomKernelTrapProbeFinished(void)
{
    /* A fault that never occurred cannot be counted as successful protection. */
    return umicomTrapProbeCompleted != UMICOM_FALSE && umicomTrapProbeArmed == UMICOM_FALSE
        ? UMICOM_TRUE : UMICOM_FALSE;
}

void UmicomRiscvMachineTrapDispatchGuarded(UmicomRiscvTrapFrame *frame)
{
    const UmicomAddress expectedFrame =
        (UmicomAddress)UmicomRiscvMachineTrapStackTop - sizeof(UmicomRiscvTrapFrame);
    if ((UmicomAddress)frame != expectedFrame || umicomTrapActive != UMICOM_FALSE ||
        UmicomTrapMarginsIntact() == UMICOM_FALSE) {
        /* Do not dereference an invalid frame even to report its cause. */
        UmicomPlatformTrapEmergencyFinish(0U, 0U, (UmicomU64)(UmicomAddress)frame, 0U, UMICOM_FALSE);
    }
    umicomTrapActive = UMICOM_TRUE;

    /* Volatile byte copying keeps the freestanding build independent of a
     * compiler-emitted memcpy call and preserves every saved register byte. */
    /* One non-nesting hart owns this comparison record. Static zero-filled
     * storage avoids an implicit hosted memset for a large local initializer.
     * Every byte is replaced below before it can affect a return decision. */
    static UmicomRiscvTrapFrame before;
    volatile UmicomU8 *const copy = (volatile UmicomU8 *)&before;
    const UmicomU8 *const original = (const UmicomU8 *)frame;
    for (UmicomSize index = 0U; index < sizeof(before); ++index) {
        copy[index] = original[index];
    }
    UmicomKernelTrapRoute route = UMICOM_TRAP_ROUTE_MACHINE_ECALL;
    UmicomBoolean probe = UMICOM_FALSE;
    if (frame->mcause == (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U)) {
        route = UMICOM_TRAP_ROUTE_MACHINE_TIMER;
    } else if (frame->mcause == 13U || frame->mcause == 15U) {
        const UmicomAddress pc = umicomTrapProbeRoute == UMICOM_TRAP_ROUTE_PROBE_LOAD ?
            (UmicomAddress)UmicomRiscvTrapLoadInstruction : (UmicomAddress)UmicomRiscvTrapStoreInstruction;
        const UmicomAddress resume = umicomTrapProbeRoute == UMICOM_TRAP_ROUTE_PROBE_LOAD ?
            (UmicomAddress)UmicomRiscvTrapLoadResume : (UmicomAddress)UmicomRiscvTrapStoreResume;
        if (umicomTrapProbeArmed == UMICOM_FALSE || frame->mepc != pc || resume != pc + 4U) {
            UmicomTrapIntegrityStop(frame);
        }
        route = umicomTrapProbeRoute;
        probe = UMICOM_TRUE;
    }
    const UmicomAddress begin = (UmicomAddress)__umicom_trap_text_begin;
    const UmicomAddress end = (UmicomAddress)__umicom_trap_text_end;
    if (UmicomKernelTrapFrameCheck(frame, route, begin, end) != UMICOM_TRAP_INTEGRITY_OK) {
        UmicomTrapIntegrityStop(frame);
    }
    if (route == UMICOM_TRAP_ROUTE_MACHINE_ECALL) {
        /* Range checking precedes instruction inspection. ECALL can begin on
         * a two-byte boundary, so read bytes rather than an unaligned word. */
        const volatile UmicomU8 *const instruction = (const volatile UmicomU8 *)(UmicomAddress)frame->mepc;
        if (instruction[0] != 0x73U || instruction[1] != 0U || instruction[2] != 0U || instruction[3] != 0U) {
            UmicomTrapIntegrityStop(frame);
        }
    }
#ifdef UMICOM_TRAP_NESTED_VALIDATION
    /* Only the separate fault-test ELF contains this injection. A real machine
     * fault occurs while mtvec names the emergency path and this frame is live. */
    if (frame->mepc == (UmicomAddress)UmicomRiscvTrapNestedEcall) {
        UmicomRiscvTrapNestedFault();
        UmicomTrapIntegrityStop(frame); /* The illegal instruction cannot return. */
    }
#endif
    if (probe != UMICOM_FALSE) {
        /* The faulting instruction did not complete. Resume only its fixed
         * cleanup label, with physical machine loads restored for the epilogue. */
        frame->mepc += 4U;
        frame->mstatus &= ~(UmicomU64)0x20000U;
        umicomTrapProbeArmed = UMICOM_FALSE;
        umicomTrapProbeCompleted = UMICOM_TRUE;
        if (umicomTrapObservations.recoveredProbes == ~(UmicomU64)0U) {
            UmicomTrapIntegrityStop(frame);
        }
        ++umicomTrapObservations.recoveredProbes;
    } else {
        /* The original ECALL/timer policy and its counters remain in service. */
        UmicomRiscvTrapDispatch(frame);
    }
    if (UmicomKernelTrapReturnCheck(&before, frame, route, begin, end) != UMICOM_TRAP_INTEGRITY_OK ||
        UmicomTrapMarginsIntact() == UMICOM_FALSE || umicomTrapObservations.returned == ~(UmicomU64)0U) {
        UmicomTrapIntegrityStop(frame);
    }
    umicomTrapObservations.interruptedStack = (UmicomAddress)before.x2_sp;
    umicomTrapObservations.frameAddress = (UmicomAddress)frame;
    umicomTrapObservations.cause = before.mcause;
    ++umicomTrapObservations.returned;
    umicomTrapActive = UMICOM_FALSE;
}

void UmicomKernelTrapIntegritySnapshotRead(UmicomKernelTrapIntegritySnapshot *out)
{
    if (out == (UmicomKernelTrapIntegritySnapshot *)0) {
        return;
    }
    /* The caller has interrupts disabled and runs outside the trap stack. */
    out->returned = umicomTrapObservations.returned;
    out->recoveredProbes = umicomTrapObservations.recoveredProbes;
    out->interruptedStack = umicomTrapObservations.interruptedStack;
    out->frameAddress = umicomTrapObservations.frameAddress;
    out->cause = umicomTrapObservations.cause;
}

_Noreturn void UmicomKernelTrapEmergencyStop(UmicomU64 cause,
    UmicomU64 pc, UmicomU64 value, UmicomU64 status)
{
    UmicomBoolean expected = UMICOM_FALSE;
#ifdef UMICOM_TRAP_NESTED_VALIDATION
    /* A failed emulator launch, arbitrary crash or ordinary user fault cannot
     * satisfy this result. The marker requires our exact injected instruction,
     * machine origin, active outer frame and illegal-instruction cause. */
    if (umicomTrapActive != UMICOM_FALSE && cause == 2U &&
        pc == (UmicomAddress)UmicomRiscvTrapNestedInstruction &&
        (status & 0x1800U) == 0x1800U) {
        expected = UMICOM_TRUE;
    }
#endif
    UmicomPlatformTrapEmergencyFinish(cause, pc, value, status, expected);
}
