/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_scheduler.c
 *
 * PURPOSE:
 *   Keep loaded user continuations alive between timer quanta and select them
 *   fairly without restarting their entry points or sharing writable frames.
 *
 * EDUCATIONAL OVERVIEW:
 *   The existing Process owner is reused for loading and teardown. While a
 *   continuation is live we keep that lower owner RUNNING and not quiesced:
 *   its run-once API cannot restart the program and its destructor cannot free
 *   the paused root. READY/PAUSED below describe dispatch eligibility, not the
 *   lower loader's lifetime. Only cancellation or a terminal event ends that
 *   protection, and even then Reap is the explicit memory-release operation.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifdef UMICOM_KERNEL_BLOCKING_IPC
#include "umicom/kernel/user_ipc.h"
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
#include "umicom/kernel/user_files.h"
#endif
#include "umicom/kernel/user_scheduler.h"
#include "umicom/kernel/riscv64/user_slice.h"

static void UmicomUserSchedulerZero(void *destination, UmicomSize bytes)
{
    /* Keep the freestanding owner independent of an implicit libc memset. */
    volatile UmicomU8 *const output = (volatile UmicomU8 *)destination;
    for (UmicomSize index = 0U; index < bytes; ++index) output[index] = 0U;
}

static UmicomKernelUserScheduleStatus UmicomUserSchedulerReady(UmicomKernelUserScheduler *scheduler)
{
    if (scheduler == (UmicomKernelUserScheduler *)0) return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    if (scheduler->initialised == UMICOM_FALSE || scheduler->self != scheduler ||
        scheduler->next >= UMICOM_USER_TASK_LIMIT) return UMICOM_USER_SCHEDULE_BAD_STATE;
    if (scheduler->poisoned != UMICOM_FALSE) return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    return scheduler->active != UMICOM_FALSE ? UMICOM_USER_SCHEDULE_BUSY : UMICOM_USER_SCHEDULE_OK;
}

static UmicomKernelUserTaskHandle UmicomUserTaskToken(const UmicomKernelUserTask *task, UmicomSize slot)
{
    return ((UmicomU64)task->generation << 32U) | (UmicomU64)(slot + 1U);
}

static UmicomKernelUserTask *UmicomUserTaskFind(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle)
{
    /* Bounds and generation are checked before lending out an internal record. */
    const UmicomU32 low = (UmicomU32)handle;
    const UmicomU32 generation = (UmicomU32)(handle >> 32U);
    if (low == 0U || low > UMICOM_USER_TASK_LIMIT || generation == 0U) return (UmicomKernelUserTask *)0;
    UmicomKernelUserTask *const task = &scheduler->tasks[low - 1U];
    if (task->state == UMICOM_USER_TASK_EMPTY || task->state == UMICOM_USER_TASK_RETAINED ||
        task->generation != generation || task->retired != UMICOM_FALSE) return (UmicomKernelUserTask *)0;
    return task;
}

static void UmicomUserTaskForget(UmicomKernelUserTask *task)
{
    /* Preserve only the token generation after all image ownership has ended. */
    const UmicomU32 generation = task->generation;
    UmicomUserSchedulerZero(task, sizeof(*task));
    task->generation = generation;
    task->retired = generation == ~(UmicomU32)0U ? UMICOM_TRUE : UMICOM_FALSE;
}

UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerInitialize(UmicomKernelUserScheduler *scheduler)
{
    if (scheduler == (UmicomKernelUserScheduler *)0) return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    if (scheduler->initialised != UMICOM_FALSE || scheduler->self != (const UmicomKernelUserScheduler *)0)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    /* The caller supplies zero-filled stable storage. Do not reset a live
     * domain: a reset would allow its previously issued handles to reappear. */
    for (UmicomSize index = 0U; index < UMICOM_USER_TASK_LIMIT; ++index) {
        if (scheduler->tasks[index].state != UMICOM_USER_TASK_EMPTY ||
            scheduler->tasks[index].process.state != UMICOM_PROCESS_EMPTY)
            return UMICOM_USER_SCHEDULE_BAD_STATE;
    }
    scheduler->self = scheduler;
    scheduler->nextIdentity = 1U;
    scheduler->initialised = UMICOM_TRUE;
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserTaskCreate(UmicomKernelUserScheduler *scheduler,
    const UmicomU8 *image, UmicomSize bytes, UmicomU64 argument, UmicomU64 sliceLimit,
    UmicomKernelUserTaskHandle *outHandle)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    if (outHandle == (UmicomKernelUserTaskHandle *)0 || image == (const UmicomU8 *)0 ||
        bytes == 0U || sliceLimit == 0U || sliceLimit > UMICOM_USER_TASK_SLICE_LIMIT)
        return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    if (scheduler->nextIdentity == 0U) return UMICOM_USER_SCHEDULE_IDENTITY_EXHAUSTED;
    UmicomSize slot = 0U;
    while (slot < UMICOM_USER_TASK_LIMIT && (scheduler->tasks[slot].state != UMICOM_USER_TASK_EMPTY ||
        scheduler->tasks[slot].retired != UMICOM_FALSE)) ++slot;
    if (slot == UMICOM_USER_TASK_LIMIT) return UMICOM_USER_SCHEDULE_CAPACITY;
    UmicomKernelUserTask *const task = &scheduler->tasks[slot];
    /* Identity consumption is monotonic even if loading later fails. Retained
     * rollback records must never share an identity with a different program. */
    const UmicomU64 identity = scheduler->nextIdentity;
    scheduler->nextIdentity = identity == ~(UmicomU64)0U ? 0U : identity + 1U;
    UmicomKernelExecutableStatus inspected = UMICOM_EXECUTABLE_INVALID_ARGUMENT;
    const UmicomKernelProcessStatus loaded =
        UmicomKernelProcessCreate(&task->process, image, bytes, identity, &inspected);
    if (loaded != UMICOM_PROCESS_OK) {
        if (task->process.state != UMICOM_PROCESS_EMPTY) task->state = UMICOM_USER_TASK_RETAINED;
        return UMICOM_USER_SCHEDULE_LOAD_FAILED; /* Do not publish a successful handle. */
    }
    /* Start exactly once with a clean integer context. The program's own entry
     * supplies its ABI setup; no Kernel pointer is inherited in a user register. */
    UmicomUserSchedulerZero(&task->frame, sizeof(task->frame));
    task->frame.mepc = (UmicomU64)task->process.entry;
    task->frame.x2_sp = (UmicomU64)UMICOM_EXECUTABLE_STACK_TOP;
    task->frame.x10_a0 = argument;
    task->frame.mstatus = (UmicomU64)2U << 32U; /* RV64 UXL, no privilege overrides. */
    task->sliceLimit = sliceLimit;
    task->state = UMICOM_USER_TASK_READY;
    task->process.state = UMICOM_PROCESS_RUNNING; /* Pin the lower owner's live lifetime. */
    task->process.quiesced = UMICOM_FALSE;
    task->process.report.identity = identity;
    ++task->generation; /* Maximum-generation records retire at Reap, never wrap. */
    *outHandle = UmicomUserTaskToken(task, slot);
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerRunOne(UmicomKernelUserScheduler *scheduler,
    UmicomU64 quantumTicks, UmicomKernelUserTaskHandle *outHandle)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    if (outHandle == (UmicomKernelUserTaskHandle *)0 || quantumTicks < UMICOM_USER_QUANTUM_MIN_TICKS ||
        quantumTicks > UMICOM_USER_QUANTUM_MAX_TICKS) return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    if (scheduler->dispatches == ~(UmicomU64)0U) return UMICOM_USER_SCHEDULE_BAD_STATE;
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* Recheck durable queue conditions before selecting runnable work. A
     * pending task spends no dispatches merely because its peer is delayed. */
    if (UmicomKernelUserIpcPump(scheduler) == UMICOM_FALSE)
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
    /* Retry terminal descriptor cleanup before admitting another user quantum. */
    if (!UmicomKernelUserFilesPump(scheduler)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
#endif
    UmicomSize selected = UMICOM_USER_TASK_LIMIT;
    for (UmicomSize offset = 0U; offset < UMICOM_USER_TASK_LIMIT; ++offset) {
        const UmicomSize slot = (scheduler->next + offset) % UMICOM_USER_TASK_LIMIT;
        if (scheduler->tasks[slot].state == UMICOM_USER_TASK_READY ||
            scheduler->tasks[slot].state == UMICOM_USER_TASK_PAUSED) {
            selected = slot;
            break;
        }
    }
    if (selected == UMICOM_USER_TASK_LIMIT) return UMICOM_USER_SCHEDULE_IDLE;
    UmicomKernelUserTask *const task = &scheduler->tasks[selected];
    if (task->slices >= task->sliceLimit || task->process.state != UMICOM_PROCESS_RUNNING)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* Bind this task's identity, never a preceding invocation's identity. */
    if (UmicomKernelUserIpcBegin(scheduler, task) == UMICOM_FALSE)
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED;
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
    if (!UmicomKernelUserFilesBegin(scheduler, task)) {
#ifdef UMICOM_KERNEL_BLOCKING_IPC
        /* IPC admission succeeded above; undo only that temporary binding. */
        if (!UmicomKernelUserIpcEnd(scheduler, task)) scheduler->poisoned = UMICOM_TRUE;
#endif
        return UMICOM_USER_SCHEDULE_ENTRY_REFUSED;
    }
#endif
    const UmicomKernelUserTaskState previousState = task->state;
    scheduler->active = UMICOM_TRUE;
    task->state = UMICOM_USER_TASK_RUNNING;
    UmicomBoolean expired = UMICOM_FALSE;
    const UmicomKernelUserScheduleStatus result = UmicomKernelUserSliceRun(task, quantumTicks, &expired);
    scheduler->active = UMICOM_FALSE;
#ifdef UMICOM_KERNEL_FILE_SERVICES
    /* Unbind even on an architecture error. No VFS access is needed to do so. */
    const UmicomBoolean filesDetached = UmicomKernelUserFilesEnd(scheduler, task);
#endif
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* No service binding may escape an invocation, including refused entry. */
    if (UmicomKernelUserIpcEnd(scheduler, task) == UMICOM_FALSE) {
        scheduler->poisoned = UMICOM_TRUE;
        task->state = UMICOM_USER_TASK_ERROR;
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
    if (!filesDetached) {
        scheduler->poisoned = UMICOM_TRUE;
        task->state = UMICOM_USER_TASK_ERROR;
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
    }
#endif
    if (result != UMICOM_USER_SCHEDULE_OK) {
        task->state = previousState; /* Ordinary refusal spends no execution budget. */
        if (result == UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR) {
            scheduler->poisoned = UMICOM_TRUE;
            task->state = UMICOM_USER_TASK_ERROR;
            task->process.state = UMICOM_PROCESS_MONITOR_ERROR;
            /* quiesced remains false: no unverified root may be reclaimed. */
        } else if (result == UMICOM_USER_SCHEDULE_INVALID_CONTEXT) {
            task->state = UMICOM_USER_TASK_ERROR;
            task->process.state = UMICOM_PROCESS_MONITOR_ERROR;
            task->process.quiesced = UMICOM_TRUE; /* No live hardware root remains. */
#ifdef UMICOM_KERNEL_BLOCKING_IPC
            /* This refused continuation will never use its endpoints again. */
            if (UmicomKernelUserIpcStop(scheduler, task) == UMICOM_FALSE)
                return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
            /* INVALID_CONTEXT was refused before execution. Its verified safe
             * image still must close descriptors before it can be collected. */
            if (!UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
#endif
        }
        return result;
    }
    ++scheduler->dispatches;
    ++task->slices;
    scheduler->next = (selected + 1U) % UMICOM_USER_TASK_LIMIT;
    *outHandle = UmicomUserTaskToken(task, selected);
#ifdef UMICOM_KERNEL_FILE_SERVICES
    /* The original slice adapter restored the timer, root and machine controls.
     * Only now may a captured file request enter the existing VFS/RAMFS. */
    if (UmicomKernelUserFilesPending(scheduler, task))
        return UmicomKernelUserFilesComplete(scheduler, task, expired);
#endif
#ifdef UMICOM_KERNEL_BLOCKING_IPC
#ifdef UMICOM_KERNEL_FILE_SERVICES
    /* A wait admitted on the final slice can terminate inside the IPC helper.
     * Close file pins on that branch too, before a parent observes completion. */
    if (scheduler->files && UmicomKernelUserIpcPending(scheduler, task)) {
        const UmicomKernelUserScheduleStatus suspended = UmicomKernelUserIpcSuspend(scheduler, task, expired);
        if (suspended == UMICOM_USER_SCHEDULE_OK && task->process.quiesced &&
            !UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
        return suspended;
    }
#endif
    /* A waiting ECALL is live work, not an unrecognised terminal stop. */
    if (UmicomKernelUserIpcPending(scheduler, task) != UMICOM_FALSE)
        return UmicomKernelUserIpcSuspend(scheduler, task, expired);
#endif
    if (expired != UMICOM_FALSE) {
        ++task->preemptions;
        if (task->slices < task->sliceLimit) {
            task->state = UMICOM_USER_TASK_PAUSED;
            return UMICOM_USER_SCHEDULE_OK; /* All registers and image pages stay owned. */
        }
        task->state = UMICOM_USER_TASK_EXHAUSTED;
        task->process.state = UMICOM_PROCESS_CLEANUP_REQUIRED;
    } else {
        /* Preserve the original monitor's terminal observations. There is no
         * synthetic page-fault cause for cancellation or quantum exhaustion. */
        switch (task->process.report.stopReason) {
            case UMICOM_USER_STOP_EXIT:
                task->state = UMICOM_USER_TASK_EXITED;
                task->process.state = UMICOM_PROCESS_EXITED;
                break;
            case UMICOM_USER_STOP_FAULT:
                task->state = UMICOM_USER_TASK_FAULTED;
                task->process.state = UMICOM_PROCESS_FAULTED;
                break;
            case UMICOM_USER_STOP_CALL_BUDGET:
                task->state = UMICOM_USER_TASK_EXHAUSTED;
                task->process.state = UMICOM_PROCESS_CALL_LIMIT;
                break;
            default:
                task->state = UMICOM_USER_TASK_ERROR;
                task->process.state = UMICOM_PROCESS_MONITOR_ERROR;
                break;
        }
    }
    task->process.quiesced = UMICOM_TRUE; /* Terminal, but not implicitly destroyed. */
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* Close only this attached domain's references, not a peer's queued data. */
    if (UmicomKernelUserIpcStop(scheduler, task) == UMICOM_FALSE)
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
    if (!UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
#endif
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserTaskQuery(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle, UmicomKernelUserTaskInfo *outInfo)
{
    /* Even a poisoned domain permits value-only diagnostics. No dispatch/free
     * is thereby authorised, and no pointer into a process is returned. */
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK && ready != UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR) return ready;
    if (outInfo == (UmicomKernelUserTaskInfo *)0) return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    const UmicomKernelUserTask *const task = UmicomUserTaskFind(scheduler, handle);
    if (task == (const UmicomKernelUserTask *)0) return UMICOM_USER_SCHEDULE_INVALID_HANDLE;
    outInfo->state = task->state;
    outInfo->identity = task->process.identity;
    outInfo->slices = task->slices;
    outInfo->preemptions = task->preemptions;
    outInfo->systemCalls = task->process.report.callCount;
    outInfo->exitValue = task->process.report.exitValue;
    outInfo->trapCause = task->process.report.trapCause;
    outInfo->resumePc = (UmicomAddress)task->frame.mepc;
    outInfo->savedStack = (UmicomAddress)task->frame.x2_sp;
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserTaskCancel(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    UmicomKernelUserTask *const task = UmicomUserTaskFind(scheduler, handle);
    if (task == (UmicomKernelUserTask *)0) return UMICOM_USER_SCHEDULE_INVALID_HANDLE;
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    if (task->state == UMICOM_USER_TASK_BLOCKED) {
        /* Discard the pending operation before reusing the original cancel
         * transition. No instruction can run in this serial Kernel interval. */
        if (UmicomKernelUserIpcStop(scheduler, task) == UMICOM_FALSE)
            return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
        task->state = UMICOM_USER_TASK_PAUSED;
    }
#endif
    if (task->state != UMICOM_USER_TASK_READY && task->state != UMICOM_USER_TASK_PAUSED)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    task->state = UMICOM_USER_TASK_CANCELLED;
    task->process.state = UMICOM_PROCESS_CLEANUP_REQUIRED; /* No more continuation may run. */
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* READY and timer-paused cancellation also release scoped endpoints. */
    if (UmicomKernelUserIpcStop(scheduler, task) == UMICOM_FALSE)
        return UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR;
#endif
    task->process.quiesced = UMICOM_TRUE;
#ifdef UMICOM_KERNEL_FILE_SERVICES
    /* Cancellation does not run user CLOSE calls. End this client's pins here. */
    if (!UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
#endif
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserTaskReap(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    UmicomKernelUserTask *const task = UmicomUserTaskFind(scheduler, handle);
    if (task == (UmicomKernelUserTask *)0) return UMICOM_USER_SCHEDULE_INVALID_HANDLE;
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    /* A blocked stack is live even though no CPU currently uses it. */
    if (task->state == UMICOM_USER_TASK_BLOCKED) return UMICOM_USER_SCHEDULE_BAD_STATE;
#endif
    if (task->state == UMICOM_USER_TASK_READY || task->state == UMICOM_USER_TASK_PAUSED ||
        task->state == UMICOM_USER_TASK_RUNNING) return UMICOM_USER_SCHEDULE_BAD_STATE;
#ifdef UMICOM_KERNEL_FILE_SERVICES
    /* A failed close cannot be bypassed by freeing/reusing this task's image. */
    if (!UmicomKernelUserFilesStop(scheduler, task)) return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
#endif
    if (UmicomKernelProcessDestroy(&task->process) != UMICOM_PROCESS_OK)
        return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
    UmicomUserTaskForget(task); /* Only now may another image occupy this slot. */
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerReapRetained(UmicomKernelUserScheduler *scheduler,
    UmicomSize *outReaped)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    if (outReaped == (UmicomSize *)0) return UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;
    *outReaped = 0U;
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
        UmicomKernelUserTask *const task = &scheduler->tasks[slot];
        if (task->state != UMICOM_USER_TASK_RETAINED) continue;
        if (UmicomKernelProcessDestroy(&task->process) != UMICOM_PROCESS_OK)
            return UMICOM_USER_SCHEDULE_CLEANUP_FAILED;
        UmicomUserTaskForget(task);
        ++*outReaped;
    }
    return UMICOM_USER_SCHEDULE_OK;
}

UmicomKernelUserScheduleStatus UmicomKernelUserSchedulerValidate(UmicomKernelUserScheduler *scheduler)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    UmicomU64 count = 0U;
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
        const UmicomKernelUserTask *const task = &scheduler->tasks[slot];
        if (task->state == UMICOM_USER_TASK_EMPTY) {
            if (task->process.state != UMICOM_PROCESS_EMPTY || task->process.pageCount != 0U ||
                (task->retired != UMICOM_FALSE && task->generation != ~(UmicomU32)0U))
                return UMICOM_USER_SCHEDULE_BAD_STATE;
            continue;
        }
        if (task->state == UMICOM_USER_TASK_RETAINED) {
            if (task->process.state != UMICOM_PROCESS_CLEANUP_REQUIRED) return UMICOM_USER_SCHEDULE_BAD_STATE;
            continue;
        }
        if (task->state == UMICOM_USER_TASK_RUNNING || task->generation == 0U || task->retired != UMICOM_FALSE ||
            task->process.identity == 0U || task->sliceLimit == 0U || task->sliceLimit > UMICOM_USER_TASK_SLICE_LIMIT ||
            task->slices > task->sliceLimit || task->preemptions > task->slices)
            return UMICOM_USER_SCHEDULE_BAD_STATE;
        const UmicomBoolean runnable = task->state == UMICOM_USER_TASK_READY || task->state == UMICOM_USER_TASK_PAUSED
            ? UMICOM_TRUE : UMICOM_FALSE;
        if (runnable != UMICOM_FALSE && (task->process.state != UMICOM_PROCESS_RUNNING ||
            task->process.quiesced != UMICOM_FALSE || task->slices == task->sliceLimit))
            return UMICOM_USER_SCHEDULE_BAD_STATE;
        if (task->process.report.memory.space != &task->process.space ||
            task->process.report.memory.pages != task->process.pages ||
            task->process.report.memory.pageCount != task->process.pageCount ||
            UmicomKernelVirtualMemoryValidate(&task->process.space) != UMICOM_KERNEL_VIRTUAL_MEMORY_OK)
            return UMICOM_USER_SCHEDULE_BAD_STATE;
        count += task->slices; /* At most four bounded slice counts can contribute. */
    }
#ifdef UMICOM_KERNEL_BLOCKING_IPC
    if (UmicomKernelUserIpcValidate(scheduler) == UMICOM_FALSE)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
#endif
#ifdef UMICOM_KERNEL_FILE_SERVICES
    if (!UmicomKernelUserFilesValidate(scheduler)) return UMICOM_USER_SCHEDULE_BAD_STATE;
#endif
    return count <= scheduler->dispatches ? UMICOM_USER_SCHEDULE_OK : UMICOM_USER_SCHEDULE_BAD_STATE;
}

const char *UmicomKernelUserTaskStateName(UmicomKernelUserTaskState state)
{
    switch (state) {
        case UMICOM_USER_TASK_BLOCKED: return "blocked";
        case UMICOM_USER_TASK_EMPTY: return "empty";
        case UMICOM_USER_TASK_READY: return "ready";
        case UMICOM_USER_TASK_RUNNING: return "running";
        case UMICOM_USER_TASK_PAUSED: return "paused";
        case UMICOM_USER_TASK_EXITED: return "exited";
        case UMICOM_USER_TASK_FAULTED: return "faulted";
        case UMICOM_USER_TASK_EXHAUSTED: return "exhausted";
        case UMICOM_USER_TASK_CANCELLED: return "cancelled";
        case UMICOM_USER_TASK_ERROR: return "error";
        case UMICOM_USER_TASK_RETAINED: return "retained";
        default: return "invalid-state";
    }
}

UmicomKernelUserScheduleStatus UmicomKernelUserTaskSetArgument(UmicomKernelUserScheduler *scheduler,
    UmicomKernelUserTaskHandle handle, UmicomU64 argument)
{
    const UmicomKernelUserScheduleStatus ready = UmicomUserSchedulerReady(scheduler);
    if (ready != UMICOM_USER_SCHEDULE_OK) return ready;
    UmicomKernelUserTask *const task = UmicomUserTaskFind(scheduler, handle);
    if (task == (UmicomKernelUserTask *)0) return UMICOM_USER_SCHEDULE_INVALID_HANDLE;
    /* Admission may not overwrite a retained syscall result or paused register. */
    if (task->state != UMICOM_USER_TASK_READY || task->slices != 0U)
        return UMICOM_USER_SCHEDULE_BAD_STATE;
    task->frame.x10_a0 = argument;
    return UMICOM_USER_SCHEDULE_OK;
}
