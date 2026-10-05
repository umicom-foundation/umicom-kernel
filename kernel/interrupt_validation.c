/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/interrupt_validation.c
 *
 * PURPOSE:
 *   Prove nested masking and exclusive timer ownership on the real hart, then
 *   show that normal threads and loaded programs can run after release.
 *
 * EDUCATIONAL OVERVIEW:
 *   The timer case becomes pending inside two critical sections. Neither an
 *   invalid outer unlock nor the inner valid unlock may deliver it. The outer
 *   unlock restores its saved MIE and the established one-shot handler then
 *   acknowledges the source. No new timer ISR is installed for this test.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/interrupt_state.h"
#include "umicom/kernel/riscv64/trap_integrity.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/threads.h"
#include "umicom/kernel/process.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
/* Stable owners: these objects cannot move while they contain borrowed views. */
static UmicomKernelScheduler umicomInterruptScheduler;
static UmicomKernelProcess umicomInterruptProcess;
static UmicomU64 umicomInterruptChecks;

static void UmicomInterruptRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomInterruptChecks;
    if (condition != UMICOM_FALSE) return;
    /* Failure is terminal evidence, not a path that force-enables interrupts. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelConsoleWrite("interrupt-ownership.reason=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomPlatformFinishFailure(193U);
    UmicomPlatformHalt();
}

static void UmicomInterruptExpect(UmicomKernelInterruptStatus actual,
    UmicomKernelInterruptStatus expected, const char *reason)
{
    if (actual != expected) {
        UmicomKernelConsoleWrite("interrupt-ownership.status=");
        UmicomKernelConsoleWriteLine(UmicomKernelInterruptStatusName(actual));
    }
    UmicomInterruptRequire(actual == expected ? UMICOM_TRUE : UMICOM_FALSE, reason);
}

static void UmicomInterruptDecimal(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}

static void UmicomInterruptPendingWait(UmicomBoolean pending)
{
    /* A device may take time to reflect a compare write in mip. Bound the wait
     * independently of interrupt delivery; MIE is clear throughout this loop. */
    for (UmicomSize spin = 0U; spin < 2000000U; ++spin) {
        UmicomRiscvInterruptState state;
        UmicomRiscvInterruptStateRead(&state);
        const UmicomBoolean observed = (state.pending & UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER) != 0U
            ? UMICOM_TRUE : UMICOM_FALSE;
        if (observed == pending) return;
    }
    UmicomInterruptRequire(UMICOM_FALSE, "timer-pending-transition-timeout");
}

static void UmicomInterruptNestedChecks(void)
{
    UmicomKernelConsoleWriteLine("interrupt-ownership.case=nested-order-and-stale-tokens");
    UmicomKernelCriticalSection scopes[UMICOM_INTERRUPT_SECTION_LIMIT];
    for (UmicomSize index = 0U; index < UMICOM_INTERRUPT_SECTION_LIMIT; ++index)
        UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &scopes[index]),
            UMICOM_INTERRUPT_OK, "enter-nested");
    UmicomKernelCriticalSection refused = 0x55U;
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(42U, &refused),
        UMICOM_INTERRUPT_WRONG_OWNER, "nested-owner");
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &refused),
        UMICOM_INTERRUPT_DEPTH_LIMIT, "bounded-depth");
    UmicomInterruptRequire(refused == 0x55U ? UMICOM_TRUE : UMICOM_FALSE, "refusal-output-unchanged");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, scopes[0]),
        UMICOM_INTERRUPT_OUT_OF_ORDER, "outer-before-inner-refused");
    for (UmicomSize count = UMICOM_INTERRUPT_SECTION_LIMIT; count != 0U; --count)
        UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, scopes[count - 1U]),
            UMICOM_INTERRUPT_OK, "leave-in-reverse-order");
    UmicomKernelCriticalSection fresh = 0U;
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &fresh), UMICOM_INTERRUPT_OK, "fresh-scope");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, scopes[0]),
        UMICOM_INTERRUPT_INVALID_TOKEN, "stale-scope-refused");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, fresh), UMICOM_INTERRUPT_OK, "fresh-leave");
    UmicomKernelInterruptSnapshot state;
    UmicomInterruptExpect(UmicomKernelInterruptSnapshotRead(&state), UMICOM_INTERRUPT_OK, "nested-snapshot");
    UmicomInterruptRequire(state.depth == 0U && state.deliveryEnabled == UMICOM_FALSE &&
        state.highestDepth == UMICOM_INTERRUPT_SECTION_LIMIT ? UMICOM_TRUE : UMICOM_FALSE,
        "disabled-caller-stays-disabled");
    UmicomKernelConsoleWriteLine("interrupt-ownership.nesting=preserved");
}

