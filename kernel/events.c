/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/events.c
 *
 * PURPOSE:
 *   Implement remembered events and bounded wait-any registration above the
 *   existing cooperative scheduler. No scheduler record is edited here.
 *
 * EDUCATIONAL OVERVIEW:
 *   The dangerous interval in a wait protocol is between checking a condition
 *   and publishing that we intend to sleep. Here the check, registration and
 *   suspension run without a yield or interrupt. Another callback cannot slip
 *   a signal into that interval. Signal then records the outcome before waking
 *   the thread, so an immediate Reset cannot erase the awarded result.
 *
 *   Pending registrations belong to the domain, not to a suspended C stack.
 *   Cancellation can abandon that stack; Pump recognises the cancelled or
 *   stale thread handle and releases only its registration. It never resumes
 *   cancelled code and never dereferences the abandoned callback's outputs.
 *
 *   This reasoning depends on one serialised hart and the scheduler's existing
 *   interrupts-disabled contract. Adding pre-emption later requires a genuine
 *   critical-section design; volatile alone would not make this safe.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/events.h"

static void UmicomEventClear(void *storage, UmicomSize bytes)
{
    /* Volatile byte stores keep scrubbing explicit in a freestanding build.
     * There is no hidden allocation and no dependency on a hosted memset. */
    volatile UmicomU8 *const destination = (volatile UmicomU8 *)storage;
    for (UmicomSize index = 0U; index < bytes; ++index) {
        destination[index] = 0U;
    }
}

static UmicomBoolean UmicomEventBoolean(UmicomBoolean value)
{
    /* An enum's storage can still be corrupted; do not treat arbitrary
     * nonzero values as a valid published state. */
    return value == UMICOM_FALSE || value == UMICOM_TRUE ? UMICOM_TRUE : UMICOM_FALSE;
}

static UmicomKernelEventHandle UmicomEventToken(UmicomSize slot, UmicomU32 generation)
{
    /* The low half uses index+1 so zero is never a live handle. */
    return ((UmicomU64)generation << 32U) | (UmicomU64)(slot + 1U);
}

static UmicomKernelEventStatus UmicomEventLookup(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle token, UmicomSize *outSlot)
{
    /* Domain validation is the caller's responsibility. Check the token before
     * it can select an array element, including malformed large slot values. */
    const UmicomU32 encoded = (UmicomU32)token;
    const UmicomU32 generation = (UmicomU32)(token >> 32U);
    if (encoded == 0U || encoded > UMICOM_EVENT_LIMIT || generation == 0U) {
        return UMICOM_EVENT_INVALID_HANDLE;
    }
    const UmicomSize slot = (UmicomSize)encoded - 1U;
    const UmicomKernelEventRecord *const event = &domain->events[slot];
    if (event->occupied != UMICOM_TRUE || event->generation != generation) {
        return UMICOM_EVENT_INVALID_HANDLE;
    }
    *outSlot = slot; /* Publish a usable index only after both checks. */
    return UMICOM_EVENT_OK;
}

static UmicomKernelEventStatus UmicomEventDomain(UmicomKernelEventDomain *domain)
{
    if (domain == (UmicomKernelEventDomain *)0) return UMICOM_EVENT_INVALID_ARGUMENT;
    if (domain->initialised != UMICOM_TRUE) return UMICOM_EVENT_NOT_INITIALISED;
    /* A struct copy is not a second domain: its borrowed scheduler and tokens
     * still belong to the original object. Refuse instead of resetting it. */
    if (domain->self != domain || domain->poisoned != UMICOM_FALSE ||
        domain->scheduler == (UmicomKernelScheduler *)0) return UMICOM_EVENT_CORRUPT_STATE;
    if (UmicomKernelSchedulerValidate(domain->scheduler) != UMICOM_THREAD_OK ||
        UmicomKernelThreadMachineReady() != UMICOM_TRUE) return UMICOM_EVENT_SCHEDULER_ERROR;
    return UMICOM_EVENT_OK;
}

