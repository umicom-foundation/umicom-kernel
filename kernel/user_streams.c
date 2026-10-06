/*-----------------------------------------------------------------------------
 * Umicom Kernel standard-stream ownership and checked byte movement
 * File: kernel/user_streams.c
 *
 * An accepted output chunk is Kernel-owned even after its producer exits. Input
 * is consumed only after checking the whole requested destination. A waiting
 * call stores values in this stable owner, never pointers into a past trap stack.
 *
 * The existing scheduler owns the continuation. A pending stream operation is
 * an additional eligibility condition on PAUSED, not a second context switch.
 * Pump completes the saved result without running or charging the user program.
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation. LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_streams.h"
#include "umicom/kernel/object_cache.h"

/* One exact binding matches the existing single-hart invocation discipline. */
static UmicomKernelUserStreams *umicomStreamOwner;
static UmicomKernelUserTask *umicomStreamTask;

static void UmicomStreamClear(void *object, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)object;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static void UmicomStreamCopy(void *destination, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)destination;
    const UmicomU8 *in = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
static UmicomBoolean UmicomStreamZero(const void *object, UmicomSize bytes)
{
    const UmicomU8 *in = (const UmicomU8 *)object;
    for (UmicomSize i = 0U; i < bytes; ++i) if (in[i] != 0U) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomStreamLive(const UmicomKernelUserTask *task)
{
    return task->state == UMICOM_USER_TASK_READY || task->state == UMICOM_USER_TASK_RUNNING ||
        task->state == UMICOM_USER_TASK_PAUSED || task->state == UMICOM_USER_TASK_BLOCKED ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelUserTaskHandle UmicomStreamToken(const UmicomKernelUserTask *task, UmicomSize slot)
{
    return ((UmicomU64)task->generation << 32U) | (slot + 1U);
}
static UmicomBoolean UmicomStreamOwnerReady(UmicomKernelUserStreams *owner)
{
    return owner && owner->self == owner && owner->initialised && !owner->closed && !owner->poisoned &&
        owner->scheduler && owner->scheduler->self == owner->scheduler && owner->scheduler->streams == owner &&
        !owner->scheduler->active && !owner->scheduler->poisoned && !umicomStreamOwner &&
        UmicomKernelObjectCacheAccessAllowed() ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelUserStreamRecord *UmicomStreamRecord(UmicomKernelUserStreams *owner,
    const UmicomKernelUserTask *task)
{
    if (owner && owner->scheduler) {
        for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i)
            if (&owner->scheduler->tasks[i] == task) return &owner->records[i];
    }
    return (UmicomKernelUserStreamRecord *)0;
}
static UmicomKernelUserStreamRecord *UmicomStreamFind(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle)
{
    const UmicomU32 low = (UmicomU32)handle;
    if (!low || low > UMICOM_USER_TASK_LIMIT || !(handle >> 32U)) return (UmicomKernelUserStreamRecord *)0;
    UmicomKernelUserStreamRecord *record = &owner->records[low - 1U];
    const UmicomKernelUserTask *task = &owner->scheduler->tasks[low - 1U];
    if (record->task != handle || handle != UmicomStreamToken(task, low - 1U) ||
        task->state == UMICOM_USER_TASK_EMPTY || record->identity != task->process.identity)
        return (UmicomKernelUserStreamRecord *)0;
    return record;
}
static UmicomBoolean UmicomStreamRecordValid(UmicomKernelUserStreamRecord *r)
{
    if (!r->task) return UmicomStreamZero(r, sizeof(*r));
    if (!r->identity || r->inputHead >= UMICOM_STREAM_INPUT_BYTES || r->inputCount > UMICOM_STREAM_INPUT_BYTES ||
        r->outputHead >= UMICOM_STREAM_OUTPUT_RECORDS || r->outputCount > UMICOM_STREAM_OUTPUT_RECORDS ||
        (r->wait != UMICOM_STREAM_WAIT_NONE && r->wait != UMICOM_STREAM_WAIT_INPUT && r->wait != UMICOM_STREAM_WAIT_OUTPUT))
        return UMICOM_FALSE;
    if (r->wait != UMICOM_STREAM_WAIT_NONE && (r->stopped || !r->bytes ||
        r->bytes > UMICOM_STREAM_TRANSFER_BYTES || !r->nextPc)) return UMICOM_FALSE;
    if (r->wait == UMICOM_STREAM_WAIT_INPUT && r->selector != UMICOM_STREAM_INPUT) return UMICOM_FALSE;
    if (r->wait == UMICOM_STREAM_WAIT_OUTPUT && r->selector != UMICOM_STREAM_OUTPUT &&
        r->selector != UMICOM_STREAM_ERROR) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < r->outputCount; ++i) {
        const UmicomKernelStreamPacket *p = &r->output[(r->outputHead + i) % UMICOM_STREAM_OUTPUT_RECORDS];
        if (!p->bytes || p->bytes > UMICOM_STREAM_TRANSFER_BYTES ||
            (p->selector != UMICOM_STREAM_OUTPUT && p->selector != UMICOM_STREAM_ERROR)) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
static void UmicomStreamForgetWait(UmicomKernelUserStreamRecord *r)
{
    r->wait = UMICOM_STREAM_WAIT_NONE;
    r->address = 0U;
    r->nextPc = 0U;
    r->bytes = 0U;
    r->selector = 0U;
    UmicomStreamClear(r->pending, sizeof(r->pending));
}
static void UmicomStreamStop(UmicomKernelUserStreamRecord *r)
{
    /* Accepted output survives. Unaccepted output and unread input do not belong
     * to the next process that happens to reuse this scheduler slot. */
    UmicomStreamForgetWait(r);
    UmicomStreamClear(r->input, sizeof(r->input));
    r->inputCount = 0U;
    r->inputHead = 0U;
    r->inputEnded = UMICOM_TRUE;
    r->stopped = UMICOM_TRUE;
}
static void UmicomStreamEnqueue(UmicomKernelUserStreamRecord *r,
    UmicomU64 selector, const UmicomU8 *data, UmicomSize bytes)
{
    /* The caller has checked capacity. Clear unused tail bytes before publishing
     * the count, so a host-side packet snapshot cannot expose old payload data. */
    UmicomKernelStreamPacket *p = &r->output[(r->outputHead + r->outputCount) % UMICOM_STREAM_OUTPUT_RECORDS];
    UmicomStreamClear(p, sizeof(*p));
    p->selector = selector;
    p->bytes = bytes;
    UmicomStreamCopy(p->data, data, bytes);
    ++r->outputCount;
}
static UmicomU64 UmicomStreamUserRead(const UmicomKernelUserMemory *memory,
    UmicomAddress address, UmicomU8 *out, UmicomSize bytes)
{
    /* Keep the earlier copy primitive's deliberately small bound unchanged.
     * Preflight the entire stream operation before its first bounded chunk. */
    UmicomU64 status = UmicomKernelUserMemoryCheck(memory, address, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    if (status != UMICOM_USER_RESULT_OK) return status;
    for (UmicomSize done = 0U; done < bytes;) {
        const UmicomSize part = bytes - done < UMICOM_USER_COPY_LIMIT ? bytes - done : UMICOM_USER_COPY_LIMIT;
        status = UmicomKernelUserMemoryRead(memory, address + done, out + done, part);
        if (status != UMICOM_USER_RESULT_OK) return status;
        done += part;
    }
    return UMICOM_USER_RESULT_OK;
}
static UmicomU64 UmicomStreamUserWrite(const UmicomKernelUserMemory *memory,
    UmicomAddress address, const UmicomU8 *in, UmicomSize bytes)
{
    UmicomU64 status = UmicomKernelUserMemoryCheck(memory, address, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE);
    if (status != UMICOM_USER_RESULT_OK) return status;
    for (UmicomSize done = 0U; done < bytes;) {
        const UmicomSize part = bytes - done < UMICOM_USER_COPY_LIMIT ? bytes - done : UMICOM_USER_COPY_LIMIT;
        status = UmicomKernelUserMemoryWrite(memory, address + done, in + done, part);
        if (status != UMICOM_USER_RESULT_OK) return status;
        done += part;
    }
    return UMICOM_USER_RESULT_OK;
}
static UmicomKernelStreamStatus UmicomStreamRead(UmicomKernelUserStreamRecord *r,
    const UmicomKernelUserMemory *memory, UmicomAddress address, UmicomSize bytes, UmicomSize *outBytes)
{
    *outBytes = 0U;
    /* Validate the complete requested span, not just today's available prefix.
     * A bad second page must leave both input and the first page unchanged. */
    if (UmicomKernelUserMemoryCheck(memory, address, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE)
        != UMICOM_USER_RESULT_OK) return UMICOM_STREAM_BAD_BUFFER;
    if (!r->inputCount) return r->inputEnded ? UMICOM_STREAM_EOF : UMICOM_STREAM_WOULD_BLOCK;
    const UmicomSize count = bytes < r->inputCount ? bytes : r->inputCount;
    UmicomU8 copy[UMICOM_STREAM_TRANSFER_BYTES];
    UmicomStreamClear(copy, sizeof(copy));
    for (UmicomSize i = 0U; i < count; ++i) copy[i] = r->input[(r->inputHead + i) % UMICOM_STREAM_INPUT_BYTES];
    if (UmicomStreamUserWrite(memory, address, copy, count) != UMICOM_USER_RESULT_OK) {
        UmicomStreamClear(copy, sizeof(copy));
        return UMICOM_STREAM_BAD_BUFFER;
    }
    UmicomStreamClear(copy, sizeof(copy));
    /* Commit consumption only after the copy-out succeeds. No mapper or other
     * hart runs between this preflight and commit in the supported profile. */
    for (UmicomSize i = 0U; i < count; ++i) r->input[(r->inputHead + i) % UMICOM_STREAM_INPUT_BYTES] = 0U;
    r->inputHead = (r->inputHead + count) % UMICOM_STREAM_INPUT_BYTES;
    r->inputCount -= count;
    *outBytes = count;
    return UMICOM_STREAM_OK;
}
static UmicomU64 UmicomStreamResult(UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame,
    UmicomAddress nextPc, UmicomKernelStreamStatus status, UmicomSize bytes)
{
    frame->x10_a0 = (UmicomU64)status;
    frame->x11_a1 = bytes;
    frame->mepc = nextPc;
    if (status != UMICOM_STREAM_OK && status != UMICOM_STREAM_EOF) ++session->rejectedCalls;
    return 1U;
}

UmicomKernelStreamStatus UmicomKernelUserStreamsAttach(UmicomKernelUserStreams *owner,
    UmicomKernelUserScheduler *scheduler)
{
    if (!owner || !scheduler) return UMICOM_STREAM_INVALID_ARGUMENT;
    if (!UmicomKernelObjectCacheAccessAllowed() || !UmicomStreamZero(owner, sizeof(*owner)) ||
        scheduler->self != scheduler || !scheduler->initialised || scheduler->active || scheduler->poisoned ||
        scheduler->streams) return UMICOM_STREAM_BAD_STATE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i)
        if (scheduler->tasks[i].state != UMICOM_USER_TASK_EMPTY) return UMICOM_STREAM_BAD_STATE;
    owner->self = owner;
    owner->scheduler = scheduler;
    owner->initialised = UMICOM_TRUE;
    scheduler->streams = owner;
    return UMICOM_STREAM_OK;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsGrant(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle)
{
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    UmicomKernelUserTaskInfo info;
    UmicomStreamClear(&info, sizeof(info));
    if (UmicomKernelUserTaskQuery(owner->scheduler, handle, &info) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_STREAM_INVALID_HANDLE;
    if (info.state != UMICOM_USER_TASK_READY || info.slices) return UMICOM_STREAM_BAD_STATE;
    UmicomKernelUserStreamRecord *r = &owner->records[(UmicomU32)handle - 1U];
    if (!UmicomStreamZero(r, sizeof(*r))) return UMICOM_STREAM_BAD_STATE;
    r->task = handle;
    r->identity = info.identity;
    return UMICOM_STREAM_OK;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsInput(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle, const UmicomU8 *data, UmicomSize bytes)
{
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    if ((!data && bytes) || bytes > UMICOM_STREAM_INPUT_BYTES) return UMICOM_STREAM_INVALID_ARGUMENT;
    UmicomKernelUserStreamRecord *r = UmicomStreamFind(owner, handle);
    if (!r) return UMICOM_STREAM_INVALID_HANDLE;
    if (!UmicomStreamRecordValid(r)) return UMICOM_STREAM_CORRUPT_STATE;
    if (r->stopped || r->inputEnded || !UmicomStreamLive(&owner->scheduler->tasks[(UmicomU32)handle - 1U]))
        return UMICOM_STREAM_BAD_STATE;
    if (bytes > UMICOM_STREAM_INPUT_BYTES - r->inputCount) return UMICOM_STREAM_WOULD_BLOCK;
    for (UmicomSize i = 0U; i < bytes; ++i)
        r->input[(r->inputHead + r->inputCount + i) % UMICOM_STREAM_INPUT_BYTES] = data[i];
    r->inputCount += bytes; /* A line is published only after its full copy. */
    return UMICOM_STREAM_OK;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsEndInput(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle)
{
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    UmicomKernelUserStreamRecord *r = UmicomStreamFind(owner, handle);
    if (!r) return UMICOM_STREAM_INVALID_HANDLE;
    if (!UmicomStreamRecordValid(r)) return UMICOM_STREAM_CORRUPT_STATE;
    if (r->stopped || !UmicomStreamLive(&owner->scheduler->tasks[(UmicomU32)handle - 1U])) return UMICOM_STREAM_BAD_STATE;
    r->inputEnded = UMICOM_TRUE; /* Idempotent, and buffered bytes are still readable. */
    return UMICOM_STREAM_OK;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsDrain(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle, UmicomKernelStreamPacket *out)
{
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    if (!out) return UMICOM_STREAM_INVALID_ARGUMENT;
    UmicomKernelUserStreamRecord *r = UmicomStreamFind(owner, handle);
    if (!r) return UMICOM_STREAM_INVALID_HANDLE;
    if (!UmicomStreamRecordValid(r)) return UMICOM_STREAM_CORRUPT_STATE;
    if (!r->outputCount) return UMICOM_STREAM_WOULD_BLOCK;
    UmicomKernelStreamPacket *p = &r->output[r->outputHead];
    UmicomStreamCopy(out, p, sizeof(*out));
    UmicomStreamClear(p, sizeof(*p));
    r->outputHead = (r->outputHead + 1U) % UMICOM_STREAM_OUTPUT_RECORDS;
    --r->outputCount;
    return UMICOM_STREAM_OK;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsQuery(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle, UmicomKernelStreamInfo *out)
{
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    if (!out) return UMICOM_STREAM_INVALID_ARGUMENT;
    UmicomKernelUserStreamRecord *r = UmicomStreamFind(owner, handle);
    if (!r) return UMICOM_STREAM_INVALID_HANDLE;
    if (!UmicomStreamRecordValid(r)) return UMICOM_STREAM_CORRUPT_STATE;
    out->inputBytes = r->inputCount;
    out->outputRecords = r->outputCount;
    out->inputEnded = r->inputEnded;
    out->stopped = r->stopped;
    out->wait = r->wait;
    return UMICOM_STREAM_OK;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsDiscard(UmicomKernelUserStreams *owner,
    UmicomKernelUserTaskHandle handle)
{
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    UmicomKernelUserStreamRecord *r = UmicomStreamFind(owner, handle);
    if (!r) return UMICOM_STREAM_INVALID_HANDLE;
    UmicomKernelUserTask *task = &owner->scheduler->tasks[(UmicomU32)handle - 1U];
    if (UmicomStreamLive(task) || !task->process.quiesced) return UMICOM_STREAM_BAD_STATE;
    if (!UmicomStreamRecordValid(r)) return UMICOM_STREAM_CORRUPT_STATE;
    UmicomStreamStop(r);
    UmicomStreamClear(r->output, sizeof(r->output));
    r->outputHead = 0U;
    r->outputCount = 0U;
    return UMICOM_STREAM_OK;
}
UmicomBoolean UmicomKernelUserStreamsBegin(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->streams) return UMICOM_TRUE;
    UmicomKernelUserStreams *owner = scheduler->streams;
    if (!UmicomStreamOwnerReady(owner) || umicomStreamTask) return UMICOM_FALSE;
    UmicomKernelUserStreamRecord *r = UmicomStreamRecord(owner, task);
    if (!r || !UmicomStreamRecordValid(r) || r->wait != UMICOM_STREAM_WAIT_NONE || r->stopped) return UMICOM_FALSE;
    if (r->task && (r->task != UmicomStreamToken(task, (UmicomSize)(r - owner->records)) ||
        r->identity != task->process.identity)) return UMICOM_FALSE;
    umicomStreamOwner = owner;
    umicomStreamTask = task; /* Even an ungranted task is bound, but has no authority. */
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserStreamsEnd(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->streams) return UMICOM_TRUE;
    if (umicomStreamOwner != scheduler->streams || umicomStreamTask != task) return UMICOM_FALSE;
    /* Unbind on every return, including a failed architecture restoration. No
     * queue or user address is touched until that restoration is verified. */
    umicomStreamOwner = (UmicomKernelUserStreams *)0;
    umicomStreamTask = (UmicomKernelUserTask *)0;
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserStreamsWaiting(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->streams) return UMICOM_FALSE;
    UmicomKernelUserStreamRecord *r = UmicomStreamRecord(scheduler->streams, task);
    return r && r->wait != UMICOM_STREAM_WAIT_NONE ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomU64 UmicomKernelUserStreamDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc)
{
    if (!session || !frame) return 0U;
    UmicomKernelUserStreams *owner = umicomStreamOwner;
    UmicomKernelUserTask *task = umicomStreamTask;
    if (!owner || !task || owner->self != owner || !owner->initialised || owner->closed || owner->poisoned ||
        !owner->scheduler || owner->scheduler->streams != owner || !owner->scheduler->active ||
        task->state != UMICOM_USER_TASK_RUNNING || &task->process.report != session ||
        !session->identity || session->identity != task->process.identity)
        return UmicomStreamResult(session, frame, nextPc, UMICOM_STREAM_UNBOUND, 0U);
    UmicomKernelUserStreamRecord *r = UmicomStreamRecord(owner, task);
    if (!r || !r->task || r->identity != session->identity ||
        r->task != UmicomStreamToken(task, (UmicomSize)(r - owner->records)))
        return UmicomStreamResult(session, frame, nextPc, UMICOM_STREAM_UNBOUND, 0U);
    if (!UmicomStreamRecordValid(r) || r->stopped || r->wait != UMICOM_STREAM_WAIT_NONE) {
        owner->poisoned = UMICOM_TRUE;
        session->stopReason = UMICOM_USER_STOP_MONITOR_ERROR;
        return 0U; /* Never resume through corrupted Kernel-owned queue metadata. */
    }
    const UmicomBoolean reading = frame->x17_a7 == UMICOM_USER_CALL_STREAM_READ ? UMICOM_TRUE : UMICOM_FALSE;
    const UmicomU64 selector = frame->x10_a0;
    const UmicomAddress address = (UmicomAddress)frame->x11_a1;
    const UmicomSize bytes = frame->x12_a2;
    if ((!reading && frame->x17_a7 != UMICOM_USER_CALL_STREAM_WRITE) ||
        (frame->x13_a3 & ~(UmicomU64)UMICOM_STREAM_NONBLOCK))
        return UmicomStreamResult(session, frame, nextPc, UMICOM_STREAM_INVALID_ARGUMENT, 0U);
    if ((reading && selector != UMICOM_STREAM_INPUT) ||
        (!reading && selector != UMICOM_STREAM_OUTPUT && selector != UMICOM_STREAM_ERROR))
        return UmicomStreamResult(session, frame, nextPc, UMICOM_STREAM_WRONG_DIRECTION, 0U);
    if (bytes > UMICOM_STREAM_TRANSFER_BYTES)
        return UmicomStreamResult(session, frame, nextPc, UMICOM_STREAM_TOO_LARGE, 0U);
    if (!bytes) return UmicomStreamResult(session, frame, nextPc, UMICOM_STREAM_OK, 0U);
    UmicomKernelStreamStatus status = UMICOM_STREAM_OK;
    UmicomSize transferred = 0U;
    if (reading) {
        status = UmicomStreamRead(r, &session->memory, address, bytes, &transferred);
    } else {
        /* Snapshot before suspension. A later completion must not reread a
         * caller buffer whose lifetime or contents might no longer match. */
        if (UmicomStreamUserRead(&session->memory, address, r->pending, bytes) != UMICOM_USER_RESULT_OK)
            status = UMICOM_STREAM_BAD_BUFFER;
        else if (r->outputCount == UMICOM_STREAM_OUTPUT_RECORDS) status = UMICOM_STREAM_WOULD_BLOCK;
        else { UmicomStreamEnqueue(r, selector, r->pending, bytes); transferred = bytes; }
    }
    if (status != UMICOM_STREAM_WOULD_BLOCK || (frame->x13_a3 & UMICOM_STREAM_NONBLOCK)) {
        UmicomStreamClear(r->pending, sizeof(r->pending));
        return UmicomStreamResult(session, frame, nextPc, status, transferred);
    }
    r->address = address;
    r->nextPc = nextPc;
    r->bytes = bytes;
    r->selector = selector;
    r->wait = reading ? UMICOM_STREAM_WAIT_INPUT : UMICOM_STREAM_WAIT_OUTPUT;
    frame->mepc = nextPc; /* Advance once. Pump never executes the ECALL again. */
    return 0U; /* Existing Assembly copies the frame and restores machine state. */
}
UmicomKernelUserScheduleStatus UmicomKernelUserStreamsSuspend(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTask *task, UmicomBoolean expired)
{
    UmicomKernelUserStreams *owner = scheduler->streams;
    UmicomKernelUserStreamRecord *r = UmicomStreamRecord(owner, task);
    if (!UmicomStreamOwnerReady(owner) || !r || !UmicomStreamRecordValid(r) ||
        r->wait == UMICOM_STREAM_WAIT_NONE || expired || task->frame.mcause != 8U ||
        task->frame.mepc != r->nextPc || task->process.report.stopReason != UMICOM_USER_STOP_NONE) {
        scheduler->poisoned = UMICOM_TRUE;
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
    task->state = UMICOM_USER_TASK_PAUSED;
    if (task->slices == task->sliceLimit) {
        /* A waiting call cannot earn another slice after exhausting the task's
         * lifetime budget. Its unaccepted output is discarded; earlier accepted
         * output remains drainable before collection. */
        task->state = UMICOM_USER_TASK_EXHAUSTED;
        task->process.state = UMICOM_PROCESS_CLEANUP_REQUIRED;
        task->process.quiesced = UMICOM_TRUE;
        UmicomStreamStop(r);
    }
    return UMICOM_USER_SCHEDULE_OK;
}
UmicomBoolean UmicomKernelUserStreamsPump(UmicomKernelUserScheduler *scheduler)
{
    if (!scheduler->streams) return UMICOM_TRUE;
    UmicomKernelUserStreams *owner = scheduler->streams;
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
        UmicomKernelUserStreamRecord *r = &owner->records[i];
        UmicomKernelUserTask *task = &scheduler->tasks[i];
        if (!UmicomStreamRecordValid(r)) { owner->poisoned = UMICOM_TRUE; return UMICOM_FALSE; }
        if (!r->task) continue;
        if (r->task != UmicomStreamToken(task, i) || r->identity != task->process.identity ||
            task->state == UMICOM_USER_TASK_EMPTY || task->state == UMICOM_USER_TASK_RETAINED) {
            owner->poisoned = UMICOM_TRUE;
            return UMICOM_FALSE;
        }
        if (!UmicomStreamLive(task)) {
            if (!task->process.quiesced) return UMICOM_FALSE;
            UmicomStreamStop(r);
            continue;
        }
        if (r->stopped) return UMICOM_FALSE;
        if (r->wait == UMICOM_STREAM_WAIT_NONE) continue;
        if (task->state != UMICOM_USER_TASK_PAUSED || task->frame.mepc != r->nextPc ||
            task->process.quiesced || task->process.state != UMICOM_PROCESS_RUNNING) return UMICOM_FALSE;
        UmicomKernelStreamStatus status = UMICOM_STREAM_WOULD_BLOCK;
        UmicomSize transferred = 0U;
        if (r->wait == UMICOM_STREAM_WAIT_INPUT) {
            status = UmicomStreamRead(r, &task->process.report.memory, r->address, r->bytes, &transferred);
        } else if (r->outputCount < UMICOM_STREAM_OUTPUT_RECORDS) {
            UmicomStreamEnqueue(r, r->selector, r->pending, r->bytes);
            status = UMICOM_STREAM_OK;
            transferred = r->bytes;
        }
        if (status == UMICOM_STREAM_WOULD_BLOCK) continue;
        /* Publish the result before clearing eligibility's wait condition. No
         * user instructions or budget counters are advanced by this pump. */
        (void)UmicomStreamResult(&task->process.report, &task->frame, r->nextPc, status, transferred);
        UmicomStreamForgetWait(r);
    }
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserStreamsStop(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->streams) return UMICOM_TRUE;
    UmicomKernelUserStreams *owner = scheduler->streams;
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_FALSE;
    UmicomKernelUserStreamRecord *r = UmicomStreamRecord(owner, task);
    if (!r || !UmicomStreamRecordValid(r)) return UMICOM_FALSE;
    if (!r->task) return UMICOM_TRUE;
    if (r->task != UmicomStreamToken(task, (UmicomSize)(r - owner->records)) ||
        r->identity != task->process.identity || UmicomStreamLive(task) || !task->process.quiesced) return UMICOM_FALSE;
    UmicomStreamStop(r);
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserStreamsReap(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->streams) return UMICOM_TRUE;
    UmicomKernelUserStreams *owner = scheduler->streams;
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_FALSE;
    UmicomKernelUserStreamRecord *r = UmicomStreamRecord(owner, task);
    if (!r || !UmicomStreamRecordValid(r)) return UMICOM_FALSE;
    if (!r->task) return UMICOM_TRUE;
    if (r->task != UmicomStreamToken(task, (UmicomSize)(r - owner->records)) ||
        r->identity != task->process.identity || UmicomStreamLive(task) || !task->process.quiesced) return UMICOM_FALSE;
    UmicomStreamStop(r);
    if (r->outputCount) return UMICOM_FALSE; /* Explicit Drain or Discard, never hidden loss. */
    UmicomStreamClear(r, sizeof(*r));
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserStreamsValidate(UmicomKernelUserScheduler *scheduler)
{
    if (!scheduler->streams) return UMICOM_TRUE;
    UmicomKernelUserStreams *owner = scheduler->streams;
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
        UmicomKernelUserStreamRecord *r = &owner->records[i];
        const UmicomKernelUserTask *task = &scheduler->tasks[i];
        if (!UmicomStreamRecordValid(r)) return UMICOM_FALSE;
        if (!r->task) continue;
        if (r->task != UmicomStreamToken(task, i) || r->identity != task->process.identity ||
            task->state == UMICOM_USER_TASK_EMPTY || task->state == UMICOM_USER_TASK_RETAINED) return UMICOM_FALSE;
        if (r->wait != UMICOM_STREAM_WAIT_NONE && (task->state != UMICOM_USER_TASK_PAUSED ||
            task->frame.mepc != r->nextPc || task->process.quiesced)) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
UmicomKernelStreamStatus UmicomKernelUserStreamsClose(UmicomKernelUserStreams *owner)
{
    if (!owner || owner->self != owner) return UMICOM_STREAM_BAD_STATE;
    if (owner->closed) return UMICOM_STREAM_OK;
    if (!UmicomStreamOwnerReady(owner)) return UMICOM_STREAM_BAD_STATE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i)
        if (owner->records[i].task || owner->scheduler->tasks[i].state != UMICOM_USER_TASK_EMPTY)
            return UMICOM_STREAM_BAD_STATE;
    owner->scheduler->streams = (UmicomKernelUserStreams *)0;
    owner->closed = UMICOM_TRUE;
    return UMICOM_STREAM_OK;
}