static void UmicomInterruptLeaseChecks(void)
{
    UmicomKernelConsoleWriteLine("interrupt-ownership.case=timer-source-leases");
    UmicomKernelCriticalSection scope = 0U;
    UmicomKernelInterruptLease lease = 0U;
    UmicomKernelInterruptLease refused = 0x66U;
    UmicomInterruptExpect(UmicomKernelInterruptSourceAcquire(41U, UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER, &lease),
        UMICOM_INTERRUPT_SECTION_REQUIRED, "acquire-needs-section");
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &scope), UMICOM_INTERRUPT_OK, "lease-scope");
    UmicomInterruptExpect(UmicomKernelInterruptSourceAcquire(41U, 0x800U, &refused),
        UMICOM_INTERRUPT_SOURCE_UNSUPPORTED, "external-controller-not-implemented");
    UmicomInterruptExpect(UmicomKernelInterruptSourceAcquire(41U, UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER, &lease),
        UMICOM_INTERRUPT_OK, "claim-timer");
    UmicomInterruptExpect(UmicomKernelInterruptSourceAcquire(41U, UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER, &refused),
        UMICOM_INTERRUPT_SOURCE_BUSY, "exclusive-timer");
    UmicomInterruptExpect(UmicomKernelInterruptSourceEnable(42U, lease), UMICOM_INTERRUPT_WRONG_OWNER, "source-owner");
    UmicomInterruptExpect(UmicomKernelInterruptSourceEnable(41U, lease), UMICOM_INTERRUPT_OK, "source-enable");
    UmicomInterruptExpect(UmicomKernelInterruptSourceRelease(41U, lease),
        UMICOM_INTERRUPT_SOURCE_ENABLED, "enabled-source-not-released");
    UmicomInterruptExpect(UmicomKernelInterruptDeliverySet(41U, UMICOM_TRUE),
        UMICOM_INTERRUPT_SECTION_ACTIVE, "no-enable-through-open-scope");
    UmicomInterruptExpect(UmicomKernelInterruptSourceDisable(41U, lease), UMICOM_INTERRUPT_OK, "source-disable");
    /* Even a not-yet-due timer still belongs to its programmer. */
    const UmicomU64 now = UmicomPlatformTimerRead();
    UmicomInterruptRequire(now < ~(UmicomU64)0U - 10000000U ? UMICOM_TRUE : UMICOM_FALSE, "future-deadline-range");
    UmicomPlatformTimerSetCompare(0U, now + 10000000U);
    UmicomInterruptExpect(UmicomKernelInterruptSourceRelease(41U, lease),
        UMICOM_INTERRUPT_DEVICE_ACTIVE, "future-timer-not-quiesced");
    UmicomPlatformTimerDisable(0U);
    UmicomInterruptPendingWait(UMICOM_FALSE);
    UmicomInterruptExpect(UmicomKernelInterruptSourceRelease(41U, lease), UMICOM_INTERRUPT_OK, "lease-release");
    UmicomInterruptExpect(UmicomKernelInterruptSourceEnable(41U, lease),
        UMICOM_INTERRUPT_INVALID_TOKEN, "old-lease-refused");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, scope), UMICOM_INTERRUPT_OK, "lease-scope-leave");
    UmicomInterruptRequire(refused == 0x66U ? UMICOM_TRUE : UMICOM_FALSE, "lease-refusal-output");
    UmicomKernelConsoleWriteLine("interrupt-ownership.timer-lease=exclusive");
}

