/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/process_supervisor.h
 *
 * PURPOSE:
 *   Give scheduled programs a parent, a terminal-result owner and an explicit
 *   cleanup path across the existing task scheduler and private message domain.
 *
 * EDUCATIONAL OVERVIEW:
 *   Stopping execution, closing IPC endpoints and collecting a program's result
 *   are different operations. The scheduler already stops instructions and
 *   closes its task's IPC references. This service retains the result, applies
 *   the parent's child-lifetime policy, and releases the image only when its
 *   authorised collector asks. No program or page-table owner is copied.
 *
 *   This is a Kernel-only service for serial execution on hart zero. A caller
 *   principal is trusted Kernel context, not an identity taken from a user
 *   argument. A future spawn/wait syscall must establish that binding itself.
 *   These task tokens are not the run-once process registry's handle domain.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_PROCESS_SUPERVISOR_H
#define UMICOM_KERNEL_PROCESS_SUPERVISOR_H
#include "umicom/kernel/user_ipc.h"

/* Zero belongs to the Kernel's guardian, never to a loaded program. The normal
 * System Manager can act as that guardian; user identities start above zero. */
#define UMICOM_SUPERVISION_GUARDIAN ((UmicomU64)0U)

/* Reuse the scheduler's generation-tagged token. There is no second slot or
 * generation algorithm to drift out of sync with the actual execution owner. */
typedef UmicomKernelUserTaskHandle UmicomKernelSupervisedProcessHandle;

typedef enum UmicomKernelChildLifetime {
    /* Direct children move to the guardian and may continue after this parent. */
    UMICOM_CHILDREN_ADOPT,
    /* Stop the current descendant tree, then retain every result for collection. */
    UMICOM_CHILDREN_CANCEL_TREE
} UmicomKernelChildLifetime;

typedef enum UmicomKernelSupervisionReason {
    UMICOM_SUPERVISION_PROGRAM_STOP,
    UMICOM_SUPERVISION_CANCEL_REQUEST,
    UMICOM_SUPERVISION_PARENT_STOP,
    UMICOM_SUPERVISION_SHUTDOWN
} UmicomKernelSupervisionReason;

typedef enum UmicomKernelSupervisionStatus {
    UMICOM_SUPERVISION_OK,
    UMICOM_SUPERVISION_INVALID_ARGUMENT,
    UMICOM_SUPERVISION_BAD_STATE,
    UMICOM_SUPERVISION_BUSY,
    UMICOM_SUPERVISION_UNSAFE,
    UMICOM_SUPERVISION_INVALID_HANDLE,
    UMICOM_SUPERVISION_WRONG_PARENT,
    UMICOM_SUPERVISION_PARENT_ENDED,
    UMICOM_SUPERVISION_ADMISSION_CLOSED,
    UMICOM_SUPERVISION_CAPACITY,
    UMICOM_SUPERVISION_LOAD_FAILED,
    UMICOM_SUPERVISION_ENTRY_REFUSED,
    UMICOM_SUPERVISION_LOWER_FAILURE,
    UMICOM_SUPERVISION_NOT_TERMINAL,
    UMICOM_SUPERVISION_CLEANUP_FAILED,
    UMICOM_SUPERVISION_NO_CHILDREN,
    UMICOM_SUPERVISION_WOULD_BLOCK,
    UMICOM_SUPERVISION_IDLE,
    UMICOM_SUPERVISION_IPC_REFUSED,
    UMICOM_SUPERVISION_CORRUPT_STATE
} UmicomKernelSupervisionStatus;

/* Stable terminal evidence contains values, never Kernel pointers or physical
 * addresses. Birth-parent identity is historical provenance, not current rights.
 * exitValue is defined only for EXITED; cancellation does not invent a fault. */
typedef struct UmicomKernelProcessCompletion {
    UmicomU64 identity;
    UmicomU64 birthParent;
    UmicomKernelUserTaskState state;
    UmicomKernelSupervisionReason reason;
    UmicomU64 exitValue;
    UmicomU64 trapCause;
    UmicomU64 systemCalls;
    UmicomU64 slices;
    UmicomU64 preemptions;
} UmicomKernelProcessCompletion;

typedef struct UmicomKernelSupervisedProcessInfo {
    UmicomU64 identity;
    UmicomU64 parent; /* Current result owner; may become the guardian. */
    UmicomU64 birthParent;
    UmicomKernelChildLifetime childLifetime;
    UmicomKernelUserTaskState state;
    UmicomBoolean terminal;
    UmicomBoolean familySettled;
    UmicomBoolean stopRequested;
    UmicomBoolean unsafe; /* A diagnostic read does not authorise reclamation. */
    UmicomKernelProcessCompletion completion;
} UmicomKernelSupervisedProcessInfo;

/* Visible for static allocation and explicit fault-injection tests. Consumers
 * must not edit these records or call the embedded scheduler/IPC owners behind
 * the supervisor's back. Keep the entire object at one stable zero-filled address. */