UmicomKernelEventStatus UmicomKernelEventDomainValidate(UmicomKernelEventDomain *domain)
{
    const UmicomKernelEventStatus status = UmicomEventDomain(domain);
    if (status != UMICOM_EVENT_OK) return status;
    /* Zero denotes exhausted tickets. It is deliberately not wrapped to one. */
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_LIMIT; ++slot) {
        const UmicomKernelEventRecord *const event = &domain->events[slot];
        if (event->generation == 0U || UmicomEventBoolean(event->occupied) == UMICOM_FALSE ||
            UmicomEventBoolean(event->retired) == UMICOM_FALSE ||
            UmicomEventBoolean(event->signalled) == UMICOM_FALSE) return UMICOM_EVENT_CORRUPT_STATE;
        if (event->retired != UMICOM_FALSE && (event->occupied != UMICOM_FALSE ||
            event->generation != ~(UmicomU32)0U)) return UMICOM_EVENT_CORRUPT_STATE;
        if (event->occupied == UMICOM_FALSE && event->signalled != UMICOM_FALSE)
            return UMICOM_EVENT_CORRUPT_STATE;
        if (event->occupied != UMICOM_FALSE && event->mode != UMICOM_EVENT_MANUAL_RESET &&
            event->mode != UMICOM_EVENT_AUTO_RESET) return UMICOM_EVENT_CORRUPT_STATE;
    }
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_WAIT_LIMIT; ++slot) {
        const UmicomKernelEventWaitRecord *const wait = &domain->waiters[slot];
        if (UmicomEventBoolean(wait->occupied) == UMICOM_FALSE) return UMICOM_EVENT_CORRUPT_STATE;
        if (wait->occupied == UMICOM_FALSE) continue;
        if (wait->thread == 0U || wait->ticket == 0U || wait->count == 0U ||
            wait->count > UMICOM_EVENT_WAIT_ANY_LIMIT ||
            UmicomEventBoolean(wait->hasDeadline) == UMICOM_FALSE ||
            wait->result.outcome < UMICOM_EVENT_PENDING ||
            wait->result.outcome > UMICOM_EVENT_CLOSED) return UMICOM_EVENT_CORRUPT_STATE;
        /* A live ticket must have been issued before the next one. Zero is
         * the exhausted sentinel and intentionally has no numeric successor. */
        if (domain->nextTicket != 0U && wait->ticket >= domain->nextTicket)
            return UMICOM_EVENT_CORRUPT_STATE;
        for (UmicomSize previous = 0U; previous < slot; ++previous) {
            if (domain->waiters[previous].occupied != UMICOM_FALSE &&
                (domain->waiters[previous].thread == wait->thread ||
                 domain->waiters[previous].ticket == wait->ticket))
                return UMICOM_EVENT_CORRUPT_STATE;
        }
        for (UmicomSize index = 0U; index < wait->count; ++index) {
            for (UmicomSize previous = 0U; previous < index; ++previous) {
                if (wait->events[previous] == wait->events[index]) return UMICOM_EVENT_CORRUPT_STATE;
            }
            /* Completed waits may name a now-closed event. Pending ones cannot:
             * Close commits their CLOSED outcome before retiring the slot. */
            UmicomSize eventSlot = 0U;
            if (wait->result.outcome == UMICOM_EVENT_PENDING &&
                UmicomEventLookup(domain, wait->events[index], &eventSlot) != UMICOM_EVENT_OK)
                return UMICOM_EVENT_CORRUPT_STATE;
        }
        if (wait->result.outcome == UMICOM_EVENT_SIGNALED ||
            wait->result.outcome == UMICOM_EVENT_CLOSED) {
            if (wait->result.index >= wait->count ||
                wait->result.event != wait->events[wait->result.index])
                return UMICOM_EVENT_CORRUPT_STATE;
        } else if (wait->result.index != UMICOM_EVENT_NO_SELECTION || wait->result.event != 0U) {
            return UMICOM_EVENT_CORRUPT_STATE;
        }
    }
    return UMICOM_EVENT_OK;
}