static void UmicomInterruptDeferredTimer(void)
{
    UmicomKernelConsoleWriteLine("interrupt-ownership.case=pending-timer-inside-two-sections");
    UmicomRiscvTrapSnapshot before;
    UmicomRiscvTrapSnapshot after;
    UmicomRiscvTrapSnapshotRead(&before);
    UmicomKernelCriticalSection setup = 0U;
    UmicomKernelCriticalSection outer = 0U;
    UmicomKernelCriticalSection inner = 0U;
    UmicomKernelInterruptLease lease = 0U;
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &setup), UMICOM_INTERRUPT_OK, "timer-setup");
    UmicomInterruptExpect(UmicomKernelInterruptSourceAcquire(41U, UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER, &lease),
        UMICOM_INTERRUPT_OK, "timer-claim");
    UmicomInterruptExpect(UmicomKernelInterruptSourceEnable(41U, lease), UMICOM_INTERRUPT_OK, "enable-timer-source");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, setup), UMICOM_INTERRUPT_OK, "finish-timer-setup");
    /* The compare remains parked, so enabling delivery here cannot fire early. */
    UmicomInterruptExpect(UmicomKernelInterruptDeliverySet(41U, UMICOM_TRUE), UMICOM_INTERRUPT_OK, "enable-owned-delivery");
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &outer), UMICOM_INTERRUPT_OK, "timer-outer");
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &inner), UMICOM_INTERRUPT_OK, "timer-inner");
    const UmicomU64 now = UmicomPlatformTimerRead();
    UmicomInterruptRequire(now < ~(UmicomU64)0U - 30000U ? UMICOM_TRUE : UMICOM_FALSE, "timer-deadline-range");
    UmicomPlatformTimerSetCompare(0U, now + 30000U);
    UmicomInterruptPendingWait(UMICOM_TRUE);
    UmicomRiscvTrapSnapshotRead(&after);
    UmicomInterruptRequire(after.timerInterruptCount == before.timerInterruptCount ? UMICOM_TRUE : UMICOM_FALSE,
        "pending-not-delivered-under-mask");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, outer), UMICOM_INTERRUPT_OUT_OF_ORDER, "pending-outer-refused");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, inner), UMICOM_INTERRUPT_OK, "pending-inner-left");
    UmicomKernelInterruptSnapshot inside;
    UmicomInterruptExpect(UmicomKernelInterruptSnapshotRead(&inside), UMICOM_INTERRUPT_OK, "pending-snapshot");
    UmicomRiscvTrapSnapshotRead(&after);
    UmicomInterruptRequire(inside.depth == 1U && inside.deliveryEnabled == UMICOM_FALSE &&
        inside.outerDeliveryEnabled == UMICOM_TRUE && (inside.pendingSources & 0x80U) != 0U &&
        after.timerInterruptCount == before.timerInterruptCount ? UMICOM_TRUE : UMICOM_FALSE,
        "inner-leave-did-not-unmask");
    UmicomKernelConsoleWriteLine("interrupt-ownership.pending-while-nested=deferred");
    /* This is the only publication of the saved outer MIE bit. The unchanged
     * machine timer handler may execute before this Leave returns to C. */
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, outer), UMICOM_INTERRUPT_OK, "outer-unlock");
    for (UmicomSize spin = 0U; spin < 2000000U; ++spin) {
        UmicomRiscvTrapSnapshotRead(&after);
        if (after.timerInterruptCount != before.timerInterruptCount) break;
    }
    UmicomInterruptRequire(after.timerInterruptCount == before.timerInterruptCount + 1U &&
        after.lastCauseCode == 7U && after.lastWasInterrupt != 0U ? UMICOM_TRUE : UMICOM_FALSE,
        "exactly-one-deferred-timer");
    UmicomInterruptPendingWait(UMICOM_FALSE);
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(41U, &setup), UMICOM_INTERRUPT_OK, "timer-release-scope");
    UmicomInterruptExpect(UmicomKernelInterruptSourceRelease(41U, lease), UMICOM_INTERRUPT_OK, "acknowledged-timer-release");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(41U, setup), UMICOM_INTERRUPT_OK, "timer-release-leave");
    UmicomKernelConsoleWriteLine("interrupt-ownership.outer-release-timer=delivered-once");
}

static UmicomU64 UmicomInterruptThread(void *argument)
{
    (void)argument;
    UmicomKernelCriticalSection scope = 0U;
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(51U, &scope), UMICOM_INTERRUPT_OK, "thread-section");
    /* None of these refusals may change the thread's RUNNING record or sp. */
    volatile UmicomU64 retained = 0x12345678U;
    UmicomInterruptRequire(UmicomKernelThreadYield(&umicomInterruptScheduler) == UMICOM_THREAD_UNSAFE_MACHINE &&
        UmicomKernelThreadWait(&umicomInterruptScheduler) == UMICOM_THREAD_UNSAFE_MACHINE &&
        UmicomKernelThreadSleepUntil(&umicomInterruptScheduler, 1U) == UMICOM_THREAD_UNSAFE_MACHINE
            ? UMICOM_TRUE : UMICOM_FALSE, "cannot-suspend-owner");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(51U, scope), UMICOM_INTERRUPT_OK, "thread-release");
    UmicomInterruptRequire(UmicomKernelThreadYield(&umicomInterruptScheduler) == UMICOM_THREAD_OK,
        "yield-after-release");
    return retained;
}

