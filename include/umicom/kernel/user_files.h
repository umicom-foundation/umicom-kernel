/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/user_files.h
 *
 * PURPOSE:
 *   Bind a private VFS client to an admitted task and close it before that
 *   task's memory can be reaped, using the existing scheduler and filesystem.
 *
 * EDUCATIONAL OVERVIEW:
 *   The trap path still has the user's satp and quantum installed. It may copy
 *   arguments, but must not weaken the filesystem's ordinary allocation gate.
 *   An accepted request therefore returns to the dispatcher. Only after the
 *   architecture adapter verifies machine restoration may VFS change state.
 *
 *   Metadata is stable, zero-filled Kernel storage. Access is serial on hart
 *   zero, not IRQ-safe or SMP-safe. The VFS mount outlives this attachment.
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_FILES_H
#define UMICOM_KERNEL_USER_FILES_H
#include "umicom/kernel/user_scheduler.h"
#include "umicom/kernel/object_cache.h"
#include "umicom/kernel/file_abi.h"

/* Visible for static allocation and fault injection only. Do not edit live
 * records. The cache owns client storage; its ticket, not a raw pointer, is the
 * authority to resolve/free it. A task token includes its slot generation. */
typedef struct UmicomKernelUserFileRecord {
    UmicomKernelUserTaskHandle task;
    UmicomU64 identity;
    UmicomKernelObjectReference client;
    UmicomBoolean opened;
    UmicomBoolean pending;
    UmicomKernelFileRequest request;
    UmicomAddress reply;
    UmicomAddress nextPc;
    UmicomU8 payload[UMICOM_FILE_IO_LIMIT]; /* Owned WRITE bytes or a copied path. */
} UmicomKernelUserFileRecord;
typedef struct UmicomKernelUserFiles {
    const struct UmicomKernelUserFiles *self;
    UmicomKernelUserScheduler *scheduler;
    UmicomKernelVfs *vfs;
    UmicomKernelVfsClient anchor; /* Pins the mounted lifetime even with no tasks. */
    UmicomKernelObjectCache clients;
    UmicomKernelUserFileRecord records[UMICOM_USER_TASK_LIMIT];
    UmicomKernelVfsLifetime state;
    UmicomU64 completed;
} UmicomKernelUserFiles;

/* Attach before task admission. This does not grant file access to any task.
 * Grant selects a fresh READY task, its trusted principal and a fixed ceiling.
 * No path ACL or credentials are invented: namespace rights cover this mount. */
UmicomKernelVfsStatus UmicomKernelUserFilesAttach(UmicomKernelUserFiles *files,
    UmicomKernelUserScheduler *scheduler, UmicomKernelVfs *vfs);
UmicomKernelVfsStatus UmicomKernelUserFilesGrant(UmicomKernelUserFiles *files,
    UmicomKernelUserTaskHandle task, UmicomKernelVfsRights rights);
/* Close requires every task to have been reaped. Cache-release failures retain
 * a CLOSING attachment for retry; successful close detaches it from the scheduler. */
UmicomKernelVfsStatus UmicomKernelUserFilesClose(UmicomKernelUserFiles *files);
UmicomBoolean UmicomKernelUserFilesValidate(UmicomKernelUserScheduler *scheduler);

/* Scheduler hooks; a null files attachment preserves the established path.
 * Begin/End bind an exact session only for its synchronous user invocation.
 * Complete runs after successful machine restoration, never inside the trap.
 * Stop closes descriptors before Reap releases user memory; failures retain
 * the reference and cannot be bypassed by reusing the task slot. */
UmicomBoolean UmicomKernelUserFilesBegin(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserFilesEnd(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserFilesPending(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomKernelUserScheduleStatus UmicomKernelUserFilesComplete(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTask *task, UmicomBoolean expired);
UmicomBoolean UmicomKernelUserFilesStop(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task);
UmicomBoolean UmicomKernelUserFilesPump(UmicomKernelUserScheduler *scheduler);
UmicomU64 UmicomKernelUserFileDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc);
void UmicomKernelUserFilesValidateExecution(void);
#endif /* UMICOM_KERNEL_USER_FILES_H */
