/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/threads.c
 *
 * PURPOSE:
 *   Manage cooperative Kernel continuations without replacing the existing
 *   process loader, trap handlers, user monitor or physical-memory allocator.
 *
 * EDUCATIONAL OVERVIEW:
 *   The dispatcher and every thread own different stacks. RunOne saves the
 *   dispatcher context and enters a selected thread; Yield/Wait/Sleep save the
 *   thread context and restore the dispatcher. A later RunOne continues the
 *   original C call, so local variables and nested calls remain alive.
 *
 *   Stack margins are checked canaries, NOT unmapped guard pages. These trusted
 *   machine-mode callbacks share all Kernel memory. Detection at a switch is
 *   useful evidence, not a security boundary or an overflow prevention scheme.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/threads.h"

/* One dispatcher tree may be active on the supported single hart. This also
 * rejects nested dispatch through a different scheduler object; otherwise a
 * callback could accidentally yield a context belonging to another stack. */
static UmicomKernelScheduler *umicomExecutingScheduler;

/* Zero through volatile bytes so freestanding compilation never introduces a
 * hosted memset dependency, and stack clearing remains an observable write. */
static void UmicomThreadClear(void *storage, UmicomSize bytes)
{
    volatile UmicomU8 *const destination = (volatile UmicomU8 *)storage;
    for (UmicomSize index = 0U; index < bytes; ++index) destination[index] = 0U;
}

static UmicomKernelThreadStatus UmicomThreadDomain(const UmicomKernelScheduler *scheduler)
{
    if (scheduler == (const UmicomKernelScheduler *)0) return UMICOM_THREAD_INVALID_ARGUMENT;
    if (scheduler->initialised != UMICOM_TRUE) return UMICOM_THREAD_NOT_INITIALISED;
    /* A copied owner still points at its original address and must be refused. */
    if (scheduler->self != scheduler || scheduler->poisoned != UMICOM_FALSE)
        return UMICOM_THREAD_CORRUPT_STATE;
    return UMICOM_THREAD_OK;
}