typedef struct UmicomKernelSupervisedProcessRecord {
    UmicomKernelSupervisedProcessHandle handle;
    UmicomU64 identity;
    UmicomU64 parent;
    UmicomU64 birthParent;
    UmicomKernelChildLifetime childLifetime;
    UmicomKernelSupervisionReason stopReason;
    UmicomBoolean occupied;
    UmicomBoolean terminal;
    UmicomBoolean familySettled;
    UmicomBoolean stopRequested;
    UmicomKernelProcessCompletion completion;
} UmicomKernelSupervisedProcessRecord;

typedef struct UmicomKernelProcessSupervisor {
    const struct UmicomKernelProcessSupervisor *self;
    UmicomBoolean initialised;
    UmicomBoolean active;
    UmicomBoolean poisoned;
    UmicomBoolean admissionClosed;
    UmicomKernelSupervisedProcessRecord children[UMICOM_USER_TASK_LIMIT];
    UmicomKernelUserScheduler scheduler; /* Sole execution and image-lifetime owner. */
    UmicomKernelUserIpc ipc; /* Existing endpoint and pending-operation ownership. */
} UmicomKernelProcessSupervisor;

typedef struct UmicomKernelSupervisionSnapshot {
    UmicomSize children;
    UmicomSize live;
    UmicomSize blocked;
    UmicomSize terminal;
    UmicomSize guardianChildren;
    UmicomSize retainedLoads; /* Failed loader cleanup without a published token. */
    UmicomBoolean admissionClosed;
    UmicomBoolean unsafe;
} UmicomKernelSupervisionSnapshot;

/* Initialise once; resetting an empty live domain would revive old task tokens.
 * All API pointers are trusted, non-overlapping Kernel spans, not user buffers.
 * This service must be called between user quanta, with interrupts disabled. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorInitialize(UmicomKernelProcessSupervisor *supervisor);
/* parent is a live principal in this domain, or the guardian. The image and
 * argument are copied/admitted through the original loader and scheduler. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSpawn(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 parent, const UmicomU8 *image, UmicomSize bytes, UmicomU64 argument,
    UmicomU64 sliceLimit, UmicomKernelChildLifetime childLifetime,
    UmicomKernelSupervisedProcessHandle *outHandle);
/* Query/Cancel/Collect require the direct parent or guardian. A sibling is not
 * an owner, and a former parent loses authority when its children are adopted. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorQuery(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle, UmicomKernelSupervisedProcessInfo *outInfo);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorCancel(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle);
/* StopTree records the whole current subtree before cancelling anything, so
 * adoption cannot let a grandchild escape an explicit tree-stop request. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorStopTree(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle);
/* These two admission helpers preserve the lower service's fresh-task checks. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSetArgument(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle, UmicomU64 argument);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorConnect(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle first, UmicomKernelMessageRights firstRights,
    UmicomKernelSupervisedProcessHandle second, UmicomKernelMessageRights secondRights,
    UmicomKernelMessageHandle *outFirst, UmicomKernelMessageHandle *outSecond);
/* Pump settles parents, cancelled descendants and IPC readiness without running
 * user instructions. RunOne calls it both before and after the existing runner. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorPump(UmicomKernelProcessSupervisor *supervisor);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorRunOne(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 quantumTicks, UmicomKernelSupervisedProcessHandle *outHandle);
/* Collect destroys an eligible terminal image, then publishes its cached result
 * and invalidates the record. On failure output and evidence are retained.
 * CollectAny scans direct children in slot order; it is nonblocking, not waitpid. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorCollect(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle, UmicomKernelProcessCompletion *outCompletion);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorCollectAny(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelProcessCompletion *outCompletion);
/* Shutdown permanently closes admission and cancels live work, but keeps every
 * result/image until explicit collection. A failed cleanup is retried, not reset. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorBeginShutdown(UmicomKernelProcessSupervisor *supervisor);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorReapRetained(UmicomKernelProcessSupervisor *supervisor,
    UmicomSize *outReaped);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSnapshot(UmicomKernelProcessSupervisor *supervisor,
    UmicomKernelSupervisionSnapshot *outInfo);
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorValidate(UmicomKernelProcessSupervisor *supervisor);
const char *UmicomKernelSupervisionStatusName(UmicomKernelSupervisionStatus status);
void UmicomKernelProcessSupervisionValidateExecution(void);
/* Parent-checked structured setup; caller identity remains trusted Kernel
 * context, exactly as for the existing numeric-argument admission helper. */
UmicomKernelSupervisionStatus UmicomKernelProcessSupervisorSetLaunch(UmicomKernelProcessSupervisor *supervisor,
    UmicomU64 caller, UmicomKernelSupervisedProcessHandle handle,
    const UmicomKernelProgramLaunchSpec *spec);
#endif /* UMICOM_KERNEL_PROCESS_SUPERVISOR_H */
