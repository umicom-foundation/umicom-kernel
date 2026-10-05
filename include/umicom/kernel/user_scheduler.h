/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/user_scheduler.h
 *
 * PURPOSE:
 *   Own separately loaded user continuations and dispatch one timer-bounded
 *   quantum at a time, without replacing cooperative Kernel callback threads.
 *
 * EDUCATIONAL OVERVIEW:
 *   A stopped CPU is not necessarily a stopped program. After a quantum ends,
 *   the saved integer frame and its private address space still describe live
 *   work. The next dispatch restores that frame rather than calling the ELF
 *   entry again. Cancelling a paused task abandons that continuation, but its
 *   memory remains owned until Reap explicitly destroys the image.
 *
 *   These are trusted Kernel APIs for one hart. Keep the scheduler in stable,
 *   zero-filled storage and never copy it. Record fields are exposed solely for
 *   static allocation and tests; only this service may mutate live records.
 *   This is not SMP scheduling, Kernel pre-emption, or a user handle ABI.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_SCHEDULER_H
#define UMICOM_KERNEL_USER_SCHEDULER_H
#include "umicom/kernel/process.h"

/* Admission is bounded before any image is loaded. A quantum is measured in
 * the selected platform's timer ticks, not host CPU cycles or instructions. */
#define UMICOM_USER_TASK_LIMIT 4U
#define UMICOM_USER_TASK_SLICE_LIMIT 4096U
#define UMICOM_USER_QUANTUM_MIN_TICKS 1000U
#define UMICOM_USER_QUANTUM_MAX_TICKS 1000000U

typedef UmicomU64 UmicomKernelUserTaskHandle;
typedef enum UmicomKernelUserTaskState {
    UMICOM_USER_TASK_EMPTY,
    UMICOM_USER_TASK_READY,
    UMICOM_USER_TASK_RUNNING,
    UMICOM_USER_TASK_PAUSED,
    UMICOM_USER_TASK_EXITED,
    UMICOM_USER_TASK_FAULTED,
    UMICOM_USER_TASK_EXHAUSTED,
    UMICOM_USER_TASK_CANCELLED,
    UMICOM_USER_TASK_ERROR,
    UMICOM_USER_TASK_RETAINED
    /* Appending preserves every established state's numeric value. */
    , UMICOM_USER_TASK_BLOCKED = 10
} UmicomKernelUserTaskState;
typedef enum UmicomKernelUserScheduleStatus {
    UMICOM_USER_SCHEDULE_OK,
    UMICOM_USER_SCHEDULE_INVALID_ARGUMENT,
    UMICOM_USER_SCHEDULE_BAD_STATE,
    UMICOM_USER_SCHEDULE_BUSY,
    UMICOM_USER_SCHEDULE_INVALID_HANDLE,
    UMICOM_USER_SCHEDULE_CAPACITY,
    UMICOM_USER_SCHEDULE_IDENTITY_EXHAUSTED,
    UMICOM_USER_SCHEDULE_LOAD_FAILED,
    UMICOM_USER_SCHEDULE_IDLE,
    UMICOM_USER_SCHEDULE_ENTRY_REFUSED,
    UMICOM_USER_SCHEDULE_INVALID_CONTEXT,
    UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR,
    UMICOM_USER_SCHEDULE_CLEANUP_FAILED
} UmicomKernelUserScheduleStatus;

typedef struct UmicomKernelUserTask {
    UmicomKernelProcess process; /* Existing loader owns every page and table. */
    UmicomRiscvTrapFrame frame;   /* Stable values, never a pointer into a past trap stack. */
    UmicomKernelUserTaskState state;
    UmicomU64 slices;
    UmicomU64 preemptions;
    UmicomU64 sliceLimit;
    UmicomU32 generation;
    UmicomBoolean retired;
} UmicomKernelUserTask;

typedef struct UmicomKernelUserScheduler {
    const struct UmicomKernelUserScheduler *self;
    UmicomBoolean initialised;
    UmicomBoolean active;   /* Serial reentry guard; it is not an atomic SMP lock. */
    UmicomBoolean poisoned; /* Unverified return forbids another dispatch or free. */
    UmicomSize next;        /* Next round-robin search begins here. */
    UmicomU64 nextIdentity; /* Never supplied by an executable or recycled on close. */
    UmicomU64 dispatches;
    UmicomKernelUserTask tasks[UMICOM_USER_TASK_LIMIT];
    /* Optional owner for blocked messages; zero keeps the original path. */
    struct UmicomKernelUserIpc *ipc;
} UmicomKernelUserScheduler;

typedef struct UmicomKernelUserTaskInfo {
    UmicomKernelUserTaskState state;
    UmicomU64 identity;
    UmicomU64 slices;
    UmicomU64 preemptions;
    UmicomU64 systemCalls;
    UmicomU64 exitValue;    /* Meaningful only for EXITED. */
    UmicomU64 trapCause;    /* Actual last mcause; budget exhaustion is not a new fault. */
    UmicomAddress resumePc;
    UmicomAddress savedStack;
} UmicomKernelUserTaskInfo;

/* Initialise once. Reinitialisation would resurrect generation-tagged tokens. */
UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerInitialize(UmicomKernelUserScheduler *scheduler);
/* The ELF buffer and outputs are trusted, non-overlapping Kernel storage.
 * Successful loading copies the executable; it does not borrow the ELF bytes. */
UmicomKernelUserScheduleStatus UmicomKernelUserTaskCreate(UmicomKernelUserScheduler *scheduler,
    const UmicomU8 *image, UmicomSize bytes, UmicomU64 argument, UmicomU64 sliceLimit,
    UmicomKernelUserTaskHandle *outHandle);
/* Run one eligible task. IDLE means no READY/PAUSED task, not that all records
 * were reaped. A refused entry preserves the continuation and does not spend
 * its quantum budget. outHandle is written only after a captured invocation. */
UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerRunOne(UmicomKernelUserScheduler *scheduler,
    UmicomU64 quantumTicks, UmicomKernelUserTaskHandle *outHandle);
UmicomKernelUserScheduleStatus UmicomKernelUserTaskQuery(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle, UmicomKernelUserTaskInfo *outInfo);
/* Cancel only a non-running live continuation. Reap only a terminal one.
 * Neither operation unwinds user code or implicitly releases external IPC
 * handles. A future supervisor must coordinate those other resource owners. */
UmicomKernelUserScheduleStatus UmicomKernelUserTaskCancel(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle);
UmicomKernelUserScheduleStatus UmicomKernelUserTaskReap(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle);
/* Rare loader rollback failures retain an unpublished record for retry. */
UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerReapRetained(UmicomKernelUserScheduler *scheduler,
    UmicomSize *outReaped);
UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerValidate(UmicomKernelUserScheduler *scheduler);
const char *UmicomKernelUserTaskStateName(UmicomKernelUserTaskState state);
void UmicomKernelUserSchedulingValidateExecution(void);
/* The attached private IPC domain, when present, participates in terminal
 * cleanup. External domains still require their separate supervisor/owner. */
/* Trusted setup before the first instruction only. Admission can pass a newly
 * issued endpoint after both task identities have been allocated. */
UmicomKernelUserScheduleStatus UmicomKernelUserTaskSetArgument(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle, UmicomU64 argument);
#endif /* UMICOM_KERNEL_USER_SCHEDULER_H */
