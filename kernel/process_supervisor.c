/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/process_supervisor.c
 *
 * PURPOSE:
 *   Coordinate parent authority, orphan policy and terminal collection above
 *   the established scheduled-task and blocking-message owners.
 *
 * EDUCATIONAL OVERVIEW:
 *   This file never switches a register frame, frees an image page directly or
 *   implements a second message queue. It asks the owners which already do
 *   those jobs. Its own records answer who collects a result and what happens
 *   to a parent's descendants when that parent can no longer supervise them.
 *
 *   Every public operation is serial Kernel work between user quanta. There
 *   are only four scheduled slots, so bounded scans are easier to audit than
 *   allocation, intrusive lists or callbacks during partial cleanup.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process_supervisor.h"

static void UmicomSupervisionClear(void *destination, UmicomSize bytes)
{
    /* Clear only a record whose lower image has already been released. A live
     * scheduler must never be reset merely to make its statistics look empty. */
    volatile UmicomU8 *const output = (volatile UmicomU8 *)destination;
    for (UmicomSize index = 0U; index < bytes; ++index) output[index] = 0U;
}

static void UmicomSupervisionCopy(void *destination, const void *source, UmicomSize bytes)
{
    /* C structure assignment may require a hosted memcpy even in a freestanding
     * build. These non-overlapping Kernel snapshots use explicit byte accesses
     * so this ownership service brings no hidden runtime dependency with it. */
    volatile UmicomU8 *const output = (volatile UmicomU8 *)destination;
    const volatile UmicomU8 *const input = (const volatile UmicomU8 *)source;
    for (UmicomSize index = 0U; index < bytes; ++index) output[index] = input[index];
}

static UmicomKernelSupervisionStatus UmicomSupervisionReady(
    const UmicomKernelProcessSupervisor *supervisor, UmicomBoolean diagnostic)
{
    if (supervisor == (const UmicomKernelProcessSupervisor *)0)
        return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    /* The embedded owners contain interior pointers. Reject a structure copy
     * before following any of those pointers into the original object. */
    if (supervisor->initialised == UMICOM_FALSE || supervisor->self != supervisor ||
        supervisor->scheduler.self != &supervisor->scheduler ||
        supervisor->ipc.self != &supervisor->ipc || supervisor->scheduler.ipc != &supervisor->ipc ||
        supervisor->ipc.scheduler != &supervisor->scheduler)
        return UMICOM_SUPERVISION_BAD_STATE;
    if (supervisor->active != UMICOM_FALSE || supervisor->scheduler.active != UMICOM_FALSE)
        return UMICOM_SUPERVISION_BUSY;
    /* Read-only evidence remains useful after an unsafe return. It must not
     * become permission to run another task or reclaim an unverified root. */
    if (diagnostic == UMICOM_FALSE && (supervisor->poisoned != UMICOM_FALSE ||
        supervisor->scheduler.poisoned != UMICOM_FALSE)) return UMICOM_SUPERVISION_UNSAFE;
    return UMICOM_SUPERVISION_OK;
}

static UmicomKernelSupervisionStatus UmicomSupervisionCorrupt(UmicomKernelProcessSupervisor *supervisor)
{
    supervisor->poisoned = UMICOM_TRUE; /* Keep records available; stop mutation. */
    return UMICOM_SUPERVISION_CORRUPT_STATE;
}

static UmicomKernelSupervisedProcessRecord *UmicomSupervisionFind(
    UmicomKernelProcessSupervisor *supervisor, UmicomKernelSupervisedProcessHandle handle)
{
    if (handle == 0U) return (UmicomKernelSupervisedProcessRecord *)0;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        if (record->occupied != UMICOM_FALSE && record->handle == handle) return record;
    }
    return (UmicomKernelSupervisedProcessRecord *)0;
}

static UmicomKernelSupervisedProcessRecord *UmicomSupervisionIdentity(
    UmicomKernelProcessSupervisor *supervisor, UmicomU64 identity)
{
    if (identity == UMICOM_SUPERVISION_GUARDIAN) return (UmicomKernelSupervisedProcessRecord *)0;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        if (record->occupied != UMICOM_FALSE && record->identity == identity) return record;
    }
    return (UmicomKernelSupervisedProcessRecord *)0;
}

