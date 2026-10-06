/*-----------------------------------------------------------------------------
 * Umicom Kernel process-owned file clients
 * File: kernel/user_files.c
 *
 * A descriptor belongs to a client, and that client belongs to one task lifetime.
 * The scheduler token and principal must both match. Reusing a task slot cannot
 * silently inherit the previous program's file positions or access rights.
 *
 * Clients are objects from the existing cache, not another allocator. Closing
 * every descriptor ends the VFS client lifetime; only then is its allocation
 * returned to the cache. Reap remains blocked when that cleanup cannot finish.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "user_files_internal.h"
#ifdef UMICOM_KERNEL_BLOCKING_IPC
#include "umicom/kernel/user_ipc.h"
#endif

UmicomKernelUserFiles *umicomFileBoundOwner;
UmicomKernelUserTask *umicomFileBoundTask;

void UmicomFileClear(void *target, UmicomSize bytes)
{
    /* Make scrubbing observable even when the allocation is about to be freed. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static UmicomBoolean UmicomFileZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *in = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (in[i] != 0U) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomKernelUserTaskHandle UmicomFileTaskToken(const UmicomKernelUserTask *task, UmicomSize slot)
{
    return ((UmicomU64)task->generation << 32U) | (slot + 1U);
}
UmicomKernelUserFileRecord *UmicomFileRecord(UmicomKernelUserFiles *files, UmicomKernelUserTask *task)
{
    /* Compare pointers before using an index. The task must be storage owned
     * by this scheduler, not a convincing-looking copy of a task structure. */
    if (!files || files->self != files || !files->scheduler) return (UmicomKernelUserFileRecord *)0;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
        if (&files->scheduler->tasks[i] == task) return &files->records[i];
    }
    return (UmicomKernelUserFileRecord *)0;
}
UmicomKernelVfsClient *UmicomFileClient(UmicomKernelUserFiles *files, UmicomKernelUserFileRecord *record)
{
    void *object = (void *)0;
    if (!record || record->client.address == (void *)0 ||
        UmicomKernelObjectCacheResolve(&files->clients, record->client, &object) != UMICOM_OBJECT_OK)
        return (UmicomKernelVfsClient *)0;
    return (UmicomKernelVfsClient *)object;
}
static UmicomBoolean UmicomFileOwnerReady(UmicomKernelUserFiles *files)
{
    return files && files->self == files && files->state == UMICOM_VFS_OPEN && files->scheduler &&
        files->scheduler->self == files->scheduler && files->scheduler->files == files &&
        !files->scheduler->active && !files->scheduler->poisoned &&
        UmicomKernelObjectCacheAccessAllowed() ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelVfsStatus UmicomKernelUserFilesAttach(UmicomKernelUserFiles *files,
    UmicomKernelUserScheduler *scheduler, UmicomKernelVfs *vfs)
{
    if (!files || !scheduler || !vfs) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_VFS_UNSAFE_CONTEXT;
    if (!UmicomFileZero(files, sizeof(*files)) || scheduler->self != scheduler ||
        !scheduler->initialised || scheduler->active || scheduler->poisoned || scheduler->files)
        return UMICOM_VFS_BAD_STATE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i)
        if (scheduler->tasks[i].state != UMICOM_USER_TASK_EMPTY) return UMICOM_VFS_BUSY;
    /* Four client records fit within four independent pages. No assumption is
     * made about their adjacency, and the cache allocates only on Grant. */
    if (UmicomKernelObjectCacheInitialize(&files->clients, sizeof(UmicomKernelVfsClient),
        16U, UMICOM_USER_TASK_LIMIT) != UMICOM_OBJECT_OK) return UMICOM_VFS_BAD_STATE;
    const UmicomKernelVfsStatus status = UmicomKernelVfsClientOpen(&files->anchor, vfs,
        ~(UmicomU64)0U, 0U);
    if (status != UMICOM_VFS_OK) {
        (void)UmicomKernelObjectCacheClose(&files->clients); /* No frame has been acquired. */
        files->self = files;
        files->state = UMICOM_VFS_CLOSED;
        return status;
    }
    /* Publish only after the mount is pinned. Even the last task exiting does
     * not permit an unrelated caller to unmount a still-attached service. */
    files->self = files;
    files->scheduler = scheduler;
    files->vfs = vfs;
    files->state = UMICOM_VFS_OPEN;
    scheduler->files = files;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelUserFilesGrant(UmicomKernelUserFiles *files,
    UmicomKernelUserTaskHandle token, UmicomKernelVfsRights rights)
{
    if (!UmicomFileOwnerReady(files)) return UMICOM_VFS_BAD_STATE;
    if ((rights & ~UMICOM_VFS_RIGHT_ALL) != 0U) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelUserTaskInfo info;
    UmicomFileClear(&info, sizeof(info));
    if (UmicomKernelUserTaskQuery(files->scheduler, token, &info) != UMICOM_USER_SCHEDULE_OK)
        return UMICOM_VFS_INVALID_ARGUMENT;
    if (info.state != UMICOM_USER_TASK_READY || info.slices != 0U) return UMICOM_VFS_BAD_STATE;
    const UmicomSize slot = (UmicomU32)token - 1U; /* Query checked the slot and generation. */
    UmicomKernelUserFileRecord *record = &files->records[slot];
    if (record->task != 0U) return UMICOM_VFS_BUSY;
    UmicomKernelObjectReference reference = {0};
    if (UmicomKernelObjectCacheAllocate(&files->clients, &reference) != UMICOM_OBJECT_OK)
        return UMICOM_VFS_NO_MEMORY;
    record->task = token;
    record->identity = info.identity;
    record->client = reference;
    UmicomKernelVfsClient *client = UmicomFileClient(files, record);
    if (!client) return UMICOM_VFS_CORRUPT_STATE; /* Retain ownership for diagnosis. */
    const UmicomKernelVfsStatus status = UmicomKernelVfsClientOpen(client, files->vfs, info.identity, rights);
    if (status != UMICOM_VFS_OK) {
        /* No descriptor was published. A failed rollback still retains its
         * allocation reference so cancelling this task can retry cleanup. */
        if (UmicomKernelObjectCacheFree(&files->clients, reference) == UMICOM_OBJECT_OK)
            UmicomFileClear(record, sizeof(*record));
        return status;
    }
    record->opened = UMICOM_TRUE;
    return UMICOM_VFS_OK;
}
UmicomBoolean UmicomKernelUserFilesBegin(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->files) return UMICOM_TRUE;
    UmicomKernelUserFiles *files = scheduler->files;
    if (!UmicomFileOwnerReady(files) || umicomFileBoundOwner || umicomFileBoundTask) return UMICOM_FALSE;
    UmicomKernelUserFileRecord *record = UmicomFileRecord(files, task);
    if (!record || record->pending) return UMICOM_FALSE;
    if (record->task != 0U) {
        const UmicomSize slot = (UmicomSize)(record - files->records);
        UmicomKernelVfsClient *client = UmicomFileClient(files, record);
        if (record->task != UmicomFileTaskToken(task, slot) || record->identity != task->process.identity ||
            !record->opened || !client || client->principal != record->identity ||
            client->vfs != files->vfs || UmicomKernelVfsClientValidate(client) != UMICOM_VFS_OK)
            return UMICOM_FALSE;
    }
    /* A task without a grant still runs, but FILE returns SERVICE_UNBOUND.
     * Binding an exact task pointer prevents an equal numeric identity in a
     * different scheduler from selecting this client's authority. */
    umicomFileBoundOwner = files;
    umicomFileBoundTask = task;
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserFilesEnd(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->files) return UMICOM_TRUE;
    if (umicomFileBoundOwner != scheduler->files || umicomFileBoundTask != task) return UMICOM_FALSE;
    /* Do not inspect VFS here: restoration can have failed. Removing this
     * binding is safe bookkeeping even when no memory may be reclaimed. */
    umicomFileBoundOwner = (UmicomKernelUserFiles *)0;
    umicomFileBoundTask = (UmicomKernelUserTask *)0;
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserFilesPending(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->files) return UMICOM_FALSE;
    UmicomKernelUserFileRecord *record = UmicomFileRecord(scheduler->files, task);
    return record && record->pending ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomBoolean UmicomKernelUserFilesStop(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->files) return UMICOM_TRUE;
    UmicomKernelUserFiles *files = scheduler->files;
    UmicomKernelUserFileRecord *record = UmicomFileRecord(files, task);
    if (!record) return UMICOM_FALSE;
    if (record->task == 0U) return UMICOM_TRUE;
    const UmicomSize slot = (UmicomSize)(record - files->records);
    if (record->task != UmicomFileTaskToken(task, slot) || record->identity != task->process.identity ||
        !task->process.quiesced || task->state == UMICOM_USER_TASK_READY ||
        task->state == UMICOM_USER_TASK_PAUSED || task->state == UMICOM_USER_TASK_RUNNING ||
        task->state == UMICOM_USER_TASK_BLOCKED || scheduler->active ||
        umicomFileBoundOwner == files) return UMICOM_FALSE;
    /* Pending requests contain copied bytes/address values. Once execution has
     * stopped, discard them before releasing the client's descriptor pins. */
    record->pending = UMICOM_FALSE;
    UmicomFileClear(record->payload, sizeof(record->payload));
    UmicomKernelVfsClient *client = UmicomFileClient(files, record);
    if (!client) return UMICOM_FALSE;
    if (record->opened) {
        UmicomSize closed = 0U;
        if (UmicomKernelVfsClientClose(client, &closed) != UMICOM_VFS_OK) return UMICOM_FALSE;
        record->opened = UMICOM_FALSE; /* A later cache failure must not close twice. */
    }
    if (UmicomKernelObjectCacheFree(&files->clients, record->client) != UMICOM_OBJECT_OK)
        return UMICOM_FALSE;
    UmicomFileClear(record, sizeof(*record));
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelUserFilesPump(UmicomKernelUserScheduler *scheduler)
{
    if (!scheduler || !scheduler->files) return scheduler ? UMICOM_TRUE : UMICOM_FALSE;
    if (!UmicomFileOwnerReady(scheduler->files)) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
        UmicomKernelUserTask *task = &scheduler->tasks[i];
        if (scheduler->files->records[i].task == 0U) continue;
        if (task->state == UMICOM_USER_TASK_EMPTY) return UMICOM_FALSE;
        if (task->state != UMICOM_USER_TASK_READY && task->state != UMICOM_USER_TASK_RUNNING &&
            task->state != UMICOM_USER_TASK_PAUSED && task->state != UMICOM_USER_TASK_BLOCKED &&
            !UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
UmicomKernelUserScheduleStatus UmicomKernelUserFilesComplete(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTask *task, UmicomBoolean expired)
{
    UmicomKernelUserFiles *files = scheduler->files;
    UmicomKernelUserFileRecord *record = UmicomFileRecord(files, task);
    /* No allocation or filesystem call is permitted before this gate. The
     * original slice adapter has already checked the complete machine return. */
    if (!UmicomFileOwnerReady(files) || !record || !record->pending || expired ||
        task->frame.mcause != 8U || task->frame.mepc != record->nextPc) {
        scheduler->poisoned = UMICOM_TRUE;
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
    UmicomKernelVfsClient *client = UmicomFileClient(files, record);
    if (!client) {
        scheduler->poisoned = UMICOM_TRUE;
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
    const UmicomU64 status = UmicomFileExecute(record, client, &task->process.report.memory);
    task->frame.x10_a0 = status; /* All other retained registers remain the user's values. */
    if (status != UMICOM_VFS_OK) ++task->process.report.rejectedCalls;
    record->pending = UMICOM_FALSE;
    UmicomFileClear(record->payload, sizeof(record->payload));
    if (files->completed != ~(UmicomU64)0U) ++files->completed;
    task->state = UMICOM_USER_TASK_PAUSED;
    if (task->slices == task->sliceLimit) {
        /* The accepted operation completed, but no further user instructions
         * are admitted after the lifetime's dispatch budget has been spent. */
        task->state = UMICOM_USER_TASK_EXHAUSTED;
        task->process.state = UMICOM_PROCESS_CLEANUP_REQUIRED;
        task->process.quiesced = UMICOM_TRUE;
#ifdef UMICOM_KERNEL_BLOCKING_IPC
        if (!UmicomKernelUserIpcStop(scheduler, task)) return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
#endif
        if (!UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
    }
    return UMICOM_USER_SCHEDULE_OK;
}
UmicomBoolean UmicomKernelUserFilesValidate(UmicomKernelUserScheduler *scheduler)
{
    if (!scheduler->files) return UMICOM_TRUE;
    UmicomKernelUserFiles *files = scheduler->files;
    if (!UmicomFileOwnerReady(files) || UmicomKernelVfsClientValidate(&files->anchor) != UMICOM_VFS_OK ||
        UmicomKernelObjectCacheValidate(&files->clients) != UMICOM_OBJECT_OK) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
        UmicomKernelUserFileRecord *record = &files->records[i];
        if (!record->task) {
            if (!UmicomFileZero(record, sizeof(*record))) return UMICOM_FALSE;
            continue;
        }
        UmicomKernelUserTask *task = &scheduler->tasks[i];
        UmicomKernelVfsClient *client = UmicomFileClient(files, record);
        if (record->task != UmicomFileTaskToken(task, i) || record->identity != task->process.identity ||
            task->state == UMICOM_USER_TASK_EMPTY || !client || record->pending) return UMICOM_FALSE;
        if (record->opened && (client->principal != record->identity || client->vfs != files->vfs ||
            UmicomKernelVfsClientValidate(client) != UMICOM_VFS_OK)) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
UmicomKernelVfsStatus UmicomKernelUserFilesClose(UmicomKernelUserFiles *files)
{
    if (!files || files->self != files || !files->scheduler || files->scheduler->files != files ||
        (files->state != UMICOM_VFS_OPEN && files->state != UMICOM_VFS_CLOSING)) return UMICOM_VFS_BAD_STATE;
    if (files->scheduler->active || files->scheduler->poisoned || umicomFileBoundOwner == files)
        return UMICOM_VFS_BUSY;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i)
        if (files->scheduler->tasks[i].state != UMICOM_USER_TASK_EMPTY || files->records[i].task != 0U)
            return UMICOM_VFS_BUSY;
    files->state = UMICOM_VFS_CLOSING; /* Keep admission closed across release retries. */
    if (files->clients.state != UMICOM_OBJECT_CACHE_CLOSED &&
        UmicomKernelObjectCacheClose(&files->clients) != UMICOM_OBJECT_OK) return UMICOM_VFS_RELEASE_FAILED;
    UmicomSize closed = 0U;
    if (files->anchor.state != UMICOM_VFS_CLOSED &&
        UmicomKernelVfsClientClose(&files->anchor, &closed) != UMICOM_VFS_OK) return UMICOM_VFS_RELEASE_FAILED;
    files->scheduler->files = (UmicomKernelUserFiles *)0;
    files->state = UMICOM_VFS_CLOSED;
    return UMICOM_VFS_OK;
}
