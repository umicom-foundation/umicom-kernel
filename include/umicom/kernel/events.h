/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/events.h
 *
 * PURPOSE:
 *   Remember notifications and let a cooperative Kernel thread wait for any
 *   of several events without replacing the existing stack scheduler.
 *
 * EDUCATIONAL OVERVIEW:
 *   Waking a thread and recording an event are different operations. A wake
 *   only makes an already suspended thread runnable. An event also remembers
 *   a signal which arrives before the thread starts waiting.
 *
 *   Manual-reset events remain signalled until Reset. Auto-reset events award
 *   one waiting thread, or remember one credit for a future waiter. Repeated
 *   signals coalesce: this is a binary event, not a counting semaphore.
 *
 *   This is trusted Kernel coordination on one hart with interrupts disabled.
 *   The caller serialises all operations. It is not a user handle service,
 *   an interrupt-safe primitive, an SMP lock, or a replacement for checking
 *   the underlying condition after a thread wakes.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_EVENTS_H
#define UMICOM_KERNEL_EVENTS_H
#include "umicom/kernel/threads.h"

/* Fixed storage keeps admission and work bounded before a Kernel heap exists. */
#define UMICOM_EVENT_LIMIT 16U
#define UMICOM_EVENT_WAIT_LIMIT UMICOM_THREAD_LIMIT
#define UMICOM_EVENT_WAIT_ANY_LIMIT 8U
#define UMICOM_EVENT_NO_SELECTION UMICOM_EVENT_WAIT_ANY_LIMIT

typedef UmicomU64 UmicomKernelEventHandle;
typedef enum UmicomKernelEventMode {
    UMICOM_EVENT_MANUAL_RESET,
    UMICOM_EVENT_AUTO_RESET
} UmicomKernelEventMode;
typedef enum UmicomKernelEventStatus {
    UMICOM_EVENT_OK,
    UMICOM_EVENT_INVALID_ARGUMENT,
    UMICOM_EVENT_NOT_INITIALISED,
    UMICOM_EVENT_BAD_STATE,
    UMICOM_EVENT_INVALID_HANDLE,
    UMICOM_EVENT_CAPACITY,
    UMICOM_EVENT_WRONG_CONTEXT,
    UMICOM_EVENT_COUNTER_EXHAUSTED,
    UMICOM_EVENT_SCHEDULER_ERROR,
    UMICOM_EVENT_CORRUPT_STATE
} UmicomKernelEventStatus;
typedef enum UmicomKernelEventOutcome {
    UMICOM_EVENT_PENDING,
    UMICOM_EVENT_SIGNALED,
    UMICOM_EVENT_TIMED_OUT,
    UMICOM_EVENT_CLOSED
} UmicomKernelEventOutcome;

/* Completion describes a decision, not ownership of another service's data.
 * A successful wait must still be followed by a check of the protected
 * condition. For a timeout, index is NO_SELECTION and event is zero. */
typedef struct UmicomKernelEventResult {
    UmicomKernelEventOutcome outcome;
    UmicomSize index;
    UmicomKernelEventHandle event;
    UmicomU64 observedTime;
} UmicomKernelEventResult;

/* Fields are public solely for static storage and fault-injection tests.
 * Only events.c may mutate a live domain. Do not copy or reset one. */