static void UmicomInterruptThreadChecks(void)
{
    UmicomKernelConsoleWriteLine("interrupt-ownership.case=thread-cannot-abandon-section");
    UmicomKernelThreadHandle thread = 0U;
    UmicomInterruptRequire(UmicomKernelSchedulerInitialize(&umicomInterruptScheduler) == UMICOM_THREAD_OK &&
        UmicomKernelThreadCreate(&umicomInterruptScheduler, UmicomInterruptThread, (void *)0, &thread) == UMICOM_THREAD_OK,
        "thread-create");
    UmicomKernelCriticalSection scope = 0U;
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(51U, &scope), UMICOM_INTERRUPT_OK, "dispatcher-section");
    UmicomInterruptRequire(UmicomKernelSchedulerRunOne(&umicomInterruptScheduler) == UMICOM_THREAD_UNSAFE_MACHINE,
        "dispatcher-cannot-switch-in-section");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(51U, scope), UMICOM_INTERRUPT_OK, "dispatcher-release");
    UmicomInterruptRequire(UmicomKernelSchedulerRunOne(&umicomInterruptScheduler) == UMICOM_THREAD_OK &&
        UmicomKernelSchedulerRunOne(&umicomInterruptScheduler) == UMICOM_THREAD_OK, "thread-run-and-resume");
    UmicomKernelThreadInfo info;
    UmicomInterruptRequire(UmicomKernelThreadReap(&umicomInterruptScheduler, thread, &info) == UMICOM_THREAD_OK &&
        info.exitValue == 0x12345678U && info.yields == 1U ? UMICOM_TRUE : UMICOM_FALSE, "thread-result");
    UmicomKernelConsoleWriteLine("interrupt-ownership.thread-resume-after-release=pass");
}

static void UmicomInterruptProcessChecks(void)
{
    UmicomKernelConsoleWriteLine("interrupt-ownership.case=process-cannot-borrow-owned-timer");
    const UmicomSize bytes = (UmicomSize)((UmicomAddress)UmicomEmbeddedExecutableEnd -
        (UmicomAddress)UmicomEmbeddedExecutableStart);
    UmicomKernelExecutableStatus executableStatus = UMICOM_EXECUTABLE_OK;
    UmicomInterruptRequire(UmicomKernelProcessCreate(&umicomInterruptProcess,
        UmicomEmbeddedExecutableStart, bytes, 701U, &executableStatus) == UMICOM_PROCESS_OK,
        "load-diagnostic");
    UmicomKernelCriticalSection scope = 0U;
    UmicomKernelInterruptLease lease = 0U;
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(61U, &scope), UMICOM_INTERRUPT_OK, "process-owner-section");
    UmicomInterruptRequire(UmicomKernelProcessRun(&umicomInterruptProcess, 7U, 1000000U) == UMICOM_PROCESS_ENTRY_REFUSED &&
        umicomInterruptProcess.state == UMICOM_PROCESS_READY, "process-refused-in-section");
    UmicomInterruptExpect(UmicomKernelInterruptSourceAcquire(61U, UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER, &lease),
        UMICOM_INTERRUPT_OK, "process-timer-claim");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(61U, scope), UMICOM_INTERRUPT_OK, "process-owner-leave");
    const UmicomU64 compare = UmicomPlatformTimerCompareRead(0U);
    UmicomInterruptRequire(UmicomKernelProcessRun(&umicomInterruptProcess, 7U, 1000000U) == UMICOM_PROCESS_ENTRY_REFUSED &&
        umicomInterruptProcess.state == UMICOM_PROCESS_READY && UmicomPlatformTimerCompareRead(0U) == compare,
        "process-does-not-steal-masked-timer");
    UmicomInterruptExpect(UmicomKernelCriticalSectionEnter(61U, &scope), UMICOM_INTERRUPT_OK, "process-release-section");
    UmicomInterruptExpect(UmicomKernelInterruptSourceRelease(61U, lease), UMICOM_INTERRUPT_OK, "process-release-timer");
    UmicomInterruptExpect(UmicomKernelCriticalSectionLeave(61U, scope), UMICOM_INTERRUPT_OK, "process-release-leave");
    UmicomInterruptRequire(UmicomKernelProcessRun(&umicomInterruptProcess, 7U, 1000000U) == UMICOM_PROCESS_OK &&
        umicomInterruptProcess.state == UMICOM_PROCESS_EXITED && umicomInterruptProcess.report.exitValue == 741U &&
        umicomInterruptProcess.quiesced == UMICOM_TRUE, "loaded-program-runs-after-release");
    UmicomInterruptRequire(UmicomKernelProcessDestroy(&umicomInterruptProcess) == UMICOM_PROCESS_OK, "destroy-diagnostic");
    UmicomKernelConsoleWriteLine("interrupt-ownership.process-retry-after-release=pass");
}

