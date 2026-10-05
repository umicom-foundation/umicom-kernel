/*-----------------------------------------------------------------------------
 * Umicom Kernel process supervision native checks
 * File: tests/process_supervision/supervision_tests.c
 *
 * PURPOSE:
 *   Exercise real ownership, authority and teardown policy while using explicit
 *   host models for privileged entry and timer state.
 *
 * EDUCATIONAL NOTE:
 *   Reuse the earlier synthetic ELF/RAM/CSR fixture instead of cloning it.
 *   Its entry gets a local name so a chosen waiting ECALL can be added here.
 *   Neither that fixture nor these tests are evidence of RISC-V execution.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#define main UmicomSupervisionEarlierSchedulingMain
#define UmicomRiscvUserExecuteFrame UmicomSupervisionEarlierEntry
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#include "umicom/kernel/process_supervisor.h"

static UmicomKernelProcessSupervisor umicomTestSupervisor;
static UmicomKernelSupervisedProcessHandle umicomTestCurrent;
static int umicomTestReentry;
static unsigned umicomTestFreeCalls;
static unsigned umicomTestFailFree;
static UmicomU64 umicomTestCall;
static UmicomU64 umicomTestArguments[4];
#define UMICOM_SUPERVISION_MODEL_CALL 100U
#define UMICOM_SUPERVISION_BUFFER ((UmicomAddress)0x600ff0U)

/* One deliberately refused release lets the real destructor demonstrate its
 * partial-progress behaviour. It is not a replacement bitmap allocator. */
UmicomKernelMemoryStatus UmicomSupervisionActualFrameFree(UmicomAddress address);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress address)
{
    ++umicomTestFreeCalls;
    if (umicomTestFailFree != 0U && umicomTestFreeCalls == umicomTestFailFree)
        return UMICOM_KERNEL_MEMORY_NOT_ALLOCATED;
    return UmicomSupervisionActualFrameFree(address);
}

UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    if (umicomTestReentry) {
        UmicomKernelSupervisedProcessInfo info = {0};
        UmicomKernelSupervisedProcessHandle ignored = 0U;
        UmicomKernelProcessCompletion completion = {0};
        UmicomModelRequire(UmicomKernelProcessSupervisorQuery(&umicomTestSupervisor, 0U, umicomTestCurrent, &info)
            == UMICOM_SUPERVISION_BUSY, "reentrant query refused during user entry");
        UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, 0U, umicomTestCurrent)
            == UMICOM_SUPERVISION_BUSY, "running context cannot be cancelled");
        UmicomModelRequire(UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, 0U, umicomTestCurrent, &completion)
            == UMICOM_SUPERVISION_BUSY, "running image cannot be collected");
        UmicomModelRequire(UmicomKernelProcessSupervisorRunOne(&umicomTestSupervisor, 1000U, &ignored)
            == UMICOM_SUPERVISION_BUSY, "nested dispatch refused");
    }
    if (umicomModelMode != UMICOM_SUPERVISION_MODEL_CALL)
        return UmicomSupervisionEarlierEntry(request, session, frame);
    /* Supply one actual C-dispatched syscall. If it completes, a model timer
     * returns control; if it blocks, its retained frame is returned directly. */
    ++umicomModelEntries;
    frame->mstatus = (UmicomU64)2U << 32U;
    frame->reserved = 0U; frame->mcause = 8U; frame->mtval = 0U;
    frame->x17_a7 = umicomTestCall;
    frame->x10_a0 = umicomTestArguments[0]; frame->x11_a1 = umicomTestArguments[1];
    frame->x12_a2 = umicomTestArguments[2]; frame->x13_a3 = umicomTestArguments[3];
    if (UmicomKernelUserTrapDispatch(session, frame) == 0U) return 0U;
    umicomModelNow = umicomModelCompare;
    frame->mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
    UmicomModelRequire(UmicomKernelUserTrapDispatch(session, frame) == 0U, "captured model quantum");
    return 0U;
}

