/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/main.c
 *
 * PURPOSE:
 *   Preserve K1's original C23 boot evidence and extend it with K2's first
 *   machine-mode trap, synchronous-exception and timer-interrupt exercises.
 *
 * EDUCATIONAL OVERVIEW:
 *   K2 deliberately tests two different reasons a CPU can enter the same trap
 *   vector:
 *
 *     1. ECALL is synchronous: the currently executing instruction asks the
 *        architecture to raise an exception.
 *
 *     2. The machine timer is asynchronous: external timer state becomes due
 *        while ordinary instructions are executing/waiting.
 *
 *   Both arrive through mtvec, are saved by trap.S, decoded by trap.c, and
 *   return with MRET.  The kernel validates the evidence instead of assuming a
 *   trap worked merely because execution continued.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import static product/milestone identity strings. */
#include "umicom/kernel/build.h"

/* Import the freestanding early serial-formatting layer. */
#include "umicom/kernel/console.h"

/* Import the entry-point declaration shared with boot.S. */
#include "umicom/kernel/kernel.h"

/* Import platform timer and QEMU finish/halt operations. */
#include "umicom/kernel/platform.h"

/* Import RV64 trap installation, test triggers and snapshots. */
#include "umicom/kernel/riscv64/trap.h"

/* K2's QEMU timer runs at 10 MHz.
 *
 * 100,000 ticks therefore represents approximately 10 ms.  The test waits for
 * an actual interrupt rather than sleeping through a hosted OS API. */
#define UMI_K2_TIMER_DELTA_TICKS ((UmiU64)100000U)

/* Write one "key=value" text line without relying on printf/libc. */
static void WriteKeyValue(const char *key, const char *value)
{
    /* Emit the field name. */
    UmiKernelConsoleWrite(key);

    /* Separate name from value using a predictable machine-readable character. */
    UmiKernelConsoleWrite("=");

    /* Emit the value followed by the console's standard CR+LF ending. */
    UmiKernelConsoleWriteLine(value);
}

/* Write one decimal unsigned record. */
static void WriteUnsignedRecord(const char *key, UmiU64 value)
{
    /* Emit the field name. */
    UmiKernelConsoleWrite(key);

    /* Emit the key/value separator. */
    UmiKernelConsoleWrite("=");

    /* Convert the 64-bit integer with the freestanding decimal helper. */
    UmiKernelConsoleWriteUnsigned(value);

    /* Terminate the record. */
    UmiKernelConsoleWriteLine("");
}

/* Write one fixed-width hexadecimal record. */
static void WriteHexRecord(const char *key, UmiU64 value)
{
    /* Emit the field name. */
    UmiKernelConsoleWrite(key);

    /* Emit the key/value separator. */
    UmiKernelConsoleWrite("=");

    /* Render the complete 64-bit value with 0x prefix. */
    UmiKernelConsoleWriteHex64(value);

    /* Terminate the record. */
    UmiKernelConsoleWriteLine("");
}

/* Stop a failed K2 acceptance path with human-readable evidence.
 *
 * The function intentionally does not return to its caller. */
static void FailK2(UmiU32 code, const char *reason)
{
    /* Give CTest and humans an unambiguous failure marker. */
    UmiKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");

    /* Emit the reason field without hosted formatting. */
    WriteKeyValue("reason", reason);

    /* Ask QEMU to exit with a nonzero bounded test code. */
    UmiPlatformFinishFailure(code);

    /* Retain a final non-returning safety boundary if the QEMU device is absent. */
    UmiPlatformHalt();
}

