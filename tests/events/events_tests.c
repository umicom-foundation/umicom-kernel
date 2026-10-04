/* Umicom Kernel events: native policy tests using real alternate stacks.
 * The existing Linux x86-64 context adapter is test-only. These cases exercise
 * events.c and threads.c, not RISC-V CSRs or interrupt execution.
 * Each CTest starts a new process, so no live scheduler/domain is reset.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/events.h"

extern UmicomBoolean umicomHostMachineReady;
static UmicomKernelScheduler scheduler;
static UmicomKernelEventDomain domain;
static UmicomKernelEventDomain second;
static unsigned int checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, \
    "check %u failed at line %d: %s\n", checks, __LINE__, #c); exit(1); } } while (0)
#define TOK(c) CHECK((c) == UMICOM_THREAD_OK)
#define EOK(c) CHECK((c) == UMICOM_EVENT_OK)

typedef struct Work {
    UmicomKernelThreadHandle thread;
    UmicomKernelEventHandle events[UMICOM_EVENT_WAIT_ANY_LIMIT];
    UmicomSize count;
    UmicomBoolean timed;
    UmicomU64 deadline;
    UmicomKernelEventResult result;
    UmicomKernelEventStatus status;
    UmicomU64 progress;
    UmicomU64 id;
} Work;
static UmicomU64 order[64];
static UmicomSize orderCount;

static UmicomU64 WaitBody(void *argument)
{
    Work *const work = (Work *)argument;
    volatile UmicomU64 local = work->id + 0xabcU;
    ++work->progress;
    work->status = UmicomKernelEventWaitAny(&domain, work->thread, work->events,
        work->count, work->timed, work->deadline, &work->result);
    CHECK(local == work->id + 0xabcU); /* Stack data survived suspension. */
    CHECK(orderCount < 64U);
    order[orderCount++] = work->id;
    ++work->progress;
    return (UmicomU64)work->result.outcome;
}
static void Start(Work *work, UmicomKernelEventHandle handle, UmicomU64 id)
{
    memset(work, 0, sizeof(*work));
    work->events[0] = handle;
    work->count = 1U;
    work->id = id;
    work->result.outcome = UMICOM_EVENT_CLOSED; /* Failure must not publish output. */
    work->result.index = 91U;
    work->result.event = 92U;
    work->result.observedTime = 93U;
    TOK(UmicomKernelThreadCreate(&scheduler, WaitBody, work, &work->thread));
}
static UmicomKernelEventHandle Create(UmicomKernelEventMode mode, UmicomBoolean signalled)
{
    UmicomKernelEventHandle handle = 0U;
    EOK(UmicomKernelEventCreate(&domain, mode, signalled, &handle));
    CHECK(handle != 0U);
    return handle;
}
static void Step(void) { TOK(UmicomKernelSchedulerRunOne(&scheduler)); }
static UmicomKernelThreadInfo Thread(UmicomKernelThreadHandle handle)
{
    UmicomKernelThreadInfo info = {0};
    TOK(UmicomKernelThreadQuery(&scheduler, handle, &info));
    return info;
}
static UmicomKernelEventSnapshot Snapshot(void)
{
    UmicomKernelEventSnapshot info = {0};
    EOK(UmicomKernelEventSnapshotRead(&domain, &info));
    return info;
}
static void Finish(Work *work, UmicomKernelEventOutcome outcome)
{
    CHECK(work->progress == 2U);
    CHECK(work->status == UMICOM_EVENT_OK);
    CHECK(work->result.outcome == outcome);
    UmicomKernelThreadInfo info = {0};
    TOK(UmicomKernelThreadReap(&scheduler, work->thread, &info));
    CHECK(info.exitValue == (UmicomU64)outcome);
}
static void Ready(void)
{
    for (unsigned int count = 0U; count < 64U; ++count) {
        const UmicomKernelThreadStatus status = UmicomKernelSchedulerRunOne(&scheduler);
        if (status == UMICOM_THREAD_IDLE) return;
        CHECK(status == UMICOM_THREAD_OK);
    }
    CHECK(0); /* A loop in a test must be a failure, not an unbounded runner. */
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *const name = argv[1];
    TOK(UmicomKernelSchedulerInitialize(&scheduler));
    if (strcmp(name, "null-initialise") == 0) {
        CHECK(UmicomKernelEventDomainInitialize(NULL, &scheduler) == UMICOM_EVENT_INVALID_ARGUMENT);
        CHECK(UmicomKernelEventDomainInitialize(&domain, NULL) == UMICOM_EVENT_INVALID_ARGUMENT);
        return 0;
    }
    if (strcmp(name, "uninitialised") == 0) {
        CHECK(UmicomKernelEventPump(&domain) == UMICOM_EVENT_NOT_INITIALISED);
        return 0;
    }
    if (strcmp(name, "machine-admission") == 0) {
        umicomHostMachineReady = UMICOM_FALSE;
        CHECK(UmicomKernelEventDomainInitialize(&domain, &scheduler) == UMICOM_EVENT_SCHEDULER_ERROR);
        return 0;
    }
    EOK(UmicomKernelEventDomainInitialize(&domain, &scheduler));
    Work a, b, c;
    if (strcmp(name, "reinitialise") == 0) {
        CHECK(UmicomKernelEventDomainInitialize(&domain, &scheduler) == UMICOM_EVENT_BAD_STATE);
    } else if (strcmp(name, "copied-domain") == 0) {
        memcpy(&second, &domain, sizeof(second));
        CHECK(UmicomKernelEventPump(&second) == UMICOM_EVENT_CORRUPT_STATE);
    } else if (strcmp(name, "invalid-mode") == 0) {
        UmicomKernelEventHandle h = 91U;
        CHECK(UmicomKernelEventCreate(&domain, (UmicomKernelEventMode)7,
            UMICOM_FALSE, &h) == UMICOM_EVENT_INVALID_ARGUMENT);
        CHECK(h == 91U);
    } else if (strcmp(name, "invalid-boolean") == 0) {
        UmicomKernelEventHandle h = 91U;
        CHECK(UmicomKernelEventCreate(&domain, UMICOM_EVENT_AUTO_RESET,
            (UmicomBoolean)5, &h) == UMICOM_EVENT_INVALID_ARGUMENT);
        CHECK(h == 91U);
    } else if (strcmp(name, "null-output") == 0) {
        CHECK(UmicomKernelEventCreate(&domain, UMICOM_EVENT_AUTO_RESET,
            UMICOM_FALSE, NULL) == UMICOM_EVENT_INVALID_ARGUMENT);
        CHECK(UmicomKernelEventSnapshotRead(&domain, NULL) == UMICOM_EVENT_INVALID_ARGUMENT);
    } else if (strcmp(name, "capacity") == 0) {
        for (unsigned int index = 0U; index < UMICOM_EVENT_LIMIT; ++index)
            (void)Create(UMICOM_EVENT_MANUAL_RESET, UMICOM_FALSE);
        UmicomKernelEventHandle h = 99U;
        CHECK(UmicomKernelEventCreate(&domain, UMICOM_EVENT_AUTO_RESET,
            UMICOM_FALSE, &h) == UMICOM_EVENT_CAPACITY);
        CHECK(h == 99U);
        CHECK(Snapshot().openEvents == UMICOM_EVENT_LIMIT);
    } else if (strcmp(name, "invalid-token") == 0) {
        CHECK(UmicomKernelEventSignal(&domain, 0U) == UMICOM_EVENT_INVALID_HANDLE);
        CHECK(UmicomKernelEventSignal(&domain, ~(UmicomU64)0U) == UMICOM_EVENT_INVALID_HANDLE);
        CHECK(UmicomKernelEventSignal(&domain, 1U) == UMICOM_EVENT_INVALID_HANDLE);
    } else if (strcmp(name, "stale-token") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        EOK(UmicomKernelEventClose(&domain, h));
        const UmicomKernelEventHandle next = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        CHECK(h != next);
        CHECK(UmicomKernelEventSignal(&domain, h) == UMICOM_EVENT_INVALID_HANDLE);
        CHECK(UmicomKernelEventReset(&domain, h) == UMICOM_EVENT_INVALID_HANDLE);
        CHECK(UmicomKernelEventClose(&domain, h) == UMICOM_EVENT_INVALID_HANDLE);
    } else if (strcmp(name, "retirement") == 0) {
        domain.events[0].generation = ~(UmicomU32)0U; /* Explicit fault-boundary injection. */
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        EOK(UmicomKernelEventClose(&domain, h));
        CHECK(domain.events[0].retired == UMICOM_TRUE);
        CHECK((UmicomU32)Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE) == 2U);
        CHECK(Snapshot().retiredEvents == 1U);
    } else if (strcmp(name, "manual-latch") == 0 || strcmp(name, "auto-latch") == 0) {
        const UmicomBoolean manual = strcmp(name, "manual-latch") == 0 ? UMICOM_TRUE : UMICOM_FALSE;
        const UmicomKernelEventHandle h = Create(manual ? UMICOM_EVENT_MANUAL_RESET : UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        EOK(UmicomKernelEventSignal(&domain, h));
        Start(&a, h, 1U); Step(); Finish(&a, UMICOM_EVENT_SIGNALED);
        CHECK(Snapshot().signalledEvents == (manual ? 1U : 0U));
    } else if (strcmp(name, "auto-coalescing") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        EOK(UmicomKernelEventSignal(&domain, h)); EOK(UmicomKernelEventSignal(&domain, h));
        Start(&a, h, 1U); Start(&b, h, 2U); Ready();
        Finish(&a, UMICOM_EVENT_SIGNALED); CHECK(b.progress == 1U);
        EOK(UmicomKernelEventClose(&domain, h)); Ready(); Finish(&b, UMICOM_EVENT_CLOSED);
    } else if (strcmp(name, "auto-fifo") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); Start(&b, h, 2U); Start(&c, h, 3U); Ready();
        EOK(UmicomKernelEventSignal(&domain, h)); Ready();
        CHECK(a.progress == 2U && b.progress == 1U && c.progress == 1U);
        EOK(UmicomKernelEventSignal(&domain, h)); Ready();
        CHECK(b.progress == 2U && c.progress == 1U);
        EOK(UmicomKernelEventSignal(&domain, h)); Ready();
        CHECK(order[0] == 1U && order[1] == 2U && order[2] == 3U);
        Finish(&a, UMICOM_EVENT_SIGNALED); Finish(&b, UMICOM_EVENT_SIGNALED); Finish(&c, UMICOM_EVENT_SIGNALED);
    } else if (strcmp(name, "manual-broadcast") == 0 || strcmp(name, "reset-awarded") == 0 ||
               strcmp(name, "close-awarded") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_MANUAL_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); Start(&b, h, 2U); Ready();
        EOK(UmicomKernelEventSignal(&domain, h));
        CHECK(Snapshot().completedWaits == 2U);
        if (strcmp(name, "close-awarded") == 0) EOK(UmicomKernelEventClose(&domain, h));
        else EOK(UmicomKernelEventReset(&domain, h));
        Ready(); Finish(&a, UMICOM_EVENT_SIGNALED); Finish(&b, UMICOM_EVENT_SIGNALED);
    } else if (strcmp(name, "reset-latch") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_TRUE);
        EOK(UmicomKernelEventReset(&domain, h)); Start(&a, h, 1U); Step();
        CHECK(a.progress == 1U); EOK(UmicomKernelEventClose(&domain, h)); Step(); Finish(&a, UMICOM_EVENT_CLOSED);
    } else if (strcmp(name, "wait-any-index") == 0 || strcmp(name, "wait-any-order") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        const UmicomKernelEventHandle j = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_TRUE);
        if (strcmp(name, "wait-any-order") == 0) EOK(UmicomKernelEventSignal(&domain, h));
        Start(&a, h, 1U); a.events[1] = j; a.count = 2U; Step();
        CHECK(a.result.index == (strcmp(name, "wait-any-order") == 0 ? 0U : 1U));
        Finish(&a, UMICOM_EVENT_SIGNALED);
    } else if (strcmp(name, "preflight-whole-set") == 0 || strcmp(name, "duplicate-set") == 0 ||
               strcmp(name, "empty-set") == 0 || strcmp(name, "oversized-set") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_TRUE);
        Start(&a, h, 1U);
        a.events[1] = strcmp(name, "duplicate-set") == 0 ? h : 0U; a.count = 2U;
        if (strcmp(name, "empty-set") == 0) a.count = 0U;
        if (strcmp(name, "oversized-set") == 0) a.count = UMICOM_EVENT_WAIT_ANY_LIMIT + 1U;
        Step(); CHECK(a.status != UMICOM_EVENT_OK);
        CHECK(a.result.index == 91U && a.result.event == 92U && a.result.observedTime == 93U);
        CHECK(Snapshot().signalledEvents == 1U && Snapshot().pendingWaits == 0U);
    } else if (strcmp(name, "outside-callback") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U);
        CHECK(UmicomKernelEventWaitAny(&domain, a.thread, a.events, 1U,
            UMICOM_FALSE, 0U, &a.result) == UMICOM_EVENT_WRONG_CONTEXT);
    } else if (strcmp(name, "spurious-wake") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); Step();
        TOK(UmicomKernelThreadWake(&scheduler, a.thread)); Step();
        CHECK(a.progress == 1U && Thread(a.thread).state == UMICOM_THREAD_WAITING);
        EOK(UmicomKernelEventSignal(&domain, h)); Step(); Finish(&a, UMICOM_EVENT_SIGNALED);
    } else if (strcmp(name, "deadline") == 0 || strcmp(name, "late-signal") == 0 ||
               strcmp(name, "deadline-maximum") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); a.timed = UMICOM_TRUE;
        a.deadline = strcmp(name, "deadline-maximum") == 0 ? ~(UmicomU64)0U : 10U;
        Step(); CHECK(Thread(a.thread).state == UMICOM_THREAD_SLEEPING);
        TOK(UmicomKernelSchedulerAdvanceTime(&scheduler, a.deadline));
        if (strcmp(name, "late-signal") == 0) EOK(UmicomKernelEventSignal(&domain, h));
        EOK(UmicomKernelEventPump(&domain)); Step();
        CHECK(a.result.observedTime == a.deadline && a.result.index == UMICOM_EVENT_NO_SELECTION);
        Finish(&a, UMICOM_EVENT_TIMED_OUT);
        if (strcmp(name, "late-signal") == 0) CHECK(Snapshot().signalledEvents == 1U);
    } else if (strcmp(name, "immediate-timeout") == 0 || strcmp(name, "ready-at-admission") == 0) {
        const UmicomBoolean ready = strcmp(name, "ready-at-admission") == 0 ? UMICOM_TRUE : UMICOM_FALSE;
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, ready);
        Start(&a, h, 1U); a.timed = UMICOM_TRUE; a.deadline = 0U;
        Step(); Finish(&a, ready ? UMICOM_EVENT_SIGNALED : UMICOM_EVENT_TIMED_OUT);
    } else if (strcmp(name, "signal-before-deadline") == 0 || strcmp(name, "spurious-timed-wake") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); a.timed = UMICOM_TRUE; a.deadline = 10U; Step();
        if (strcmp(name, "spurious-timed-wake") == 0) {
            TOK(UmicomKernelThreadWake(&scheduler, a.thread)); Step();
            CHECK(a.progress == 1U && Thread(a.thread).state == UMICOM_THREAD_SLEEPING);
        }
        TOK(UmicomKernelSchedulerAdvanceTime(&scheduler, 9U));
        EOK(UmicomKernelEventSignal(&domain, h));
        TOK(UmicomKernelSchedulerAdvanceTime(&scheduler, 10U));
        Step(); CHECK(a.result.observedTime == 9U); Finish(&a, UMICOM_EVENT_SIGNALED);
    } else if (strcmp(name, "close-wait-any") == 0 || strcmp(name, "close-reuse-before-resume") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        const UmicomKernelEventHandle j = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); a.events[1] = j; a.count = 2U; Step();
        EOK(UmicomKernelEventClose(&domain, j));
        if (strcmp(name, "close-reuse-before-resume") == 0) {
            const UmicomKernelEventHandle next = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
            CHECK(next != j); EOK(UmicomKernelEventSignal(&domain, next));
        }
        Step(); CHECK(a.result.index == 1U && a.result.event == j); Finish(&a, UMICOM_EVENT_CLOSED);
    } else if (strcmp(name, "cancel-wait") == 0 || strcmp(name, "cancel-reap") == 0 ||
               strcmp(name, "cancel-awarded") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); Step();
        const UmicomBoolean awarded = strcmp(name, "cancel-awarded") == 0 ? UMICOM_TRUE : UMICOM_FALSE;
        if (awarded) EOK(UmicomKernelEventSignal(&domain, h));
        TOK(UmicomKernelThreadCancel(&scheduler, a.thread));
        if (strcmp(name, "cancel-reap") == 0) {
            UmicomKernelThreadInfo result = {0}; TOK(UmicomKernelThreadReap(&scheduler, a.thread, &result));
        }
        EOK(UmicomKernelEventPump(&domain));
        CHECK(a.progress == 1U && Snapshot().pendingWaits == 0U && Snapshot().completedWaits == 0U);
        EOK(UmicomKernelEventSignal(&domain, h));
        Start(&b, h, 2U); Step(); Finish(&b, UMICOM_EVENT_SIGNALED);
    } else if (strcmp(name, "ticket-exhaustion") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_MANUAL_RESET, UMICOM_TRUE);
        domain.nextTicket = ~(UmicomU64)0U;
        Start(&a, h, 1U); Step(); Finish(&a, UMICOM_EVENT_SIGNALED);
        Start(&b, h, 2U); Step(); CHECK(b.status == UMICOM_EVENT_COUNTER_EXHAUSTED);
    } else if (strcmp(name, "corrupt-event") == 0) {
        (void)Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        domain.events[0].signalled = (UmicomBoolean)7;
        CHECK(UmicomKernelEventPump(&domain) == UMICOM_EVENT_CORRUPT_STATE);
        return 0;
    } else if (strcmp(name, "corrupt-wait") == 0) {
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_FALSE);
        Start(&a, h, 1U); Step();
        domain.waiters[0].count = UMICOM_EVENT_WAIT_ANY_LIMIT + 1U;
        CHECK(UmicomKernelEventPump(&domain) == UMICOM_EVENT_CORRUPT_STATE);
        return 0;
    } else if (strcmp(name, "scheduler-poison") == 0) {
        scheduler.poisoned = UMICOM_TRUE;
        CHECK(UmicomKernelEventPump(&domain) == UMICOM_EVENT_SCHEDULER_ERROR);
        return 0;
    } else if (strcmp(name, "separate-domains") == 0) {
        EOK(UmicomKernelEventDomainInitialize(&second, &scheduler));
        UmicomKernelEventHandle h = Create(UMICOM_EVENT_MANUAL_RESET, UMICOM_FALSE), j = 0U;
        EOK(UmicomKernelEventCreate(&second, UMICOM_EVENT_MANUAL_RESET, UMICOM_FALSE, &j));
        CHECK(h == j); /* Tokens are local to their trusted domain, not global. */
        EOK(UmicomKernelEventSignal(&second, j));
        CHECK(Snapshot().signalledEvents == 0U);
    } else if (strcmp(name, "repeated-lifetimes") == 0) {
        UmicomKernelEventHandle stale = 0U;
        for (unsigned int lifetime = 0U; lifetime < 2000U; ++lifetime) {
            const UmicomKernelEventHandle h = Create(UMICOM_EVENT_AUTO_RESET, UMICOM_TRUE);
            if (stale != 0U) CHECK(UmicomKernelEventSignal(&domain, stale) == UMICOM_EVENT_INVALID_HANDLE);
            Start(&a, h, 1U); Step(); Finish(&a, UMICOM_EVENT_SIGNALED);
            EOK(UmicomKernelEventClose(&domain, h)); stale = h;
            orderCount = 0U; /* Only this test trace is reset, not either live owner. */
        }
        CHECK(Snapshot().openEvents == 0U);
    } else if (strcmp(name, "all-waiter-slots") == 0) {
        Work work[UMICOM_THREAD_LIMIT];
        const UmicomKernelEventHandle h = Create(UMICOM_EVENT_MANUAL_RESET, UMICOM_FALSE);
        for (unsigned int i = 0U; i < UMICOM_THREAD_LIMIT; ++i) Start(&work[i], h, i);
        Ready(); CHECK(Snapshot().pendingWaits == UMICOM_THREAD_LIMIT);
        EOK(UmicomKernelEventSignal(&domain, h)); Ready();
        for (unsigned int i = 0U; i < UMICOM_THREAD_LIMIT; ++i) Finish(&work[i], UMICOM_EVENT_SIGNALED);
    } else {
        fprintf(stderr, "unknown test: %s\n", name); return 2;
    }
    EOK(UmicomKernelEventDomainValidate(&domain));
    printf("%s: %u checks passed\n", name, checks);
    return 0;
}
