/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_monitor.c
 *
 * PURPOSE:
 *   Handle a deliberately small set of user environment calls and return their
 *   results without allowing user pointers to become machine-mode authority.
 *
 * EDUCATIONAL OVERVIEW:
 *   This monitor currently runs in M-mode. It is not a supervisor system-call
 *   server, POSIX layer, or scheduler. The register ABI is nevertheless real:
 *   a user ECALL traps, C validates its arguments, and Assembly resumes the
 *   interrupted user registers after the instruction.
 *
 *   Expected user faults stop only this invocation. An unknown call returns
 *   a defined refusal so a program can handle an unsupported request itself.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifdef UMICOM_KERNEL_BLOCKING_IPC
/* A waiting syscall returns to the scheduler, not a busy loop in user code. */
#include "umicom/kernel/user_ipc.h"
#endif
#include "umicom/kernel/address.h"
#include "umicom/kernel/riscv64/user_execution.h"
#ifdef UMICOM_KERNEL_USER_SLICES
/* A bound scheduling invocation can pause at a timer without ending the program. */
#include "umicom/kernel/riscv64/user_slice.h"
#endif

/* Keep the original minimal monitor usable by its independent native tests.
 * The Kernel build enables this branch only when the message service is linked. */
#ifdef UMICOM_KERNEL_MESSAGE_CHANNELS
#include "umicom/kernel/message_service.h"
#endif

/* COPY is deliberately small. An immutable page-table view is required while
 * validating and copying, otherwise a second hart could change the mapping
 * between these operations. This monitor does not permit that concurrency. */