/* Verify K2's deliberate M-mode ECALL exception. */
static void RunExceptionTest(void)
{
    /* Use caller-owned stack storage because there is no kernel allocator yet. */
    UmiRiscvTrapSnapshot snapshot;

    /* Announce the operation before deliberately entering the exception path. */
    UmiKernelConsoleWriteLine("exception-test=begin");

    /* Execute ECALL in M-mode.
     *
     * trap.c recognises cause 11, records it and advances mepc by four bytes.
     * If that handling is incorrect, control cannot return normally here. */
    UmiRiscvTriggerMachineEcall();

    /* Copy the trap handler's stable post-exception evidence. */
    UmiRiscvTrapSnapshotRead(&snapshot);

    /* Publish the important values before deciding whether the test passed. */
    WriteUnsignedRecord("trap.exception.count", snapshot.exceptionCount);
    WriteUnsignedRecord("trap.exception.cause", snapshot.lastCauseCode);
    WriteHexRecord("trap.exception.mepc", snapshot.lastMepc);
    WriteHexRecord("trap.exception.mtval", snapshot.lastMtval);

    /* Exactly one exception must have been recognised. */
    if (snapshot.exceptionCount != (UmiU64)1U) {
        FailK2((UmiU32)10U, "exception-count");
    }

    /* An ECALL is synchronous, so the interrupt flag must have been clear. */
    if (snapshot.lastWasInterrupt != (UmiU64)0U) {
        FailK2((UmiU32)11U, "exception-marked-as-interrupt");
    }

    /* M-mode ECALL is architectural cause code 11. */
    if (snapshot.lastCauseCode != UMI_RISCV_EXCEPTION_ECALL_M_MODE) {
        FailK2((UmiU32)12U, "wrong-ecall-cause");
    }

    /* Reaching this marker proves MRET resumed after the ECALL instruction. */
    UmiKernelConsoleWriteLine("exception-test=pass");
}

/* Verify one real QEMU machine-timer interrupt. */
static void RunTimerTest(UmiU64 hartId)
{
    /* Keep pre/post trap evidence in caller-owned stack storage. */
    UmiRiscvTrapSnapshot snapshot;

    /* State clearly that the asynchronous test is beginning. */
    UmiKernelConsoleWriteLine("timer-test=begin");

    /* Read QEMU's current 64-bit machine time. */
    const UmiU64 now = UmiPlatformTimerRead();

    /* Determine the greatest representable 64-bit unsigned value. */
    const UmiU64 maximum = ~(UmiU64)0U;

    /* Refuse integer wrap rather than programming a deadline in the past. */
    if (now > maximum - UMI_K2_TIMER_DELTA_TICKS) {
        FailK2((UmiU32)20U, "timer-deadline-overflow");
    }

    /* Add the deliberate ~10 ms interval after proving the addition is safe. */
    const UmiU64 deadline = now + UMI_K2_TIMER_DELTA_TICKS;

    /* Publish both values so learners can see the absolute timer model. */
    WriteUnsignedRecord("timer.now", now);
    WriteUnsignedRecord("timer.deadline", deadline);

    /* Program mtimecmp before enabling interrupts.
     *
     * This ordering prevents an interrupt source from becoming active while
     * its intended deadline is still uninitialised. */
    UmiPlatformTimerSetCompare(hartId, deadline);

    /* Enable mie.MTIE and then mstatus.MIE through the small Assembly helper. */
    UmiRiscvMachineTimerInterruptEnable();

    /* WFI is architecturally allowed to resume for reasons other than the exact
     * timer event we want.  Also, on a heavily paused host the timer could
     * become due immediately after interrupt enable but before the first WFI.
     *
     * Therefore check the handler-owned counter BEFORE each WFI and again after
     * any returned interrupt, rather than assuming the first WFI owns the event. */
    for (;;) {

        /* Read current trap state before sleeping.
         *
         * This closes the race where the timer interrupt already ran between
         * UmiRiscvMachineTimerInterruptEnable() and this loop. */
        UmiRiscvTrapSnapshotRead(&snapshot);

        /* If the timer handler has already completed, do not execute a WFI with
         * MTIE disabled and accidentally wait forever. */
        if (snapshot.timerInterruptCount != (UmiU64)0U) {
            break;
        }

        /* Otherwise wait efficiently until the hart observes an interrupt/event. */
        UmiRiscvWaitForInterrupt();
    }

    /* Return the kernel to a deterministic interrupts-disabled state.
     *
     * The handler already disabled MTIE/compare; this also ensures the global
     * MIE bit is clear after MRET restored the pre-trap status. */
    UmiRiscvMachineTimerInterruptDisable();

    /* Read one final stable copy after interrupt delivery is disabled. */
    UmiRiscvTrapSnapshotRead(&snapshot);

    /* Publish the timer evidence. */
    WriteUnsignedRecord(
        "trap.timer.count",
        snapshot.timerInterruptCount
    );
    WriteUnsignedRecord(
        "trap.timer.cause",
        snapshot.lastCauseCode
    );
    WriteHexRecord(
        "trap.timer.mepc",
        snapshot.lastMepc
    );

    /* K2 expects exactly one one-shot timer interrupt. */
    if (snapshot.timerInterruptCount != (UmiU64)1U) {
        FailK2((UmiU32)21U, "timer-count");
    }

    /* The last event must have arrived through the interrupt path. */
    if (snapshot.lastWasInterrupt != (UmiU64)1U) {
        FailK2((UmiU32)22U, "timer-marked-as-exception");
    }

    /* Architectural machine-timer interrupt code is 7. */
    if (snapshot.lastCauseCode != UMI_RISCV_INTERRUPT_MACHINE_TIMER) {
        FailK2((UmiU32)23U, "wrong-timer-cause");
    }

    /* Confirm that the timer did not corrupt the earlier ECALL evidence count. */
    if (snapshot.exceptionCount != (UmiU64)1U) {
        FailK2((UmiU32)24U, "exception-count-changed");
    }

    /* Announce successful asynchronous trap delivery/return. */
    UmiKernelConsoleWriteLine("timer-test=pass");
}