void UmicomKernelInterruptOwnershipValidate(void)
{
    UmicomKernelConsoleWriteLine("interrupt-ownership-test=begin");
    UmicomRiscvSupervisorMachineState before;
    UmicomRiscvSupervisorMachineState after;
    UmicomRiscvSupervisorMachineStateRead(&before);
    const UmicomU64 oldCompare = UmicomPlatformTimerCompareRead(0U);
    UmicomKernelPhysicalMemorySnapshot memoryBefore;
    UmicomKernelPhysicalMemorySnapshot memoryAfter;
    UmicomInterruptRequire(UmicomKernelPhysicalMemorySnapshotRead(&memoryBefore) == UMICOM_KERNEL_MEMORY_OK,
        "initial-frame-accounting");
    UmicomInterruptExpect(UmicomKernelInterruptInitialize(UmicomRiscvTrapVectorAddress(),
        (UmicomAddress)UmicomRiscvMachineTrapStackTop), UMICOM_INTERRUPT_OK, "controller-initialise");
    UmicomPlatformTimerDisable(0U);
    UmicomInterruptPendingWait(UMICOM_FALSE);
    UmicomInterruptNestedChecks();
    UmicomInterruptLeaseChecks();
    UmicomInterruptDeferredTimer();
    UmicomInterruptThreadChecks();
    UmicomInterruptProcessChecks();
    UmicomKernelInterruptSnapshot final;
    UmicomInterruptExpect(UmicomKernelInterruptSnapshotRead(&final), UMICOM_INTERRUPT_OK, "final-controller");
    UmicomInterruptRequire(final.depth == 0U && final.leasedSources == 0U && final.enabledSources == 0U &&
        final.deliveryEnabled == UMICOM_FALSE && final.poisoned == UMICOM_FALSE, "no-live-ownership");
    UmicomRiscvTrapSnapshot trapBefore;
    UmicomRiscvTrapSnapshot trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomInterruptRequire(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U,
        "original-ecall-still-works");
    UmicomPlatformTimerSetCompare(0U, oldCompare);
    UmicomRiscvSupervisorMachineStateRead(&after);
    /* MRET changes MPP/MPIE and the diagnostic trap CSRs. All unrelated controls
     * and the installed entry pair must still belong to the original caller. */
    const UmicomU64 returnBits = 0x1880U;
    UmicomInterruptRequire((before.mstatus & ~returnBits) == (after.mstatus & ~returnBits) &&
        before.mie == after.mie && before.mtvec == after.mtvec && before.mscratch == after.mscratch &&
        before.satp == after.satp && before.mideleg == after.mideleg && before.medeleg == after.medeleg &&
        before.pmpcfg0 == after.pmpcfg0 && before.pmpaddr0 == after.pmpaddr0 &&
        UmicomPlatformTimerCompareRead(0U) == oldCompare, "control-state-restored");
    UmicomInterruptRequire(UmicomKernelPhysicalMemorySnapshotRead(&memoryAfter) == UMICOM_KERNEL_MEMORY_OK &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK &&
        memoryBefore.allocatedFrames == memoryAfter.allocatedFrames &&
        memoryBefore.reservedFrames == memoryAfter.reservedFrames && memoryBefore.freeFrames == memoryAfter.freeFrames,
        "final-frame-accounting");
    UmicomKernelConsoleWriteLine("interrupt-ownership.original-trap-policy=preserved");
    UmicomKernelConsoleWriteLine("interrupt-ownership.control-state=restored");
    UmicomKernelConsoleWriteLine("interrupt-ownership.frame-accounting=restored");
    UmicomInterruptDecimal("interrupt-ownership.completed-cases", 5U);
    UmicomInterruptDecimal("interrupt-ownership.completed-checks", umicomInterruptChecks);
    UmicomKernelConsoleWriteLine("interrupt-ownership-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_INTERRUPT_OWNERSHIP_READY");
}