static UmicomKernelSupervisedProcessHandle UmicomTestSpawn(UmicomU64 parent,
    UmicomKernelChildLifetime policy, UmicomU64 budget)
{
    UmicomKernelSupervisedProcessHandle handle = 0U;
    UmicomModelRequire(UmicomKernelProcessSupervisorSpawn(&umicomTestSupervisor, parent,
        umicomModelElf, sizeof(umicomModelElf), 17U, budget, policy, &handle) == UMICOM_SUPERVISION_OK,
        "load a supervised child through the actual loader");
    return handle;
}
static UmicomKernelSupervisedProcessInfo UmicomTestInfo(UmicomKernelSupervisedProcessHandle handle)
{
    UmicomKernelSupervisedProcessInfo info = {0};
    UmicomModelRequire(UmicomKernelProcessSupervisorQuery(&umicomTestSupervisor, 0U, handle, &info)
        == UMICOM_SUPERVISION_OK, "guardian value query");
    return info;
}
static UmicomKernelUserTask *UmicomTestTask(UmicomKernelSupervisedProcessHandle handle)
{
    /* Test-only inspection controls deterministic failure/ordering scenarios. */
    return &umicomTestSupervisor.scheduler.tasks[(UmicomU32)handle - 1U];
}
static UmicomKernelSupervisionStatus UmicomTestRun(UmicomKernelSupervisedProcessHandle handle, unsigned mode)
{
    umicomTestCurrent = handle;
    umicomTestSupervisor.scheduler.next = (UmicomU32)handle - 1U;
    umicomModelMode = mode;
    UmicomKernelSupervisedProcessHandle selected = 0U;
    const UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorRunOne(&umicomTestSupervisor, 1000U, &selected);
    if (status == UMICOM_SUPERVISION_OK) UmicomModelRequire(selected == handle, "chosen model task ran");
    return status;
}
static void UmicomTestCancel(UmicomKernelSupervisedProcessHandle handle)
{
    UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, 0U, handle)
        == UMICOM_SUPERVISION_OK, "request cancellation");
}
static UmicomKernelProcessCompletion UmicomTestCollect(UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle)
{
    UmicomKernelProcessCompletion completion = {0};
    UmicomModelRequire(UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, caller, handle, &completion)
        == UMICOM_SUPERVISION_OK, "collect only after lower cleanup succeeds");
    return completion;
}
static void UmicomTestPair(UmicomKernelSupervisedProcessHandle receiver, UmicomKernelSupervisedProcessHandle sender,
    UmicomKernelMessageHandle *receive, UmicomKernelMessageHandle *send)
{
    UmicomModelRequire(UmicomKernelProcessSupervisorConnect(&umicomTestSupervisor, 0U,
        receiver, UMICOM_MESSAGE_RIGHT_RECEIVE | UMICOM_MESSAGE_RIGHT_QUERY,
        sender, UMICOM_MESSAGE_RIGHT_SEND | UMICOM_MESSAGE_RIGHT_QUERY, receive, send)
        == UMICOM_SUPERVISION_OK, "connect only authorised fresh children");
}
static void UmicomTestCall(UmicomKernelSupervisedProcessHandle task, UmicomU64 call,
    UmicomKernelMessageHandle endpoint, UmicomU64 bytes)
{
    umicomTestCall = call;
    umicomTestArguments[0] = endpoint; umicomTestArguments[1] = UMICOM_SUPERVISION_BUFFER;
    umicomTestArguments[2] = bytes; umicomTestArguments[3] = UMICOM_IPC_WAIT_FOREVER;
    UmicomModelRequire(UmicomTestRun(task, UMICOM_SUPERVISION_MODEL_CALL) == UMICOM_SUPERVISION_OK,
        "actual C message call is dispatched");
}
static void UmicomTestFinish(void)
{
    UmicomModelRequire(UmicomKernelProcessSupervisorBeginShutdown(&umicomTestSupervisor) == UMICOM_SUPERVISION_OK,
        "close admission and stop remaining work");
    UmicomKernelProcessCompletion completion = {0};
    while (UmicomKernelProcessSupervisorCollectAny(&umicomTestSupervisor, 0U, &completion) == UMICOM_SUPERVISION_OK) { }
    UmicomSize reaped = 0U;
    UmicomModelRequire(UmicomKernelProcessSupervisorReapRetained(&umicomTestSupervisor, &reaped) == UMICOM_SUPERVISION_OK,
        "retry unpublished cleanup");
    UmicomKernelSupervisionSnapshot snapshot = {0};
    UmicomKernelMessageSnapshot messages = {0};
    UmicomKernelUserIpcInfo waits = {0};
    UmicomModelRequire(UmicomKernelProcessSupervisorSnapshot(&umicomTestSupervisor, &snapshot) == UMICOM_SUPERVISION_OK &&
        snapshot.children == 0U && snapshot.retainedLoads == 0U, "no child/result ownership remains");
    UmicomModelRequire(UmicomKernelMessageSnapshotRead(&umicomTestSupervisor.ipc.messages, &messages) == UMICOM_MESSAGE_OK &&
        messages.channels == 0U && messages.handles == 0U && messages.messages == 0U, "no endpoint or queue leaked");
    UmicomModelRequire(UmicomKernelUserIpcSnapshot(&umicomTestSupervisor.ipc, &waits) && waits.waiting == 0U,
        "no abandoned waiter");
    UmicomModelRequire(UmicomKernelProcessSupervisorValidate(&umicomTestSupervisor) == UMICOM_SUPERVISION_OK,
        "complete owner graph validates");
    UmicomModelNoLeaks();
}

