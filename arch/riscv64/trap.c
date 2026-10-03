/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: arch/riscv64/trap.c
 *
 * PURPOSE:
 *   Decode machine-mode traps after trap.S has saved the interrupted
 *   integer-register/CSR state into UmicomRiscvTrapFrame.
 *
 * EDUCATIONAL OVERVIEW:
 *   Assembly owns the mechanism of entering/leaving a trap safely.
 *
 *   This C23 file owns the policy:
 *
 *     - a deliberate M-mode ECALL is recognised as the synchronous exception
 *       teaching case and is allowed to continue after ECALL;
 *
 *     - a machine-timer interrupt is acknowledged by moving mtimecmp away,
 *       recorded, and then returned from;
 *
 *     - any other trap is treated as unexpected and terminates the QEMU test
 *       with explicit diagnostic evidence.
 *
 *   The split is intentional: architecture entry/return rules remain in the
 *   minimum necessary Assembly, while cause decoding and test policy remain
 *   readable C23.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import the small early console used for unexpected-trap diagnostics. */
#include "umicom/kernel/console.h"

/* Import QEMU platform timer and test-finisher operations. */
#include "umicom/kernel/platform.h"

/* Import the machine trap frame, cause constants and Assembly helper contracts. */
#include "umicom/kernel/riscv64/trap.h"

/* Keep the exception counter in zero-initialized BSS.
 *
 * volatile is used because the trap handler changes this value outside the
 * ordinary instruction flow of the code that later reads it.
 *
 * This does NOT make volatile a general inter-thread synchronization primitive.
 * The current configuration has one active hart and reads the counter only after the controlled trap
 * has returned, so stronger atomics are not required for this milestone. */
static volatile UmicomU64 gExceptionCount;

/* Count completed machine-timer interrupt handlers using the same single-hart
 * communication rule described above. */
static volatile UmicomU64 gTimerInterruptCount;

/* Record only the low mcause code (without the interrupt flag) of the most
 * recently handled trap. */
static volatile UmicomU64 gLastCauseCode;

/* Record the mepc captured for the most recently handled trap. */
static volatile UmicomU64 gLastMepc;

/* Record the mtval captured for the most recently handled trap. */
static volatile UmicomU64 gLastMtval;

/* Record 1 for interrupt, 0 for synchronous exception. */
static volatile UmicomU64 gLastWasInterrupt;

/* Emit one diagnostic "name=0x..." line during an unexpected trap.
 *
 * Keeping the helper local avoids publishing a trap-test-only reporting API. */
static void WriteUnexpectedHex(const char *name, UmicomU64 value)
{
    /* Emit the field name first. */
    UmicomKernelConsoleWrite(name);

    /* Use '=' so the serial record remains easy for humans/tools to parse. */
    UmicomKernelConsoleWrite("=");

    /* Render the complete RV64 value in hexadecimal. */
    UmicomKernelConsoleWriteHex64(value);

    /* Terminate this diagnostic record. */
    UmicomKernelConsoleWriteLine("");
}

/* Terminate the controlled QEMU experiment after an unexpected trap.
 *
 * This function does not return because continuing from an unknown exception
 * could repeatedly fault, corrupt state, or hide a real kernel defect. */
static void UnexpectedTrap(const UmicomRiscvTrapFrame *frame)
{
    /* Mark the failure clearly so CTest's FAIL_REGULAR_EXPRESSION sees it. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");

    /* Explain the category without pretending we handled it. */
    UmicomKernelConsoleWriteLine("reason=unexpected-trap");

    /* Preserve the raw architectural values needed to understand the failure. */
    WriteUnexpectedHex("mcause", frame->mcause);
    WriteUnexpectedHex("mepc", frame->mepc);
    WriteUnexpectedHex("mtval", frame->mtval);

    /* Ask QEMU's test finisher for a nonzero guest result.
     *
     * Failure code 2 is a small local diagnostic code meaning unexpected trap. */
    UmicomPlatformFinishFailure((UmicomU32)2U);

    /* The QEMU finisher should stop the machine.  Keep an explicit halt if a
     * future/non-QEMU platform ignores that test device. */
    UmicomPlatformHalt();
}

