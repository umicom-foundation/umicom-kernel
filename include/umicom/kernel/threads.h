/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/threads.h
 *
 * PURPOSE:
 *   Own bounded Kernel threads and their separate stacks, with cooperative
 *   round-robin dispatch, sleeping, explicit wake-up and terminal collection.
 *
 * EDUCATIONAL OVERVIEW:
 *   Threads share Kernel authority and an address space. They are not isolated
 *   user processes. A callback gives up the CPU by yielding, waiting, sleeping
 *   or returning; RunOne resumes it exactly where it suspended. No arbitrary
 *   instruction is pre-empted and a callback that never yields can stall this
 *   scheduler. A dispatch budget limits switches, not elapsed callback time.
 *
 *   Store each scheduler at a stable, zero-filled Kernel address. Its stacks
 *   are part of that object, avoiding a contiguous-allocation assumption about
 *   the physical frame allocator. Do not copy, reset or directly edit a live
 *   scheduler. The caller serialises all access on one hart.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_THREADS_H
#define UMICOM_KERNEL_THREADS_H
#include "umicom/kernel/thread_context.h"

#define UMICOM_THREAD_LIMIT 8U
#define UMICOM_THREAD_STACK_BYTES 16384U
#define UMICOM_THREAD_STACK_MARGIN 64U
#define UMICOM_THREAD_NO_SLOT UMICOM_THREAD_LIMIT

/* The two diagnostic margins must leave an ABI-aligned usable stack. */
_Static_assert(UMICOM_THREAD_STACK_BYTES > 2U * UMICOM_THREAD_STACK_MARGIN,
    "Thread stack must contain usable bytes between its margins");
_Static_assert((UMICOM_THREAD_STACK_BYTES % 16U) == 0U && (UMICOM_THREAD_STACK_MARGIN % 16U) == 0U,
    "Thread stack boundaries must preserve sixteen-byte call alignment");

typedef UmicomU64 UmicomKernelThreadHandle;
typedef UmicomU64 (*UmicomKernelThreadEntry)(void *argument);
typedef enum UmicomKernelThreadState {
    UMICOM_THREAD_EMPTY,
    UMICOM_THREAD_READY,
    UMICOM_THREAD_RUNNING,
    UMICOM_THREAD_SLEEPING,
    UMICOM_THREAD_WAITING,
    UMICOM_THREAD_FINISHED,
    UMICOM_THREAD_CANCELLED
} UmicomKernelThreadState;
typedef enum UmicomKernelThreadStatus {
    UMICOM_THREAD_OK,
    UMICOM_THREAD_INVALID_ARGUMENT,
    UMICOM_THREAD_NOT_INITIALISED,
    UMICOM_THREAD_BAD_STATE,
    UMICOM_THREAD_UNSAFE_MACHINE,
    UMICOM_THREAD_INVALID_HANDLE,
    UMICOM_THREAD_CAPACITY,
    UMICOM_THREAD_WRONG_CONTEXT,
    UMICOM_THREAD_NOT_FINISHED,
    UMICOM_THREAD_IDLE,
    UMICOM_THREAD_CLOCK_REVERSED,
    UMICOM_THREAD_COUNTER_EXHAUSTED,
    UMICOM_THREAD_CORRUPT_STATE
} UmicomKernelThreadStatus;

/* Visible for static allocation and fault-injection tests only. All mutation
 * belongs to threads.c; consumers use handles and value snapshots below. */
typedef struct UmicomKernelThreadRecord {
    UmicomKernelThreadContext context;
    UmicomKernelThreadEntry entry;
    void *argument; /* Borrowed until reaped; cancellation does not destroy it. */
    UmicomU64 wakeTime;
    UmicomU64 exitValue;
    UmicomU64 dispatches;
    UmicomU64 yields;
    UmicomU32 generation;
    UmicomKernelThreadState state;
    UmicomBoolean retired; /* Generation exhaustion spends capacity, never wraps. */
    alignas(16) UmicomU8 stack[UMICOM_THREAD_STACK_BYTES];
} UmicomKernelThreadRecord;
typedef struct UmicomKernelScheduler {
    const struct UmicomKernelScheduler *self;
    UmicomBoolean initialised;
    UmicomBoolean active; /* Reentry guard, not an atomic SMP lock. */
    UmicomBoolean poisoned; /* Unsafe return requires diagnosis, not more runs. */
    UmicomSize current;
    UmicomSize next;
    UmicomU64 now; /* Monotonic time supplied explicitly by the dispatcher. */
    UmicomU64 dispatches;
    UmicomKernelThreadContext dispatcher;
    UmicomKernelThreadRecord threads[UMICOM_THREAD_LIMIT];
} UmicomKernelScheduler;