UmicomKernelEventStatus UmicomKernelEventDomainInitialize(
    UmicomKernelEventDomain *domain, UmicomKernelScheduler *scheduler)
{
    if (domain == (UmicomKernelEventDomain *)0 || scheduler == (UmicomKernelScheduler *)0)
        return UMICOM_EVENT_INVALID_ARGUMENT;
    /* Reinitialisation would make stale tokens usable again, even if the
     * domain currently has no open events. */
    if (domain->self != (const UmicomKernelEventDomain *)0 || domain->initialised != UMICOM_FALSE)
        return UMICOM_EVENT_BAD_STATE;
    if (UmicomKernelSchedulerValidate(scheduler) != UMICOM_THREAD_OK ||
        UmicomKernelThreadMachineReady() != UMICOM_TRUE) return UMICOM_EVENT_SCHEDULER_ERROR;
    UmicomEventClear(domain, sizeof(*domain));
    domain->self = domain; /* Record its stable address before publication. */
    domain->scheduler = scheduler;
    domain->nextTicket = 1U;
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_LIMIT; ++slot) {
        domain->events[slot].generation = 1U;
    }
    domain->initialised = UMICOM_TRUE; /* Only the complete object is live. */
    return UMICOM_EVENT_OK;
}

static UmicomKernelEventStatus UmicomEventThread(
    UmicomKernelEventDomain *domain, UmicomKernelEventWaitRecord *wait,
    UmicomKernelThreadInfo *outInfo, UmicomBoolean *outLive)
{
    const UmicomKernelThreadStatus status =
        UmicomKernelThreadQuery(domain->scheduler, wait->thread, outInfo);
    *outLive = UMICOM_FALSE;
    if (status == UMICOM_THREAD_INVALID_HANDLE) return UMICOM_EVENT_OK;
    if (status != UMICOM_THREAD_OK) return UMICOM_EVENT_SCHEDULER_ERROR;
    if (outInfo->state != UMICOM_THREAD_CANCELLED && outInfo->state != UMICOM_THREAD_FINISHED)
        *outLive = UMICOM_TRUE;
    return UMICOM_EVENT_OK;
}

static UmicomKernelEventStatus UmicomEventComplete(
    UmicomKernelEventDomain *domain, UmicomKernelEventWaitRecord *wait,
    UmicomKernelEventOutcome outcome, UmicomSize index, UmicomU64 now)
{
    UmicomKernelThreadInfo info;
    UmicomBoolean live = UMICOM_FALSE;
    const UmicomKernelEventStatus status = UmicomEventThread(domain, wait, &info, &live);
    if (status != UMICOM_EVENT_OK) return status;
    if (live == UMICOM_FALSE) {
        /* Cancellation abandons the call stack, not a pointer we may write
         * into later. Discard only this domain's copied registration. */
        UmicomEventClear(wait, sizeof(*wait));
        return UMICOM_EVENT_OK;
    }
    /* Remember the decision before scheduling a continuation. Reset or Close
     * cannot subsequently retract this already awarded result. */
    wait->result.outcome = outcome;
    wait->result.index = index;
    wait->result.event = index == UMICOM_EVENT_NO_SELECTION ? 0U : wait->events[index];
    wait->result.observedTime = now;
    if (info.state == UMICOM_THREAD_WAITING || info.state == UMICOM_THREAD_SLEEPING) {
        if (UmicomKernelThreadWake(domain->scheduler, wait->thread) != UMICOM_THREAD_OK) {
            /* An unsafe scheduler relationship must not be called success.
             * Preserve the records for diagnosis instead of reusing slots. */
            domain->poisoned = UMICOM_TRUE;
            return UMICOM_EVENT_SCHEDULER_ERROR;
        }
    } else if (info.state != UMICOM_THREAD_READY && info.state != UMICOM_THREAD_RUNNING) {
        domain->poisoned = UMICOM_TRUE;
        return UMICOM_EVENT_SCHEDULER_ERROR;
    }
    return UMICOM_EVENT_OK;
}