static UmicomU64 UmicomKernelUserCopy(
    const UmicomKernelUserMemory *memory,
    UmicomAddress destination,
    UmicomAddress source,
    UmicomSize bytes
)
{
    /* Refuse excessive work before a malicious address can be followed. */
    if (bytes > UMICOM_USER_COPY_LIMIT) {
        return UMICOM_USER_RESULT_TOO_LARGE;
    }
    /* Preflight both spans before reading or writing even their first byte. */
    UmicomU64 status = UmicomKernelUserMemoryCheck(
        memory, source, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    if (status != UMICOM_USER_RESULT_OK) {
        return status;
    }
    status = UmicomKernelUserMemoryCheck(
        memory, destination, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE);
    if (status != UMICOM_USER_RESULT_OK) {
        return status;
    }
    /* Copy through Kernel storage. Overlapping user spans then behave as a
     * read followed by a write, rather than corrupting unread source bytes. */
    UmicomU8 scratch[UMICOM_USER_COPY_LIMIT];
    status = UmicomKernelUserMemoryRead(memory, source, scratch, bytes);
    if (status != UMICOM_USER_RESULT_OK) {
        return status;
    }
    return UmicomKernelUserMemoryWrite(memory, destination, scratch, bytes);
}

UmicomU64 UmicomKernelUserTrapDispatch(
    UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame
)
{
    /* Both pointers are supplied by the machine entry, never by a user register.
     * A missing pointer is an internal contract failure and cannot be resumed. */
    if (session == (UmicomKernelUserSession *)0 || frame == (UmicomRiscvTrapFrame *)0) {
        return 0U;
    }

    /* Keep actual hardware observations before deciding whether to resume. */
    session->trapCause = frame->mcause;
    session->trapPc = frame->mepc;
    session->trapValue = frame->mtval;
    session->trapStatus = frame->mstatus;
    session->trapStack = (UmicomAddress)frame->x2_sp;

    /* MPP=0 proves a user-origin trap. A machine fault is not an expected user
     * protection event and must never be counted as a successful boundary test. */
    if (((frame->mstatus >> 11U) & 3U) != 0U) {
        session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
        return 0U;
    }
#ifdef UMICOM_KERNEL_USER_SLICES
    /* Preserve the original deadline semantics for every unbound session.
     * A due slice returns to Kernel; an early MTIP observation resumes the
     * same frame. Neither case advances an interrupted instruction. */
    const UmicomU64 sliceDecision = UmicomKernelUserSliceOnTrap(session, frame);
    if (sliceDecision != 0U) {
        return sliceDecision == 2U ? 1U : 0U;
    }
#endif
    if (frame->mcause == (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U)) {
        /* Even a user program that never makes another call can be stopped. */
        session->stopReason = UMICOM_USER_STOP_DEADLINE;
        return 0U;
    }
    if (frame->mcause != 8U) {
        /* The report preserves the precise cause for the machine owner. */
        session->stopReason = UMICOM_USER_STOP_FAULT;
        return 0U;
    }

    /* A caller-controlled stack is intentionally NOT dereferenced here. ECALL
     * arguments are registers; EXIT can safely terminate even a broken stack. */
    ++session->callCount;
    if (session->callCount > UMICOM_USER_CALL_LIMIT) {
        session->stopReason = UMICOM_USER_STOP_CALL_BUDGET;
        return 0U;
    }
    if (UmicomKernelUserMemoryCheck(
            &session->memory, (UmicomAddress)frame->mepc, 4U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != UMICOM_USER_RESULT_OK) {
        session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
        return 0U;
    }

    /* EXIT consumes its value but does not select a machine continuation PC. */
    if (frame->x17_a7 == UMICOM_USER_CALL_EXIT) {
        session->exitValue = frame->x10_a0;
        session->stopReason = UMICOM_USER_STOP_EXIT;
        return 0U;
    }

    /* ECALL is always four bytes even when compressed instructions are enabled.
     * Check the new PC before performing a service with observable effects. */
    UmicomAddress nextPc = 0U;
    if (UmicomKernelAddressAddChecked((UmicomAddress)frame->mepc, 4U, &nextPc) == UMICOM_FALSE ||
        UmicomKernelUserMemoryCheck(&session->memory, nextPc, 2U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != UMICOM_USER_RESULT_OK) {
        session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
        return 0U;
    }

#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* The origin, executable-PC and call-budget checks above still apply.
     * The service advances the validated ECALL continuation exactly once. */
    if (UmicomKernelUserIpcRecognizes(frame->x17_a7) != UMICOM_FALSE) {
        return UmicomKernelUserIpcDispatch(session, frame, nextPc);
    }
#endif

    UmicomU64 result = UMICOM_USER_RESULT_UNKNOWN_CALL;
    if (frame->x17_a7 == UMICOM_USER_CALL_IDENTITY) {
        /* Ignore all supplied identity arguments; authority belongs to setup. */
        result = session->identity;
    } else if (frame->x17_a7 == UMICOM_USER_CALL_COPY) {
        result = UmicomKernelUserCopy(&session->memory,
            (UmicomAddress)frame->x10_a0, (UmicomAddress)frame->x11_a1,
            (UmicomSize)frame->x12_a2);
        if (result == UMICOM_USER_RESULT_OK) {
            session->copiedBytes += frame->x12_a2;
        } else {
            ++session->rejectedCalls;
        }
#ifdef UMICOM_KERNEL_MESSAGE_CHANNELS
    } else if (UmicomKernelMessageServiceRecognizes(frame->x17_a7) != UMICOM_FALSE) {
        /* Owner identity comes from this trusted session. All PC, privilege and
         * call-budget checks above still precede any message side effect. */
        result = (UmicomU64)UmicomKernelMessageServiceDispatch(session, frame);
        if (result != (UmicomU64)UMICOM_MESSAGE_OK) {
            ++session->rejectedCalls;
        }
#endif
    } else {
        ++session->rejectedCalls;
    }

    /* The result goes only into a0. All other saved integer registers remain
     * exactly as the user left them, including its stack and return address. */
    frame->x10_a0 = result;
    frame->mepc = (UmicomU64)nextPc;
    return 1U;
}