typedef struct UmicomKernelEventRecord {
    UmicomU32 generation;
    UmicomKernelEventMode mode;
    UmicomBoolean occupied;
    UmicomBoolean retired;
    UmicomBoolean signalled;
} UmicomKernelEventRecord;
typedef struct UmicomKernelEventWaitRecord {
    UmicomBoolean occupied;
    UmicomKernelThreadHandle thread;
    UmicomU64 ticket; /* Older registrations are considered first. */
    UmicomSize count;
    UmicomKernelEventHandle events[UMICOM_EVENT_WAIT_ANY_LIMIT];
    UmicomBoolean hasDeadline;
    UmicomU64 deadline;
    UmicomKernelEventResult result;
} UmicomKernelEventWaitRecord;
typedef struct UmicomKernelEventDomain {
    const struct UmicomKernelEventDomain *self;
    UmicomKernelScheduler *scheduler; /* Borrowed for the whole domain lifetime. */
    UmicomBoolean initialised;
    UmicomBoolean poisoned; /* An unsafe wake is diagnosed, never ignored. */
    UmicomU64 nextTicket;
    UmicomKernelEventRecord events[UMICOM_EVENT_LIMIT];
    UmicomKernelEventWaitRecord waiters[UMICOM_EVENT_WAIT_LIMIT];
} UmicomKernelEventDomain;
typedef struct UmicomKernelEventSnapshot {
    UmicomSize openEvents;
    UmicomSize signalledEvents;
    UmicomSize retiredEvents;
    UmicomSize pendingWaits;
    UmicomSize completedWaits; /* Awarded, but the callback has not resumed yet. */
} UmicomKernelEventSnapshot;

/* Initialise stable zero-filled storage once. The scheduler must already be
 * initialised and must outlive the domain. Even an empty domain is not reset:
 * its generations keep old handles invalid. All pointers are trusted Kernel
 * pointers. Input/output storage must not overlap the domain or scheduler. */
UmicomKernelEventStatus UmicomKernelEventDomainInitialize(
    UmicomKernelEventDomain *domain, UmicomKernelScheduler *scheduler);
UmicomKernelEventStatus UmicomKernelEventCreate(
    UmicomKernelEventDomain *domain, UmicomKernelEventMode mode,
    UmicomBoolean initiallySignalled, UmicomKernelEventHandle *outHandle);

/* Signal awards eligible waiters now. Reset clears only the remembered state;
 * it does not retract a completion already awarded to a runnable thread. */
UmicomKernelEventStatus UmicomKernelEventSignal(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle handle);
UmicomKernelEventStatus UmicomKernelEventReset(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle handle);

/* Closing wakes registered waiters with CLOSED, then invalidates this token.
 * A later use of that token is INVALID_HANDLE, not an enduring closed object.
 * A generation at its maximum retires the slot rather than wrapping. */
UmicomKernelEventStatus UmicomKernelEventClose(
    UmicomKernelEventDomain *domain, UmicomKernelEventHandle handle);

/* Called by the running thread named by thread. The callback resumes this
 * same C call; it is not restarted from entry. The handles are copied before
 * suspension. Duplicate handles and an empty set are refused.
 *
 * AdvanceTime on the scheduler drives deadlines. Pump between dispatches to
 * settle timeouts promptly and discard registrations of cancelled/reaped
 * threads. Signal, Reset, Close and WaitAny also pump before doing new work.
 *
 * An already available event wins over a deadline on admission. Once waiting,
 * an elapsed deadline is settled before a later Signal can award a credit.
 * A direct ThreadWake is only a hint: WaitAny rechecks and sleeps again unless
 * an event, close or deadline actually completed the wait.
 *
 * The output is changed only on successful completion. OK means the wait
 * completed; inspect outcome to distinguish a signal, timeout and close. */
UmicomKernelEventStatus UmicomKernelEventWaitAny(
    UmicomKernelEventDomain *domain, UmicomKernelThreadHandle thread,
    const UmicomKernelEventHandle *events, UmicomSize count,
    UmicomBoolean hasDeadline, UmicomU64 deadline,
    UmicomKernelEventResult *outResult);
UmicomKernelEventStatus UmicomKernelEventPump(UmicomKernelEventDomain *domain);
UmicomKernelEventStatus UmicomKernelEventDomainValidate(UmicomKernelEventDomain *domain);
UmicomKernelEventStatus UmicomKernelEventSnapshotRead(
    UmicomKernelEventDomain *domain, UmicomKernelEventSnapshot *outSnapshot);
const char *UmicomKernelEventStatusName(UmicomKernelEventStatus status);
void UmicomKernelEventsValidateExecution(void);
#endif /* UMICOM_KERNEL_EVENTS_H */