static UmicomKernelEventStatus UmicomEventSelect(
    UmicomKernelEventDomain *domain, UmicomKernelEventWaitRecord *wait,
    UmicomU64 now, UmicomBoolean *outSelected)
{
    *outSelected = UMICOM_FALSE;
    /* Array order breaks ties within one wait. The enclosing pump considers
     * registrations in ticket order, so slot reuse does not change fairness. */
    for (UmicomSize index = 0U; index < wait->count; ++index) {
        UmicomSize slot = 0U;
        if (UmicomEventLookup(domain, wait->events[index], &slot) != UMICOM_EVENT_OK)
            return UMICOM_EVENT_CORRUPT_STATE;
        UmicomKernelEventRecord *const event = &domain->events[slot];
        if (event->signalled == UMICOM_FALSE) continue;
        const UmicomKernelEventStatus status =
            UmicomEventComplete(domain, wait, UMICOM_EVENT_SIGNALED, index, now);
        if (status != UMICOM_EVENT_OK) return status;
        /* A dead waiter did not earn a credit. Live completions are retained
         * until the waiting call resumes, even if a peer resets the event. */
        if (wait->occupied != UMICOM_FALSE) {
            if (event->mode == UMICOM_EVENT_AUTO_RESET) event->signalled = UMICOM_FALSE;
            *outSelected = UMICOM_TRUE;
        }
        return UMICOM_EVENT_OK;
    }
    return UMICOM_EVENT_OK;
}

UmicomKernelEventStatus UmicomKernelEventPump(UmicomKernelEventDomain *domain)
{
    const UmicomKernelEventStatus status = UmicomKernelEventDomainValidate(domain);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomKernelSchedulerInfo schedulerInfo;
    if (UmicomKernelSchedulerSnapshot(domain->scheduler, &schedulerInfo) != UMICOM_THREAD_OK)
        return UMICOM_EVENT_SCHEDULER_ERROR;
    /* Settle cancellation and deadlines before a later signal changes state.
     * The clock is supplied by the existing dispatcher, not read from MMIO. */
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_WAIT_LIMIT; ++slot) {
        UmicomKernelEventWaitRecord *const wait = &domain->waiters[slot];
        if (wait->occupied == UMICOM_FALSE) continue;
        UmicomKernelThreadInfo info;
        UmicomBoolean live = UMICOM_FALSE;
        const UmicomKernelEventStatus threadStatus = UmicomEventThread(domain, wait, &info, &live);
        if (threadStatus != UMICOM_EVENT_OK) return threadStatus;
        if (live == UMICOM_FALSE) {
            UmicomEventClear(wait, sizeof(*wait));
            continue;
        }
        if (wait->result.outcome == UMICOM_EVENT_PENDING &&
            wait->hasDeadline != UMICOM_FALSE && wait->deadline <= schedulerInfo.now) {
            const UmicomKernelEventStatus completeStatus = UmicomEventComplete(
                domain, wait, UMICOM_EVENT_TIMED_OUT, UMICOM_EVENT_NO_SELECTION, schedulerInfo.now);
            if (completeStatus != UMICOM_EVENT_OK) return completeStatus;
        }
    }
    UmicomU64 previousTicket = 0U;
    for (UmicomSize pass = 0U; pass < UMICOM_EVENT_WAIT_LIMIT; ++pass) {
        UmicomKernelEventWaitRecord *next = (UmicomKernelEventWaitRecord *)0;
        for (UmicomSize slot = 0U; slot < UMICOM_EVENT_WAIT_LIMIT; ++slot) {
            UmicomKernelEventWaitRecord *const wait = &domain->waiters[slot];
            if (wait->occupied != UMICOM_FALSE && wait->result.outcome == UMICOM_EVENT_PENDING &&
                wait->ticket > previousTicket &&
                (next == (UmicomKernelEventWaitRecord *)0 || wait->ticket < next->ticket)) next = wait;
        }
        if (next == (UmicomKernelEventWaitRecord *)0) break;
        previousTicket = next->ticket; /* Preserve ordering even if selection clears it. */
        UmicomBoolean selected = UMICOM_FALSE;
        const UmicomKernelEventStatus selectedStatus =
            UmicomEventSelect(domain, next, schedulerInfo.now, &selected);
        if (selectedStatus != UMICOM_EVENT_OK) return selectedStatus;
    }
    return UMICOM_EVENT_OK;
}