static int UmicomSupervisionCase(const char *name)
{
    UmicomModelSetup();
    /* Two user data pages exercise a deferred destination which crosses a page. */
    UmicomModelPut(64U + 2U * 56U + 40U, 8192U, 8U);
    UmicomModelRequire(UmicomKernelProcessSupervisorInitialize(&umicomTestSupervisor) == UMICOM_SUPERVISION_OK,
        "initialise supervisor at stable storage");
    UmicomKernelSupervisedProcessHandle untouched = 0x12345678U;
    UmicomKernelProcessCompletion completion = { .identity = 0xabcU };
    if (!strcmp(name, "null-init")) {
        UmicomModelRequire(UmicomKernelProcessSupervisorInitialize(NULL) == UMICOM_SUPERVISION_INVALID_ARGUMENT &&
            UmicomKernelProcessSupervisorPump(NULL) == UMICOM_SUPERVISION_INVALID_ARGUMENT, "null owners refused");
    } else if (!strcmp(name, "repeated-init")) {
        UmicomModelRequire(UmicomKernelProcessSupervisorInitialize(&umicomTestSupervisor) == UMICOM_SUPERVISION_BAD_STATE,
            "no live reinitialisation");
    } else if (!strcmp(name, "copied-owner")) {
        UmicomKernelProcessSupervisor *copy = malloc(sizeof(*copy)); UmicomModelRequire(copy != NULL, "host test allocation");
        *copy = umicomTestSupervisor;
        UmicomModelRequire(UmicomKernelProcessSupervisorPump(copy) == UMICOM_SUPERVISION_BAD_STATE, "copy rejected"); free(copy);
    } else if (!strcmp(name, "invalid-spawn") || !strcmp(name, "unknown-parent")) {
        const UmicomU64 parent = !strcmp(name, "unknown-parent") ? 999U : 0U;
        UmicomModelRequire(UmicomKernelProcessSupervisorSpawn(&umicomTestSupervisor, parent,
            umicomModelElf, sizeof(umicomModelElf), 0U, parent ? 64U : 0U, UMICOM_CHILDREN_ADOPT, &untouched)
            == (parent ? UMICOM_SUPERVISION_WRONG_PARENT : UMICOM_SUPERVISION_INVALID_ARGUMENT) &&
            untouched == 0x12345678U && UmicomModelMemory().allocatedFrames == 0U, "refusal publishes no child");
    } else if (!strcmp(name, "capacity")) {
        for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) (void)UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
        const UmicomU64 before = UmicomModelMemory().allocatedFrames;
        UmicomModelRequire(UmicomKernelProcessSupervisorSpawn(&umicomTestSupervisor, 0U, umicomModelElf, sizeof(umicomModelElf),
            0U, 64U, UMICOM_CHILDREN_ADOPT, &untouched) == UMICOM_SUPERVISION_CAPACITY &&
            untouched == 0x12345678U && before == UmicomModelMemory().allocatedFrames, "no eviction at admission limit");
    } else if (!strcmp(name, "collect-any-empty")) {
        UmicomModelRequire(UmicomKernelProcessSupervisorCollectAny(&umicomTestSupervisor, 0U, &completion)
            == UMICOM_SUPERVISION_NO_CHILDREN && completion.identity == 0xabcU, "no child differs from a live child");
    } else if (!strcmp(name, "shutdown") || !strcmp(name, "shutdown-idempotent") || !strcmp(name, "shutdown-rejects-spawn")) {
        const UmicomKernelSupervisedProcessHandle a = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
        (void)UmicomTestSpawn(UmicomTestInfo(a).identity, UMICOM_CHILDREN_ADOPT, 64U);
        UmicomModelRequire(UmicomKernelProcessSupervisorBeginShutdown(&umicomTestSupervisor) == UMICOM_SUPERVISION_OK &&
            UmicomKernelProcessSupervisorBeginShutdown(&umicomTestSupervisor) == UMICOM_SUPERVISION_OK,
            "shutdown idempotently retains results");
        UmicomModelRequire(UmicomTestInfo(a).completion.reason == UMICOM_SUPERVISION_SHUTDOWN, "shutdown reason retained");
        UmicomModelRequire(UmicomKernelProcessSupervisorSpawn(&umicomTestSupervisor, 0U, umicomModelElf, sizeof(umicomModelElf),
            0U, 64U, UMICOM_CHILDREN_ADOPT, &untouched) == UMICOM_SUPERVISION_ADMISSION_CLOSED, "closed admission stays closed");
    } else if (!strcmp(name, "rollback-budgets") || !strcmp(name, "retained-load")) {
        /* Spend free space deliberately, without mocking successful allocation.
         * A one-frame budget also forces rollback of a newly allocated root. */
        const UmicomSize limit = !strcmp(name, "retained-load") ? 2U : 25U;
        for (UmicomSize available = 1U; available < limit; ++available) {
            const UmicomAddress reserved = (UmicomAddress)umicomModelRam + available * UMICOM_KERNEL_PAGE_SIZE;
            const UmicomSize bytes = sizeof(umicomModelRam) - available * UMICOM_KERNEL_PAGE_SIZE;
            UmicomModelRequire(UmicomKernelPhysicalMemoryReserveRange(reserved, bytes) == UMICOM_KERNEL_MEMORY_OK,
                "reserve model budget");
            if (!strcmp(name, "retained-load")) { umicomTestFreeCalls = 0U; umicomTestFailFree = 1U; }
            const UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorSpawn(&umicomTestSupervisor, 0U,
                umicomModelElf, sizeof(umicomModelElf), 0U, 64U, UMICOM_CHILDREN_ADOPT, &untouched);
            umicomTestFailFree = 0U;
            if (status == UMICOM_SUPERVISION_OK) { UmicomTestCancel(untouched); (void)UmicomTestCollect(0U, untouched); }
            else UmicomModelRequire(status == UMICOM_SUPERVISION_LOAD_FAILED, "bounded load refusal");
            if (!strcmp(name, "retained-load")) {
                UmicomKernelSupervisionSnapshot snapshot = {0};
                UmicomModelRequire(UmicomKernelProcessSupervisorSnapshot(&umicomTestSupervisor, &snapshot) == UMICOM_SUPERVISION_OK &&
                    snapshot.retainedLoads == 1U && snapshot.children == 0U, "failed rollback retains unpublished owner");
            }
            UmicomSize recovered = 0U;
            UmicomModelRequire(UmicomKernelProcessSupervisorReapRetained(&umicomTestSupervisor, &recovered) == UMICOM_SUPERVISION_OK,
                "retained rollback recovery");
            UmicomModelRequire(UmicomKernelPhysicalMemoryReleaseReservedRange(reserved, bytes) == UMICOM_KERNEL_MEMORY_OK,
                "restore model budget"); UmicomModelNoLeaks();
        }
    } else if (!strcmp(name, "repeated-lifetimes") || !strcmp(name, "stale-generation")) {
        UmicomKernelSupervisedProcessHandle stale = 0U;
        const unsigned count = !strcmp(name, "stale-generation") ? 2U : 1000U;
        for (unsigned i = 0U; i < count; ++i) {
            const UmicomKernelSupervisedProcessHandle child = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
            UmicomModelRequire(child != stale, "new generation after collection");
            if (stale != 0U) UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, 0U, stale)
                == UMICOM_SUPERVISION_INVALID_HANDLE, "old handle cannot select new image");
            UmicomTestCancel(child); (void)UmicomTestCollect(0U, child); stale = child; UmicomModelNoLeaks();
        }
    } else if (!strcmp(name, "cleanup-retry") || !strcmp(name, "partial-cleanup-retry")) {
        /* Fail every release boundary in turn, including partially dismantled
         * page tables and partially scrubbed image ownership. */
        unsigned limit = 1U;
        for (unsigned failure = 1U; failure <= limit; ++failure) {
            const UmicomKernelSupervisedProcessHandle child = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
            UmicomModelRequire(UmicomTestRun(child, UMICOM_MODEL_EXIT) == UMICOM_SUPERVISION_OK, "terminal image for cleanup test");
            const UmicomKernelProcessCompletion original = UmicomTestInfo(child).completion;
            if (!strcmp(name, "partial-cleanup-retry")) limit = (unsigned)(UmicomTestTask(child)->process.space.pageTableFrames +
                UmicomTestTask(child)->process.pageCount);
            umicomTestFreeCalls = 0U; umicomTestFailFree = failure;
            UmicomModelRequire(UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, 0U, child, &completion)
                == UMICOM_SUPERVISION_CLEANUP_FAILED && completion.identity == 0xabcU,
                "failed collection preserves output and child");
            UmicomModelRequire(UmicomTestInfo(child).completion.exitValue == original.exitValue, "evidence survives failed teardown");
            umicomTestFailFree = 0U;
            const UmicomKernelProcessCompletion recovered = UmicomTestCollect(0U, child);
            UmicomModelRequire(recovered.identity == original.identity && recovered.exitValue == original.exitValue,
                "retry publishes original result once"); UmicomModelNoLeaks();
        }
    } else if (!strcmp(name, "cancel-blocked") || !strcmp(name, "unrelated-peer") || !strcmp(name, "message-exchange") ||
        !strcmp(name, "queued-message-survives") || !strcmp(name, "collect-ipc-cleanup") ||
        !strcmp(name, "bad-connect") || !strcmp(name, "connect-after-run")) {
        const UmicomKernelSupervisedProcessHandle receiver = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
        const UmicomKernelSupervisedProcessHandle sender = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
        UmicomKernelMessageHandle receive = 0U, send = 0U;
        if (!strcmp(name, "bad-connect")) {
            UmicomModelRequire(UmicomKernelProcessSupervisorConnect(&umicomTestSupervisor, 0U, receiver,
                0x8000U, sender, UMICOM_MESSAGE_RIGHT_SEND, &receive, &send) == UMICOM_SUPERVISION_IPC_REFUSED &&
                receive == 0U && send == 0U, "invalid rights publish no endpoints");
        } else if (!strcmp(name, "connect-after-run")) {
            UmicomModelRequire(UmicomTestRun(receiver, UMICOM_MODEL_TIMER) == UMICOM_SUPERVISION_OK, "pause receiver");
            UmicomModelRequire(UmicomKernelProcessSupervisorConnect(&umicomTestSupervisor, 0U, receiver,
                UMICOM_MESSAGE_RIGHT_RECEIVE, sender, UMICOM_MESSAGE_RIGHT_SEND, &receive, &send)
                == UMICOM_SUPERVISION_IPC_REFUSED, "IPC admission stays fresh-only");
        } else {
            UmicomTestPair(receiver, sender, &receive, &send);
            UmicomTestCall(receiver, UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT, receive, sizeof(UmicomKernelMessage));
            UmicomModelRequire(UmicomTestInfo(receiver).state == UMICOM_USER_TASK_BLOCKED, "actual receiver suspended");
            if (!strcmp(name, "message-exchange") || !strcmp(name, "queued-message-survives")) {
                UmicomU8 source[32]; memset(source, 0x65, sizeof(source));
                UmicomModelRequire(UmicomKernelUserMemoryWrite(&UmicomTestTask(sender)->process.report.memory,
                    UMICOM_SUPERVISION_BUFFER, source, sizeof(source)) == UMICOM_USER_RESULT_OK, "seed sender bytes");
                UmicomTestCall(sender, UMICOM_USER_CALL_MESSAGE_SEND_WAIT, send, sizeof(source));
                memset(source, 0x99, sizeof(source));
                UmicomModelRequire(UmicomKernelUserMemoryWrite(&UmicomTestTask(sender)->process.report.memory,
                    UMICOM_SUPERVISION_BUFFER, source, sizeof(source)) == UMICOM_USER_RESULT_OK, "overwrite sender source");
                UmicomTestCancel(sender); (void)UmicomTestCollect(0U, sender);
                UmicomU8 result[56] = {0};
                UmicomModelRequire(UmicomTestInfo(receiver).state == UMICOM_USER_TASK_PAUSED &&
                    UmicomKernelUserMemoryRead(&UmicomTestTask(receiver)->process.report.memory,
                        UMICOM_SUPERVISION_BUFFER, result, sizeof(result)) == UMICOM_USER_RESULT_OK && result[24] == 0x65U,
                    "accepted bytes survive sender collection");
            } else if (!strcmp(name, "unrelated-peer")) {
                const UmicomKernelSupervisedProcessHandle survivor = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
                UmicomTestCancel(sender);
                UmicomModelRequire(UmicomTestInfo(receiver).state == UMICOM_USER_TASK_PAUSED &&
                    UmicomTestTask(receiver)->frame.x10_a0 == UMICOM_MESSAGE_PEER_CLOSED &&
                    UmicomTestInfo(survivor).state == UMICOM_USER_TASK_READY, "peer closes without stopping unrelated sibling");
            } else {
                UmicomTestCancel(receiver); (void)UmicomTestCollect(0U, receiver);
                UmicomKernelUserIpcInfo waits = {0};
                UmicomModelRequire(UmicomKernelUserIpcSnapshot(&umicomTestSupervisor.ipc, &waits) && waits.waiting == 0U,
                    "wait descriptor removed before image reclaimed");
            }
        }
    } else {
        const UmicomKernelSupervisedProcessHandle parent = UmicomTestSpawn(0U,
            !strcmp(name, "cancel-descendants") || !strcmp(name, "grandchild-policy") ? UMICOM_CHILDREN_CANCEL_TREE : UMICOM_CHILDREN_ADOPT,
            !strcmp(name, "task-budget") ? 1U : 64U);
        const UmicomU64 principal = UmicomTestInfo(parent).identity;
        if (!strcmp(name, "normal-completion") || !strcmp(name, "completion-retained") || !strcmp(name, "collect-once")) {
            UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_EXIT) == UMICOM_SUPERVISION_OK, "capture exit");
            const UmicomU64 allocated = UmicomModelMemory().allocatedFrames;
            UmicomModelRequire(allocated != 0U && UmicomTestInfo(parent).completion.exitValue == principal + 73U,
                "exit retains pages and result");
            UmicomModelRequire(UmicomKernelProcessSupervisorPump(&umicomTestSupervisor) == UMICOM_SUPERVISION_OK &&
                allocated == UmicomModelMemory().allocatedFrames, "pump never implicitly reaps");
            (void)UmicomTestCollect(0U, parent);
            UmicomModelRequire(UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, 0U, parent, &completion)
                == UMICOM_SUPERVISION_INVALID_HANDLE && completion.identity == 0xabcU, "second collection refused");
        } else if (!strcmp(name, "collect-live") || !strcmp(name, "collect-any-waiting")) {
            const UmicomKernelSupervisionStatus status = !strcmp(name, "collect-live") ?
                UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, 0U, parent, &completion) :
                UmicomKernelProcessSupervisorCollectAny(&umicomTestSupervisor, 0U, &completion);
            UmicomModelRequire(status == (!strcmp(name, "collect-live") ? UMICOM_SUPERVISION_NOT_TERMINAL : UMICOM_SUPERVISION_WOULD_BLOCK) &&
                completion.identity == 0xabcU, "live continuation remains owned");
        } else if (!strcmp(name, "argument-before-run") || !strcmp(name, "argument-after-run")) {
            if (!strcmp(name, "argument-after-run")) UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_TIMER) == UMICOM_SUPERVISION_OK, "pause first");
            UmicomModelRequire(UmicomKernelProcessSupervisorSetArgument(&umicomTestSupervisor, 0U, parent, 92U)
                == (!strcmp(name, "argument-before-run") ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_BAD_STATE), "argument only before entry");
        } else if (!strcmp(name, "single-cancel") || !strcmp(name, "duplicate-cancel") || !strcmp(name, "cancel-paused")) {
            if (!strcmp(name, "cancel-paused")) UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_TIMER) == UMICOM_SUPERVISION_OK, "pause first");
            UmicomTestCancel(parent);
            UmicomModelRequire(UmicomTestInfo(parent).completion.reason == UMICOM_SUPERVISION_CANCEL_REQUEST &&
                UmicomTestInfo(parent).completion.trapCause == 0U, "cancellation is not a invented hardware fault");
            UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, 0U, parent) == UMICOM_SUPERVISION_BAD_STATE,
                "second cancel does not change result");
        } else if (!strcmp(name, "entry-refusal") || !strcmp(name, "reentrant-call") || !strcmp(name, "task-budget") ||
            !strcmp(name, "syscall-budget") || !strcmp(name, "invalid-context")) {
            if (!strcmp(name, "entry-refusal")) {
                umicomModelAllowed = UMICOM_FALSE;
                UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_TIMER) == UMICOM_SUPERVISION_ENTRY_REFUSED &&
                    UmicomTestTask(parent)->slices == 0U, "refusal spends no quantum"); umicomModelAllowed = UMICOM_TRUE;
            } else if (!strcmp(name, "reentrant-call")) {
                umicomTestReentry = 1; UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_EXIT) == UMICOM_SUPERVISION_OK, "outer entry succeeds");
            } else if (!strcmp(name, "task-budget")) {
                UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_TIMER) == UMICOM_SUPERVISION_OK &&
                    UmicomTestInfo(parent).state == UMICOM_USER_TASK_EXHAUSTED, "total quantum limit remains terminal");
            } else if (!strcmp(name, "syscall-budget")) {
                UmicomTestTask(parent)->process.report.callCount = UMICOM_USER_CALL_LIMIT;
                UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_CALL_TIMER) == UMICOM_SUPERVISION_OK &&
                    UmicomTestInfo(parent).state == UMICOM_USER_TASK_EXHAUSTED, "call exhaustion retained");
            } else {
                UmicomTestTask(parent)->frame.mepc = 0U;
                UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_TIMER) == UMICOM_SUPERVISION_LOWER_FAILURE &&
                    UmicomTestInfo(parent).terminal != UMICOM_FALSE, "invalid continuation ended without executing");
            }
        } else if (!strcmp(name, "machine-state-failure") || !strcmp(name, "poison-no-free") ||
            !strcmp(name, "corrupt-parent") || !strcmp(name, "corrupt-record")) {
            const UmicomU64 before = UmicomModelMemory().allocatedFrames;
            if (!strcmp(name, "corrupt-parent")) umicomTestSupervisor.children[0].parent = principal;
            else if (!strcmp(name, "corrupt-record")) umicomTestSupervisor.children[0].handle ^= (UmicomU64)1U << 32U;
            else UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_CORRUPT_MACHINE) == UMICOM_SUPERVISION_UNSAFE, "unverified return poisons owner");
            if (!strncmp(name, "corrupt-", 8U)) UmicomModelRequire(UmicomKernelProcessSupervisorPump(&umicomTestSupervisor)
                == UMICOM_SUPERVISION_CORRUPT_STATE, "damaged authority refused");
            UmicomModelRequire(UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, 0U, parent, &completion)
                == UMICOM_SUPERVISION_UNSAFE && UmicomModelMemory().allocatedFrames == before, "unsafe owner cannot free frames");
            return 2; /* Deliberately retained static model memory, not a cleanup pass. */
        } else if (!strcmp(name, "bad-output")) {
            UmicomModelRequire(UmicomKernelProcessSupervisorCollect(&umicomTestSupervisor, 0U, parent, NULL)
                == UMICOM_SUPERVISION_INVALID_ARGUMENT && UmicomKernelProcessSupervisorQuery(&umicomTestSupervisor, 0U, parent, NULL)
                == UMICOM_SUPERVISION_INVALID_ARGUMENT, "invalid outputs refused");
        } else {
            const UmicomKernelSupervisedProcessHandle child = UmicomTestSpawn(principal, UMICOM_CHILDREN_ADOPT, 64U);
            const UmicomU64 childId = UmicomTestInfo(child).identity;
            if (!strcmp(name, "child-authority")) {
                UmicomKernelSupervisedProcessInfo info = {0};
                UmicomModelRequire(UmicomKernelProcessSupervisorQuery(&umicomTestSupervisor, principal, child, &info) == UMICOM_SUPERVISION_OK,
                    "direct parent may query child");
                UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, principal, child) == UMICOM_SUPERVISION_OK,
                    "direct parent may cancel"); (void)UmicomTestCollect(principal, child);
            } else if (!strcmp(name, "self-refusal") || !strcmp(name, "ancestor-refusal")) {
                UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, childId,
                    !strcmp(name, "self-refusal") ? child : parent) == UMICOM_SUPERVISION_WRONG_PARENT,
                    "self or ancestor is not a child-owned object");
            } else if (!strcmp(name, "sibling-refusal")) {
                const UmicomKernelSupervisedProcessHandle sibling = UmicomTestSpawn(principal, UMICOM_CHILDREN_ADOPT, 64U);
                UmicomModelRequire(UmicomKernelProcessSupervisorCancel(&umicomTestSupervisor, childId, sibling)
                    == UMICOM_SUPERVISION_WRONG_PARENT, "sibling cannot cancel sibling");
            } else if (!strcmp(name, "collect-any-owned")) {
                UmicomModelRequire(UmicomTestRun(child, UMICOM_MODEL_EXIT) == UMICOM_SUPERVISION_OK, "child exited");
                UmicomModelRequire(UmicomKernelProcessSupervisorCollectAny(&umicomTestSupervisor, 0U, &completion)
                    == UMICOM_SUPERVISION_WOULD_BLOCK, "guardian collect-any scans its own direct children");
                UmicomModelRequire(UmicomKernelProcessSupervisorCollectAny(&umicomTestSupervisor, principal, &completion)
                    == UMICOM_SUPERVISION_OK && completion.identity == childId, "parent collects direct result");
            } else if (!strcmp(name, "adopt-on-exit") || !strcmp(name, "adopt-on-fault") || !strcmp(name, "terminal-parent")) {
                UmicomModelRequire(UmicomTestRun(parent, !strcmp(name, "adopt-on-fault") ? UMICOM_MODEL_FAULT : UMICOM_MODEL_EXIT)
                    == UMICOM_SUPERVISION_OK, "parent ended");
                UmicomModelRequire(UmicomTestInfo(child).parent == 0U && UmicomTestInfo(child).state == UMICOM_USER_TASK_READY,
                    "orphan survives under guardian");
                UmicomModelRequire(UmicomKernelProcessSupervisorSpawn(&umicomTestSupervisor, principal, umicomModelElf,
                    sizeof(umicomModelElf), 0U, 64U, UMICOM_CHILDREN_ADOPT, &untouched) == UMICOM_SUPERVISION_PARENT_ENDED,
                    "terminal parent cannot create children");
                (void)UmicomTestCollect(0U, parent);
                UmicomModelRequire(UmicomTestRun(child, UMICOM_MODEL_EXIT) == UMICOM_SUPERVISION_OK, "adopted continuation still runs");
            } else if (!strcmp(name, "cancel-descendants") || !strcmp(name, "grandchild-policy") || !strcmp(name, "stop-tree")) {
                const UmicomKernelSupervisedProcessHandle grandchild = UmicomTestSpawn(childId, UMICOM_CHILDREN_ADOPT, 64U);
                const UmicomKernelSupervisedProcessHandle sibling = UmicomTestSpawn(0U, UMICOM_CHILDREN_ADOPT, 64U);
                if (!strcmp(name, "stop-tree")) UmicomModelRequire(UmicomKernelProcessSupervisorStopTree(&umicomTestSupervisor, 0U, parent)
                    == UMICOM_SUPERVISION_OK, "explicit subtree stop");
                else UmicomModelRequire(UmicomTestRun(parent, UMICOM_MODEL_FAULT) == UMICOM_SUPERVISION_OK, "parent fault cascades");
                UmicomModelRequire(UmicomTestInfo(child).state == UMICOM_USER_TASK_CANCELLED &&
                    UmicomTestInfo(grandchild).state == UMICOM_USER_TASK_CANCELLED &&
                    UmicomTestInfo(sibling).state == UMICOM_USER_TASK_READY, "whole descendant tree stops but sibling survives");
            } else return 0;
        }
    }
    UmicomTestFinish();
    return 1;
}
int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    const int result = UmicomSupervisionCase(argv[1]);
    if (result == 0) { fprintf(stderr, "Unknown test: %s\n", argv[1]); return EXIT_FAILURE; }
    printf("PASS %s (%u checks)%s\n", argv[1], umicomModelChecks,
        result == 2 ? " - unsafe model retains ownership intentionally" : "");
    return EXIT_SUCCESS;
}
