/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/user_ipc.h
 *
 * PURPOSE:
 *   Own pending user message calls alongside one resumable task scheduler.
 *
 * EDUCATIONAL OVERVIEW:
 *   A blocked task still owns its image, stack and register frame, but it is
 *   not runnable. This owner records tokens, address values and copied SEND
 *   bytes, never a pointer into an invocation's temporary trap stack. Completion
 *   writes the stable frame before publishing it as runnable again.
 *
 *   A private channel domain prevents identity collisions between independent
 *   schedulers. Both owners have stable, zero-filled storage and are not copied,
 *   reset or detached during their lifetime. Access is serial on hart zero,
 *   with Kernel interrupts disabled; this is not an IRQ-safe or SMP wait queue.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_IPC_H
#define UMICOM_KERNEL_USER_IPC_H
#include "umicom/kernel/user_scheduler.h"
#include "umicom/kernel/message_service.h"
#include "umicom/kernel/blocking_ipc_abi.h"

typedef struct UmicomKernelUserIpcWait {
    UmicomKernelUserTaskHandle task; /* Generation-tagged: a replacement slot is a different task. */
    UmicomU64 identity;
    UmicomU64 order; /* Registration order, not task creation or execution order. */
    UmicomKernelMessageHandle endpoint;
    UmicomAddress address; /* Revalidated before a deferred receive writes any byte. */
    UmicomAddress nextPc;
    UmicomSize bytes;
    UmicomU64 deadline;
    UmicomBoolean hasDeadline;
    UmicomBoolean sending;
    UmicomBoolean pending;
    UmicomU8 payload[UMICOM_MESSAGE_MAX_BYTES]; /* Only a deferred SEND owns these bytes. */
} UmicomKernelUserIpcWait;
typedef struct UmicomKernelUserIpc {
    const struct UmicomKernelUserIpc *self;
    UmicomKernelUserScheduler *scheduler;
    UmicomKernelMessageDomain messages; /* Namespace private to this scheduler's identities. */
    UmicomKernelUserIpcWait waits[UMICOM_USER_TASK_LIMIT];
    UmicomU64 nextOrder;
    UmicomU64 now;
    UmicomU64 completed;
    UmicomU64 blocked;
    UmicomU64 timedOut;
    UmicomBoolean initialised;
} UmicomKernelUserIpc;
typedef struct UmicomKernelUserIpcInfo {
    UmicomSize waiting;
    UmicomBoolean hasDeadline;
    UmicomU64 nextDeadline;
    UmicomU64 blocked;
    UmicomU64 completed;
    UmicomU64 timedOut;
} UmicomKernelUserIpcInfo;

/* Attach once before any task admission. Both objects and all input/output
 * pointers are trusted Kernel storage, must not overlap and must remain alive. */
UmicomKernelUserScheduleStatus UmicomKernelUserIpcAttach(
    UmicomKernelUserIpc *ipc, UmicomKernelUserScheduler *scheduler);
/* Kernel admission derives identities from task tokens, never user arguments.
 * SetArgument separately supplies each fresh task with its new endpoint token. */
UmicomKernelMessageStatus UmicomKernelUserIpcConnect(UmicomKernelUserIpc *ipc,
    UmicomKernelUserTaskHandle first, UmicomKernelMessageRights firstRights,
    UmicomKernelUserTaskHandle second, UmicomKernelMessageRights secondRights,
    UmicomKernelMessageHandle *outFirst, UmicomKernelMessageHandle *outSecond);
/* RunOne pumps before selection. An idle dispatcher must continue pumping time
 * for finite deadlines; IDLE does not mean all tasks have exited. The snapshot
 * exposes the next deadline but does not itself run or complete an operation. */
UmicomBoolean UmicomKernelUserIpcPump(UmicomKernelUserScheduler *scheduler);
UmicomBoolean UmicomKernelUserIpcSnapshot(UmicomKernelUserIpc *ipc, UmicomKernelUserIpcInfo *outInfo);

/* Internal scheduler/trap hooks. A null scheduler->ipc retains the old path. */
UmicomBoolean UmicomKernelUserIpcBegin(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserIpcEnd(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserIpcPending(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomKernelUserScheduleStatus UmicomKernelUserIpcSuspend(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTask *task, UmicomBoolean expired);
UmicomBoolean UmicomKernelUserIpcStop(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserIpcValidate(UmicomKernelUserScheduler *scheduler);
UmicomBoolean UmicomKernelUserIpcRecognizes(UmicomU64 number);
UmicomU64 UmicomKernelUserIpcDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc);
void UmicomKernelBlockingIpcValidateExecution(void);
#endif /* UMICOM_KERNEL_USER_IPC_H */