UmicomKernelEventStatus UmicomKernelEventCreate(
    UmicomKernelEventDomain *domain, UmicomKernelEventMode mode,
    UmicomBoolean initiallySignalled, UmicomKernelEventHandle *outHandle)
{
    if (outHandle == (UmicomKernelEventHandle *)0 ||
        (mode != UMICOM_EVENT_MANUAL_RESET && mode != UMICOM_EVENT_AUTO_RESET) ||
        UmicomEventBoolean(initiallySignalled) == UMICOM_FALSE) return UMICOM_EVENT_INVALID_ARGUMENT;
    const UmicomKernelEventStatus status = UmicomKernelEventPump(domain);
    if (status != UMICOM_EVENT_OK) return status;
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_LIMIT; ++slot) {
        UmicomKernelEventRecord *const event = &domain->events[slot];
        if (event->occupied != UMICOM_FALSE || event->retired != UMICOM_FALSE) continue;
        event->mode = mode;
        event->signalled = initiallySignalled;
        event->occupied = UMICOM_TRUE;
        *outHandle = UmicomEventToken(slot, event->generation);
        return UMICOM_EVENT_OK;
    }
    return UMICOM_EVENT_CAPACITY; /* Never evict an event to make admission succeed. */
}

UmicomKernelEventStatus UmicomKernelEventSignal(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle handle)
{
    UmicomKernelEventStatus status = UmicomKernelEventPump(domain);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomSize slot = 0U;
    status = UmicomEventLookup(domain, handle, &slot);
    if (status != UMICOM_EVENT_OK) return status;
    domain->events[slot].signalled = UMICOM_TRUE; /* Remember even without a waiter. */
    return UmicomKernelEventPump(domain);
}

UmicomKernelEventStatus UmicomKernelEventReset(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle handle)
{
    UmicomKernelEventStatus status = UmicomKernelEventPump(domain);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomSize slot = 0U;
    status = UmicomEventLookup(domain, handle, &slot);
    if (status != UMICOM_EVENT_OK) return status;
    domain->events[slot].signalled = UMICOM_FALSE;
    return UMICOM_EVENT_OK;
}

UmicomKernelEventStatus UmicomKernelEventClose(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle handle)
{
    UmicomKernelEventStatus status = UmicomKernelEventPump(domain);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomSize slot = 0U;
    status = UmicomEventLookup(domain, handle, &slot);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomKernelSchedulerInfo info;
    if (UmicomKernelSchedulerSnapshot(domain->scheduler, &info) != UMICOM_THREAD_OK)
        return UMICOM_EVENT_SCHEDULER_ERROR;
    for (UmicomSize waitSlot = 0U; waitSlot < UMICOM_EVENT_WAIT_LIMIT; ++waitSlot) {
        UmicomKernelEventWaitRecord *const wait = &domain->waiters[waitSlot];
        if (wait->occupied == UMICOM_FALSE || wait->result.outcome != UMICOM_EVENT_PENDING) continue;
        for (UmicomSize index = 0U; index < wait->count; ++index) {
            if (wait->events[index] != handle) continue;
            status = UmicomEventComplete(domain, wait, UMICOM_EVENT_CLOSED, index, info.now);
            if (status != UMICOM_EVENT_OK) return status;
            break;
        }
    }
    UmicomKernelEventRecord *const event = &domain->events[slot];
    event->occupied = UMICOM_FALSE;
    event->signalled = UMICOM_FALSE;
    if (event->generation == ~(UmicomU32)0U) event->retired = UMICOM_TRUE;
    else ++event->generation; /* Old wait results may report it, but cannot reuse it. */
    return UMICOM_EVENT_OK;
}

