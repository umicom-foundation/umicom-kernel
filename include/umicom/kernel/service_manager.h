/*-----------------------------------------------------------------------------
 * Umicom Kernel managed native-service lifetimes
 * File: include/umicom/kernel/service_manager.h
 *
 * The Master Controller keeps native Slave Controller processes alive only
 * while their explicit readiness and health leases remain valid. Dependencies
 * refer to live healthy instances, unlike the terminal-success dependencies of
 * boot_services.h. The one-shot boot controller remains available unchanged.
 *
 * Keep the owner in stable zero-filled Kernel memory; never copy or reset it.
 * All calls are serial on hart zero, between user quanta. This is neither an
 * SMP lock nor Kernel pre-emption. Names and launch strings are copied; ELF
 * buffers must remain immutable and alive until the manager has closed.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_SERVICE_MANAGER_H
#define UMICOM_KERNEL_SERVICE_MANAGER_H
#include "umicom/kernel/process_supervisor.h"
#include "umicom/kernel/user_streams.h"
#include "umicom/kernel/service_report_abi.h"

/* The plan fits the underlying scheduler: prerequisites and consumers can be
 * live together without relying on an overcommitted task slot. */
#define UMICOM_MANAGED_SERVICE_LIMIT UMICOM_USER_TASK_LIMIT
#define UMICOM_MANAGED_SERVICE_NAME_BYTES 32U
#define UMICOM_MANAGED_SERVICE_ATTEMPTS 4U
#define UMICOM_MANAGED_SERVICE_TIME_LIMIT 100000000ULL

typedef enum UmicomKernelManagedServiceState {
    UMICOM_MANAGED_WAITING, UMICOM_MANAGED_STARTING, UMICOM_MANAGED_HEALTHY,
    UMICOM_MANAGED_STOPPING, UMICOM_MANAGED_BACKOFF, UMICOM_MANAGED_FAILED,
    UMICOM_MANAGED_STOPPED
} UmicomKernelManagedServiceState;
typedef enum UmicomKernelServiceManagerPhase {
    UMICOM_MANAGER_STARTING, UMICOM_MANAGER_HEALTHY, UMICOM_MANAGER_DEGRADED,
    UMICOM_MANAGER_RECOVERY, UMICOM_MANAGER_STOPPING, UMICOM_MANAGER_STOPPED, UMICOM_MANAGER_UNSAFE
} UmicomKernelServiceManagerPhase;
typedef enum UmicomKernelManagedServiceReason {
    UMICOM_MANAGED_NO_FAILURE, UMICOM_MANAGED_LOAD_FAILURE, UMICOM_MANAGED_SETUP_FAILURE,
    UMICOM_MANAGED_EXITED, UMICOM_MANAGED_FAULTED, UMICOM_MANAGED_BUDGET,
    UMICOM_MANAGED_NOT_READY, UMICOM_MANAGED_HEALTH_EXPIRED,
    UMICOM_MANAGED_DEPENDENCY_LOST, UMICOM_MANAGED_RESTART_REQUEST,
    UMICOM_MANAGED_SHUTDOWN
} UmicomKernelManagedServiceReason;
typedef enum UmicomKernelServiceManagerStatus {
    UMICOM_MANAGER_OK, UMICOM_MANAGER_INVALID_ARGUMENT, UMICOM_MANAGER_INVALID_PLAN,
    UMICOM_MANAGER_BAD_STATE, UMICOM_MANAGER_BUSY, UMICOM_MANAGER_ENTRY_REFUSED,
    UMICOM_MANAGER_CLOCK_REVERSED, UMICOM_MANAGER_CLEANUP_FAILED,
    UMICOM_MANAGER_ATTEMPTS_EXHAUSTED, UMICOM_MANAGER_STATE_UNSAFE
} UmicomKernelServiceManagerStatus;

typedef struct UmicomKernelManagedServiceSpec {
    const char *name;
    const UmicomU8 *image;
    UmicomSize imageBytes;
    UmicomKernelProgramLaunchSpec launch;
    UmicomU32 dependencies; /* Plan-index bits; all prerequisites must be HEALTHY. */
    UmicomU32 attempts;     /* Total admissions, including dependency-driven restarts. */
    UmicomU64 startupTicks;
    UmicomU64 healthTicks;
    UmicomU64 reportTicks;  /* Park after an accepted report for this interval. */
    UmicomU64 retryTicks;
    UmicomU64 sliceLimit;   /* Never renewed by a heartbeat; the syscall limit also remains. */
    UmicomBoolean required;
} UmicomKernelManagedServiceSpec;