static UmicomBoolean UmicomThreadStackIntact(const UmicomKernelThreadRecord *thread)
{
    /* Fill patterns differ at the two ends so a reversed range is detectable. */
    for (UmicomSize index = 0U; index < UMICOM_THREAD_STACK_MARGIN; ++index) {
        if (thread->stack[index] != (UmicomU8)0xa7U ||
            thread->stack[UMICOM_THREAD_STACK_BYTES - 1U - index] != (UmicomU8)0x5dU)
            return UMICOM_FALSE;
    }
    /* The running stack pointer is live, not the last suspended value. Its
     * saved value can be inspected reliably only after switching away. */
    if (thread->state != UMICOM_THREAD_RUNNING) {
        const UmicomAddress low = (UmicomAddress)&thread->stack[UMICOM_THREAD_STACK_MARGIN];
        const UmicomAddress high = (UmicomAddress)&thread->stack[UMICOM_THREAD_STACK_BYTES - UMICOM_THREAD_STACK_MARGIN];
        if (thread->context.stackPointer < low || thread->context.stackPointer > high ||
            (thread->context.stackPointer & 15U) != 0U) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}

UmicomKernelThreadStatus UmicomKernelSchedulerValidate(UmicomKernelScheduler *scheduler)
{
    const UmicomKernelThreadStatus status = UmicomThreadDomain(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->next >= UMICOM_THREAD_LIMIT ||
        (scheduler->active != UMICOM_FALSE && scheduler->active != UMICOM_TRUE))
        return UMICOM_THREAD_CORRUPT_STATE;
    UmicomSize running = 0U;
    for (UmicomSize index = 0U; index < UMICOM_THREAD_LIMIT; ++index) {
        const UmicomKernelThreadRecord *const thread = &scheduler->threads[index];
        if (thread->generation == 0U || thread->state > UMICOM_THREAD_CANCELLED ||
            thread->state < UMICOM_THREAD_EMPTY ||
            (thread->retired != UMICOM_FALSE && thread->retired != UMICOM_TRUE))
            return UMICOM_THREAD_CORRUPT_STATE;
        if (thread->retired != UMICOM_FALSE &&
            (thread->generation != ~(UmicomU32)0U || thread->state != UMICOM_THREAD_EMPTY))
            return UMICOM_THREAD_CORRUPT_STATE;
        if (thread->state == UMICOM_THREAD_EMPTY) continue;
        if (thread->entry == (UmicomKernelThreadEntry)0 ||
            UmicomThreadStackIntact(thread) == UMICOM_FALSE)
            return UMICOM_THREAD_CORRUPT_STATE;
        if (thread->state == UMICOM_THREAD_SLEEPING && thread->wakeTime <= scheduler->now)
            return UMICOM_THREAD_CORRUPT_STATE;
        if (thread->state == UMICOM_THREAD_RUNNING) {
            ++running;
            if (scheduler->current != index) return UMICOM_THREAD_CORRUPT_STATE;
        }
    }
    /* A switch exposes exactly one running thread, or none on the dispatcher. */
    if ((scheduler->active != UMICOM_FALSE && running != 1U) ||
        (scheduler->active == UMICOM_FALSE &&
            (running != 0U || scheduler->current != UMICOM_THREAD_NO_SLOT)))
        return UMICOM_THREAD_CORRUPT_STATE;
    return UMICOM_THREAD_OK;
}

static UmicomKernelThreadStatus UmicomThreadLookup(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle, UmicomSize *outSlot)
{
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    /* Bounds are checked before the token can select a record. */
    const UmicomU32 encoded = (UmicomU32)handle;
    const UmicomU32 generation = (UmicomU32)(handle >> 32U);
    if (encoded == 0U || encoded > UMICOM_THREAD_LIMIT || generation == 0U)
        return UMICOM_THREAD_INVALID_HANDLE;
    const UmicomSize slot = (UmicomSize)encoded - 1U;
    const UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
    if (thread->state == UMICOM_THREAD_EMPTY || thread->generation != generation)
        return UMICOM_THREAD_INVALID_HANDLE;
    *outSlot = slot;
    return UMICOM_THREAD_OK;
}

static void UmicomThreadDescribe(const UmicomKernelThreadRecord *thread,
    UmicomKernelThreadInfo *outInfo)
{
    /* Copy fields explicitly; do not leak stack/context pointers in snapshots. */
    outInfo->state = thread->state;
    outInfo->wakeTime = thread->wakeTime;
    outInfo->exitValue = thread->exitValue;
    outInfo->dispatches = thread->dispatches;
    outInfo->yields = thread->yields;
}

UmicomKernelThreadStatus UmicomKernelSchedulerInitialize(UmicomKernelScheduler *scheduler)
{
    if (scheduler == (UmicomKernelScheduler *)0) return UMICOM_THREAD_INVALID_ARGUMENT;
    /* The zero-initialised-storage contract is deliberate. Never reset a live
     * object, even when empty: its stale-handle generations must survive. */
    if (scheduler->self != (const UmicomKernelScheduler *)0 || scheduler->initialised != UMICOM_FALSE)
        return UMICOM_THREAD_BAD_STATE;
    UmicomThreadClear(scheduler, sizeof(*scheduler));
    scheduler->self = scheduler;
    scheduler->current = UMICOM_THREAD_NO_SLOT;
    for (UmicomSize index = 0U; index < UMICOM_THREAD_LIMIT; ++index)
        scheduler->threads[index].generation = 1U;
    scheduler->initialised = UMICOM_TRUE; /* Publish only the complete domain. */
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelThreadCreate(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadEntry entry, void *argument, UmicomKernelThreadHandle *outHandle)
{
    if (entry == (UmicomKernelThreadEntry)0 || outHandle == (UmicomKernelThreadHandle *)0)
        return UMICOM_THREAD_INVALID_ARGUMENT;
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active != UMICOM_FALSE || umicomExecutingScheduler != (UmicomKernelScheduler *)0)
        return UMICOM_THREAD_BAD_STATE;
    if (UmicomKernelThreadMachineReady() == UMICOM_FALSE) return UMICOM_THREAD_UNSAFE_MACHINE;
    for (UmicomSize slot = 0U; slot < UMICOM_THREAD_LIMIT; ++slot) {
        UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
        if (thread->state != UMICOM_THREAD_EMPTY || thread->retired != UMICOM_FALSE) continue;
        /* Preserve the slot generation while clearing old data and register state. */
        const UmicomU32 generation = thread->generation;
        UmicomThreadClear(thread, sizeof(*thread));
        thread->generation = generation;
        thread->entry = entry;
        thread->argument = argument;
        for (UmicomSize byte = 0U; byte < UMICOM_THREAD_STACK_MARGIN; ++byte) {
            thread->stack[byte] = (UmicomU8)0xa7U;
            thread->stack[UMICOM_THREAD_STACK_BYTES - 1U - byte] = (UmicomU8)0x5dU;
        }
        UmicomKernelThreadContextPrepare(&thread->context,
            (UmicomAddress)&thread->stack[UMICOM_THREAD_STACK_BYTES - UMICOM_THREAD_STACK_MARGIN],
            (UmicomAddress)scheduler, slot);
        thread->state = UMICOM_THREAD_READY;
        *outHandle = ((UmicomU64)generation << 32U) | (UmicomU64)(slot + 1U);
        return UMICOM_THREAD_OK;
    }
    return UMICOM_THREAD_CAPACITY; /* No eviction and no output on failure. */
}

UmicomKernelThreadStatus UmicomKernelThreadQuery(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle, UmicomKernelThreadInfo *outInfo)
{
    if (outInfo == (UmicomKernelThreadInfo *)0) return UMICOM_THREAD_INVALID_ARGUMENT;
    UmicomSize slot = 0U;
    const UmicomKernelThreadStatus status = UmicomThreadLookup(scheduler, handle, &slot);
    if (status != UMICOM_THREAD_OK) return status;
    UmicomThreadDescribe(&scheduler->threads[slot], outInfo);
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelThreadCancel(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle)
{
    UmicomSize slot = 0U;
    const UmicomKernelThreadStatus status = UmicomThreadLookup(scheduler, handle, &slot);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active != UMICOM_FALSE || umicomExecutingScheduler != (UmicomKernelScheduler *)0)
        return UMICOM_THREAD_BAD_STATE;
    UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
    if (thread->state != UMICOM_THREAD_READY && thread->state != UMICOM_THREAD_SLEEPING &&
        thread->state != UMICOM_THREAD_WAITING) return UMICOM_THREAD_BAD_STATE;
    /* Cancellation abandons a suspended continuation. Its stack and borrowed
     * argument remain owned until Reap. Kernel callbacks must arrange any
     * application resource cleanup themselves before cancelling a continuation. */
    thread->state = UMICOM_THREAD_CANCELLED;
    thread->wakeTime = 0U;
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelThreadReap(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle, UmicomKernelThreadInfo *outInfo)
{
    if (outInfo == (UmicomKernelThreadInfo *)0) return UMICOM_THREAD_INVALID_ARGUMENT;
    UmicomSize slot = 0U;
    const UmicomKernelThreadStatus status = UmicomThreadLookup(scheduler, handle, &slot);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active != UMICOM_FALSE || umicomExecutingScheduler != (UmicomKernelScheduler *)0)
        return UMICOM_THREAD_BAD_STATE;
    UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
    if (thread->state != UMICOM_THREAD_FINISHED && thread->state != UMICOM_THREAD_CANCELLED)
        return UMICOM_THREAD_NOT_FINISHED;
    UmicomThreadDescribe(thread, outInfo);
    const UmicomU32 oldGeneration = thread->generation;
    /* We are on the dispatcher stack. No instruction can still be using the
     * terminal stack, so scrub it before making the record available again. */
    UmicomThreadClear(thread, sizeof(*thread));
    if (oldGeneration == ~(UmicomU32)0U) {
        thread->generation = oldGeneration;
        thread->retired = UMICOM_TRUE;
    } else {
        thread->generation = oldGeneration + 1U;
    }
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelSchedulerAdvanceTime(UmicomKernelScheduler *scheduler,
    UmicomU64 now)
{
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active != UMICOM_FALSE || umicomExecutingScheduler != (UmicomKernelScheduler *)0)
        return UMICOM_THREAD_BAD_STATE;
    if (now < scheduler->now) return UMICOM_THREAD_CLOCK_REVERSED;
    scheduler->now = now;
    /* No wrap arithmetic or fabricated ticks. Every due sleeper becomes ready
     * before another continuation is selected. Equal deadlines wake together. */
    for (UmicomSize index = 0U; index < UMICOM_THREAD_LIMIT; ++index) {
        UmicomKernelThreadRecord *const thread = &scheduler->threads[index];
        if (thread->state == UMICOM_THREAD_SLEEPING && thread->wakeTime <= now) {
            thread->wakeTime = 0U;
            thread->state = UMICOM_THREAD_READY;
        }
    }
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelSchedulerSnapshot(UmicomKernelScheduler *scheduler,
    UmicomKernelSchedulerInfo *outInfo)
{
    if (outInfo == (UmicomKernelSchedulerInfo *)0) return UMICOM_THREAD_INVALID_ARGUMENT;
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    UmicomKernelSchedulerInfo info;
    UmicomThreadClear(&info, sizeof(info));
    info.now = scheduler->now;
    info.dispatches = scheduler->dispatches;
    for (UmicomSize index = 0U; index < UMICOM_THREAD_LIMIT; ++index) {
        const UmicomKernelThreadRecord *const thread = &scheduler->threads[index];
        switch (thread->state) {
            case UMICOM_THREAD_EMPTY:
                if (thread->retired != UMICOM_FALSE) ++info.retiredSlots;
                else ++info.freeSlots;
                break;
            case UMICOM_THREAD_READY: ++info.ready; break;
            case UMICOM_THREAD_RUNNING: ++info.running; break;
            case UMICOM_THREAD_WAITING: ++info.waiting; break;
            case UMICOM_THREAD_SLEEPING:
                ++info.sleeping;
                if (info.hasDeadline == UMICOM_FALSE || thread->wakeTime < info.nextDeadline)
                    info.nextDeadline = thread->wakeTime;
                info.hasDeadline = UMICOM_TRUE;
                break;
            case UMICOM_THREAD_FINISHED:
            case UMICOM_THREAD_CANCELLED: ++info.terminal; break;
        }
    }
    /* Copy fields explicitly so freestanding code does not need a compiler-
     * generated memcpy call for this larger value snapshot. */
    outInfo->ready = info.ready;
    outInfo->running = info.running;
    outInfo->sleeping = info.sleeping;
    outInfo->waiting = info.waiting;
    outInfo->terminal = info.terminal;
    outInfo->freeSlots = info.freeSlots;
    outInfo->retiredSlots = info.retiredSlots;
    outInfo->now = info.now;
    outInfo->dispatches = info.dispatches;
    outInfo->hasDeadline = info.hasDeadline;
    outInfo->nextDeadline = info.nextDeadline;
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelSchedulerRunOne(UmicomKernelScheduler *scheduler)
{
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active != UMICOM_FALSE || umicomExecutingScheduler != (UmicomKernelScheduler *)0)
        return UMICOM_THREAD_BAD_STATE;
    if (UmicomKernelThreadMachineReady() == UMICOM_FALSE) return UMICOM_THREAD_UNSAFE_MACHINE;
    for (UmicomSize offset = 0U; offset < UMICOM_THREAD_LIMIT; ++offset) {
        const UmicomSize slot = (scheduler->next + offset) % UMICOM_THREAD_LIMIT;
        UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
        if (thread->state != UMICOM_THREAD_READY) continue;
        if (scheduler->dispatches == ~(UmicomU64)0U || thread->dispatches == ~(UmicomU64)0U)
            return UMICOM_THREAD_COUNTER_EXHAUSTED;
        ++scheduler->dispatches;
        ++thread->dispatches;
        scheduler->next = (slot + 1U) % UMICOM_THREAD_LIMIT;
        scheduler->current = slot;
        scheduler->active = UMICOM_TRUE;
        umicomExecutingScheduler = scheduler;
        thread->state = UMICOM_THREAD_RUNNING;
        /* This returns after the selected callback yields, waits or finishes.
         * It is not a function call that restarts the callback from its entry. */
        UmicomKernelThreadContextSwitch(&scheduler->dispatcher, &thread->context);
        scheduler->active = UMICOM_FALSE;
        umicomExecutingScheduler = (UmicomKernelScheduler *)0;
        scheduler->current = UMICOM_THREAD_NO_SLOT;
        if (UmicomKernelThreadMachineReady() == UMICOM_FALSE ||
            thread->state == UMICOM_THREAD_RUNNING ||
            UmicomKernelSchedulerValidate(scheduler) != UMICOM_THREAD_OK) {
            scheduler->poisoned = UMICOM_TRUE;
            return UMICOM_THREAD_CORRUPT_STATE;
        }
        return UMICOM_THREAD_OK;
    }
    return UMICOM_THREAD_IDLE;
}

static UmicomKernelThreadStatus UmicomThreadSuspend(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadState state, UmicomU64 wakeTime)
{
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active == UMICOM_FALSE || umicomExecutingScheduler != scheduler ||
        scheduler->current >= UMICOM_THREAD_LIMIT)
        return UMICOM_THREAD_WRONG_CONTEXT;
    if (UmicomKernelThreadMachineReady() == UMICOM_FALSE) return UMICOM_THREAD_UNSAFE_MACHINE;
    UmicomKernelThreadRecord *const thread = &scheduler->threads[scheduler->current];
    if (thread->yields == ~(UmicomU64)0U) return UMICOM_THREAD_COUNTER_EXHAUSTED;
    ++thread->yields;
    thread->wakeTime = wakeTime;
    thread->state = state;
    UmicomKernelThreadContextSwitch(&thread->context, &scheduler->dispatcher);
    /* RunOne changed the record back to RUNNING before resuming this very
     * stack. The return value belongs to the original suspended C call. */
    return UMICOM_THREAD_OK;
}

UmicomKernelThreadStatus UmicomKernelThreadYield(UmicomKernelScheduler *scheduler)
{
    return UmicomThreadSuspend(scheduler, UMICOM_THREAD_READY, 0U);
}
UmicomKernelThreadStatus UmicomKernelThreadWait(UmicomKernelScheduler *scheduler)
{
    return UmicomThreadSuspend(scheduler, UMICOM_THREAD_WAITING, 0U);
}
UmicomKernelThreadStatus UmicomKernelThreadSleepUntil(UmicomKernelScheduler *scheduler,
    UmicomU64 deadline)
{
    const UmicomKernelThreadStatus status = UmicomKernelSchedulerValidate(scheduler);
    if (status != UMICOM_THREAD_OK) return status;
    if (scheduler->active == UMICOM_FALSE || umicomExecutingScheduler != scheduler)
        return UMICOM_THREAD_WRONG_CONTEXT;
    /* A deadline already reached needs no context switch or wake credit. */
    if (deadline <= scheduler->now) return UMICOM_THREAD_OK;
    return UmicomThreadSuspend(scheduler, UMICOM_THREAD_SLEEPING, deadline);
}
UmicomKernelThreadStatus UmicomKernelThreadWake(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle)
{
    UmicomSize slot = 0U;
    const UmicomKernelThreadStatus status = UmicomThreadLookup(scheduler, handle, &slot);
    if (status != UMICOM_THREAD_OK) return status;
    UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
    if (thread->state == UMICOM_THREAD_READY) return UMICOM_THREAD_OK;
    if (thread->state != UMICOM_THREAD_SLEEPING && thread->state != UMICOM_THREAD_WAITING)
        return UMICOM_THREAD_BAD_STATE;
    thread->wakeTime = 0U;
    thread->state = UMICOM_THREAD_READY;
    return UMICOM_THREAD_OK;
}

_Noreturn void UmicomKernelThreadStart(UmicomKernelScheduler *scheduler, UmicomSize slot)
{
    /* Only a prepared architecture context enters here. Returning through an
     * unknown frame would be less safe than a visible fatal Kernel exception. */
    if (UmicomKernelSchedulerValidate(scheduler) != UMICOM_THREAD_OK ||
        scheduler->active == UMICOM_FALSE || scheduler->current != slot)
        __builtin_trap();
    UmicomKernelThreadRecord *const thread = &scheduler->threads[slot];
    const UmicomU64 result = thread->entry(thread->argument);
    thread->exitValue = result;
    thread->wakeTime = 0U;
    thread->state = UMICOM_THREAD_FINISHED;
    /* Completion keeps the stack intact for the owner to inspect and reap. */
    UmicomKernelThreadContextSwitch(&thread->context, &scheduler->dispatcher);
    __builtin_trap(); /* A terminal continuation is never dispatched again. */
}

const char *UmicomKernelThreadStatusName(UmicomKernelThreadStatus status)
{
    switch (status) {
        case UMICOM_THREAD_OK: return "ok";
        case UMICOM_THREAD_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_THREAD_NOT_INITIALISED: return "not-initialised";
        case UMICOM_THREAD_BAD_STATE: return "bad-state";
        case UMICOM_THREAD_UNSAFE_MACHINE: return "unsafe-machine-state";
        case UMICOM_THREAD_INVALID_HANDLE: return "invalid-handle";
        case UMICOM_THREAD_CAPACITY: return "capacity";
        case UMICOM_THREAD_WRONG_CONTEXT: return "wrong-context";
        case UMICOM_THREAD_NOT_FINISHED: return "not-finished";
        case UMICOM_THREAD_IDLE: return "idle";
        case UMICOM_THREAD_CLOCK_REVERSED: return "clock-reversed";
        case UMICOM_THREAD_COUNTER_EXHAUSTED: return "counter-exhausted";
        case UMICOM_THREAD_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-status";
    }
}
