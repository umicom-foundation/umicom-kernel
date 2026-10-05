/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_ipc.c
 *
 * PURPOSE:
 *   Suspend a checked message call when its queue cannot progress, then finish
 *   the same call before making its retained user frame runnable.
 *
 * EDUCATIONAL OVERVIEW:
 *   The queue, not a notification, is the authority. We try an operation,
 *   register WOULD_BLOCK without yielding in between, and recheck waits before
 *   each scheduling decision. Only user instructions are pre-empted. Kernel
 *   queue, mapping and wait changes remain serial on one hart.
 *
 *   A pending send owns copied bytes. A pending receive owns an address value
 *   which is validated again before writing. Cancellation discards an operation
 *   that has not committed; it never retracts an already accepted packet.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_ipc.h"
#include "umicom/kernel/riscv64/user_slice.h"
#include "umicom/kernel/platform.h"

/* These exact Kernel pointers bind one serial invocation. User arguments never
 * select this domain, its scheduler, or the authenticated task identity. */
static UmicomKernelUserIpc *umicomIpcActive;
static UmicomKernelUserTask *umicomIpcActiveTask;

static void UmicomIpcClear(void *address, UmicomSize bytes)
{
    /* Scrub retained payloads without a hosted memset dependency. */
    volatile UmicomU8 *const output = (volatile UmicomU8 *)address;
    for (UmicomSize index = 0U; index < bytes; ++index) output[index] = 0U;
}
static void UmicomIpcCount(UmicomU64 *counter)
{
    /* Diagnostics saturate; the registration sequence separately refuses wrap. */
    if (*counter != ~(UmicomU64)0U) ++*counter;
}
static UmicomBoolean UmicomIpcReady(const UmicomKernelUserIpc *ipc)
{
    return ipc != (const UmicomKernelUserIpc *)0 && ipc->initialised != UMICOM_FALSE &&
        ipc->self == ipc && ipc->scheduler != (UmicomKernelUserScheduler *)0 &&
        ipc->scheduler->self == ipc->scheduler && ipc->scheduler->initialised != UMICOM_FALSE &&
        ipc->scheduler->ipc == ipc ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomSize UmicomIpcSlot(UmicomKernelUserScheduler *scheduler, const UmicomKernelUserTask *task)
{
    /* Equality avoids subtracting unrelated pointers on an invalid internal call. */
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot)
        if (&scheduler->tasks[slot] == task) return slot;
    return UMICOM_USER_TASK_LIMIT;
}
static UmicomKernelUserTaskHandle UmicomIpcToken(const UmicomKernelUserTask *task, UmicomSize slot)
{
    return ((UmicomU64)task->generation << 32U) | (UmicomU64)(slot + 1U);
}
static UmicomBoolean UmicomIpcMatches(const UmicomKernelUserIpcWait *wait,
    const UmicomKernelUserTask *task, UmicomSize slot)
{
    return wait->task == UmicomIpcToken(task, slot) && wait->identity == task->process.identity &&
        task->generation != 0U && task->retired == UMICOM_FALSE ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelUserScheduleStatus UmicomKernelUserIpcAttach(
    UmicomKernelUserIpc *ipc, UmicomKernelUserScheduler *scheduler)
{
    if (ipc == (UmicomKernelUserIpc *)0 || scheduler == (UmicomKernelUserScheduler *)0)
        return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    if (ipc->initialised != UMICOM_FALSE || ipc->self != (const UmicomKernelUserIpc *)0 ||
        scheduler->ipc != (struct UmicomKernelUserIpc *)0 ||
        UmicomKernelUserSchedulerValidate(scheduler) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    /* Start before identity admission. Attaching later must not silently move
     * an existing program into a different service/authority namespace. */
    if (scheduler->nextIdentity != 1U || scheduler->dispatches != 0U)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot)
        if (scheduler->tasks[slot].state != UMICOM_USER_TASK_EMPTY)
            return UMICOM_USER_SCHEDULE_BAD_STATE;
    if (UmicomKernelMessageInitialize(&ipc->messages) != UMICOM_MESSAGE_OK)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    ipc->scheduler = scheduler;
    ipc->nextOrder = 1U;
    ipc->now = UmicomPlatformTimerRead();
    ipc->self = ipc;
    ipc->initialised = UMICOM_TRUE;
    scheduler->ipc = ipc; /* Publish only after the owned channel domain exists. */
    return UMICOM_USER_SCHEDULE_OK;
}
UmicomKernelMessageStatus UmicomKernelUserIpcConnect(UmicomKernelUserIpc *ipc,
    UmicomKernelUserTaskHandle first, UmicomKernelMessageRights firstRights,
    UmicomKernelUserTaskHandle second, UmicomKernelMessageRights secondRights,
    UmicomKernelMessageHandle *outFirst, UmicomKernelMessageHandle *outSecond)
{
    if (UmicomIpcReady(ipc) == UMICOM_FALSE || first == second ||
        outFirst == (UmicomKernelMessageHandle *)0 || outSecond == (UmicomKernelMessageHandle *)0 ||
        outFirst == outSecond) return UMICOM_MESSAGE_INVALID_ARGUMENT;
    UmicomKernelUserTaskInfo a;
    UmicomKernelUserTaskInfo b;
    if (UmicomKernelUserTaskQuery(ipc->scheduler, first, &a) != UMICOM_USER_SCHEDULE_OK ||
        UmicomKernelUserTaskQuery(ipc->scheduler, second, &b) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_MESSAGE_INVALID_HANDLE;
    if (a.state != UMICOM_USER_TASK_READY || b.state != UMICOM_USER_TASK_READY || a.slices != 0U || b.slices != 0U)
        return UMICOM_MESSAGE_BAD_STATE;
    return UmicomKernelMessagePairCreate(&ipc->messages, a.identity, firstRights,
        b.identity, secondRights, outFirst, outSecond);
}
UmicomBoolean UmicomKernelUserIpcBegin(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (scheduler->ipc == (struct UmicomKernelUserIpc *)0) return UMICOM_TRUE;
    UmicomKernelUserIpc *const ipc = scheduler->ipc;
    if (UmicomIpcReady(ipc) == UMICOM_FALSE || umicomIpcActive != (UmicomKernelUserIpc *)0 ||
        UmicomIpcSlot(scheduler, task) == UMICOM_USER_TASK_LIMIT) return UMICOM_FALSE;
    /* The original nonblocking service sees this same task on every quantum.
     * A conflicting external binding is refused, never overwritten. */
    if (UmicomKernelMessageServiceBind(&ipc->messages, task->process.identity) != UMICOM_MESSAGE_OK)
        return UMICOM_FALSE;
    umicomIpcActiveTask = task;
    umicomIpcActive = ipc;
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserIpcEnd(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (scheduler->ipc == (struct UmicomKernelUserIpc *)0) return UMICOM_TRUE;
    if (umicomIpcActive != scheduler->ipc || umicomIpcActiveTask != task) return UMICOM_FALSE;
    if (UmicomKernelMessageServiceUnbind(&scheduler->ipc->messages, task->process.identity) != UMICOM_MESSAGE_OK)
        return UMICOM_FALSE;
    umicomIpcActiveTask = (UmicomKernelUserTask *)0;
    umicomIpcActive = (UmicomKernelUserIpc *)0;
    return UMICOM_TRUE;
}

static UmicomKernelMessageStatus UmicomIpcRead(const UmicomKernelUserMemory *memory,
    UmicomAddress source, UmicomU8 *destination, UmicomSize bytes)
{
    /* Preflight the complete span, then reuse the existing bounded copy API. */
    if (UmicomKernelUserMemoryCheck(memory, source, bytes,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ) != UMICOM_USER_RESULT_OK) return UMICOM_MESSAGE_BAD_USER_BUFFER;
    for (UmicomSize offset = 0U; offset < bytes;) {
        const UmicomSize left = bytes - offset;
        const UmicomSize count = left < UMICOM_USER_COPY_LIMIT ? left : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryRead(memory, source + offset, destination + offset, count) != UMICOM_USER_RESULT_OK)
            return UMICOM_MESSAGE_BAD_USER_BUFFER;
        offset += count;
    }
    return UMICOM_MESSAGE_OK;
}
static UmicomKernelMessageStatus UmicomIpcAttempt(UmicomKernelUserIpc *ipc,
    UmicomKernelUserTask *task, const UmicomKernelUserIpcWait *request)
{
    if (request->sending != UMICOM_FALSE)
        return UmicomKernelMessageSend(&ipc->messages, task->process.identity,
            request->endpoint, request->payload, request->bytes);
    UmicomKernelMessage message;
    UmicomKernelMessageStatus status = UmicomKernelMessagePeek(&ipc->messages,
        task->process.identity, request->endpoint, &message);
    if (status != UMICOM_MESSAGE_OK) return status;
    const UmicomSize bytes = UMICOM_MESSAGE_HEADER_BYTES + message.bytes;
    if (request->bytes < bytes) return UMICOM_MESSAGE_BUFFER_TOO_SMALL;
    /* A suspended mapping may have changed. Check the whole delivery again
     * before its first byte; an invalid second page leaves the packet queued. */
    if (UmicomKernelUserMemoryCheck(&task->process.report.memory, request->address, bytes,
        UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != UMICOM_USER_RESULT_OK) return UMICOM_MESSAGE_BAD_USER_BUFFER;
    for (UmicomSize offset = 0U; offset < bytes;) {
        const UmicomSize left = bytes - offset;
        const UmicomSize count = left < UMICOM_USER_COPY_LIMIT ? left : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryWrite(&task->process.report.memory, request->address + offset,
            (const UmicomU8 *)&message + offset, count) != UMICOM_USER_RESULT_OK) return UMICOM_MESSAGE_BAD_USER_BUFFER;
        offset += count;
    }
    /* No mapper or second consumer runs between this copy and exact consume. */
    return UmicomKernelMessageConsume(&ipc->messages, task->process.identity,
        request->endpoint, message.sequence);
}
static UmicomU64 UmicomIpcReturn(UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame,
    UmicomAddress nextPc, UmicomU64 status)
{
    frame->x10_a0 = status;
    frame->mepc = (UmicomU64)nextPc;
    if (status != UMICOM_MESSAGE_OK) ++session->rejectedCalls;
    return 1U;
}
UmicomBoolean UmicomKernelUserIpcRecognizes(UmicomU64 number)
{
    return number == UMICOM_USER_CALL_MESSAGE_SEND_WAIT || number == UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT
        ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomU64 UmicomKernelUserIpcDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc)
{
    if (session == (UmicomKernelUserSession *)0 || frame == (UmicomRiscvTrapFrame *)0) return 0U;
    UmicomKernelUserIpc *const ipc = umicomIpcActive;
    UmicomKernelUserTask *const task = umicomIpcActiveTask;
    if (ipc == (UmicomKernelUserIpc *)0 || task == (UmicomKernelUserTask *)0 ||
        session != &task->process.report || session->identity != task->process.identity)
        return UmicomIpcReturn(session, frame, nextPc, UMICOM_MESSAGE_SERVICE_UNBOUND);
    const UmicomSize slot = UmicomIpcSlot(ipc->scheduler, task);
    if (slot == UMICOM_USER_TASK_LIMIT || task->state != UMICOM_USER_TASK_RUNNING ||
        ipc->waits[slot].pending != UMICOM_FALSE || UmicomKernelUserIpcRecognizes(frame->x17_a7) == UMICOM_FALSE) {
        session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
        return 0U;
    }
    const UmicomU64 timeout = frame->x13_a3;
    const UmicomU64 now = UmicomPlatformTimerRead();
    if (timeout != UMICOM_IPC_WAIT_FOREVER &&
        (timeout > UMICOM_IPC_WAIT_MAX_TICKS || now > ~(UmicomU64)0U - timeout))
        return UmicomIpcReturn(session, frame, nextPc, UMICOM_MESSAGE_INVALID_ARGUMENT);
    UmicomKernelUserIpcWait request;
    UmicomIpcClear(&request, sizeof(request));
    request.endpoint = frame->x10_a0;
    request.address = (UmicomAddress)frame->x11_a1;
    request.bytes = frame->x12_a2;
    request.sending = frame->x17_a7 == UMICOM_USER_CALL_MESSAGE_SEND_WAIT ? UMICOM_TRUE : UMICOM_FALSE;
    if ((request.sending != UMICOM_FALSE && request.bytes == 0U) ||
        (request.sending == UMICOM_FALSE && request.bytes < UMICOM_MESSAGE_HEADER_BYTES))
        return UmicomIpcReturn(session, frame, nextPc, UMICOM_MESSAGE_INVALID_ARGUMENT);
    if (request.bytes > (request.sending != UMICOM_FALSE ? UMICOM_MESSAGE_MAX_BYTES : sizeof(UmicomKernelMessage)))
        return UmicomIpcReturn(session, frame, nextPc, UMICOM_MESSAGE_TOO_LARGE);
    UmicomKernelMessageStatus status;
    if (request.sending != UMICOM_FALSE)
        status = UmicomIpcRead(&session->memory, request.address, request.payload, request.bytes);
    else {
        /* An empty queue must not put a known invalid destination to sleep. */
        status = UmicomKernelUserMemoryCheck(&session->memory, request.address, request.bytes,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) == UMICOM_USER_RESULT_OK
            ? UMICOM_MESSAGE_OK : UMICOM_MESSAGE_BAD_USER_BUFFER;
    }
    if (status == UMICOM_MESSAGE_OK) status = UmicomIpcAttempt(ipc, task, &request);
    if (status != UMICOM_MESSAGE_WOULD_BLOCK || timeout == 0U)
        return UmicomIpcReturn(session, frame, nextPc, (UmicomU64)status);
    if (ipc->nextOrder == 0U)
        return UmicomIpcReturn(session, frame, nextPc, UMICOM_MESSAGE_SEQUENCE_EXHAUSTED);
    request.task = UmicomIpcToken(task, slot);
    request.identity = session->identity;
    request.order = ipc->nextOrder;
    request.nextPc = nextPc;
    request.hasDeadline = timeout != UMICOM_IPC_WAIT_FOREVER ? UMICOM_TRUE : UMICOM_FALSE;
    request.deadline = request.hasDeadline != UMICOM_FALSE ? now + timeout : 0U;
    request.pending = UMICOM_TRUE;
    /* Copy values into stable storage; do not borrow this temporary request. */
    for (UmicomSize index = 0U; index < sizeof(request); ++index)
        ((UmicomU8 *)&ipc->waits[slot])[index] = ((const UmicomU8 *)&request)[index];
    ipc->nextOrder = request.order == ~(UmicomU64)0U ? 0U : request.order + 1U;
    UmicomIpcCount(&ipc->blocked);
    frame->mepc = (UmicomU64)nextPc;
    frame->x10_a0 = UMICOM_MESSAGE_WOULD_BLOCK; /* Not exposed while the task is blocked. */
    session->stopReason = UMICOM_USER_STOP_NONE;
    UmicomIpcClear(&request, sizeof(request));
    return 0U; /* The existing Assembly captures the frame and returns to Kernel. */
}
UmicomBoolean UmicomKernelUserIpcPending(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (scheduler->ipc == (struct UmicomKernelUserIpc *)0) return UMICOM_FALSE;
    const UmicomSize slot = UmicomIpcSlot(scheduler, task);
    return slot < UMICOM_USER_TASK_LIMIT && scheduler->ipc->waits[slot].pending != UMICOM_FALSE
        ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomBoolean UmicomKernelUserIpcStop(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (scheduler->ipc == (struct UmicomKernelUserIpc *)0) return UMICOM_TRUE;
    const UmicomSize slot = UmicomIpcSlot(scheduler, task);
    if (slot == UMICOM_USER_TASK_LIMIT || UmicomIpcReady(scheduler->ipc) == UMICOM_FALSE) {
        scheduler->poisoned = UMICOM_TRUE;
        return UMICOM_FALSE;
    }
    /* Discard only the uncommitted request. Already accepted outgoing packets
     * belong to the peer's queue and survive this task's termination. */
    UmicomIpcClear(&scheduler->ipc->waits[slot], sizeof(scheduler->ipc->waits[slot]));
    UmicomSize closed = 0U;
    if (UmicomKernelMessageCloseOwner(&scheduler->ipc->messages, task->process.identity, &closed) != UMICOM_MESSAGE_OK) {
        scheduler->poisoned = UMICOM_TRUE;
        return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
UmicomKernelUserScheduleStatus UmicomKernelUserIpcSuspend(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTask *task, UmicomBoolean expired)
{
    const UmicomSize slot = UmicomIpcSlot(scheduler, task);
    if (scheduler->ipc == (struct UmicomKernelUserIpc *)0 || slot == UMICOM_USER_TASK_LIMIT ||
        expired != UMICOM_FALSE || task->frame.mcause != 8U || task->process.report.stopReason != UMICOM_USER_STOP_NONE ||
        task->frame.mepc != scheduler->ipc->waits[slot].nextPc ||
        UmicomIpcMatches(&scheduler->ipc->waits[slot], task, slot) == UMICOM_FALSE ||
        UmicomKernelUserFrameValid(&task->process.report.memory, &task->frame) == UMICOM_FALSE) {
        scheduler->poisoned = UMICOM_TRUE;
        task->state = UMICOM_USER_TASK_ERROR;
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
    /* Waiting does not replenish the original execution budget. The captured
     * invocation spent one slice; waiting itself spends no additional slices. */
    if (task->slices >= task->sliceLimit) {
        if (UmicomKernelUserIpcStop(scheduler, task) == UMICOM_FALSE)
            return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
        task->state = UMICOM_USER_TASK_EXHAUSTED;
        task->process.state = UMICOM_PROCESS_CLEANUP_REQUIRED;
        task->process.quiesced = UMICOM_TRUE;
    } else task->state = UMICOM_USER_TASK_BLOCKED;
    return UMICOM_USER_SCHEDULE_OK;
}
UmicomBoolean UmicomKernelUserIpcValidate(UmicomKernelUserScheduler *scheduler)
{
    UmicomKernelUserIpc *const ipc = scheduler->ipc;
    if (ipc == (UmicomKernelUserIpc *)0) return UMICOM_TRUE;
    if (UmicomIpcReady(ipc) == UMICOM_FALSE ||
        UmicomKernelMessageValidate(&ipc->messages) != UMICOM_MESSAGE_OK) return UMICOM_FALSE;
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
        const UmicomKernelUserIpcWait *const wait = &ipc->waits[slot];
        const UmicomKernelUserTask *const task = &scheduler->tasks[slot];
        if ((task->state == UMICOM_USER_TASK_BLOCKED) != (wait->pending != UMICOM_FALSE)) return UMICOM_FALSE;
        if (wait->pending == UMICOM_FALSE) continue;
        if (UmicomIpcMatches(wait, task, slot) == UMICOM_FALSE || wait->order == 0U ||
            task->process.state != UMICOM_PROCESS_RUNNING || task->process.quiesced != UMICOM_FALSE ||
            task->slices >= task->sliceLimit || task->frame.mepc != wait->nextPc || wait->bytes == 0U ||
            wait->bytes > (wait->sending != UMICOM_FALSE ? UMICOM_MESSAGE_MAX_BYTES : sizeof(UmicomKernelMessage)) ||
            (wait->sending == UMICOM_FALSE && wait->bytes < UMICOM_MESSAGE_HEADER_BYTES)) return UMICOM_FALSE;
        for (UmicomSize previous = 0U; previous < slot; ++previous)
            if (ipc->waits[previous].pending != UMICOM_FALSE && ipc->waits[previous].order == wait->order)
                return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserIpcPump(UmicomKernelUserScheduler *scheduler)
{
    if (scheduler == (UmicomKernelUserScheduler *)0) return UMICOM_FALSE;
    UmicomKernelUserIpc *const ipc = scheduler->ipc;
    if (ipc == (UmicomKernelUserIpc *)0) return UMICOM_TRUE;
    if (scheduler->active != UMICOM_FALSE || scheduler->poisoned != UMICOM_FALSE ||
        umicomIpcActive != (UmicomKernelUserIpc *)0) return UMICOM_FALSE;
    if (UmicomKernelUserIpcValidate(scheduler) == UMICOM_FALSE) {
        scheduler->poisoned = UMICOM_TRUE;
        return UMICOM_FALSE;
    }
    const UmicomU64 now = UmicomPlatformTimerRead();
    if (now < ipc->now) {
        scheduler->poisoned = UMICOM_TRUE; /* Reversed time cannot settle reliable deadlines. */
        return UMICOM_FALSE;
    }
    ipc->now = now;
    UmicomU64 previousOrder = 0U;
    for (UmicomSize visited = 0U; visited < UMICOM_USER_TASK_LIMIT; ++visited) {
        UmicomSize selected = UMICOM_USER_TASK_LIMIT;
        for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
            const UmicomKernelUserIpcWait *const candidate = &ipc->waits[slot];
            if (candidate->pending == UMICOM_FALSE || candidate->order <= previousOrder) continue;
            if (selected == UMICOM_USER_TASK_LIMIT || candidate->order < ipc->waits[selected].order) selected = slot;
        }
        if (selected == UMICOM_USER_TASK_LIMIT) break;
        UmicomKernelUserIpcWait *const wait = &ipc->waits[selected];
        UmicomKernelUserTask *const task = &scheduler->tasks[selected];
        previousOrder = wait->order;
        /* Refuse a corrupt continuation before committing a queue operation. */
        if (UmicomKernelUserFrameValid(&task->process.report.memory, &task->frame) == UMICOM_FALSE) {
            scheduler->poisoned = UMICOM_TRUE;
            return UMICOM_FALSE;
        }
        /* An elapsed registered deadline precedes later readiness. Admission
         * instead tries the immediate operation before deciding to register. */
        const UmicomBoolean due = wait->hasDeadline != UMICOM_FALSE && now >= wait->deadline ? UMICOM_TRUE : UMICOM_FALSE;
        const UmicomU64 status = due != UMICOM_FALSE ? UMICOM_IPC_RESULT_TIMED_OUT :
            (UmicomU64)UmicomIpcAttempt(ipc, task, wait);
        if (status == UMICOM_MESSAGE_WOULD_BLOCK) continue;
        if (status == UMICOM_MESSAGE_CORRUPT_STATE || status == UMICOM_MESSAGE_STATE_CHANGED) {
            scheduler->poisoned = UMICOM_TRUE;
            return UMICOM_FALSE;
        }
        /* Publish the result before runnable state. ECALL was advanced once at
         * registration, so completion must not increment the call count again. */
        task->frame.x10_a0 = status;
        if (status != UMICOM_MESSAGE_OK) ++task->process.report.rejectedCalls;
        UmicomIpcClear(wait, sizeof(*wait));
        task->state = UMICOM_USER_TASK_PAUSED;
        UmicomIpcCount(&ipc->completed);
        if (due != UMICOM_FALSE) UmicomIpcCount(&ipc->timedOut);
    }
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserIpcSnapshot(UmicomKernelUserIpc *ipc, UmicomKernelUserIpcInfo *outInfo)
{
    if (UmicomIpcReady(ipc) == UMICOM_FALSE || outInfo == (UmicomKernelUserIpcInfo *)0 ||
        ipc->scheduler->active != UMICOM_FALSE) return UMICOM_FALSE;
    UmicomIpcClear(outInfo, sizeof(*outInfo));
    outInfo->blocked = ipc->blocked; outInfo->completed = ipc->completed; outInfo->timedOut = ipc->timedOut;
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
        const UmicomKernelUserIpcWait *const wait = &ipc->waits[slot];
        if (wait->pending == UMICOM_FALSE) continue;
        ++outInfo->waiting;
        if (wait->hasDeadline != UMICOM_FALSE &&
            (outInfo->hasDeadline == UMICOM_FALSE || wait->deadline < outInfo->nextDeadline)) {
            outInfo->hasDeadline = UMICOM_TRUE;
            outInfo->nextDeadline = wait->deadline;
        }
    }
    return UMICOM_TRUE;
}