UmicomKernelEventStatus UmicomKernelEventWaitAny(
    UmicomKernelEventDomain *domain, UmicomKernelThreadHandle thread,
    const UmicomKernelEventHandle *events, UmicomSize count,
    UmicomBoolean hasDeadline, UmicomU64 deadline, UmicomKernelEventResult *outResult)
{
    if (events == (const UmicomKernelEventHandle *)0 || outResult == (UmicomKernelEventResult *)0 ||
        count == 0U || count > UMICOM_EVENT_WAIT_ANY_LIMIT ||
        UmicomEventBoolean(hasDeadline) == UMICOM_FALSE) return UMICOM_EVENT_INVALID_ARGUMENT;
    UmicomKernelEventStatus status = UmicomKernelEventPump(domain);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomKernelThreadInfo threadInfo;
    if (UmicomKernelThreadQuery(domain->scheduler, thread, &threadInfo) != UMICOM_THREAD_OK)
        return UMICOM_EVENT_INVALID_HANDLE;
    if (threadInfo.state != UMICOM_THREAD_RUNNING) return UMICOM_EVENT_WRONG_CONTEXT;
    /* Preflight the whole set before a signalled auto-reset event is consumed. */
    for (UmicomSize index = 0U; index < count; ++index) {
        UmicomSize slot = 0U;
        status = UmicomEventLookup(domain, events[index], &slot);
        if (status != UMICOM_EVENT_OK) return status;
        for (UmicomSize previous = 0U; previous < index; ++previous) {
            if (events[previous] == events[index]) return UMICOM_EVENT_INVALID_ARGUMENT;
        }
    }
    UmicomKernelEventWaitRecord *wait = (UmicomKernelEventWaitRecord *)0;
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_WAIT_LIMIT; ++slot) {
        if (domain->waiters[slot].occupied != UMICOM_FALSE &&
            domain->waiters[slot].thread == thread) return UMICOM_EVENT_BAD_STATE;
        if (domain->waiters[slot].occupied == UMICOM_FALSE && wait == (UmicomKernelEventWaitRecord *)0)
            wait = &domain->waiters[slot];
    }
    if (wait == (UmicomKernelEventWaitRecord *)0) return UMICOM_EVENT_CAPACITY;
    if (domain->nextTicket == 0U) return UMICOM_EVENT_COUNTER_EXHAUSTED;
    UmicomEventClear(wait, sizeof(*wait));
    wait->thread = thread;
    wait->ticket = domain->nextTicket;
    domain->nextTicket = domain->nextTicket == ~(UmicomU64)0U ? 0U : domain->nextTicket + 1U;
    wait->count = count;
    for (UmicomSize index = 0U; index < count; ++index) wait->events[index] = events[index];
    wait->hasDeadline = hasDeadline;
    wait->deadline = deadline;
    wait->result.index = UMICOM_EVENT_NO_SELECTION;
    wait->occupied = UMICOM_TRUE; /* Only the copied, complete subscription is visible. */
    UmicomKernelSchedulerInfo info;
    if (UmicomKernelSchedulerSnapshot(domain->scheduler, &info) != UMICOM_THREAD_OK) {
        UmicomEventClear(wait, sizeof(*wait));
        return UMICOM_EVENT_SCHEDULER_ERROR;
    }
    /* A previously remembered credit is useful even for an immediate poll. */
    UmicomBoolean selected = UMICOM_FALSE;
    status = UmicomEventSelect(domain, wait, info.now, &selected);
    while (status == UMICOM_EVENT_OK && wait->result.outcome == UMICOM_EVENT_PENDING) {
        status = UmicomKernelEventPump(domain);
        if (status != UMICOM_EVENT_OK || wait->result.outcome != UMICOM_EVENT_PENDING) break;
        /* No callback can run between the registration/check above and this
         * suspension. That is the single-hart no-lost-wakeup argument. */
        const UmicomKernelThreadStatus suspendStatus = hasDeadline != UMICOM_FALSE
            ? UmicomKernelThreadSleepUntil(domain->scheduler, deadline)
            : UmicomKernelThreadWait(domain->scheduler);
        if (suspendStatus != UMICOM_THREAD_OK) {
            status = UMICOM_EVENT_SCHEDULER_ERROR;
            break;
        }
        /* A raw ThreadWake may have resumed us without awarding a result.
         * Loop through Pump and suspend again rather than fabricate a signal. */
    }
    if (status == UMICOM_EVENT_OK) {
        outResult->outcome = wait->result.outcome;
        outResult->index = wait->result.index;
        outResult->event = wait->result.event;
        outResult->observedTime = wait->result.observedTime;
    }
    UmicomEventClear(wait, sizeof(*wait)); /* The caller stack is live again. */
    return status;
}