void UmiKernelMain(UmiU64 hartId, UmiAddress deviceTreeAddress)
{
    /* Initialize the polling UART before any boot/trap evidence is printed. */
    UmiKernelConsoleInitialize();

    /* Preserve the original K1 begin marker as a regression contract. */
    UmiKernelConsoleWriteLine("UMICOM_KERNEL_BEGIN");

    /* Identify the running image. */
    WriteKeyValue("name", UMICOM_KERNEL_NAME);
    WriteKeyValue("version", UMICOM_KERNEL_VERSION);
    WriteKeyValue("milestone", UMICOM_KERNEL_MILESTONE);
    WriteKeyValue("arch", UMICOM_KERNEL_ARCHITECTURE);
    WriteKeyValue("machine", UMICOM_KERNEL_MACHINE);
    WriteKeyValue("build", UMICOM_KERNEL_BUILD_ID);

    /* Publish the boot hart selected by boot.S. */
    WriteUnsignedRecord("hart", hartId);

    /* Publish QEMU's DTB address without parsing the tree yet. */
    WriteHexRecord("dtb", (UmiU64)deviceTreeAddress);

    /* Preserve K1's exact boot-success field so the K1 CTest remains useful. */
    UmiKernelConsoleWriteLine("state=booted");

    /* Install the direct machine-mode trap vector while interrupts are disabled. */
    UmiRiscvTrapInstall();

    /* Publish the exact vector address placed into mtvec. */
    WriteHexRecord(
        "trap-vector",
        (UmiU64)UmiRiscvTrapVectorAddress()
    );

    /* Prove synchronous exception entry, decoding, mepc adjustment and MRET. */
    RunExceptionTest();

    /* Prove asynchronous machine-timer entry, acknowledgement and MRET. */
    RunTimerTest(hartId);

    /* State the stronger K2 capability only after both trap classes passed. */
    UmiKernelConsoleWriteLine("state=trap-timer-ready");

    /* This marker is the K2 CTest acceptance expression. */
    UmiKernelConsoleWriteLine("K2_TRAP_TIMER_PASS");

    /* Preserve the general end-of-evidence marker established by K1. */
    UmiKernelConsoleWriteLine("UMICOM_KERNEL_END");

    /* End the QEMU run as a successful real-emulator test. */
    UmiPlatformFinishSuccess();

    /* Keep a final safety halt if the emulator does not honour its test device. */
    UmiPlatformHalt();
}
