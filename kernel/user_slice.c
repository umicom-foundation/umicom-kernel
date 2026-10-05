/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_slice.c
 *
 * PURPOSE:
 *   Classify timer stops for exactly one bound user session and validate the
 *   integer continuation before it is restored by privileged Assembly.
 *
 * EDUCATIONAL OVERVIEW:
 *   An interrupt captures the next instruction to execute. Unlike ECALL, a
 *   timer does not ask us to skip an instruction: changing mepc here would
 *   silently lose work. We retain that PC and every integer register, then
 *   restore the machine caller so its dispatcher can select another task.
 *
 *   MTIP may lag a compare-register write. A timer observed before this slice's
 *   deadline is therefore resumed, not charged as a completed quantum. A small
 *   retry bound prevents a broken timer model from becoming an endless storm.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/user_slice.h"
#include "umicom/kernel/platform.h"

/* One active session matches the single-hart private-vector execution model. */
static struct {
    UmicomKernelUserSession *session;
    UmicomU64 deadline;
    UmicomSize earlyTimers;
    UmicomBoolean expired;
} umicomUserSlice;

UmicomBoolean UmicomKernelUserSliceBegin(UmicomKernelUserSession *session, UmicomU64 deadline)
{
    /* Never overwrite a different invocation's binding. */
    if (session == (UmicomKernelUserSession *)0 || umicomUserSlice.session != (UmicomKernelUserSession *)0) {
        return UMICOM_FALSE;
    }
    umicomUserSlice.deadline = deadline;
    umicomUserSlice.earlyTimers = 0U;
    umicomUserSlice.expired = UMICOM_FALSE;
    umicomUserSlice.session = session; /* Publish only after its observations are reset. */
    return UMICOM_TRUE;
}

UmicomBoolean UmicomKernelUserSliceEnd(UmicomKernelUserSession *session, UmicomBoolean *outExpired)
{
    /* A mismatched caller must not detach someone else's still-live session. */
    if (session == (UmicomKernelUserSession *)0 || session != umicomUserSlice.session ||
        outExpired == (UmicomBoolean *)0) {
        return UMICOM_FALSE;
    }
    *outExpired = umicomUserSlice.expired;
    umicomUserSlice.session = (UmicomKernelUserSession *)0;
    return UMICOM_TRUE;
}

UmicomU64 UmicomKernelUserSliceOnTrap(UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    /* Other calls, faults and run-once sessions keep their original semantics. */
    if (session == (UmicomKernelUserSession *)0 || session != umicomUserSlice.session ||
        frame == (UmicomRiscvTrapFrame *)0 ||
        frame->mcause != (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U)) {
        return 0U;
    }
    if (((frame->mstatus >> 11U) & 3U) != 0U) {
        session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
        return 1U; /* Never turn a machine-origin error into a user pre-emption. */
    }
    if (UmicomPlatformTimerRead() < umicomUserSlice.deadline) {
        if (++umicomUserSlice.earlyTimers > 16U) {
            session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
            return 1U;
        }
        return 2U; /* Keep the instruction and all saved registers unchanged. */
    }
    umicomUserSlice.expired = UMICOM_TRUE;
    session->stopReason = UMICOM_USER_STOP_NONE; /* Paused is not exited or faulted. */
    return 1U;
}

UmicomBoolean UmicomKernelUserFrameValid(const UmicomKernelUserMemory *memory,
    const UmicomRiscvTrapFrame *frame)
{
    /* Only integer U-mode continuations belong to this protocol. Do not copy
     * an unchecked privilege word into mstatus even though Assembly masks it. */
    const UmicomU64 forbidden = ((UmicomU64)3U << 11U) | ((UmicomU64)1U << 17U) |
        ((UmicomU64)3U << 13U) | ((UmicomU64)3U << 9U) | ((UmicomU64)3U << 18U) | 0x108U;
    if (frame == (const UmicomRiscvTrapFrame *)0 || frame->reserved != 0U ||
        (frame->mstatus & forbidden) != 0U || ((frame->mstatus >> 32U) & 3U) != 2U ||
        (frame->mepc & 1U) != 0U) {
        return UMICOM_FALSE;
    }
    /* A saved sp is just a user register. It can be invalid: the private trap
     * entry still uses Kernel storage, so EXIT need not dereference that sp.
     * The PC, however, must name an executable user halfword before resumption. */
    return UmicomKernelUserMemoryCheck(memory, (UmicomAddress)frame->mepc, 2U,
        UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) == UMICOM_USER_RESULT_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
