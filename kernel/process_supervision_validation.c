/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/process_supervision_validation.c
 *
 * PURPOSE:
 *   Exercise parent authority and lifetime policy using separately loaded user
 *   programs, real timer quanta and the existing blocking-message service.
 *
 * EDUCATIONAL NOTE:
 *   This is guest acceptance, not the native hardware model. Private task
 *   inspection is used only to arrange a known runnable order or a diagnostic
 *   program mode. The service under test never writes scheduler records.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process_supervisor.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/ipc_workload.h"
#include "umicom/kernel/riscv64/supervisor.h"

/* Existing independent programs are immutable loader input, not Kernel calls. */
extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
extern const UmicomU8 UmicomBlockingExecutableStart[];
extern const UmicomU8 UmicomBlockingExecutableEnd[];
static UmicomKernelProcessSupervisor umicomGuestSupervisor;
static UmicomU64 umicomSupervisionChecks;
static UmicomU64 umicomSupervisionCases;
/* Snapshot slots keep the guest harness independent of compiler-generated
 * structure-return memcpy calls. They hold values, not image ownership. */
static UmicomKernelSupervisedProcessInfo umicomSupervisionInfoSlots[UMICOM_USER_TASK_LIMIT];
static UmicomKernelProcessCompletion umicomSupervisionCollected;
static void UmicomSupervisionGuestClear(void *destination, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)destination;
    for (UmicomSize index = 0U; index < bytes; ++index) output[index] = 0U;
}