void UmicomRiscvTrapDispatch(UmicomRiscvTrapFrame *frame)
{
    /* Reject an impossible null frame before dereferencing it.
     *
     * trap.S always passes sp, but this guard documents the C contract and
     * prevents a future direct caller from silently reading address zero. */
    if (frame == (UmicomRiscvTrapFrame *)0) {

        /* State the failure before stopping the virtual machine. */
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWriteLine("reason=null-trap-frame");

        /* Use a different bounded code so a debugger can distinguish this path. */
        UmicomPlatformFinishFailure((UmicomU32)3U);

        /* Never return into an unknown trap context. */
        UmicomPlatformHalt();
    }

    /* The top bit of RV64 mcause is 1 for interrupt and 0 for exception. */
    const UmicomU64 isInterrupt =
        (frame->mcause & UMICOM_RISCV_MCAUSE_INTERRUPT_BIT) != 0U
            ? (UmicomU64)1U
            : (UmicomU64)0U;

    /* Remove the interrupt flag so the remaining integer is the architectural
     * exception/interrupt cause code. */
    const UmicomU64 causeCode =
        frame->mcause & ~UMICOM_RISCV_MCAUSE_INTERRUPT_BIT;

    /* Publish a minimal "last trap" snapshot before cause-specific handling.
     *
     * Even if a later check fails, the serial/debugger state describes which
     * architectural event reached the dispatcher. */
    gLastCauseCode = causeCode;
    gLastMepc = frame->mepc;
    gLastMtval = frame->mtval;
    gLastWasInterrupt = isInterrupt;

    /* Handle the controlled synchronous exception. */
    if (
        isInterrupt == 0U &&
        causeCode == UMICOM_RISCV_EXCEPTION_ECALL_M_MODE
    ) {
        /* Count the successfully recognised exception. */
        ++gExceptionCount;

        /* Advance beyond the deliberate ECALL instruction.
         *
         * Machine-mode ECALL is a fixed 32-bit SYSTEM instruction.  Returning
         * to the same mepc would execute ECALL again and trap forever. */
        frame->mepc += (UmicomU64)4U;

        /* Return to trap.S, which restores registers/CSRs and executes MRET. */
        return;
    }

    /* Handle the controlled asynchronous interrupt. */
    if (
        isInterrupt != 0U &&
        causeCode == UMICOM_RISCV_INTERRUPT_MACHINE_TIMER
    ) {
        /* Read the executing hardware-thread ID so the matching compare
         * register can be acknowledged/disabled. */
        const UmicomU64 hartId = UmicomRiscvReadHartId();

        /* Move mtimecmp to the maximum 64-bit value.
         *
         * A machine timer remains pending while mtime >= mtimecmp.  Moving the
         * deadline far into the future removes the current pending condition. */
        UmicomPlatformTimerDisable(hartId);

        /* Disable MTIE immediately so this one-shot controlled trap test cannot retrigger
         * before ordinary code regains control. */
        UmicomRiscvMachineTimerInterruptDisable();

        /* Prevent MRET from re-enabling global M-mode interrupts.
         *
         * Hardware copied the pre-trap MIE=1 state into mstatus.MPIE when the
         * interrupt arrived.  trap.S later restores frame->mstatus before MRET.
         * Clearing MPIE in the saved frame makes MRET restore MIE=0, returning
         * to its deliberately interrupts-disabled baseline after this one
         * controlled asynchronous event. */
        frame->mstatus &= ~UMICOM_RISCV_MSTATUS_MPIE;

        /* Count the completed timer-interrupt handling path. */
        ++gTimerInterruptCount;

        /* Return to trap.S; the saved mepc already points at the interrupted
         * instruction and therefore needs no synchronous-exception adjustment. */
        return;
    }

    /* Anything outside the two deliberately supported cases is a real
     * unexpected kernel trap and must not be hidden. */
    UnexpectedTrap(frame);
}

void UmicomRiscvTrapSnapshotRead(UmicomRiscvTrapSnapshot *outSnapshot)
{
    /* A caller must provide storage because the kernel has no allocator and we
     * avoid returning a large structure through ABI-specific hidden mechanics. */
    if (outSnapshot == (UmicomRiscvTrapSnapshot *)0) {

        /* A null request simply has nowhere to publish a snapshot. */
        return;
    }

    /* Copy each volatile live field into ordinary caller-owned memory.
     *
     * The current diagnostic path calls this only after its controlled event has completed and the
     * relevant interrupt source is disabled, so the copy is stable on the one
     * active hart. */
    outSnapshot->exceptionCount = gExceptionCount;
    outSnapshot->timerInterruptCount = gTimerInterruptCount;
    outSnapshot->lastCauseCode = gLastCauseCode;
    outSnapshot->lastMepc = gLastMepc;
    outSnapshot->lastMtval = gLastMtval;
    outSnapshot->lastWasInterrupt = gLastWasInterrupt;
}
