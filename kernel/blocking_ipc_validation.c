/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/blocking_ipc_validation.c
 *
 * PURPOSE:
 *   Exercise waiting messages through independently loaded, timer-scheduled
 *   user programs. No host model contributes a runtime success marker here.
 *
 * EDUCATIONAL NOTE:
 *   The dispatcher chooses which ready program runs. A blocked program spends
 *   neither user instructions nor further system calls until its pending call
 *   completes. Tests inspect private task records only to arrange deterministic
 *   admission and read diagnostic observations, not as a new service interface.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_ipc.h"
#include "umicom/kernel/ipc_workload.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"

extern const UmicomU8 UmicomBlockingExecutableStart[];
extern const UmicomU8 UmicomBlockingExecutableEnd[];
static UmicomKernelUserScheduler umicomIpcScheduler;
static UmicomKernelUserIpc umicomIpcDomain;
static UmicomSize umicomIpcChecks;
static UmicomSize umicomIpcCases;
static void UmicomIpcExpect(UmicomBoolean condition, const char *reason)
{
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("blocking-ipc.reason="); UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(0x87U); UmicomPlatformHalt();
    }
    ++umicomIpcChecks;
}
static void UmicomIpcCase(const char *name)
{
    UmicomKernelConsoleWrite("blocking-ipc.case="); UmicomKernelConsoleWriteLine(name); ++umicomIpcCases;
}
static UmicomKernelUserTaskInfo UmicomIpcInfo(UmicomKernelUserTaskHandle handle)
{
    UmicomKernelUserTaskInfo info;
    UmicomIpcExpect(UmicomKernelUserTaskQuery(&umicomIpcScheduler, handle, &info) == UMICOM_USER_SCHEDULE_OK,
        "task query");
    return info;
}
static UmicomKernelUserTask *UmicomIpcRecord(UmicomKernelUserTaskHandle handle)
{
    return &umicomIpcScheduler.tasks[(UmicomU32)handle - 1U]; /* Only freshly checked internal tokens. */
}
static UmicomKernelUserTaskHandle UmicomIpcCreate(UmicomU64 mode, UmicomU64 budget)
{
    UmicomKernelUserTaskHandle handle = 0U;
    const UmicomSize bytes = (UmicomSize)((UmicomAddress)UmicomBlockingExecutableEnd - (UmicomAddress)UmicomBlockingExecutableStart);
    UmicomIpcExpect(UmicomKernelUserTaskCreate(&umicomIpcScheduler, UmicomBlockingExecutableStart,
        bytes, 0U, budget, &handle) == UMICOM_USER_SCHEDULE_OK, "load independent IPC program");
    const UmicomKernelIpcObservation initial = { .mode = mode, .starts = 0U, .packets = 0U, .stackProof = 0U };
    UmicomIpcExpect(UmicomKernelUserMemoryWrite(&UmicomIpcRecord(handle)->process.report.memory,
        UMICOM_IPC_OBSERVATION_ADDRESS, (const UmicomU8 *)&initial, sizeof(initial)) == UMICOM_USER_RESULT_OK,
        "configure diagnostic program before entry");
    return handle;
}
static UmicomKernelIpcObservation UmicomIpcObservation(UmicomKernelUserTaskHandle handle)
{
    UmicomKernelIpcObservation observation;
    UmicomIpcExpect(UmicomKernelUserMemoryRead(&UmicomIpcRecord(handle)->process.report.memory,
        UMICOM_IPC_OBSERVATION_ADDRESS, (UmicomU8 *)&observation, sizeof(observation)) == UMICOM_USER_RESULT_OK,
        "read private program observation");
    return observation;
}
static void UmicomIpcPair(UmicomKernelUserTaskHandle receiver, UmicomKernelUserTaskHandle sender)
{
    UmicomKernelMessageHandle receive = 0U, send = 0U;
    UmicomIpcExpect(UmicomKernelUserIpcConnect(&umicomIpcDomain, receiver,
        UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY, sender,
        UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_QUERY, &receive, &send) == UMICOM_MESSAGE_OK,
        "create private owned channel");
    UmicomIpcExpect(UmicomKernelUserTaskSetArgument(&umicomIpcScheduler, receiver, receive) == UMICOM_USER_SCHEDULE_OK &&
        UmicomKernelUserTaskSetArgument(&umicomIpcScheduler, sender, send) == UMICOM_USER_SCHEDULE_OK,
        "admit endpoint arguments");
}
static void UmicomIpcRun(UmicomKernelUserTaskHandle desired)
{
    /* Controlled tests select a known runnable record; ordinary loops below
     * leave next untouched and exercise the normal round-robin search. */
    if (desired != 0U) umicomIpcScheduler.next = (UmicomU32)desired - 1U;
    UmicomKernelUserTaskHandle selected = 0U;
    UmicomIpcExpect(UmicomKernelUserSchedulerRunOne(&umicomIpcScheduler, 10000U, &selected)
        == UMICOM_USER_SCHEDULE_OK, "captured user invocation");
    if (desired != 0U) UmicomIpcExpect(desired == selected, "selected requested runnable task");
}
static void UmicomIpcBlock(UmicomKernelUserTaskHandle handle)
{
    for (UmicomSize attempt = 0U; attempt < 64U; ++attempt) {
        const UmicomKernelUserTaskState state = UmicomIpcInfo(handle).state;
        if (state == UMICOM_USER_TASK_BLOCKED) return;
        UmicomIpcExpect(state == UMICOM_USER_TASK_READY || state == UMICOM_USER_TASK_PAUSED,
            "program must not exit before its intended wait");
        UmicomIpcRun(handle);
    }
    UmicomIpcExpect(UMICOM_FALSE, "wait registration bounded by dispatch count");
}
static void UmicomIpcExit(UmicomKernelUserTaskHandle handle, UmicomU64 expected)
{
    for (UmicomSize attempt = 0U; attempt < 128U; ++attempt) {
        const UmicomKernelUserTaskInfo info = UmicomIpcInfo(handle);
        if (info.state == UMICOM_USER_TASK_EXITED) {
            UmicomIpcExpect(info.exitValue == expected, "program result"); return;
        }
        UmicomIpcExpect(info.state == UMICOM_USER_TASK_READY || info.state == UMICOM_USER_TASK_PAUSED,
            "completion must be runnable, not another block or a fault");
        UmicomIpcRun(handle);
    }
    UmicomIpcExpect(UMICOM_FALSE, "exit bounded by dispatch count");
}
static void UmicomIpcCleanup(UmicomKernelUserTaskHandle handle)
{
    const UmicomKernelUserTaskState state = UmicomIpcInfo(handle).state;
    if (state == UMICOM_USER_TASK_READY || state == UMICOM_USER_TASK_PAUSED || state == UMICOM_USER_TASK_BLOCKED)
        UmicomIpcExpect(UmicomKernelUserTaskCancel(&umicomIpcScheduler, handle) == UMICOM_USER_SCHEDULE_OK,
            "cancel before abandoning resources");
    UmicomIpcExpect(UmicomKernelUserTaskReap(&umicomIpcScheduler, handle) == UMICOM_USER_SCHEDULE_OK, "explicit image reclamation");
}
static void UmicomIpcEmpty(void)
{
    UmicomKernelMessageSnapshot messages;
    UmicomKernelUserIpcInfo waits;
    UmicomIpcExpect(UmicomKernelMessageSnapshotRead(&umicomIpcDomain.messages, &messages) == UMICOM_MESSAGE_OK &&
        messages.channels == 0U && messages.handles == 0U && messages.messages == 0U,
        "all private IPC ownership returned");
    UmicomIpcExpect(UmicomKernelUserIpcSnapshot(&umicomIpcDomain, &waits) && waits.waiting == 0U,
        "no abandoned wait descriptors");
    UmicomIpcExpect(UmicomKernelUserSchedulerValidate(&umicomIpcScheduler) == UMICOM_USER_SCHEDULE_OK,
        "scheduler invariants after cleanup");
}
static UmicomBoolean UmicomIpcMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec && a->mscratch == b->mscratch &&
        a->medeleg == b->medeleg && a->mideleg == b->mideleg && a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 &&
        a->pmpaddr0 == b->pmpaddr0 && a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval
        ? UMICOM_TRUE : UMICOM_FALSE;
}
void UmicomKernelBlockingIpcValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("blocking-ipc-test=begin");
    UmicomKernelPhysicalMemorySnapshot memoryBefore, memoryAfter;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomKernelPhysicalMemorySnapshotRead(&memoryBefore);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    const UmicomU64 compareBefore = UmicomPlatformTimerCompareRead(0U);
    UmicomIpcExpect(UmicomKernelUserSchedulerInitialize(&umicomIpcScheduler) == UMICOM_USER_SCHEDULE_OK &&
        UmicomKernelUserIpcAttach(&umicomIpcDomain, &umicomIpcScheduler) == UMICOM_USER_SCHEDULE_OK, "initialise owned service");

    UmicomIpcCase("producer-consumer-resume");
    UmicomKernelUserTaskHandle receiver = UmicomIpcCreate(UMICOM_IPC_MODE_EXCHANGE, 256U);
    UmicomKernelUserTaskHandle sender = UmicomIpcCreate(UMICOM_IPC_MODE_EXCHANGE, 256U);
    UmicomIpcPair(receiver, sender);
    UmicomIpcBlock(receiver); /* Empty queue: no producer has executed yet. */
    UmicomIpcBlock(sender);   /* Full queue: consumer has not been dispatched again. */
    UmicomIpcExpect(umicomIpcDomain.waits[(UmicomU32)sender - 1U].sending != UMICOM_FALSE,
        "actual full-queue send was suspended");
    for (UmicomSize dispatch = 0U; dispatch < 512U; ++dispatch) {
        if (UmicomIpcInfo(receiver).state == UMICOM_USER_TASK_EXITED && UmicomIpcInfo(sender).state == UMICOM_USER_TASK_EXITED) break;
        UmicomIpcRun(0U); /* Ordinary round robin, not a Kernel-driven message retry. */
    }
    UmicomIpcExit(sender, 0x7101U); UmicomIpcExit(receiver, 0x7102U);
    const UmicomKernelIpcObservation sent = UmicomIpcObservation(sender), received = UmicomIpcObservation(receiver);
    UmicomIpcExpect(sent.starts == 1U && received.starts == 1U && sent.packets == UMICOM_IPC_PACKET_COUNT &&
        received.packets == UMICOM_IPC_PACKET_COUNT && sent.stackProof == 0xaaaU && received.stackProof == 0xaaaU,
        "resumed stacks and copied packet order");
    UmicomKernelConsoleWriteLine("blocking-ipc.messages-received=12");
    UmicomKernelConsoleWriteLine("blocking-ipc.resume-without-restarting=pass");
    UmicomIpcCleanup(sender); UmicomIpcCleanup(receiver); UmicomIpcEmpty();

    UmicomIpcCase("finite-deadline-without-user-spin");
    receiver = UmicomIpcCreate(UMICOM_IPC_MODE_TIMEOUT, 128U);
    sender = UmicomIpcCreate(UMICOM_IPC_MODE_ENDLESS, 128U); UmicomIpcPair(receiver, sender); UmicomIpcBlock(receiver);
    UmicomKernelUserIpcInfo pending;
    UmicomIpcExpect(UmicomKernelUserIpcSnapshot(&umicomIpcDomain, &pending) && pending.hasDeadline, "finite deadline recorded");
    const UmicomU64 calls = UmicomIpcInfo(receiver).systemCalls, slices = UmicomIpcInfo(receiver).slices;
    /* This diagnostic dispatcher polls the platform clock. The blocked user
     * does not run. An eventual idle loop can sleep until the exposed deadline. */
    for (UmicomSize poll = 0U; poll < 10000000U && UmicomPlatformTimerRead() < pending.nextDeadline; ++poll) { }
    UmicomIpcExpect(UmicomPlatformTimerRead() >= pending.nextDeadline && UmicomKernelUserIpcPump(&umicomIpcScheduler), "complete due wait");
    UmicomIpcExpect(UmicomIpcInfo(receiver).systemCalls == calls && UmicomIpcInfo(receiver).slices == slices,
        "waiting spent no user calls or slices");
    UmicomIpcCleanup(sender); UmicomIpcExit(receiver, 0x7200U); UmicomIpcCleanup(receiver); UmicomIpcEmpty();
    UmicomKernelConsoleWriteLine("blocking-ipc.deadline-without-user-spin=pass");

    for (UmicomSize variant = 0U; variant < 2U; ++variant) {
        UmicomIpcCase(variant == 0U ? "peer-fault-cleanup" : "peer-slice-exhaustion");
        receiver = UmicomIpcCreate(UMICOM_IPC_MODE_PEER_CLOSE, 128U);
        sender = UmicomIpcCreate(variant == 0U ? UMICOM_IPC_MODE_FAULT : UMICOM_IPC_MODE_ENDLESS, 1U);
        UmicomIpcPair(receiver, sender); UmicomIpcBlock(receiver); UmicomIpcRun(sender);
        UmicomIpcExpect(UmicomIpcInfo(sender).state == (variant == 0U ? UMICOM_USER_TASK_FAULTED : UMICOM_USER_TASK_EXHAUSTED),
            "expected peer terminal state");
        UmicomIpcExpect(UmicomKernelUserIpcPump(&umicomIpcScheduler), "peer closure completes pending receive");
        UmicomIpcExit(receiver, 0x7300U); UmicomIpcCleanup(sender); UmicomIpcCleanup(receiver); UmicomIpcEmpty();
    }
    UmicomKernelConsoleWriteLine("blocking-ipc.peer-terminal-cleanup=pass");

    UmicomIpcCase("cancel-and-slot-reuse");
    receiver = UmicomIpcCreate(UMICOM_IPC_MODE_PEER_CLOSE, 128U);
    sender = UmicomIpcCreate(UMICOM_IPC_MODE_ENDLESS, 128U); UmicomIpcPair(receiver, sender); UmicomIpcBlock(receiver);
    const UmicomKernelUserTaskHandle old = receiver;
    UmicomIpcExpect(UmicomKernelUserTaskReap(&umicomIpcScheduler, receiver) == UMICOM_USER_SCHEDULE_BAD_STATE, "blocked stack stays owned");
    UmicomIpcCleanup(receiver); UmicomIpcCleanup(sender);
    receiver = UmicomIpcCreate(UMICOM_IPC_MODE_REFUSAL, 128U);
    sender = UmicomIpcCreate(UMICOM_IPC_MODE_ENDLESS, 128U); UmicomIpcPair(receiver, sender);
    UmicomIpcExpect(receiver != old && UmicomKernelUserTaskCancel(&umicomIpcScheduler, old)
        == UMICOM_USER_SCHEDULE_INVALID_HANDLE && UmicomKernelUserIpcPump(&umicomIpcScheduler), "stale wait cannot select replacement");
    UmicomIpcExit(receiver, 0x7400U); UmicomIpcCleanup(sender); UmicomIpcCleanup(receiver); UmicomIpcEmpty();

    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomIpcExpect(UmicomIpcMachineEqual(&machineBefore, &machineAfter) &&
        UmicomPlatformTimerCompareRead(0U) == compareBefore, "restore exact machine controls");
    UmicomRiscvTrapSnapshot trapBefore, trapAfter;
    UmicomRiscvTrapSnapshotRead(&trapBefore); UmicomRiscvTriggerMachineEcall(); UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomIpcExpect(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U && trapAfter.lastCauseCode == 11U,
        "original machine ECALL still works");
    UmicomKernelPhysicalMemorySnapshotRead(&memoryAfter);
    UmicomIpcExpect(memoryAfter.freeFrames == memoryBefore.freeFrames && memoryAfter.allocatedFrames == memoryBefore.allocatedFrames &&
        memoryAfter.reservedFrames == memoryBefore.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "frame ownership fully restored");
    UmicomKernelConsoleWriteLine("blocking-ipc.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("blocking-ipc.machine-state=restored");
    UmicomKernelConsoleWriteLine("blocking-ipc.frame-accounting=restored");
    UmicomKernelConsoleWrite("blocking-ipc.completed-cases="); UmicomKernelConsoleWriteUnsigned(umicomIpcCases); UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWrite("blocking-ipc.completed-checks="); UmicomKernelConsoleWriteUnsigned(umicomIpcChecks); UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("blocking-ipc-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_BLOCKING_IPC_READY");
}