static void UmicomSupervisionExpect(UmicomBoolean condition, const char *reason)
{
    ++umicomSupervisionChecks;
    if (condition != UMICOM_FALSE) return;
    UmicomKernelConsoleWrite("process-supervision.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x71U);
    UmicomPlatformHalt();
}
static void UmicomSupervisionCase(const char *name)
{
    ++umicomSupervisionCases;
    UmicomKernelConsoleWrite("process-supervision.case=");
    UmicomKernelConsoleWriteLine(name);
}
static const UmicomKernelSupervisedProcessInfo *UmicomSupervisionInfo(UmicomKernelSupervisedProcessHandle handle)
{
    const UmicomU32 slot = (UmicomU32)handle;
    UmicomSupervisionExpect(slot > 0U && slot <= UMICOM_USER_TASK_LIMIT, "bounded guest snapshot slot");
    UmicomKernelSupervisedProcessInfo *const info = &umicomSupervisionInfoSlots[slot - 1U];
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorQuery(&umicomGuestSupervisor, 0U, handle, info)
        == UMICOM_SUPERVISION_OK, "guardian can inspect a live record");
    return info;
}
static UmicomKernelSupervisedProcessHandle UmicomSupervisionCreate(UmicomU64 parent, UmicomU64 argument,
    UmicomKernelChildLifetime policy, UmicomBoolean blocking)
{
    const UmicomU8 *const start = blocking != UMICOM_FALSE ? UmicomBlockingExecutableStart : UmicomEmbeddedExecutableStart;
    const UmicomU8 *const end = blocking != UMICOM_FALSE ? UmicomBlockingExecutableEnd : UmicomEmbeddedExecutableEnd;
    const UmicomSize bytes = (UmicomSize)((UmicomAddress)end - (UmicomAddress)start);
    UmicomKernelSupervisedProcessHandle handle = 0U;
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorSpawn(&umicomGuestSupervisor, parent, start, bytes,
        argument, 512U, policy, &handle) == UMICOM_SUPERVISION_OK, "admit independently loaded image");
    return handle;
}
static void UmicomSupervisionMode(UmicomKernelSupervisedProcessHandle handle, UmicomU64 mode)
{
    (void)UmicomSupervisionInfo(handle); /* Validate the generation before test-only inspection. */
    UmicomKernelProcess *const process = &umicomGuestSupervisor.scheduler.tasks[(UmicomU32)handle - 1U].process;
    /* The built-in diagnostic exposes this explicit observation page. Setting a
     * test mode through checked copying is not a production process-memory API. */
    UmicomSupervisionExpect(UmicomKernelUserMemoryWrite(&process->report.memory,
        UMICOM_IPC_OBSERVATION_ADDRESS, (const UmicomU8 *)&mode, sizeof(mode)) == UMICOM_USER_RESULT_OK,
        "set diagnostic mode before first entry");
}
static void UmicomSupervisionPair(UmicomKernelSupervisedProcessHandle receiver, UmicomKernelSupervisedProcessHandle sender)
{
    UmicomKernelMessageHandle receive = 0U, send = 0U;
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorConnect(&umicomGuestSupervisor, 0U,
        receiver, UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY,
        sender, UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_QUERY, &receive, &send)
        == UMICOM_SUPERVISION_OK, "connect endpoint owners");
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorSetArgument(&umicomGuestSupervisor, 0U, receiver, receive)
        == UMICOM_SUPERVISION_OK, "give receiver its fresh endpoint");
    /* The diagnostic fault/loop sender keeps its original argument. Its
     * endpoint is still owned and closed when the scheduler ends that task. */
}
static void UmicomSupervisionExchangePair(UmicomKernelSupervisedProcessHandle receiver, UmicomKernelSupervisedProcessHandle sender)
{
    UmicomKernelMessageHandle receive = 0U, send = 0U;
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorConnect(&umicomGuestSupervisor, 0U,
        receiver, UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY,
        sender, UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_QUERY, &receive, &send)
        == UMICOM_SUPERVISION_OK, "connect exchange endpoints");
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorSetArgument(&umicomGuestSupervisor, 0U, receiver, receive)
        == UMICOM_SUPERVISION_OK && UmicomKernelProcessSupervisorSetArgument(&umicomGuestSupervisor, 0U, sender, send)
        == UMICOM_SUPERVISION_OK, "publish endpoint arguments before entry");
}
static void UmicomSupervisionRun(UmicomKernelSupervisedProcessHandle desired)
{
    if (desired != 0U) {
        (void)UmicomSupervisionInfo(desired);
        /* Test-only deterministic order makes the child block before the parent
         * faults. Ordinary exchange below uses the unmodified round-robin order. */
        umicomGuestSupervisor.scheduler.next = (UmicomU32)desired - 1U;
    }
    UmicomKernelSupervisedProcessHandle selected = 0U;
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorRunOne(&umicomGuestSupervisor, 10000U, &selected)
        == UMICOM_SUPERVISION_OK, "captured supervised user quantum");
    if (desired != 0U) UmicomSupervisionExpect(selected == desired, "selected diagnostic task");
}
static void UmicomSupervisionBlock(UmicomKernelSupervisedProcessHandle handle)
{
    for (UmicomSize attempt = 0U; attempt < 128U; ++attempt) {
        const UmicomKernelUserTaskState state = UmicomSupervisionInfo(handle)->state;
        if (state == UMICOM_USER_TASK_BLOCKED) return;
        UmicomSupervisionExpect(state == UMICOM_USER_TASK_READY || state == UMICOM_USER_TASK_PAUSED,
            "receiver remains runnable until its wait");
        UmicomSupervisionRun(handle);
    }
    UmicomSupervisionExpect(UMICOM_FALSE, "bounded wait registration");
}
static void UmicomSupervisionFinish(UmicomKernelSupervisedProcessHandle handle, UmicomKernelUserTaskState expected)
{
    for (UmicomSize attempt = 0U; attempt < 256U; ++attempt) {
        const UmicomKernelSupervisedProcessInfo *const info = UmicomSupervisionInfo(handle);
        if (info->terminal != UMICOM_FALSE) {
            UmicomSupervisionExpect(info->state == expected, "correct terminal classification"); return;
        }
        UmicomSupervisionExpect(info->state == UMICOM_USER_TASK_READY || info->state == UMICOM_USER_TASK_PAUSED,
            "terminal check does not busy-dispatch a blocked task");
        UmicomSupervisionRun(handle);
    }
    UmicomSupervisionExpect(UMICOM_FALSE, "bounded task completion");
}
static const UmicomKernelProcessCompletion *UmicomSupervisionCollect(UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle)
{
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorCollect(&umicomGuestSupervisor, caller, handle, &umicomSupervisionCollected)
        == UMICOM_SUPERVISION_OK, "collect terminal result after image teardown");
    return &umicomSupervisionCollected;
}
static void UmicomSupervisionCancelCollect(UmicomKernelSupervisedProcessHandle handle)
{
    if (UmicomSupervisionInfo(handle)->terminal == UMICOM_FALSE)
        UmicomSupervisionExpect(UmicomKernelProcessSupervisorCancel(&umicomGuestSupervisor, 0U, handle)
            == UMICOM_SUPERVISION_OK, "cancel before reclamation");
    (void)UmicomSupervisionCollect(0U, handle);
}
static void UmicomSupervisionEmpty(void)
{
    UmicomKernelSupervisionSnapshot info;
    UmicomSupervisionGuestClear(&info, sizeof(info));
    UmicomKernelMessageSnapshot messages;
    UmicomSupervisionGuestClear(&messages, sizeof(messages));
    UmicomKernelUserIpcInfo waits;
    UmicomSupervisionGuestClear(&waits, sizeof(waits));
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorSnapshot(&umicomGuestSupervisor, &info) == UMICOM_SUPERVISION_OK &&
        info.children == 0U && info.retainedLoads == 0U, "no lost child owner");
    UmicomSupervisionExpect(UmicomKernelMessageSnapshotRead(&umicomGuestSupervisor.ipc.messages, &messages) == UMICOM_MESSAGE_OK &&
        messages.channels == 0U && messages.handles == 0U && messages.messages == 0U, "private channels returned");
    UmicomSupervisionExpect(UmicomKernelUserIpcSnapshot(&umicomGuestSupervisor.ipc, &waits) && waits.waiting == 0U,
        "no stale wait refers to a reclaimed frame");
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorValidate(&umicomGuestSupervisor) == UMICOM_SUPERVISION_OK,
        "family and lower-owner invariants");
    UmicomKernelConsoleWriteLine("process-supervision.case-result=pass");
}
static UmicomBoolean UmicomSupervisionMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec && a->mscratch == b->mscratch &&
        a->medeleg == b->medeleg && a->mideleg == b->mideleg && a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 &&
        a->pmpaddr0 == b->pmpaddr0 && a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
void UmicomKernelProcessSupervisionValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("process-supervision-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomSupervisionGuestClear(&before, sizeof(before));
    UmicomSupervisionGuestClear(&after, sizeof(after));
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomSupervisionGuestClear(&machineBefore, sizeof(machineBefore));
    UmicomSupervisionGuestClear(&machineAfter, sizeof(machineAfter));
    UmicomKernelPhysicalMemorySnapshotRead(&before);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    const UmicomU64 timerBefore = UmicomPlatformTimerCompareRead(0U);
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorInitialize(&umicomGuestSupervisor) == UMICOM_SUPERVISION_OK,
        "initialise private supervision domain");

    UmicomSupervisionCase("parent-authority-and-result-collection");
    UmicomKernelSupervisedProcessHandle parent = UmicomSupervisionCreate(0U, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    const UmicomU64 parentId = UmicomSupervisionInfo(parent)->identity;
    UmicomKernelSupervisedProcessHandle child = UmicomSupervisionCreate(parentId, 7U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    UmicomKernelSupervisedProcessHandle sibling = UmicomSupervisionCreate(0U, 11U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    const UmicomU64 childId = UmicomSupervisionInfo(child)->identity;
    UmicomKernelProcessCompletion completion;
    UmicomSupervisionGuestClear(&completion, sizeof(completion));
    completion.identity = 0xfeedU;
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorCollect(&umicomGuestSupervisor, parentId, child, &completion)
        == UMICOM_SUPERVISION_NOT_TERMINAL && completion.identity == 0xfeedU, "live collection refused without output changes");
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorCancel(&umicomGuestSupervisor, UmicomSupervisionInfo(sibling)->identity, child)
        == UMICOM_SUPERVISION_WRONG_PARENT, "sibling has no child authority");
    UmicomSupervisionFinish(child, UMICOM_USER_TASK_EXITED);
    UmicomSupervisionExpect(UmicomSupervisionInfo(child)->completion.exitValue == childId + 40U,
        "independent diagnostic returns its identity and argument");
    const UmicomKernelProcessCompletion *const collected = UmicomSupervisionCollect(parentId, child);
    UmicomSupervisionExpect(collected->identity == childId && collected->birthParent == parentId,
        "collector gets original child evidence");
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorCollect(&umicomGuestSupervisor, 0U, child, &completion)
        == UMICOM_SUPERVISION_INVALID_HANDLE, "collection happens once");
    UmicomSupervisionFinish(sibling, UMICOM_USER_TASK_EXITED);
    UmicomSupervisionCancelCollect(sibling); UmicomSupervisionCancelCollect(parent); UmicomSupervisionEmpty();

    UmicomSupervisionCase("parent-fault-cancels-descendants-not-siblings");
    parent = UmicomSupervisionCreate(0U, 2U, UMICOM_CHILDREN_CANCEL_TREE, UMICOM_FALSE);
    child = UmicomSupervisionCreate(UmicomSupervisionInfo(parent)->identity, 0U, UMICOM_CHILDREN_ADOPT, UMICOM_TRUE);
    UmicomSupervisionMode(child, UMICOM_IPC_MODE_PEER_CLOSE);
    UmicomKernelSupervisedProcessHandle grandchild = UmicomSupervisionCreate(UmicomSupervisionInfo(child)->identity,
        3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    sibling = UmicomSupervisionCreate(0U, 17U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    UmicomSupervisionPair(child, parent); UmicomSupervisionBlock(child);
    UmicomSupervisionFinish(parent, UMICOM_USER_TASK_FAULTED);
    UmicomSupervisionExpect(UmicomSupervisionInfo(child)->state == UMICOM_USER_TASK_CANCELLED &&
        UmicomSupervisionInfo(grandchild)->state == UMICOM_USER_TASK_CANCELLED &&
        UmicomSupervisionInfo(child)->completion.reason == UMICOM_SUPERVISION_PARENT_STOP &&
        UmicomSupervisionInfo(sibling)->state == UMICOM_USER_TASK_READY, "only the dependent family was stopped");
    UmicomSupervisionFinish(sibling, UMICOM_USER_TASK_EXITED);
    UmicomSupervisionCancelCollect(parent); UmicomSupervisionCancelCollect(child);
    UmicomSupervisionCancelCollect(grandchild); UmicomSupervisionCancelCollect(sibling); UmicomSupervisionEmpty();
    UmicomKernelConsoleWriteLine("process-supervision.sibling-isolation=pass");

    UmicomSupervisionCase("adopt-blocked-child-and-resume-peer-close");
    parent = UmicomSupervisionCreate(0U, 9U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    const UmicomU64 birthParent = UmicomSupervisionInfo(parent)->identity;
    child = UmicomSupervisionCreate(birthParent, 0U, UMICOM_CHILDREN_ADOPT, UMICOM_TRUE);
    UmicomSupervisionMode(child, UMICOM_IPC_MODE_PEER_CLOSE);
    sibling = UmicomSupervisionCreate(0U, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    UmicomSupervisionPair(child, sibling); UmicomSupervisionBlock(child);
    UmicomSupervisionFinish(parent, UMICOM_USER_TASK_EXITED);
    UmicomSupervisionExpect(UmicomSupervisionInfo(child)->state == UMICOM_USER_TASK_BLOCKED &&
        UmicomSupervisionInfo(child)->parent == 0U && UmicomSupervisionInfo(child)->birthParent == birthParent,
        "adoption preserves blocked continuation and provenance");
    UmicomSupervisionCancelCollect(parent); UmicomSupervisionCancelCollect(sibling);
    UmicomSupervisionFinish(child, UMICOM_USER_TASK_EXITED);
    UmicomSupervisionExpect(UmicomSupervisionInfo(child)->completion.exitValue == 0x7300U,
        "adopted child returned from its original waiting syscall");
    UmicomSupervisionCancelCollect(child); UmicomSupervisionEmpty();
    UmicomKernelConsoleWriteLine("process-supervision.orphan-adoption=pass");

    UmicomSupervisionCase("explicit-subtree-stop");
    parent = UmicomSupervisionCreate(0U, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    child = UmicomSupervisionCreate(UmicomSupervisionInfo(parent)->identity, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    grandchild = UmicomSupervisionCreate(UmicomSupervisionInfo(child)->identity, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    sibling = UmicomSupervisionCreate(0U, 5U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorStopTree(&umicomGuestSupervisor, 0U, parent) == UMICOM_SUPERVISION_OK &&
        UmicomSupervisionInfo(grandchild)->state == UMICOM_USER_TASK_CANCELLED &&
        UmicomSupervisionInfo(sibling)->state == UMICOM_USER_TASK_READY, "tree stop includes adopted-policy grandchildren");
    UmicomSupervisionCancelCollect(parent); UmicomSupervisionCancelCollect(child);
    UmicomSupervisionCancelCollect(grandchild); UmicomSupervisionCancelCollect(sibling); UmicomSupervisionEmpty();

    UmicomSupervisionCase("supervised-producer-consumer-lifetime");
    child = UmicomSupervisionCreate(0U, 0U, UMICOM_CHILDREN_ADOPT, UMICOM_TRUE);
    sibling = UmicomSupervisionCreate(0U, 0U, UMICOM_CHILDREN_ADOPT, UMICOM_TRUE);
    UmicomSupervisionExchangePair(child, sibling); UmicomSupervisionBlock(child); UmicomSupervisionBlock(sibling);
    for (UmicomSize dispatch = 0U; dispatch < 768U && UmicomSupervisionInfo(sibling)->terminal == UMICOM_FALSE; ++dispatch)
        UmicomSupervisionRun(0U);
    UmicomSupervisionExpect(UmicomSupervisionInfo(sibling)->state == UMICOM_USER_TASK_EXITED &&
        UmicomSupervisionInfo(sibling)->completion.exitValue == 0x7101U, "producer completed real waiting sends");
    /* Accepted packets are channel-owned. Reclaiming the producer before the
     * consumer finishes must not revoke bytes already accepted for delivery. */
    UmicomSupervisionCancelCollect(sibling); UmicomSupervisionFinish(child, UMICOM_USER_TASK_EXITED);
    UmicomSupervisionExpect(UmicomSupervisionInfo(child)->completion.exitValue == 0x7102U,
        "consumer verified every copied packet after producer collection");
    UmicomSupervisionCancelCollect(child); UmicomSupervisionEmpty();
    UmicomKernelConsoleWriteLine("process-supervision.ipc-cleanup-and-delivery=pass");

    UmicomSupervisionCase("shutdown-retains-terminal-evidence");
    parent = UmicomSupervisionCreate(0U, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    child = UmicomSupervisionCreate(UmicomSupervisionInfo(parent)->identity, 3U, UMICOM_CHILDREN_ADOPT, UMICOM_FALSE);
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorBeginShutdown(&umicomGuestSupervisor) == UMICOM_SUPERVISION_OK &&
        UmicomKernelProcessSupervisorBeginShutdown(&umicomGuestSupervisor) == UMICOM_SUPERVISION_OK,
        "shutdown is idempotent and does not discard reports");
    UmicomKernelSupervisionSnapshot snapshot;
    UmicomSupervisionGuestClear(&snapshot, sizeof(snapshot));
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorSnapshot(&umicomGuestSupervisor, &snapshot) == UMICOM_SUPERVISION_OK &&
        snapshot.admissionClosed && snapshot.terminal == 2U && snapshot.live == 0U, "closed admission with collectable records");
    UmicomKernelSupervisedProcessHandle refused = 0xfeedU;
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorSpawn(&umicomGuestSupervisor, 0U, UmicomEmbeddedExecutableStart,
        (UmicomSize)((UmicomAddress)UmicomEmbeddedExecutableEnd - (UmicomAddress)UmicomEmbeddedExecutableStart),
        0U, 64U, UMICOM_CHILDREN_ADOPT, &refused) == UMICOM_SUPERVISION_ADMISSION_CLOSED && refused == 0xfeedU,
        "shutdown cannot admit a replacement image");
    for (UmicomSize count = 0U; count < 2U; ++count)
        UmicomSupervisionExpect(UmicomKernelProcessSupervisorCollectAny(&umicomGuestSupervisor, 0U, &completion)
            == UMICOM_SUPERVISION_OK && completion.reason == UMICOM_SUPERVISION_SHUTDOWN, "collect shutdown evidence");
    UmicomSupervisionExpect(UmicomKernelProcessSupervisorCollectAny(&umicomGuestSupervisor, 0U, &completion)
        == UMICOM_SUPERVISION_NO_CHILDREN, "no terminal record published twice"); UmicomSupervisionEmpty();

    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomSupervisionExpect(UmicomSupervisionMachineEqual(&machineBefore, &machineAfter) &&
        timerBefore == UmicomPlatformTimerCompareRead(0U), "exact machine controls returned");
    UmicomRiscvTrapSnapshot trapBefore, trapAfter;
    UmicomSupervisionGuestClear(&trapBefore, sizeof(trapBefore));
    UmicomSupervisionGuestClear(&trapAfter, sizeof(trapAfter));
    UmicomRiscvTrapSnapshotRead(&trapBefore); UmicomRiscvTriggerMachineEcall(); UmicomRiscvTrapSnapshotRead(&trapAfter);
    UmicomSupervisionExpect(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U && trapAfter.lastCauseCode == 11U,
        "original machine trap policy remains available");
    UmicomKernelPhysicalMemorySnapshotRead(&after);
    UmicomSupervisionExpect(before.freeFrames == after.freeFrames && before.allocatedFrames == after.allocatedFrames &&
        before.reservedFrames == after.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "all temporary image and page-table frames returned");
    UmicomKernelConsoleWriteLine("process-supervision.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("process-supervision.machine-state=restored");
    UmicomKernelConsoleWriteLine("process-supervision.frame-accounting=restored");
    UmicomKernelConsoleWrite("process-supervision.completed-cases=");
    UmicomKernelConsoleWriteUnsigned(umicomSupervisionCases); UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWrite("process-supervision.completed-checks=");
    UmicomKernelConsoleWriteUnsigned(umicomSupervisionChecks); UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("process-supervision-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PROCESS_SUPERVISION_READY");
}