UmicomKernelEventStatus UmicomKernelEventSnapshotRead(
    UmicomKernelEventDomain *domain, UmicomKernelEventSnapshot *outSnapshot)
{
    if (outSnapshot == (UmicomKernelEventSnapshot *)0) return UMICOM_EVENT_INVALID_ARGUMENT;
    const UmicomKernelEventStatus status = UmicomKernelEventPump(domain);
    if (status != UMICOM_EVENT_OK) return status;
    UmicomKernelEventSnapshot snapshot;
    UmicomEventClear(&snapshot, sizeof(snapshot));
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_LIMIT; ++slot) {
        const UmicomKernelEventRecord *const event = &domain->events[slot];
        if (event->occupied != UMICOM_FALSE) ++snapshot.openEvents;
        if (event->signalled != UMICOM_FALSE) ++snapshot.signalledEvents;
        if (event->retired != UMICOM_FALSE) ++snapshot.retiredEvents;
    }
    for (UmicomSize slot = 0U; slot < UMICOM_EVENT_WAIT_LIMIT; ++slot) {
        const UmicomKernelEventWaitRecord *const wait = &domain->waiters[slot];
        if (wait->occupied == UMICOM_FALSE) continue;
        if (wait->result.outcome == UMICOM_EVENT_PENDING) ++snapshot.pendingWaits;
        else ++snapshot.completedWaits;
    }
    outSnapshot->openEvents = snapshot.openEvents;
    outSnapshot->signalledEvents = snapshot.signalledEvents;
    outSnapshot->retiredEvents = snapshot.retiredEvents;
    outSnapshot->pendingWaits = snapshot.pendingWaits;
    outSnapshot->completedWaits = snapshot.completedWaits;
    return UMICOM_EVENT_OK;
}

const char *UmicomKernelEventStatusName(UmicomKernelEventStatus status)
{
    switch (status) {
        case UMICOM_EVENT_OK: return "ok";
        case UMICOM_EVENT_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_EVENT_NOT_INITIALISED: return "not-initialised";
        case UMICOM_EVENT_BAD_STATE: return "bad-state";
        case UMICOM_EVENT_INVALID_HANDLE: return "invalid-handle";
        case UMICOM_EVENT_CAPACITY: return "capacity";
        case UMICOM_EVENT_WRONG_CONTEXT: return "wrong-context";
        case UMICOM_EVENT_COUNTER_EXHAUSTED: return "counter-exhausted";
        case UMICOM_EVENT_SCHEDULER_ERROR: return "scheduler-error";
        case UMICOM_EVENT_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-status";
    }
}