typedef struct UmicomKernelManagedServiceInfo {
    char name[UMICOM_MANAGED_SERVICE_NAME_BYTES];
    UmicomKernelManagedServiceState state;
    UmicomKernelManagedServiceReason reason;
    UmicomBoolean required;
    UmicomU32 attempts;
    UmicomU64 identity;
    UmicomU64 sequence;
    UmicomU64 reports;
    UmicomU64 readyAt;
    UmicomU64 deadline;   /* Readiness deadline before READY; health deadline afterwards. */
    UmicomU64 resumeAt;
    UmicomU64 retryAt;
    UmicomKernelProcessCompletion completion; /* Last collected attempt, not a live pointer. */
} UmicomKernelManagedServiceInfo;
typedef void (*UmicomKernelManagedServiceOutput)(void *context, const char *name,
    const UmicomKernelStreamPacket *packet);

/* Public layout permits static allocation and fault-injection tests. Ordinary
 * callers inspect Query snapshots; they must not edit these live records. */
typedef struct UmicomKernelManagedServiceRecord {
    UmicomKernelManagedServiceSpec spec;
    UmicomKernelManagedServiceInfo info;
    UmicomKernelLaunchImage launch;
    UmicomKernelSupervisedProcessHandle handle;
    UmicomBoolean streamsGranted;
    UmicomBoolean terminalObserved;
    UmicomBoolean checkpointPending;
    UmicomU64 reportOperation;
    UmicomU64 reportSequence;
    UmicomU64 reportTime;
    UmicomAddress reportNextPc;
} UmicomKernelManagedServiceRecord;
typedef struct UmicomKernelServiceManager {
    const struct UmicomKernelServiceManager *self;
    UmicomBoolean initialised;
    UmicomBoolean active;
    UmicomBoolean stopping;
    UmicomBoolean recovery;
    UmicomBoolean closed;
    UmicomBoolean retainedLoad;
    UmicomKernelServiceManagerPhase phase;
    UmicomSize count;
    UmicomSize order[UMICOM_MANAGED_SERVICE_LIMIT]; /* Prerequisites first. */
    UmicomU64 now;
    UmicomKernelManagedServiceOutput output;
    void *outputContext;
    UmicomKernelManagedServiceRecord records[UMICOM_MANAGED_SERVICE_LIMIT];
    UmicomKernelProcessSupervisor supervisor;
    UmicomKernelUserStreams streams;
} UmicomKernelServiceManager;

/* All pointers are trusted, non-overlapping Kernel storage. Invalid plans
 * change neither the zero-filled owner nor physical-frame accounting. */
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerInitialize(
    UmicomKernelServiceManager *manager, const UmicomKernelManagedServiceSpec *specs,
    UmicomSize count, UmicomKernelManagedServiceOutput output, void *context);
/* At most one user quantum. Step also settles deadlines and cleanup when no
 * task is runnable. Call it regularly; deadlines are decisions, not guarantees
 * that a stalled Kernel will run a handler at an exact wall-clock instant. */
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerStep(
    UmicomKernelServiceManager *manager, UmicomU64 quantumTicks);
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerRestart(
    UmicomKernelServiceManager *manager, UmicomSize index);
/* No forced reset: a failed collection retains the handle for another Close. */
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerClose(UmicomKernelServiceManager *manager);
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerQuery(
    UmicomKernelServiceManager *manager, UmicomSize index, UmicomKernelManagedServiceInfo *outInfo);
const char *UmicomKernelManagedServiceReasonName(UmicomKernelManagedServiceReason reason);
const char *UmicomKernelManagedServiceStateName(UmicomKernelManagedServiceState state);

/* Narrow scheduler/monitor hooks. An unattached scheduler keeps its old path.
 * Only the current exact session can report readiness. The report is committed
 * after the architecture adapter has verified restoration of machine controls. */
UmicomBoolean UmicomKernelServiceTaskEligible(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelServiceCheckpointPending(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelServiceCheckpointCommit(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomU64 UmicomKernelServiceReportDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc);
void UmicomKernelManagedServicesValidateExecution(void);
#endif /* UMICOM_KERNEL_SERVICE_MANAGER_H */