static UmicomKernelSupervisionStatus UmicomSupervisionGraph(UmicomKernelProcessSupervisor *supervisor)
{
    /* Parent authority is metadata, so validate it before a cleanup scan uses
     * it to select descendants. Monotonic identities rule out backwards cycles. */
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        const UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        if (record->occupied == UMICOM_FALSE) continue;
        if (record->handle == 0U || record->identity == 0U || record->parent >= record->identity ||
            record->birthParent >= record->identity ||
            (record->childLifetime != UMICOM_CHILDREN_ADOPT && record->childLifetime != UMICOM_CHILDREN_CANCEL_TREE))
            return UmicomSupervisionCorrupt(supervisor);
        if (record->parent != 0U && UmicomSupervisionIdentity(supervisor, record->parent) == (UmicomKernelSupervisedProcessRecord *)0)
            return UmicomSupervisionCorrupt(supervisor);
        for (UmicomSize previous = 0U; previous < index; ++previous)
            if (supervisor->children[previous].occupied != UMICOM_FALSE &&
                (supervisor->children[previous].identity == record->identity || supervisor->children[previous].handle == record->handle))
                return UmicomSupervisionCorrupt(supervisor);
    }
    return UMICOM_SUPERVISION_OK;
}

static UmicomBoolean UmicomSupervisionTerminal(UmicomKernelUserTaskState state)
{
    return state == UMICOM_USER_TASK_EXITED || state == UMICOM_USER_TASK_FAULTED ||
        state == UMICOM_USER_TASK_EXHAUSTED || state == UMICOM_USER_TASK_CANCELLED ||
        state == UMICOM_USER_TASK_ERROR ? UMICOM_TRUE : UMICOM_FALSE;
}

static UmicomKernelSupervisionStatus UmicomSupervisionCaller(
    UmicomKernelProcessSupervisor *supervisor, UmicomU64 caller)
{
    if (caller == UMICOM_SUPERVISION_GUARDIAN) return UMICOM_SUPERVISION_OK;
    UmicomKernelSupervisedProcessRecord *const parent = UmicomSupervisionIdentity(supervisor, caller);
    if (parent == (UmicomKernelSupervisedProcessRecord *)0) return UMICOM_SUPERVISION_WRONG_PARENT;
    UmicomKernelUserTaskInfo info;
    UmicomSupervisionClear(&info, sizeof(info));
    if (UmicomKernelUserTaskQuery(&supervisor->scheduler, parent->handle, &info) != UMICOM_USER_SCHEDULE_OK ||
        info.identity != caller) return UmicomSupervisionCorrupt(supervisor);
    /* A terminal or closing parent cannot acquire fresh authority between
     * recording its death and completing its children's adoption. */
    if (parent->terminal != UMICOM_FALSE || parent->stopRequested != UMICOM_FALSE ||
        UmicomSupervisionTerminal(info.state) != UMICOM_FALSE) return UMICOM_SUPERVISION_PARENT_ENDED;
    return UMICOM_SUPERVISION_OK;
}

static UmicomKernelSupervisionStatus UmicomSupervisionAuthority(
    UmicomKernelProcessSupervisor *supervisor, UmicomU64 caller,
    const UmicomKernelSupervisedProcessRecord *record)
{
    const UmicomKernelSupervisionStatus status = UmicomSupervisionCaller(supervisor, caller);
    if (status != UMICOM_SUPERVISION_OK) return status;
    return caller == UMICOM_SUPERVISION_GUARDIAN || caller == record->parent
        ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_WRONG_PARENT;
}

static UmicomKernelSupervisionStatus UmicomSupervisionObserve(UmicomKernelProcessSupervisor *supervisor)
{
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        if (record->occupied == UMICOM_FALSE) continue;
        UmicomKernelUserTaskInfo info;
        UmicomSupervisionClear(&info, sizeof(info));
        if (UmicomKernelUserTaskQuery(&supervisor->scheduler, record->handle, &info) != UMICOM_USER_SCHEDULE_OK ||
            info.identity != record->identity) return UmicomSupervisionCorrupt(supervisor);
        if (record->terminal != UMICOM_FALSE) continue; /* Never rewrite an earlier terminal result. */
        if (info.state == UMICOM_USER_TASK_READY || info.state == UMICOM_USER_TASK_PAUSED ||
            info.state == UMICOM_USER_TASK_BLOCKED) continue;
        if (UmicomSupervisionTerminal(info.state) == UMICOM_FALSE) return UmicomSupervisionCorrupt(supervisor);
        /* Lower execution has ended. IPC cleanup belongs to the scheduler; the
         * following value snapshot survives even an unsuccessful later Reap. */
        record->completion.identity = info.identity;
        record->completion.birthParent = record->birthParent;
        record->completion.state = info.state;
        record->completion.reason = info.state == UMICOM_USER_TASK_CANCELLED
            ? record->stopReason : UMICOM_SUPERVISION_PROGRAM_STOP;
        record->completion.exitValue = info.state == UMICOM_USER_TASK_EXITED ? info.exitValue : 0U;
        record->completion.trapCause = info.state == UMICOM_USER_TASK_CANCELLED ? 0U : info.trapCause;
        record->completion.systemCalls = info.systemCalls;
        record->completion.slices = info.slices;
        record->completion.preemptions = info.preemptions;
        record->terminal = UMICOM_TRUE;
    }
    return UMICOM_SUPERVISION_OK;
}

