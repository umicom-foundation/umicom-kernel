/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/executable_validation.c
 *
 * PURPOSE:
 *   Run independently linked ELF bytes through the real loader and user monitor,
 *   then prove that process teardown restores the physical-memory baseline.
 *
 * EDUCATIONAL OVERVIEW:
 *   The Kernel cannot jump to a symbol in this program: it is embedded as data,
 *   not linked as Kernel code. Only its checked ELF entry tells the loader where
 *   execution begins. The same file is loaded twice into independent frames.
 *   Afterwards we reload it for protection, deadline and recovery cases.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
/* Stable BSS owners avoid hidden large stack allocations and never move while
 * their borrowed user-memory views point inside them. */
static UmicomKernelProcess umicomExecutableProcessA;
static UmicomKernelProcess umicomExecutableProcessB;

static void UmicomExecutableRequire(UmicomBoolean condition, const char *reason)
{
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("reason=");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(190U);
        UmicomPlatformHalt();
    }
}
static void UmicomExecutableDecimal(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}
static void UmicomExecutableHex(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteHex64(value);
    UmicomKernelConsoleWriteLine("");
}
static UmicomBoolean UmicomExecutableAccountingEqual(
    const UmicomKernelPhysicalMemorySnapshot *left,
    const UmicomKernelPhysicalMemorySnapshot *right)
{
    return left->allocatedFrames == right->allocatedFrames &&
        left->reservedFrames == right->reservedFrames && left->freeFrames == right->freeFrames
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static void UmicomExecutableCreate(UmicomKernelProcess *process,
    const UmicomU8 *bytes, UmicomSize count, UmicomU64 identity)
{
    UmicomKernelExecutableStatus inspected = UMICOM_EXECUTABLE_INVALID_ARGUMENT;
    const UmicomKernelProcessStatus loaded =
        UmicomKernelProcessCreate(process, bytes, count, identity, &inspected);
    UmicomKernelConsoleWrite("executable.inspect=");
    UmicomKernelConsoleWriteLine(UmicomKernelExecutableStatusName(inspected));
    UmicomKernelConsoleWrite("executable.load=");
    UmicomKernelConsoleWriteLine(UmicomKernelProcessStatusName(loaded));
    UmicomExecutableRequire(loaded == UMICOM_PROCESS_OK && process->state == UMICOM_PROCESS_READY
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-load");
}
static void UmicomExecutableRun(UmicomKernelProcess *process, const char *name,
    UmicomU64 argument, UmicomKernelProcessState expectedState, UmicomU64 expectedCause)
{
    UmicomKernelConsoleWrite("executable.case=");
    UmicomKernelConsoleWriteLine(name);
    UmicomExecutableRequire(UmicomKernelProcessRun(process, argument, 1000000U) == UMICOM_PROCESS_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-run");
    UmicomKernelConsoleWrite("executable.process-state=");
    UmicomKernelConsoleWriteLine(UmicomKernelProcessStateName(process->state));
    UmicomExecutableDecimal("executable.identity", process->identity);
    UmicomExecutableHex("executable.trap.cause", process->report.trapCause);
    UmicomExecutableDecimal("executable.previous-privilege", (process->report.trapStatus >> 11U) & 3U);
    UmicomExecutableRequire(process->state == expectedState &&
        process->report.trapCause == expectedCause &&
        ((process->report.trapStatus >> 11U) & 3U) == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-terminal-event");
    /* Both successful and negative cases first run IDENTITY and COPY. */
    UmicomExecutableRequire(process->report.copiedBytes == 16U && process->report.rejectedCalls == 0U
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-native-services");
    if (expectedState == UMICOM_PROCESS_EXITED) {
        UmicomExecutableDecimal("executable.exit-value", process->report.exitValue);
        UmicomExecutableRequire(process->report.exitValue == process->identity + argument + 33U &&
            process->report.callCount == 3U ? UMICOM_TRUE : UMICOM_FALSE, "executable-data-bss-or-exit");
    }
    /* A terminal image is retained for inspection, not implicitly restarted. */
    UmicomExecutableRequire(UmicomKernelProcessRun(process, argument, 1000000U) == UMICOM_PROCESS_BAD_STATE
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-terminal-reentry");
    UmicomKernelConsoleWriteLine("executable.machine-state=restored");
    UmicomKernelConsoleWriteLine("executable.case-result=pass");
}

void UmicomKernelExecutableLoadingValidate(void)
{
    UmicomKernelConsoleWriteLine("executable-loading-test=begin");
    const UmicomSize bytes = (UmicomSize)((UmicomAddress)UmicomEmbeddedExecutableEnd -
        (UmicomAddress)UmicomEmbeddedExecutableStart);
    UmicomExecutableDecimal("executable.file.bytes", bytes);
    UmicomKernelPhysicalMemorySnapshot baseline;
    UmicomExecutableRequire(UmicomKernelPhysicalMemorySnapshotRead(&baseline) == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-baseline");
    UmicomKernelExecutableStatus inspected = UMICOM_EXECUTABLE_OK;
    /* Truncation is refused before any page-table root or data page is allocated. */
    UmicomExecutableRequire(UmicomKernelProcessCreate(&umicomExecutableProcessA,
        UmicomEmbeddedExecutableStart, 63U, 501U, &inspected) == UMICOM_PROCESS_EXECUTABLE_REFUSED &&
        inspected == UMICOM_EXECUTABLE_TRUNCATED ? UMICOM_TRUE : UMICOM_FALSE, "executable-truncation");
    UmicomKernelPhysicalMemorySnapshot afterRefusal;
    UmicomExecutableRequire(UmicomKernelPhysicalMemorySnapshotRead(&afterRefusal) == UMICOM_KERNEL_MEMORY_OK &&
        UmicomExecutableAccountingEqual(&baseline, &afterRefusal) != UMICOM_FALSE
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-refusal-allocated-memory");
    UmicomKernelConsoleWriteLine("executable.invalid-input=refused-without-allocation");

    UmicomExecutableCreate(&umicomExecutableProcessA, UmicomEmbeddedExecutableStart, bytes, 501U);
    UmicomExecutableCreate(&umicomExecutableProcessB, UmicomEmbeddedExecutableStart, bytes, 502U);
    UmicomExecutableHex("executable.entry", umicomExecutableProcessA.entry);
    UmicomExecutableHex("executable.root.a", umicomExecutableProcessA.space.rootTablePhysicalAddress);
    UmicomExecutableHex("executable.root.b", umicomExecutableProcessB.space.rootTablePhysicalAddress);
    UmicomExecutableRequire(umicomExecutableProcessA.entry == umicomExecutableProcessB.entry &&
        umicomExecutableProcessA.space.rootTablePhysicalAddress != umicomExecutableProcessB.space.rootTablePhysicalAddress
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-independent-roots");
    /* Even executable text is privately copied in this eager-loading profile.
     * There is no accidental dependence on a shared writable backing page. */
    for (UmicomSize left = 0U; left < umicomExecutableProcessA.pageCount; ++left) {
        for (UmicomSize right = 0U; right < umicomExecutableProcessB.pageCount; ++right) {
            UmicomExecutableRequire(umicomExecutableProcessA.pages[left].physicalBase !=
                umicomExecutableProcessB.pages[right].physicalBase ? UMICOM_TRUE : UMICOM_FALSE,
                "executable-shared-backing-frame");
        }
    }
    UmicomExecutableRequire(UmicomKernelProcessCreate(&umicomExecutableProcessA,
        UmicomEmbeddedExecutableStart, bytes, 599U, &inspected) == UMICOM_PROCESS_BAD_STATE
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-live-owner-overwrite");
    UmicomExecutableRun(&umicomExecutableProcessA, "loaded-program-a", 7U, UMICOM_PROCESS_EXITED, 8U);
    UmicomExecutableRun(&umicomExecutableProcessB, "loaded-program-b", 19U, UMICOM_PROCESS_EXITED, 8U);
    UmicomKernelConsoleWriteLine("executable.independent-images=pass");
    UmicomExecutableRequire(UmicomKernelProcessDestroy(&umicomExecutableProcessA) == UMICOM_PROCESS_OK &&
        UmicomKernelProcessDestroy(&umicomExecutableProcessB) == UMICOM_PROCESS_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-destroy-pair");

    UmicomExecutableCreate(&umicomExecutableProcessA, UmicomEmbeddedExecutableStart, bytes, 503U);
    UmicomExecutableRun(&umicomExecutableProcessA, "loaded-read-only-refusal", 2U, UMICOM_PROCESS_FAULTED, 15U);
    UmicomExecutableRequire(UmicomKernelProcessDestroy(&umicomExecutableProcessA) == UMICOM_PROCESS_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-destroy-fault");
    UmicomExecutableCreate(&umicomExecutableProcessA, UmicomEmbeddedExecutableStart, bytes, 504U);
    UmicomExecutableRun(&umicomExecutableProcessA, "loaded-program-deadline", 3U, UMICOM_PROCESS_TIMED_OUT,
        UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U);
    UmicomExecutableRequire(UmicomKernelProcessDestroy(&umicomExecutableProcessA) == UMICOM_PROCESS_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-destroy-deadline");
    UmicomExecutableCreate(&umicomExecutableProcessA, UmicomEmbeddedExecutableStart, bytes, 505U);
    UmicomExecutableRun(&umicomExecutableProcessA, "reload-after-fault-and-deadline", 7U, UMICOM_PROCESS_EXITED, 8U);
    UmicomExecutableRequire(UmicomKernelProcessDestroy(&umicomExecutableProcessA) == UMICOM_PROCESS_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-destroy-reload");
    UmicomExecutableRequire(UmicomKernelProcessDestroy(&umicomExecutableProcessA) == UMICOM_PROCESS_BAD_STATE
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-double-destroy");

    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomExecutableRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U &&
        trapAfter.lastCauseCode == UMICOM_RISCV_EXCEPTION_ECALL_M_MODE
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-original-trap-handler");
    UmicomKernelPhysicalMemorySnapshot final;
    UmicomExecutableRequire(UmicomKernelPhysicalMemorySnapshotRead(&final) == UMICOM_KERNEL_MEMORY_OK &&
        UmicomExecutableAccountingEqual(&baseline, &final) != UMICOM_FALSE &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK
        ? UMICOM_TRUE : UMICOM_FALSE, "executable-frame-accounting");
    UmicomKernelConsoleWriteLine("executable.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("executable.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("executable.completed-cases=5");
    UmicomKernelConsoleWriteLine("executable-loading-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_EXECUTABLE_LOADING_READY");
}
