/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_scheduling_validation.c
 *
 * PURPOSE:
 *   Exercise real timer pre-emption of separate ELF images, full integer
 *   continuations, native services, faults, cancellation and bounded execution.
 *
 * EDUCATIONAL NOTE:
 *   Host policy tests cannot prove that MRET or interrupt return preserved a
 *   user register. These cases run the separately linked diagnostic on QEMU.
 *   The gate opens after both tasks made progress across at least two timer
 *   stops, so a fast or slow emulator does not change the required event order.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_scheduler.h"
#include "umicom/kernel/user_workload.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/console.h"

extern const UmicomU8 UmicomScheduledExecutableStart[];
extern const UmicomU8 UmicomScheduledExecutableEnd[];
/* Process views borrow interior pointers, so this domain must never move. */
static UmicomKernelUserScheduler umicomScheduledValidation;
static UmicomSize umicomScheduledChecks;
/* Leave a ten-millisecond guest quantum on the selected 10 MHz timer. The
 * legal API minimum is useful for policy tests, but an emulator under load
 * must have time to reach the loop before this execution test expires it. */
#define UMICOM_SCHEDULING_VALIDATION_QUANTUM 100000U

static void UmicomSchedulingRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomScheduledChecks;
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("reason=");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(219U);
        UmicomPlatformHalt();
    }
}
#define UMICOM_SCHEDULING_REQUIRE(condition, reason) \
    UmicomSchedulingRequire((condition) ? UMICOM_TRUE : UMICOM_FALSE, (reason))
static void UmicomSchedulingNumber(const char *key, UmicomU64 value)
{
    UmicomKernelConsoleWrite(key);
    UmicomKernelConsoleWrite("=");
    UmicomKernelConsoleWriteUnsigned(value);
    UmicomKernelConsoleWriteLine("");
}
static UmicomKernelUserTaskHandle UmicomSchedulingCreate(UmicomU64 argument, UmicomU64 budget)
{
    UmicomKernelUserTaskHandle handle = 0U;
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserTaskCreate(&umicomScheduledValidation,
        UmicomScheduledExecutableStart,
        (UmicomSize)(UmicomScheduledExecutableEnd - UmicomScheduledExecutableStart), argument, budget, &handle)
        == UMICOM_USER_SCHEDULE_OK, "user-scheduling-load");
    return handle;
}
static UmicomKernelUserTaskInfo UmicomSchedulingInfo(UmicomKernelUserTaskHandle handle)
{
    UmicomKernelUserTaskInfo info;
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserTaskQuery(&umicomScheduledValidation, handle, &info)
        == UMICOM_USER_SCHEDULE_OK, "user-scheduling-query");
    return info;
}
static UmicomKernelUserTask *UmicomSchedulingRecord(UmicomKernelUserTaskHandle handle)
{
    /* Validation alone inspects these private records to prove physical-frame
     * separation. Normal service consumers receive value-only Query results. */
    const UmicomU32 slot = (UmicomU32)handle - 1U;
    UMICOM_SCHEDULING_REQUIRE(slot < UMICOM_USER_TASK_LIMIT, "user-scheduling-test-handle");
    return &umicomScheduledValidation.tasks[slot];
}
static UmicomKernelScheduledObservation UmicomSchedulingObservation(UmicomKernelUserTaskHandle handle)
{
    UmicomKernelScheduledObservation observation;
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserMemoryRead(&UmicomSchedulingRecord(handle)->process.report.memory,
        UMICOM_SCHEDULED_OBSERVATION_ADDRESS, (UmicomU8 *)&observation, sizeof(observation)) == UMICOM_USER_RESULT_OK,
        "user-scheduling-observation");
    return observation;
}
static void UmicomSchedulingReleaseGate(UmicomKernelUserTaskHandle handle)
{
    const UmicomU64 released = 1U;
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserMemoryWrite(&UmicomSchedulingRecord(handle)->process.report.memory,
        UMICOM_SCHEDULED_OBSERVATION_ADDRESS + __builtin_offsetof(UmicomKernelScheduledObservation, release),
        (const UmicomU8 *)&released, sizeof(released)) == UMICOM_USER_RESULT_OK, "user-scheduling-release-gate");
}
static UmicomKernelUserTaskHandle UmicomSchedulingDispatch(void)
{
    UmicomKernelUserTaskHandle selected = 0U;
    const UmicomKernelUserScheduleStatus status = UmicomKernelUserSchedulerRunOne(
        &umicomScheduledValidation, UMICOM_SCHEDULING_VALIDATION_QUANTUM, &selected);
    if (status != UMICOM_USER_SCHEDULE_OK) UmicomSchedulingNumber("user-scheduling.dispatch-status", (UmicomU64)status);
    UMICOM_SCHEDULING_REQUIRE(status == UMICOM_USER_SCHEDULE_OK, "user-scheduling-dispatch");
    const UmicomKernelUserTaskInfo info = UmicomSchedulingInfo(selected);
    if (info.state == UMICOM_USER_TASK_PAUSED) {
        const UmicomKernelUserTask *const task = UmicomSchedulingRecord(selected);
        UMICOM_SCHEDULING_REQUIRE(info.trapCause == (UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U) &&
            info.resumePc == task->process.report.trapPc && ((task->frame.mstatus >> 11U) & 3U) == 0U,
            "user-scheduling-timer-pc-preserved");
        UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserTaskReap(&umicomScheduledValidation, selected)
            == UMICOM_USER_SCHEDULE_BAD_STATE, "user-scheduling-paused-free-refused");
        UMICOM_SCHEDULING_REQUIRE(UmicomKernelProcessDestroy(&UmicomSchedulingRecord(selected)->process)
            == UMICOM_PROCESS_BAD_STATE, "user-scheduling-lower-owner-pinned");
    }
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserSchedulerValidate(&umicomScheduledValidation)
        == UMICOM_USER_SCHEDULE_OK, "user-scheduling-domain-invariants");
    return selected;
}
static void UmicomSchedulingReap(UmicomKernelUserTaskHandle handle)
{
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserTaskReap(&umicomScheduledValidation, handle)
        == UMICOM_USER_SCHEDULE_OK, "user-scheduling-reap");
    UmicomKernelUserTaskInfo info;
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserTaskQuery(&umicomScheduledValidation, handle, &info)
        == UMICOM_USER_SCHEDULE_INVALID_HANDLE, "user-scheduling-reaped-token");
}
static void UmicomSchedulingNormalResult(UmicomKernelUserTaskHandle handle, UmicomU64 argument)
{
    const UmicomKernelUserTaskInfo info = UmicomSchedulingInfo(handle);
    const UmicomKernelScheduledObservation observation = UmicomSchedulingObservation(handle);
    UMICOM_SCHEDULING_REQUIRE(info.state == UMICOM_USER_TASK_EXITED && info.exitValue == info.identity + argument + 0x600U &&
        info.systemCalls == 4U && info.trapCause == 8U && observation.identity == info.identity &&
        observation.starts == 1U && observation.iterations != 0U && observation.completed == 1U &&
        observation.localResult == 0U && UmicomSchedulingRecord(handle)->process.report.copiedBytes == 16U,
        "user-scheduling-program-registers-stack-or-services");
}

void UmicomKernelUserSchedulingValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("user-scheduling-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomKernelPhysicalMemorySnapshotRead(&before);
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserSchedulerInitialize(&umicomScheduledValidation)
        == UMICOM_USER_SCHEDULE_OK, "user-scheduling-initialise");

    UmicomKernelConsoleWriteLine("user-scheduling.case=two-cpu-bound-images");
    const UmicomKernelUserTaskHandle a = UmicomSchedulingCreate(7U, 512U);
    const UmicomKernelUserTaskHandle b = UmicomSchedulingCreate(19U, 512U);
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingRecord(a)->process.space.rootTablePhysicalAddress !=
        UmicomSchedulingRecord(b)->process.space.rootTablePhysicalAddress, "user-scheduling-distinct-roots");
    UmicomBoolean released = UMICOM_FALSE;
    UmicomKernelUserTaskHandle previous = 0U;
    for (UmicomSize attempt = 0U; attempt < 1024U; ++attempt) {
        const UmicomKernelUserTaskInfo ai = UmicomSchedulingInfo(a);
        const UmicomKernelUserTaskInfo bi = UmicomSchedulingInfo(b);
        if (ai.state == UMICOM_USER_TASK_EXITED && bi.state == UMICOM_USER_TASK_EXITED) break;
        const UmicomKernelUserTaskHandle selected = UmicomSchedulingDispatch();
        if (ai.state <= UMICOM_USER_TASK_PAUSED && bi.state <= UMICOM_USER_TASK_PAUSED && previous != 0U)
            UMICOM_SCHEDULING_REQUIRE(selected != previous, "user-scheduling-round-robin");
        previous = selected;
        const UmicomKernelScheduledObservation ao = UmicomSchedulingObservation(a);
        const UmicomKernelScheduledObservation bo = UmicomSchedulingObservation(b);
        if (released == UMICOM_FALSE && UmicomSchedulingInfo(a).preemptions >= 2U &&
            UmicomSchedulingInfo(b).preemptions >= 2U && ao.iterations != 0U && bo.iterations != 0U) {
            UMICOM_SCHEDULING_REQUIRE(ao.starts == 1U && bo.starts == 1U && ao.identity != bo.identity,
                "user-scheduling-separate-progress");
            UmicomSchedulingReleaseGate(a);
            UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingObservation(b).release == 0U,
                "user-scheduling-writable-frame-isolation");
            UmicomSchedulingReleaseGate(b);
            released = UMICOM_TRUE;
        }
    }
    UMICOM_SCHEDULING_REQUIRE(released != UMICOM_FALSE, "user-scheduling-both-progress");
    UmicomSchedulingNormalResult(a, 7U);
    UmicomSchedulingNormalResult(b, 19U);
    UmicomSchedulingNumber("user-scheduling.task-a.preemptions", UmicomSchedulingInfo(a).preemptions);
    UmicomSchedulingNumber("user-scheduling.task-b.preemptions", UmicomSchedulingInfo(b).preemptions);
    UmicomKernelConsoleWriteLine("user-scheduling.registers-and-nested-stacks=preserved");
    UmicomKernelConsoleWriteLine("user-scheduling.address-space-isolation=pass");
    UmicomSchedulingReap(a); UmicomSchedulingReap(b);

    UmicomKernelConsoleWriteLine("user-scheduling.case=fault-does-not-stop-neighbour");
    const UmicomKernelUserTaskHandle fault = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_FAULT, 64U);
    const UmicomKernelUserTaskHandle survivor = UmicomSchedulingCreate(11U, 512U);
    for (UmicomSize attempt = 0U; attempt < 576U; ++attempt) {
        if (UmicomSchedulingInfo(fault).state == UMICOM_USER_TASK_FAULTED &&
            UmicomSchedulingInfo(survivor).state == UMICOM_USER_TASK_EXITED) break;
        (void)UmicomSchedulingDispatch();
        if (UmicomSchedulingInfo(survivor).preemptions >= 2U && UmicomSchedulingObservation(survivor).iterations != 0U)
            UmicomSchedulingReleaseGate(survivor);
    }
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(fault).state == UMICOM_USER_TASK_FAULTED &&
        UmicomSchedulingInfo(fault).trapCause == 15U, "user-scheduling-fault-cause");
    UmicomSchedulingNormalResult(survivor, 11U);
    UmicomSchedulingReap(fault); UmicomSchedulingReap(survivor);

    UmicomKernelConsoleWriteLine("user-scheduling.case=bounded-busy-loop-and-invalid-stack-exit");
    const UmicomKernelUserTaskHandle busy = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_BUSY, 3U);
    const UmicomKernelUserTaskHandle badStack = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_BAD_STACK, 64U);
    for (UmicomSize attempt = 0U; attempt < 67U; ++attempt) {
        if (UmicomSchedulingInfo(busy).state == UMICOM_USER_TASK_EXHAUSTED &&
            UmicomSchedulingInfo(badStack).state == UMICOM_USER_TASK_EXITED) break;
        (void)UmicomSchedulingDispatch();
    }
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(busy).state == UMICOM_USER_TASK_EXHAUSTED &&
        UmicomSchedulingInfo(busy).preemptions == 3U, "user-scheduling-quantum-budget");
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(badStack).state == UMICOM_USER_TASK_EXITED &&
        UmicomSchedulingInfo(badStack).exitValue == 0x605U && UmicomSchedulingInfo(badStack).savedStack == 0U,
        "user-scheduling-zero-stack-exit");
    UmicomSchedulingReap(busy); UmicomSchedulingReap(badStack);

    UmicomKernelConsoleWriteLine("user-scheduling.case=cancel-paused-and-reject-stale-handle");
    const UmicomKernelUserTaskHandle cancelled = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_BUSY, 64U);
    (void)UmicomSchedulingDispatch();
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(cancelled).state == UMICOM_USER_TASK_PAUSED &&
        UmicomKernelUserTaskCancel(&umicomScheduledValidation, cancelled) == UMICOM_USER_SCHEDULE_OK,
        "user-scheduling-cancel-pause");
    UmicomSchedulingReap(cancelled);
    const UmicomKernelUserTaskHandle replacement = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_BAD_STACK, 64U);
    UmicomKernelUserTaskInfo staleInfo;
    UMICOM_SCHEDULING_REQUIRE(cancelled != replacement &&
        UmicomKernelUserTaskQuery(&umicomScheduledValidation, cancelled, &staleInfo) == UMICOM_USER_SCHEDULE_INVALID_HANDLE,
        "user-scheduling-generation-reuse");
    for (UmicomSize attempt = 0U; attempt < 64U && UmicomSchedulingInfo(replacement).state != UMICOM_USER_TASK_EXITED; ++attempt)
        (void)UmicomSchedulingDispatch();
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(replacement).exitValue == 0x605U, "user-scheduling-replacement-runs");
    UmicomSchedulingReap(replacement);

    UmicomKernelConsoleWriteLine("user-scheduling.case=cumulative-system-call-budget");
    const UmicomKernelUserTaskHandle calls = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_CALLS, 512U);
    for (UmicomSize attempt = 0U; attempt < 512U && UmicomSchedulingInfo(calls).state != UMICOM_USER_TASK_EXHAUSTED; ++attempt)
        (void)UmicomSchedulingDispatch();
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(calls).state == UMICOM_USER_TASK_EXHAUSTED &&
        UmicomSchedulingInfo(calls).systemCalls == UMICOM_USER_CALL_LIMIT + 1U &&
        UmicomSchedulingRecord(calls)->process.report.stopReason == UMICOM_USER_STOP_CALL_BUDGET,
        "user-scheduling-call-budget-not-reset");
    UmicomSchedulingReap(calls);

    UmicomKernelConsoleWriteLine("user-scheduling.case=interrupt-owner-refusal-before-entry");
    const UmicomKernelUserTaskHandle protected = UmicomSchedulingCreate(UMICOM_SCHEDULED_OPERATION_BUSY, 64U);
    UmicomKernelCriticalSection section = 0U;
    UmicomKernelUserTaskHandle unchanged = 0x123U;
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelCriticalSectionEnter(0x701U, &section) == UMICOM_INTERRUPT_OK,
        "user-scheduling-section-enter");
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserSchedulerRunOne(&umicomScheduledValidation,
        UMICOM_USER_QUANTUM_MIN_TICKS, &unchanged) == UMICOM_USER_SCHEDULE_ENTRY_REFUSED && unchanged == 0x123U,
        "user-scheduling-section-entry-refused");
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelCriticalSectionLeave(0x701U, section) == UMICOM_INTERRUPT_OK,
        "user-scheduling-section-leave");
    UMICOM_SCHEDULING_REQUIRE(UmicomSchedulingInfo(protected).state == UMICOM_USER_TASK_READY &&
        UmicomSchedulingInfo(protected).slices == 0U, "user-scheduling-refusal-preserves-task");
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserTaskCancel(&umicomScheduledValidation, protected)
        == UMICOM_USER_SCHEDULE_OK, "user-scheduling-cancel-unstarted");
    UmicomSchedulingReap(protected);
    UMICOM_SCHEDULING_REQUIRE(UmicomKernelUserSchedulerRunOne(&umicomScheduledValidation,
        UMICOM_USER_QUANTUM_MIN_TICKS, &unchanged) == UMICOM_USER_SCHEDULE_IDLE, "user-scheduling-idle");

    UmicomRiscvTrapSnapshot originalBefore;
    UmicomRiscvTrapSnapshot originalAfter;
    UmicomRiscvTrapSnapshotRead(&originalBefore);
    UmicomRiscvTriggerMachineEcall();
    UmicomRiscvTrapSnapshotRead(&originalAfter);
    UMICOM_SCHEDULING_REQUIRE(originalAfter.exceptionCount == originalBefore.exceptionCount + 1U,
        "user-scheduling-original-trap");
    UmicomKernelPhysicalMemorySnapshotRead(&after);
    UMICOM_SCHEDULING_REQUIRE(before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK &&
        UmicomKernelUserSchedulerValidate(&umicomScheduledValidation) == UMICOM_USER_SCHEDULE_OK,
        "user-scheduling-final-accounting");
    UmicomKernelConsoleWriteLine("user-scheduling.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("user-scheduling.machine-state=restored-after-each-slice");
    UmicomKernelConsoleWriteLine("user-scheduling.frame-accounting=restored");
    UmicomSchedulingNumber("user-scheduling.completed-cases", 6U);
    UmicomSchedulingNumber("user-scheduling.completed-checks", umicomScheduledChecks);
    UmicomKernelConsoleWriteLine("user-scheduling-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_USER_SCHEDULING_READY");
}