static UmicomBoolean UmicomSupervisionDescendant(UmicomKernelProcessSupervisor *supervisor,
    const UmicomKernelSupervisedProcessRecord *record, UmicomU64 ancestor)
{
    /* Parent identities are older than their children's identities. The bound
     * also prevents an accidental cycle from turning cleanup into an endless loop. */
    UmicomU64 cursor = record->parent;
    for (UmicomSize depth = 0U; depth < UMICOM_USER_TASK_LIMIT && cursor != 0U; ++depth) {
        if (cursor == ancestor) return UMICOM_TRUE;
        const UmicomKernelSupervisedProcessRecord *const parent = UmicomSupervisionIdentity(supervisor, cursor);
        if (parent == (const UmicomKernelSupervisedProcessRecord *)0) break;
        cursor = parent->parent;
    }
    return UMICOM_FALSE;
}

static void UmicomSupervisionRequestStop(UmicomKernelSupervisedProcessRecord *record,
    UmicomKernelSupervisionReason reason)
{
    if (record->terminal != UMICOM_FALSE || record->stopRequested != UMICOM_FALSE) return;
    /* The first accepted stop reason wins; retrying cleanup is not a new death. */
    record->stopReason = reason;
    record->stopRequested = UMICOM_TRUE;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorInitialize(UmicomKernelProcessSupervisor *supervisor)
{
    if (supervisor == (UmicomKernelProcessSupervisor *)0) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    if (supervisor->initialised != UMICOM_FALSE || supervisor->self != (const UmicomKernelProcessSupervisor *)0)
        return UMICOM_SUPERVISION_BAD_STATE;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index)
        if (supervisor->children[index].occupied != UMICOM_FALSE || supervisor->children[index].handle != 0U)
            return UMICOM_SUPERVISION_BAD_STATE;
    /* Initialise the existing owners in dependency order. We do not attach IPC
     * to an already-running namespace or move an existing task into a new domain. */
    if (UmicomKernelUserSchedulerInitialize(&supervisor->scheduler) != UMICOM_USER_SCHEDULE_OK ||
        UmicomKernelUserIpcAttach(&supervisor->ipc, &supervisor->scheduler) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_SUPERVISION_BAD_STATE;
    supervisor->self = supervisor;
    supervisor->initialised = UMICOM_TRUE;
    return UMICOM_SUPERVISION_OK;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorPump(UmicomKernelProcessSupervisor *supervisor)
{
    const UmicomKernelSupervisionStatus ready = UmicomSupervisionReady(supervisor, UMICOM_FALSE);
    if (ready != UMICOM_SUPERVISION_OK) return ready;
    const UmicomKernelSupervisionStatus graph = UmicomSupervisionGraph(supervisor);
    if (graph != UMICOM_SUPERVISION_OK) return graph;
    /* Each pass settles at least one newly terminal parent. No user runs here,
     * so at most the bounded number of family records can become newly terminal. */
    for (UmicomSize pass = 0U; pass <= UMICOM_USER_TASK_LIMIT; ++pass) {
        UmicomKernelSupervisionStatus status = UmicomSupervisionObserve(supervisor);
        if (status != UMICOM_SUPERVISION_OK) return status;
        for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
            const UmicomKernelSupervisedProcessRecord *const parent = &supervisor->children[index];
            if (parent->occupied == UMICOM_FALSE || parent->terminal == UMICOM_FALSE ||
                parent->familySettled != UMICOM_FALSE || parent->childLifetime != UMICOM_CHILDREN_CANCEL_TREE) continue;
            /* Mark the entire tree before adopting any direct child. Otherwise
             * an intermediate parent's adoption could hide a grandchild. */
            for (UmicomSize child = 0U; child < UMICOM_USER_TASK_LIMIT; ++child) {
                UmicomKernelSupervisedProcessRecord *const descendant = &supervisor->children[child];
                if (descendant->occupied != UMICOM_FALSE &&
                    UmicomSupervisionDescendant(supervisor, descendant, parent->identity) != UMICOM_FALSE)
                    UmicomSupervisionRequestStop(descendant, UMICOM_SUPERVISION_PARENT_STOP);
            }
        }
        for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
            UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
            if (record->occupied == UMICOM_FALSE || record->terminal != UMICOM_FALSE ||
                record->stopRequested == UMICOM_FALSE) continue;
            supervisor->active = UMICOM_TRUE;
            const UmicomKernelUserScheduleStatus stopped =
                UmicomKernelUserTaskCancel(&supervisor->scheduler, record->handle);
            supervisor->active = UMICOM_FALSE;
            if (stopped != UMICOM_USER_SCHEDULE_OK)
                return supervisor->scheduler.poisoned != UMICOM_FALSE ? UMICOM_SUPERVISION_UNSAFE : UMICOM_SUPERVISION_LOWER_FAILURE;
        }
        status = UmicomSupervisionObserve(supervisor);
        if (status != UMICOM_SUPERVISION_OK) return status;
        UmicomBoolean settledOne = UMICOM_FALSE;
        for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
            UmicomKernelSupervisedProcessRecord *const parent = &supervisor->children[index];
            if (parent->occupied == UMICOM_FALSE || parent->terminal == UMICOM_FALSE ||
                parent->familySettled != UMICOM_FALSE) continue;
            /* Cancellation requests are already recorded for all descendants.
             * Direct children can now be adopted without losing those requests. */
            for (UmicomSize child = 0U; child < UMICOM_USER_TASK_LIMIT; ++child) {
                UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[child];
                if (record->occupied != UMICOM_FALSE && record->parent == parent->identity)
                    record->parent = UMICOM_SUPERVISION_GUARDIAN;
            }
            parent->familySettled = UMICOM_TRUE;
            settledOne = UMICOM_TRUE;
        }
        if (settledOne == UMICOM_FALSE) {
            /* Endpoint cleanup may have made an unrelated receiver ready. Settle
             * its existing wait before the next runnable-task selection. */
            if (UmicomKernelUserIpcPump(&supervisor->scheduler) == UMICOM_FALSE)
                return UMICOM_SUPERVISION_UNSAFE;
            return UMICOM_SUPERVISION_OK;
        }
    }
    return UmicomSupervisionCorrupt(supervisor);
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSpawn(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 parent, const UmicomU8 *image, UmicomSize bytes, UmicomU64 argument,
    UmicomU64 sliceLimit, UmicomKernelChildLifetime childLifetime, UmicomKernelSupervisedProcessHandle *outHandle)
{
    if (outHandle == (UmicomKernelSupervisedProcessHandle *)0 || image == (const UmicomU8 *)0 || bytes == 0U ||
        sliceLimit == 0U || sliceLimit > UMICOM_USER_TASK_SLICE_LIMIT ||
        (childLifetime != UMICOM_CHILDREN_ADOPT && childLifetime != UMICOM_CHILDREN_CANCEL_TREE))
        return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    if (supervisor->admissionClosed != UMICOM_FALSE) return UMICOM_SUPERVISION_ADMISSION_CLOSED;
    status = UmicomSupervisionCaller(supervisor, parent);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomSize slot = 0U;
    while (slot < UMICOM_USER_TASK_LIMIT && supervisor->children[slot].occupied != UMICOM_FALSE) ++slot;
    if (slot == UMICOM_USER_TASK_LIMIT) return UMICOM_SUPERVISION_CAPACITY;
    UmicomKernelSupervisedProcessHandle handle = 0U;
    supervisor->active = UMICOM_TRUE;
    const UmicomKernelUserScheduleStatus loaded = UmicomKernelUserTaskCreate(&supervisor->scheduler,
        image, bytes, argument, sliceLimit, &handle);
    supervisor->active = UMICOM_FALSE;
    if (loaded != UMICOM_USER_SCHEDULE_OK) {
        /* The existing scheduler retains unusual failed-rollbacks separately.
         * No successful child token is published for an unsuccessful load. */
        if (loaded == UMICOM_USER_SCHEDULE_CAPACITY) return UMICOM_SUPERVISION_CAPACITY;
        return UMICOM_SUPERVISION_LOAD_FAILED;
    }
    UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[slot];
    record->occupied = UMICOM_TRUE;
    record->handle = handle; /* Preserve the owner even if an internal query fails. */
    UmicomKernelUserTaskInfo info;
    UmicomSupervisionClear(&info, sizeof(info));
    if (UmicomKernelUserTaskQuery(&supervisor->scheduler, handle, &info) != UMICOM_USER_SCHEDULE_OK ||
        info.identity == 0U || (parent != 0U && parent >= info.identity))
        return UmicomSupervisionCorrupt(supervisor);
    record->identity = info.identity;
    record->parent = parent;
    record->birthParent = parent;
    record->childLifetime = childLifetime;
    *outHandle = handle; /* Admission becomes visible only after loading and ownership agree. */
    return UMICOM_SUPERVISION_OK;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorQuery(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle, UmicomKernelSupervisedProcessInfo *outInfo)
{
    UmicomKernelSupervisionStatus status = UmicomSupervisionReady(supervisor, UMICOM_TRUE);
    if (status != UMICOM_SUPERVISION_OK) return status;
    if (outInfo == (UmicomKernelSupervisedProcessInfo *)0) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    /* Do not pump an unsafe domain just to print diagnostics. Its last reliable
     * completion records remain readable, with unsafe explicitly reported. */
    const UmicomBoolean unsafe = supervisor->poisoned != UMICOM_FALSE || supervisor->scheduler.poisoned != UMICOM_FALSE;
    if (unsafe == UMICOM_FALSE) {
        status = UmicomKernelProcessSupervisorPump(supervisor);
        if (status != UMICOM_SUPERVISION_OK) return status;
    }
    UmicomKernelSupervisedProcessRecord *const record = UmicomSupervisionFind(supervisor, handle);
    if (record == (UmicomKernelSupervisedProcessRecord *)0) return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, record);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelUserTaskInfo task;
    UmicomSupervisionClear(&task, sizeof(task));
    if (UmicomKernelUserTaskQuery(&supervisor->scheduler, handle, &task) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_SUPERVISION_CORRUPT_STATE;
    UmicomKernelSupervisedProcessInfo result;
    UmicomSupervisionClear(&result, sizeof(result));
    result.identity = record->identity; result.parent = record->parent; result.birthParent = record->birthParent;
    result.childLifetime = record->childLifetime; result.state = task.state;
    result.terminal = record->terminal; result.familySettled = record->familySettled;
    result.stopRequested = record->stopRequested; result.unsafe = unsafe;
    UmicomSupervisionCopy(&result.completion, &record->completion, sizeof(result.completion));
    UmicomSupervisionCopy(outInfo, &result, sizeof(result)); /* No mutable image or lower-task pointer crosses this boundary. */
    return UMICOM_SUPERVISION_OK;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorCancel(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle)
{
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelSupervisedProcessRecord *const record = UmicomSupervisionFind(supervisor, handle);
    if (record == (UmicomKernelSupervisedProcessRecord *)0) return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, record);
    if (status != UMICOM_SUPERVISION_OK) return status;
    if (record->terminal != UMICOM_FALSE) return UMICOM_SUPERVISION_BAD_STATE;
    UmicomSupervisionRequestStop(record, UMICOM_SUPERVISION_CANCEL_REQUEST);
    return UmicomKernelProcessSupervisorPump(supervisor);
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorStopTree(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle)
{
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelSupervisedProcessRecord *const root = UmicomSupervisionFind(supervisor, handle);
    if (root == (UmicomKernelSupervisedProcessRecord *)0) return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, root);
    if (status != UMICOM_SUPERVISION_OK) return status;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        UmicomKernelSupervisedProcessRecord *const child = &supervisor->children[index];
        if (child->occupied != UMICOM_FALSE && (child == root ||
            UmicomSupervisionDescendant(supervisor, child, root->identity) != UMICOM_FALSE))
            UmicomSupervisionRequestStop(child, UMICOM_SUPERVISION_CANCEL_REQUEST);
    }
    return UmicomKernelProcessSupervisorPump(supervisor);
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSetArgument(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle, UmicomU64 argument)
{
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelSupervisedProcessRecord *const record = UmicomSupervisionFind(supervisor, handle);
    if (record == (UmicomKernelSupervisedProcessRecord *)0) return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, record);
    if (status != UMICOM_SUPERVISION_OK) return status;
    return UmicomKernelUserTaskSetArgument(&supervisor->scheduler, handle, argument) == UMICOM_USER_SCHEDULE_OK
        ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_BAD_STATE;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorConnect(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle first, UmicomKernelMessageRights firstRights,
    UmicomKernelSupervisedProcessHandle second, UmicomKernelMessageRights secondRights,
    UmicomKernelMessageHandle *outFirst, UmicomKernelMessageHandle *outSecond)
{
    if (outFirst == (UmicomKernelMessageHandle *)0 || outSecond == (UmicomKernelMessageHandle *)0 ||
        outFirst == outSecond || first == second) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelSupervisedProcessRecord *const a = UmicomSupervisionFind(supervisor, first);
    UmicomKernelSupervisedProcessRecord *const b = UmicomSupervisionFind(supervisor, second);
    if (a == (UmicomKernelSupervisedProcessRecord *)0 || b == (UmicomKernelSupervisedProcessRecord *)0)
        return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, a);
    if (status != UMICOM_SUPERVISION_OK) return status;
    status = UmicomSupervisionAuthority(supervisor, caller, b);
    if (status != UMICOM_SUPERVISION_OK) return status;
    /* Connect owns no queue implementation. Unknown rights, exhausted capacity
     * and non-fresh task arguments remain the existing IPC owner's decision. */
    return UmicomKernelUserIpcConnect(&supervisor->ipc, first, firstRights, second, secondRights,
        outFirst, outSecond) == UMICOM_MESSAGE_OK ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_IPC_REFUSED;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorRunOne(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 quantumTicks, UmicomKernelSupervisedProcessHandle *outHandle)
{
    if (outHandle == (UmicomKernelSupervisedProcessHandle *)0 || quantumTicks < UMICOM_USER_QUANTUM_MIN_TICKS ||
        quantumTicks > UMICOM_USER_QUANTUM_MAX_TICKS) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelUserTaskHandle selected = 0U;
    supervisor->active = UMICOM_TRUE;
    const UmicomKernelUserScheduleStatus run = UmicomKernelUserSchedulerRunOne(&supervisor->scheduler, quantumTicks, &selected);
    supervisor->active = UMICOM_FALSE;
    if (supervisor->scheduler.poisoned != UMICOM_FALSE) return UMICOM_SUPERVISION_UNSAFE;
    /* Even an invalid-context refusal may have ended a task. Settle that result
     * before another task is admitted; ordinary entry refusal spends no budget. */
    status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    if (run == UMICOM_USER_SCHEDULE_IDLE) return UMICOM_SUPERVISION_IDLE;
    if (run == UMICOM_USER_SCHEDULE_ENTRY_REFUSED) return UMICOM_SUPERVISION_ENTRY_REFUSED;
    if (run != UMICOM_USER_SCHEDULE_OK) return UMICOM_SUPERVISION_LOWER_FAILURE;
    if (UmicomSupervisionFind(supervisor, selected) == (UmicomKernelSupervisedProcessRecord *)0)
        return UmicomSupervisionCorrupt(supervisor);
    *outHandle = selected;
    return UMICOM_SUPERVISION_OK;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorCollect(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle, UmicomKernelProcessCompletion *outCompletion)
{
    if (outCompletion == (UmicomKernelProcessCompletion *)0) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelSupervisedProcessRecord *const record = UmicomSupervisionFind(supervisor, handle);
    if (record == (UmicomKernelSupervisedProcessRecord *)0) return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, record);
    if (status != UMICOM_SUPERVISION_OK) return status;
    if (record->terminal == UMICOM_FALSE || record->familySettled == UMICOM_FALSE)
        return UMICOM_SUPERVISION_NOT_TERMINAL;
    supervisor->active = UMICOM_TRUE;
    const UmicomKernelUserScheduleStatus reaped = UmicomKernelUserTaskReap(&supervisor->scheduler, handle);
    supervisor->active = UMICOM_FALSE;
    if (reaped != UMICOM_USER_SCHEDULE_OK) return UMICOM_SUPERVISION_CLEANUP_FAILED;
    /* Only this successful lower release commits collection. A second collect
     * cannot observe the same record, and failed cleanup preserved all evidence. */
    UmicomSupervisionCopy(outCompletion, &record->completion, sizeof(*outCompletion));
    UmicomSupervisionClear(record, sizeof(*record));
    return UMICOM_SUPERVISION_OK;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorCollectAny(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelProcessCompletion *outCompletion)
{
    if (outCompletion == (UmicomKernelProcessCompletion *)0) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    status = UmicomSupervisionCaller(supervisor, caller);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomBoolean ownsChild = UMICOM_FALSE;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        const UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        if (record->occupied == UMICOM_FALSE || record->parent != caller) continue;
        ownsChild = UMICOM_TRUE;
        if (record->terminal != UMICOM_FALSE && record->familySettled != UMICOM_FALSE)
            return UmicomKernelProcessSupervisorCollect(supervisor, caller, record->handle, outCompletion);
    }
    /* An empty child set is not the same as a child which has not stopped yet. */
    return ownsChild != UMICOM_FALSE ? UMICOM_SUPERVISION_WOULD_BLOCK : UMICOM_SUPERVISION_NO_CHILDREN;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorBeginShutdown(UmicomKernelProcessSupervisor *supervisor)
{
    const UmicomKernelSupervisionStatus ready = UmicomSupervisionReady(supervisor, UMICOM_FALSE);
    if (ready != UMICOM_SUPERVISION_OK) return ready;
    supervisor->admissionClosed = UMICOM_TRUE; /* A one-way gate, not a domain reset. */
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index)
        if (supervisor->children[index].occupied != UMICOM_FALSE)
            UmicomSupervisionRequestStop(&supervisor->children[index], UMICOM_SUPERVISION_SHUTDOWN);
    return UmicomKernelProcessSupervisorPump(supervisor);
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorReapRetained(UmicomKernelProcessSupervisor *supervisor,
    UmicomSize *outReaped)
{
    const UmicomKernelSupervisionStatus ready = UmicomSupervisionReady(supervisor, UMICOM_FALSE);
    if (ready != UMICOM_SUPERVISION_OK) return ready;
    if (outReaped == (UmicomSize *)0) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    supervisor->active = UMICOM_TRUE;
    const UmicomKernelUserScheduleStatus status = UmicomKernelUserSchedulerReapRetained(&supervisor->scheduler, outReaped);
    supervisor->active = UMICOM_FALSE;
    /* The lower count includes completed releases on an error. Preserve that
     * explicit partial-progress contract rather than fabricating all-or-nothing. */
    return status == UMICOM_USER_SCHEDULE_OK ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_CLEANUP_FAILED;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSnapshot(UmicomKernelProcessSupervisor *supervisor,
    UmicomKernelSupervisionSnapshot *outInfo)
{
    const UmicomKernelSupervisionStatus ready = UmicomSupervisionReady(supervisor, UMICOM_TRUE);
    if (ready != UMICOM_SUPERVISION_OK) return ready;
    if (outInfo == (UmicomKernelSupervisionSnapshot *)0) return UMICOM_SUPERVISION_INVALID_ARGUMENT;
    UmicomKernelSupervisionSnapshot result;
    UmicomSupervisionClear(&result, sizeof(result));
    result.admissionClosed = supervisor->admissionClosed;
    result.unsafe = supervisor->poisoned != UMICOM_FALSE || supervisor->scheduler.poisoned != UMICOM_FALSE;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        const UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        /* Read-only inspection of unpublished rollback ownership. The embedded
         * scheduler alone mutates these lower task records. */
        if (supervisor->scheduler.tasks[index].state == UMICOM_USER_TASK_RETAINED) ++result.retainedLoads;
        if (record->occupied == UMICOM_FALSE) continue;
        ++result.children;
        if (record->parent == UMICOM_SUPERVISION_GUARDIAN) ++result.guardianChildren;
        if (record->terminal != UMICOM_FALSE) ++result.terminal;
        else {
            ++result.live;
            UmicomKernelUserTaskInfo task;
            UmicomSupervisionClear(&task, sizeof(task));
            if (UmicomKernelUserTaskQuery(&supervisor->scheduler, record->handle, &task) != UMICOM_USER_SCHEDULE_OK)
                return UMICOM_SUPERVISION_CORRUPT_STATE;
            if (task.state == UMICOM_USER_TASK_BLOCKED) ++result.blocked;
        }
    }
    UmicomSupervisionCopy(outInfo, &result, sizeof(result));
    return UMICOM_SUPERVISION_OK;
}

UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorValidate(UmicomKernelProcessSupervisor *supervisor)
{
    const UmicomKernelSupervisionStatus ready = UmicomSupervisionReady(supervisor, UMICOM_FALSE);
    if (ready != UMICOM_SUPERVISION_OK) return ready;
    if (UmicomKernelUserSchedulerValidate(&supervisor->scheduler) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_SUPERVISION_BAD_STATE; /* A partial failed destructor can be retried, not reset. */
    UmicomSize published = 0U, records = 0U;
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        const UmicomKernelUserTaskState state = supervisor->scheduler.tasks[index].state;
        if (state != UMICOM_USER_TASK_EMPTY && state != UMICOM_USER_TASK_RETAINED) ++published;
        const UmicomKernelSupervisedProcessRecord *const record = &supervisor->children[index];
        if (record->occupied == UMICOM_FALSE) {
            if (record->handle != 0U || record->identity != 0U) return UMICOM_SUPERVISION_CORRUPT_STATE;
            continue;
        }
        ++records;
        UmicomKernelUserTaskInfo task;
        UmicomSupervisionClear(&task, sizeof(task));
        if (UmicomKernelUserTaskQuery(&supervisor->scheduler, record->handle, &task) != UMICOM_USER_SCHEDULE_OK ||
            task.identity != record->identity || record->identity == 0U || record->parent >= record->identity ||
            record->birthParent >= record->identity ||
            (record->childLifetime != UMICOM_CHILDREN_ADOPT && record->childLifetime != UMICOM_CHILDREN_CANCEL_TREE))
            return UMICOM_SUPERVISION_CORRUPT_STATE;
        if (record->parent != 0U && UmicomSupervisionIdentity(supervisor, record->parent) == (UmicomKernelSupervisedProcessRecord *)0)
            return UMICOM_SUPERVISION_CORRUPT_STATE;
        for (UmicomSize earlier = 0U; earlier < index; ++earlier)
            if (supervisor->children[earlier].occupied != UMICOM_FALSE &&
                (supervisor->children[earlier].handle == record->handle || supervisor->children[earlier].identity == record->identity))
                return UMICOM_SUPERVISION_CORRUPT_STATE;
        if (record->terminal != UMICOM_FALSE && (UmicomSupervisionTerminal(task.state) == UMICOM_FALSE ||
            record->completion.identity != record->identity || record->completion.birthParent != record->birthParent ||
            record->completion.state != task.state)) return UMICOM_SUPERVISION_CORRUPT_STATE;
        if (record->familySettled != UMICOM_FALSE) {
            if (record->terminal == UMICOM_FALSE) return UMICOM_SUPERVISION_CORRUPT_STATE;
            for (UmicomSize child = 0U; child < UMICOM_USER_TASK_LIMIT; ++child)
                if (supervisor->children[child].occupied != UMICOM_FALSE && supervisor->children[child].parent == record->identity)
                    return UMICOM_SUPERVISION_CORRUPT_STATE;
        }
    }
    /* There must be no live scheduled image without an authority/lifetime record. */
    return records == published ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_CORRUPT_STATE;
}

const char *UmicomKernelSupervisionStatusName(UmicomKernelSupervisionStatus status)
{
    switch (status) {
        case UMICOM_SUPERVISION_OK: return "ok";
        case UMICOM_SUPERVISION_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_SUPERVISION_BAD_STATE: return "bad-state";
        case UMICOM_SUPERVISION_BUSY: return "busy";
        case UMICOM_SUPERVISION_UNSAFE: return "unsafe-state";
        case UMICOM_SUPERVISION_INVALID_HANDLE: return "invalid-handle";
        case UMICOM_SUPERVISION_WRONG_PARENT: return "wrong-parent";
        case UMICOM_SUPERVISION_PARENT_ENDED: return "parent-ended";
        case UMICOM_SUPERVISION_ADMISSION_CLOSED: return "admission-closed";
        case UMICOM_SUPERVISION_CAPACITY: return "capacity";
        case UMICOM_SUPERVISION_LOAD_FAILED: return "load-failed";
        case UMICOM_SUPERVISION_ENTRY_REFUSED: return "entry-refused";
        case UMICOM_SUPERVISION_LOWER_FAILURE: return "lower-service-failure";
        case UMICOM_SUPERVISION_NOT_TERMINAL: return "not-terminal";
        case UMICOM_SUPERVISION_CLEANUP_FAILED: return "cleanup-failed";
        case UMICOM_SUPERVISION_NO_CHILDREN: return "no-children";
        case UMICOM_SUPERVISION_WOULD_BLOCK: return "would-block";
        case UMICOM_SUPERVISION_IDLE: return "idle";
        case UMICOM_SUPERVISION_IPC_REFUSED: return "ipc-refused";
        case UMICOM_SUPERVISION_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-status";
    }
}

#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSetLaunch(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle,
    const UmicomKernelProgramLaunchSpec *spec)
{
    /* Reuse the family authority checks rather than invent a launch-specific
     * token domain. A sibling cannot edit another program's initial arguments. */
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorPump(supervisor);
    if (status != UMICOM_SUPERVISION_OK) return status;
    UmicomKernelSupervisedProcessRecord *const record = UmicomSupervisionFind(supervisor, handle);
    if (!record) return UMICOM_SUPERVISION_INVALID_HANDLE;
    status = UmicomSupervisionAuthority(supervisor, caller, record);
    if (status != UMICOM_SUPERVISION_OK) return status;
    return UmicomKernelUserTaskSetLaunch(&supervisor->scheduler, handle, spec) == UMICOM_USER_SCHEDULE_OK
        ? UMICOM_SUPERVISION_OK : UMICOM_SUPERVISION_BAD_STATE;
}
#endif