typedef struct UmicomKernelThreadInfo {
    UmicomKernelThreadState state;
    UmicomU64 wakeTime;
    UmicomU64 exitValue; /* Meaningful only for FINISHED, not CANCELLED. */
    UmicomU64 dispatches;
    UmicomU64 yields;
} UmicomKernelThreadInfo;
typedef struct UmicomKernelSchedulerInfo {
    UmicomSize ready;
    UmicomSize running;
    UmicomSize sleeping;
    UmicomSize waiting;
    UmicomSize terminal;
    UmicomSize freeSlots;
    UmicomSize retiredSlots;
    UmicomU64 now;
    UmicomU64 dispatches;
    UmicomBoolean hasDeadline;
    UmicomU64 nextDeadline; /* Valid only when hasDeadline is true. */
} UmicomKernelSchedulerInfo;

/* Initialise zero-filled storage once. Reinitialisation would revive tokens. */
UmicomKernelThreadStatus UmicomKernelSchedulerInitialize(UmicomKernelScheduler *scheduler);
/* All pointers are trusted Kernel pointers and must not overlap the scheduler.
 * Create, Cancel, Reap and AdvanceTime are dispatcher-only operations. */
UmicomKernelThreadStatus UmicomKernelThreadCreate(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadEntry entry, void *argument, UmicomKernelThreadHandle *outHandle);
UmicomKernelThreadStatus UmicomKernelThreadCancel(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle);
/* Reap is explicit. A suspended or running stack cannot be scrubbed/reused.
 * The terminal result is copied out before the stack is cleared and the handle
 * invalidated. Closing a cancelled thread does not run callback destructors. */
UmicomKernelThreadStatus UmicomKernelThreadReap(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle, UmicomKernelThreadInfo *outInfo);
UmicomKernelThreadStatus UmicomKernelThreadQuery(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle, UmicomKernelThreadInfo *outInfo);

/* Run one ready continuation. IDLE means none is ready now, not that every
 * thread has finished. Sleepers and explicit waiters retain their stacks. */
UmicomKernelThreadStatus UmicomKernelSchedulerRunOne(UmicomKernelScheduler *scheduler);
/* Advance the logical clock without sleeping or programming any hardware.
 * The caller can pass UmicomPlatformTimerRead() between dispatches. */
UmicomKernelThreadStatus UmicomKernelSchedulerAdvanceTime(UmicomKernelScheduler *scheduler,
    UmicomU64 now);
UmicomKernelThreadStatus UmicomKernelSchedulerSnapshot(UmicomKernelScheduler *scheduler,
    UmicomKernelSchedulerInfo *outInfo);
UmicomKernelThreadStatus UmicomKernelSchedulerValidate(UmicomKernelScheduler *scheduler);

/* These operations suspend only the current callback of this scheduler. Their
 * C calls return after a later dispatch restores the same stack and registers. */
UmicomKernelThreadStatus UmicomKernelThreadYield(UmicomKernelScheduler *scheduler);
UmicomKernelThreadStatus UmicomKernelThreadSleepUntil(UmicomKernelScheduler *scheduler,
    UmicomU64 deadline);
UmicomKernelThreadStatus UmicomKernelThreadWait(UmicomKernelScheduler *scheduler);
/* Wake may be called by a peer callback or dispatcher. A ready target is an
 * idempotent success, but no wake credit is saved for a future Wait. Condition
 * checks and Wait must not yield between them; this is a serialised protocol. */
UmicomKernelThreadStatus UmicomKernelThreadWake(UmicomKernelScheduler *scheduler,
    UmicomKernelThreadHandle handle);

const char *UmicomKernelThreadStatusName(UmicomKernelThreadStatus status);
void UmicomKernelThreadsValidateExecution(void);
/* Architecture bootstrap target, not an application entry point. */
_Noreturn void UmicomKernelThreadStart(UmicomKernelScheduler *scheduler, UmicomSize slot);
#endif /* UMICOM_KERNEL_THREADS_H */
