/*-----------------------------------------------------------------------------
 * Umicom Kernel live-service readiness, health and replacement policy
 * File: kernel/service_manager.c
 *
 * A healthy instance is live evidence, not a permanent success flag. Losing a
 * prerequisite revokes its consumers before another quantum is selected. Old
 * instances are collected before replacements are admitted, so two generations
 * never accidentally serve the same managed identity at once.
 *
 * The existing supervisor, scheduler, stream owner and loader retain all their
 * mechanisms. This controller supplies policy and a small reporting boundary.
 * It does not renew execution budgets or parse stdout as a readiness signal.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/service_manager.h"
#include "umicom/kernel/object_cache.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/user_slice.h"

#define UMICOM_MANAGED_LAUNCH_BASE ((UmicomAddress)0x10000U)
/* Single-hart dynamic binding, present only while this controller calls the
 * existing supervisor. A mere matching identity is never a substitute for the
 * exact task report pointer and current running state. */
static UmicomKernelServiceManager *umicomReportingManager;

static void UmicomManagedClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static void UmicomManagedCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    const volatile UmicomU8 *in = (const volatile UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
static UmicomBoolean UmicomManagedZero(const void *p, UmicomSize bytes)
{
    const UmicomU8 *in = (const UmicomU8 *)p;
    for (UmicomSize i = 0U; i < bytes; ++i) if (in[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomSize UmicomManagedName(const char *name)
{
    if (!name) return 0U;
    for (UmicomSize i = 0U; i < UMICOM_MANAGED_SERVICE_NAME_BYTES; ++i) {
        const char c = name[i];
        if (!c) return i;
        /* Labels can reach a terminal: accept identifiers, not control bytes. */
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return 0U;
    }
    return 0U;
}
static UmicomBoolean UmicomManagedEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < UMICOM_MANAGED_SERVICE_NAME_BYTES; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomKernelServiceManagerStatus UmicomManagedUnsafe(UmicomKernelServiceManager *m)
{
    m->phase = UMICOM_MANAGER_UNSAFE;
    return UMICOM_MANAGER_STATE_UNSAFE;
}
static UmicomKernelServiceManagerStatus UmicomManagedGate(UmicomKernelServiceManager *m)
{
    if (!m || m->self != m || !m->initialised) return UMICOM_MANAGER_BAD_STATE;
    if (m->active) return UMICOM_MANAGER_BUSY;
    if (m->phase == UMICOM_MANAGER_UNSAFE) return UMICOM_MANAGER_STATE_UNSAFE;
    if (!m->count || m->count > UMICOM_MANAGED_SERVICE_LIMIT || m->supervisor.scheduler.services != m)
        return UmicomManagedUnsafe(m);
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_MANAGER_ENTRY_REFUSED;
    return UMICOM_MANAGER_OK;
}
static UmicomKernelServiceManagerStatus UmicomManagedClock(UmicomKernelServiceManager *m)
{
    const UmicomU64 now = UmicomPlatformTimerRead();
    if (now < m->now) {
        m->phase = UMICOM_MANAGER_UNSAFE;
        return UMICOM_MANAGER_CLOCK_REVERSED;
    }
    m->now = now;
    return UMICOM_MANAGER_OK;
}
static UmicomBoolean UmicomManagedTaskTerminal(UmicomKernelUserTaskState state)
{
    return state == UMICOM_USER_TASK_EXITED || state == UMICOM_USER_TASK_FAULTED ||
        state == UMICOM_USER_TASK_EXHAUSTED || state == UMICOM_USER_TASK_CANCELLED ||
        state == UMICOM_USER_TASK_ERROR ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelManagedServiceRecord *UmicomManagedRecord(
    UmicomKernelServiceManager *m, const UmicomKernelUserTask *task)
{
    if (!m || m->self != m || !m->initialised || m->count > UMICOM_MANAGED_SERVICE_LIMIT) return 0;
    for (UmicomSize i = 0U; i < m->count; ++i) {
        UmicomKernelManagedServiceRecord *r = &m->records[i];
        const UmicomU32 slot = (UmicomU32)r->handle;
        if (slot && slot <= UMICOM_USER_TASK_LIMIT &&
            task == &m->supervisor.scheduler.tasks[slot - 1U] &&
            task->generation == (UmicomU32)(r->handle >> 32U) &&
            task->process.identity == r->info.identity) return r;
    }
    return 0;
}

UmicomKernelServiceManagerStatus UmicomKernelServiceManagerInitialize(
    UmicomKernelServiceManager *m, const UmicomKernelManagedServiceSpec *specs,
    UmicomSize count, UmicomKernelManagedServiceOutput output, void *context)
{
    if (!m || !specs || !count || count > UMICOM_MANAGED_SERVICE_LIMIT) return UMICOM_MANAGER_INVALID_ARGUMENT;
    if (!UmicomManagedZero(m, sizeof(*m))) return UMICOM_MANAGER_BAD_STATE;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_MANAGER_ENTRY_REFUSED;
    const UmicomU32 all = ((UmicomU32)1U << (unsigned)count) - 1U;
    /* Complete validation precedes publishing the owner or allocating frames. */
    for (UmicomSize i = 0U; i < count; ++i) {
        const UmicomKernelManagedServiceSpec *s = &specs[i];
        if (!UmicomManagedName(s->name) || !s->image || !s->imageBytes ||
            s->imageBytes > UMICOM_EXECUTABLE_MAX_FILE_BYTES || !s->attempts ||
            s->attempts > UMICOM_MANAGED_SERVICE_ATTEMPTS || !s->startupTicks ||
            s->startupTicks > UMICOM_MANAGED_SERVICE_TIME_LIMIT || !s->healthTicks ||
            s->healthTicks > UMICOM_MANAGED_SERVICE_TIME_LIMIT || !s->reportTicks ||
            s->reportTicks >= s->healthTicks || s->retryTicks > UMICOM_MANAGED_SERVICE_TIME_LIMIT ||
            !s->sliceLimit || s->sliceLimit > UMICOM_USER_TASK_SLICE_LIMIT ||
            (s->dependencies & ~all) || (s->dependencies & ((UmicomU32)1U << (unsigned)i)) ||
            (s->required != UMICOM_FALSE && s->required != UMICOM_TRUE)) return UMICOM_MANAGER_INVALID_PLAN;
        for (UmicomSize j = 0U; j < i; ++j)
            if (UmicomManagedEqual(s->name, specs[j].name)) return UMICOM_MANAGER_INVALID_PLAN;
        UmicomKernelLaunchImage packed;
        if (UmicomKernelProgramLaunchPack(&s->launch, UMICOM_MANAGED_LAUNCH_BASE, &packed) != UMICOM_LAUNCH_OK)
            return UMICOM_MANAGER_INVALID_PLAN;
    }
    UmicomSize order[UMICOM_MANAGED_SERVICE_LIMIT] = {0};
    UmicomSize ordered = 0U;
    UmicomU32 seen = 0U;
    for (UmicomSize pass = 0U; pass < count; ++pass)
        for (UmicomSize i = 0U; i < count; ++i) {
            const UmicomU32 bit = (UmicomU32)1U << (unsigned)i;
            if (!(seen & bit) && !(specs[i].dependencies & ~seen)) {
                seen |= bit; order[ordered++] = i;
            }
        }
    if (seen != all) return UMICOM_MANAGER_INVALID_PLAN; /* Includes disconnected cycles. */
    m->self = m; m->initialised = UMICOM_TRUE; m->count = count;
    m->output = output; m->outputContext = context; m->now = UmicomPlatformTimerRead();
    for (UmicomSize i = 0U; i < count; ++i) {
        UmicomKernelManagedServiceRecord *r = &m->records[i];
        m->order[i] = order[i];
        UmicomManagedCopy(&r->spec, &specs[i], sizeof(r->spec));
        const UmicomSize length = UmicomManagedName(specs[i].name);
        UmicomManagedCopy(r->info.name, specs[i].name, length);
        r->info.required = specs[i].required; r->spec.name = r->info.name;
        if (UmicomKernelProgramLaunchPack(&specs[i].launch, UMICOM_MANAGED_LAUNCH_BASE, &r->launch) != UMICOM_LAUNCH_OK)
            return UmicomManagedUnsafe(m);
        /* Input arrays need not outlive initialization. Only the copied, offset-
         * based launch image is used to prepare a real task later. */
        r->spec.launch.arguments = 0; r->spec.launch.environment = 0;
    }
    if (UmicomKernelProcessSupervisorInitialize(&m->supervisor) != UMICOM_SUPERVISION_OK ||
        UmicomKernelUserStreamsAttach(&m->streams, &m->supervisor.scheduler) != UMICOM_STREAM_OK)
        return UmicomManagedUnsafe(m);
    m->supervisor.scheduler.services = m;
    return UMICOM_MANAGER_OK;
}
static UmicomBoolean UmicomManagedLaunch(UmicomKernelManagedServiceRecord *r,
    UmicomKernelLaunchString *args, UmicomKernelLaunchString *env, UmicomKernelProgramLaunchSpec *spec)
{
    /* Rebase the canonical packer's detached addresses; no second quoting or
     * environment grammar is introduced by this manager. */
    const UmicomU64 base = UMICOM_MANAGED_LAUNCH_BASE + __builtin_offsetof(UmicomKernelLaunchImage, text);
    if (!r->launch.info.argumentCount || r->launch.info.argumentCount > UMICOM_LAUNCH_ARGUMENT_LIMIT ||
        r->launch.info.environmentCount > UMICOM_LAUNCH_ENVIRONMENT_LIMIT) return UMICOM_FALSE;
    for (unsigned list = 0U; list < 2U; ++list) {
        const UmicomSize n = list ? r->launch.info.environmentCount : r->launch.info.argumentCount;
        const UmicomU64 *addresses = list ? r->launch.environment : r->launch.arguments;
        UmicomKernelLaunchString *out = list ? env : args;
        for (UmicomSize i = 0U; i < n; ++i) {
            if (addresses[i] < base || addresses[i] - base >= sizeof(r->launch.text)) return UMICOM_FALSE;
            const UmicomSize offset = addresses[i] - base;
            UmicomSize bytes = 0U;
            while (offset + bytes < sizeof(r->launch.text) && r->launch.text[offset + bytes]) ++bytes;
            if (offset + bytes == sizeof(r->launch.text)) return UMICOM_FALSE;
            out[i].data = (const char *)&r->launch.text[offset]; out[i].bytes = bytes;
        }
    }
    spec->arguments = args; spec->argumentCount = r->launch.info.argumentCount;
    spec->environment = env; spec->environmentCount = r->launch.info.environmentCount;
    return UMICOM_TRUE;
}
static void UmicomManagedStop(UmicomKernelManagedServiceRecord *r, UmicomKernelManagedServiceReason reason)
{
    /* Preserve the first observed reason across cleanup retries. Revoking this
     * state also revokes dependency readiness before any new quantum can run. */
    if (r->info.state != UMICOM_MANAGED_STOPPING) {
        r->info.reason = reason; r->info.state = UMICOM_MANAGED_STOPPING;
    }
    r->checkpointPending = UMICOM_FALSE;
}
static UmicomBoolean UmicomManagedDependencies(UmicomKernelServiceManager *m, UmicomSize index)
{
    for (UmicomSize j = 0U; j < m->count; ++j)
        if ((m->records[index].spec.dependencies & ((UmicomU32)1U << (unsigned)j)) &&
            m->records[j].info.state != UMICOM_MANAGED_HEALTHY) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static void UmicomManagedPropagate(UmicomKernelServiceManager *m)
{
    /* Revoke consumers transitively, regardless of plan array order. No user
     * instruction runs while this bounded fixed point is being established. */
    for (UmicomSize pass = 0U; pass < m->count; ++pass)
        for (UmicomSize i = 0U; i < m->count; ++i) {
            UmicomKernelManagedServiceRecord *r = &m->records[i];
            if ((r->info.state == UMICOM_MANAGED_HEALTHY || r->info.state == UMICOM_MANAGED_STARTING) &&
                !UmicomManagedDependencies(m, i)) UmicomManagedStop(r, UMICOM_MANAGED_DEPENDENCY_LOST);
            if (r->info.state == UMICOM_MANAGED_WAITING || r->info.state == UMICOM_MANAGED_BACKOFF) {
                for (UmicomSize j = 0U; j < m->count; ++j)
                    if ((r->spec.dependencies & ((UmicomU32)1U << (unsigned)j)) &&
                        m->records[j].info.state == UMICOM_MANAGED_FAILED) {
                        r->info.state = UMICOM_MANAGED_FAILED; r->info.reason = UMICOM_MANAGED_DEPENDENCY_LOST;
                    }
            }
        }
    for (UmicomSize i = 0U; i < m->count; ++i)
        if (m->records[i].info.required && m->records[i].info.state == UMICOM_MANAGED_FAILED) m->recovery = UMICOM_TRUE;
    if (m->stopping || m->recovery)
        for (UmicomSize i = 0U; i < m->count; ++i) {
            UmicomKernelManagedServiceRecord *r = &m->records[i];
            if (r->handle) UmicomManagedStop(r, UMICOM_MANAGED_SHUTDOWN);
            else if (r->info.state != UMICOM_MANAGED_FAILED) r->info.state = UMICOM_MANAGED_STOPPED;
        }
}
static UmicomKernelServiceManagerStatus UmicomManagedObserve(UmicomKernelServiceManager *m)
{
    for (UmicomSize i = 0U; i < m->count; ++i) {
        UmicomKernelManagedServiceRecord *r = &m->records[i];
        if (!r->handle) continue;
        UmicomKernelSupervisedProcessInfo info;
        UmicomManagedClear(&info, sizeof(info));
        if (UmicomKernelProcessSupervisorQuery(&m->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &info)
                != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
        if (UmicomManagedTaskTerminal(info.state)) {
            if (!r->terminalObserved) {
                r->terminalObserved = UMICOM_TRUE;
                const UmicomKernelManagedServiceReason reason = info.state == UMICOM_USER_TASK_FAULTED
                    ? UMICOM_MANAGED_FAULTED : (info.state == UMICOM_USER_TASK_EXHAUSTED
                    ? UMICOM_MANAGED_BUDGET : UMICOM_MANAGED_EXITED);
                UmicomManagedStop(r, reason);
            }
        } else if ((r->info.state == UMICOM_MANAGED_STARTING || r->info.state == UMICOM_MANAGED_HEALTHY) &&
            m->now >= r->info.deadline) {
            UmicomManagedStop(r, r->info.state == UMICOM_MANAGED_STARTING
                ? UMICOM_MANAGED_NOT_READY : UMICOM_MANAGED_HEALTH_EXPIRED);
        }
    }
    UmicomManagedPropagate(m);
    return UMICOM_MANAGER_OK;
}
static UmicomKernelServiceManagerStatus UmicomManagedDrain(UmicomKernelServiceManager *m,
    UmicomKernelManagedServiceRecord *r)
{
    if (!r->streamsGranted) return UMICOM_MANAGER_OK;
    /* At most the existing bounded record queue can be present. Output callbacks
     * are synchronous trusted consumers, never called inside a user trap. */
    for (UmicomSize n = 0U; n < UMICOM_STREAM_OUTPUT_RECORDS; ++n) {
        UmicomKernelStreamPacket packet;
        UmicomManagedClear(&packet, sizeof(packet));
        const UmicomKernelStreamStatus status = UmicomKernelUserStreamsDrain(&m->streams, r->handle, &packet);
        if (status == UMICOM_STREAM_WOULD_BLOCK || status == UMICOM_STREAM_EOF) return UMICOM_MANAGER_OK;
        if (status != UMICOM_STREAM_OK) return UmicomManagedUnsafe(m);
        if (m->output) m->output(m->outputContext, r->info.name, &packet);
    }
    return UMICOM_MANAGER_OK;
}
static UmicomKernelServiceManagerStatus UmicomManagedCleanup(UmicomKernelServiceManager *m)
{
    if (m->retainedLoad) {
        UmicomSize count = 0U;
        const UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorReapRetained(&m->supervisor, &count);
        if (status == UMICOM_SUPERVISION_CLEANUP_FAILED) return UMICOM_MANAGER_CLEANUP_FAILED;
        if (status != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
        m->retainedLoad = UMICOM_FALSE;
    }
    /* Reverse dependency order releases consumers first. A failed release stops
     * admission of every replacement until this entire cleanup fence clears. */
    for (UmicomSize n = m->count; n > 0U; --n) {
        const UmicomSize index = m->order[n - 1U];
        if (index >= m->count) return UmicomManagedUnsafe(m);
        UmicomKernelManagedServiceRecord *r = &m->records[index];
        if (r->info.state != UMICOM_MANAGED_STOPPING) continue;
        if (r->handle) {
            UmicomKernelSupervisedProcessInfo info;
            UmicomManagedClear(&info, sizeof(info));
            if (UmicomKernelProcessSupervisorQuery(&m->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &info)
                    != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
            if (!UmicomManagedTaskTerminal(info.state)) {
                const UmicomKernelSupervisionStatus cancelled = UmicomKernelProcessSupervisorCancel(
                    &m->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle);
                if (cancelled == UMICOM_SUPERVISION_CLEANUP_FAILED) return UMICOM_MANAGER_CLEANUP_FAILED;
                if (cancelled != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
            }
            const UmicomKernelServiceManagerStatus drained = UmicomManagedDrain(m, r);
            if (drained != UMICOM_MANAGER_OK) return drained;
            /* Reap can retire stream metadata before a later frame release is
             * refused. Do not try to drain that retired token on the retry. */
            r->streamsGranted = UMICOM_FALSE;
            UmicomKernelProcessCompletion completion;
            UmicomManagedClear(&completion, sizeof(completion));
            const UmicomKernelSupervisionStatus collected = UmicomKernelProcessSupervisorCollect(
                &m->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &completion);
            if (collected == UMICOM_SUPERVISION_CLEANUP_FAILED) return UMICOM_MANAGER_CLEANUP_FAILED;
            if (collected != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
            UmicomManagedCopy(&r->info.completion, &completion, sizeof(completion));
            r->handle = 0U;
        }
        r->checkpointPending = UMICOM_FALSE;
        if (m->stopping || m->recovery) r->info.state = UMICOM_MANAGED_STOPPED;
        else if (r->info.attempts < r->spec.attempts && m->now <= ~(UmicomU64)0U - r->spec.retryTicks) {
            r->info.retryAt = m->now + r->spec.retryTicks; r->info.state = UMICOM_MANAGED_BACKOFF;
        } else r->info.state = UMICOM_MANAGED_FAILED;
    }
    UmicomManagedPropagate(m);
    return UMICOM_MANAGER_OK;
}
static UmicomKernelServiceManagerStatus UmicomManagedStart(UmicomKernelServiceManager *m, UmicomSize index)
{
    UmicomKernelManagedServiceRecord *r = &m->records[index];
    ++r->info.attempts; r->info.state = UMICOM_MANAGED_STARTING;
    r->info.reason = UMICOM_MANAGED_NO_FAILURE; r->info.sequence = 0U;
    r->info.readyAt = 0U; r->info.resumeAt = 0U;
    r->checkpointPending = UMICOM_FALSE; r->terminalObserved = UMICOM_FALSE;
    if (m->now > ~(UmicomU64)0U - r->spec.startupTicks) {
        UmicomManagedStop(r, UMICOM_MANAGED_NOT_READY); return UMICOM_MANAGER_OK;
    }
    r->info.deadline = m->now + r->spec.startupTicks;
    const UmicomKernelSupervisionStatus admitted = UmicomKernelProcessSupervisorSpawn(&m->supervisor,
        UMICOM_SUPERVISION_GUARDIAN, r->spec.image, r->spec.imageBytes, 0U, r->spec.sliceLimit,
        UMICOM_CHILDREN_CANCEL_TREE, &r->handle);
    if (admitted == UMICOM_SUPERVISION_LOAD_FAILED) {
        m->retainedLoad = UMICOM_TRUE; UmicomManagedStop(r, UMICOM_MANAGED_LOAD_FAILURE); return UMICOM_MANAGER_OK;
    }
    if (admitted != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
    UmicomKernelSupervisedProcessInfo info;
    UmicomManagedClear(&info, sizeof(info));
    if (UmicomKernelProcessSupervisorQuery(&m->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &info)
            != UMICOM_SUPERVISION_OK) return UmicomManagedUnsafe(m);
    r->info.identity = info.identity;
    UmicomKernelLaunchString args[UMICOM_LAUNCH_ARGUMENT_LIMIT];
    UmicomKernelLaunchString env[UMICOM_LAUNCH_ENVIRONMENT_LIMIT];
    UmicomKernelProgramLaunchSpec launch;
    UmicomManagedClear(args, sizeof(args)); UmicomManagedClear(env, sizeof(env)); UmicomManagedClear(&launch, sizeof(launch));
    if (!UmicomManagedLaunch(r, args, env, &launch) ||
        UmicomKernelProcessSupervisorSetLaunch(&m->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &launch)
            != UMICOM_SUPERVISION_OK || UmicomKernelUserStreamsGrant(&m->streams, r->handle) != UMICOM_STREAM_OK) {
        UmicomManagedStop(r, UMICOM_MANAGED_SETUP_FAILURE); return UMICOM_MANAGER_OK;
    }
    r->streamsGranted = UMICOM_TRUE;
    if (UmicomKernelUserStreamsEndInput(&m->streams, r->handle) != UMICOM_STREAM_OK) return UmicomManagedUnsafe(m);
    return UMICOM_MANAGER_OK;
}
static void UmicomManagedPhase(UmicomKernelServiceManager *m)
{
    if (m->phase == UMICOM_MANAGER_UNSAFE) return;
    if (m->stopping) { m->phase = m->closed ? UMICOM_MANAGER_STOPPED : UMICOM_MANAGER_STOPPING; return; }
    if (m->recovery) { m->phase = UMICOM_MANAGER_RECOVERY; return; }
    UmicomBoolean all = UMICOM_TRUE, failed = UMICOM_FALSE;
    for (UmicomSize i = 0U; i < m->count; ++i) {
        if (m->records[i].info.state != UMICOM_MANAGED_HEALTHY) all = UMICOM_FALSE;
        if (m->records[i].info.state == UMICOM_MANAGED_FAILED) failed = UMICOM_TRUE;
    }
    m->phase = all ? UMICOM_MANAGER_HEALTHY : (failed ? UMICOM_MANAGER_DEGRADED : UMICOM_MANAGER_STARTING);
}
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerStep(UmicomKernelServiceManager *m, UmicomU64 quantum)
{
    UmicomKernelServiceManagerStatus status = UmicomManagedGate(m);
    if (status != UMICOM_MANAGER_OK) return status;
    if (m->closed || m->stopping) return UMICOM_MANAGER_BAD_STATE;
    if (quantum < UMICOM_USER_QUANTUM_MIN_TICKS || quantum > UMICOM_USER_QUANTUM_MAX_TICKS)
        return UMICOM_MANAGER_INVALID_ARGUMENT;
    if (umicomReportingManager) return UMICOM_MANAGER_BUSY;
    m->active = UMICOM_TRUE;
    status = UmicomManagedClock(m);
    if (status == UMICOM_MANAGER_OK) status = UmicomManagedObserve(m);
    if (status == UMICOM_MANAGER_OK) status = UmicomManagedCleanup(m);
    if (status == UMICOM_MANAGER_OK && !m->recovery) {
        /* Admit one fresh process per step. More than one process can remain
         * alive: READY is a report, not an exit or a completed process lifetime. */
        for (UmicomSize n = 0U; n < m->count; ++n) {
            const UmicomSize i = m->order[n];
            UmicomKernelManagedServiceRecord *r = &m->records[i];
            if ((r->info.state == UMICOM_MANAGED_WAITING || r->info.state == UMICOM_MANAGED_BACKOFF) &&
                m->now >= r->info.retryAt && UmicomManagedDependencies(m, i)) {
                status = UmicomManagedStart(m, i); break;
            }
        }
        if (status == UMICOM_MANAGER_OK) status = UmicomManagedObserve(m);
        if (status == UMICOM_MANAGER_OK) status = UmicomManagedCleanup(m);
        if (status == UMICOM_MANAGER_OK && !m->recovery) {
            UmicomKernelSupervisedProcessHandle selected = 0U;
            umicomReportingManager = m;
            const UmicomKernelSupervisionStatus ran = UmicomKernelProcessSupervisorRunOne(&m->supervisor, quantum, &selected);
            umicomReportingManager = 0; /* Detach on every return, including a refused entry. */
            if (ran == UMICOM_SUPERVISION_ENTRY_REFUSED) status = UMICOM_MANAGER_ENTRY_REFUSED;
            else if (ran != UMICOM_SUPERVISION_OK && ran != UMICOM_SUPERVISION_IDLE) status = UmicomManagedUnsafe(m);
            if (status == UMICOM_MANAGER_OK) status = UmicomManagedClock(m);
            if (status == UMICOM_MANAGER_OK) status = UmicomManagedObserve(m);
            if (status == UMICOM_MANAGER_OK)
                for (UmicomSize i = 0U; i < m->count && status == UMICOM_MANAGER_OK; ++i)
                    if (m->records[i].info.state != UMICOM_MANAGED_STOPPING)
                        status = UmicomManagedDrain(m, &m->records[i]);
            if (status == UMICOM_MANAGER_OK) status = UmicomManagedCleanup(m);
        }
    }
    /* An aggregate HEALTHY snapshot must not survive revoked readiness merely
     * because later cleanup needs a retry. UNSAFE is retained by Phase. */
    UmicomManagedPhase(m);
    m->active = UMICOM_FALSE;
    return status;
}
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerRestart(UmicomKernelServiceManager *m, UmicomSize index)
{
    UmicomKernelServiceManagerStatus status = UmicomManagedGate(m);
    if (status != UMICOM_MANAGER_OK) return status;
    if (index >= m->count) return UMICOM_MANAGER_INVALID_ARGUMENT;
    if (m->closed || m->stopping || m->recovery) return UMICOM_MANAGER_BAD_STATE;
    UmicomKernelManagedServiceRecord *r = &m->records[index];
    if (r->info.attempts >= r->spec.attempts) return UMICOM_MANAGER_ATTEMPTS_EXHAUSTED;
    if (!r->handle || r->info.state == UMICOM_MANAGED_STOPPING) return UMICOM_MANAGER_BAD_STATE;
    /* Revoke first, but perform cancellation/collection in the next Step. The
     * consumer graph is already unavailable when this request returns. */
    UmicomManagedStop(r, UMICOM_MANAGED_RESTART_REQUEST);
    UmicomManagedPropagate(m); UmicomManagedPhase(m);
    return UMICOM_MANAGER_OK;
}
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerClose(UmicomKernelServiceManager *m)
{
    UmicomKernelServiceManagerStatus status = UmicomManagedGate(m);
    if (status != UMICOM_MANAGER_OK) return status;
    if (m->closed) return UMICOM_MANAGER_OK;
    m->active = UMICOM_TRUE; m->stopping = UMICOM_TRUE;
    UmicomManagedPropagate(m);
    status = UmicomManagedCleanup(m);
    if (status == UMICOM_MANAGER_OK) {
        if (UmicomKernelProcessSupervisorBeginShutdown(&m->supervisor) != UMICOM_SUPERVISION_OK ||
            UmicomKernelUserStreamsClose(&m->streams) != UMICOM_STREAM_OK) status = UMICOM_MANAGER_CLEANUP_FAILED;
        else { m->closed = UMICOM_TRUE; m->phase = UMICOM_MANAGER_STOPPED; }
    }
    UmicomManagedPhase(m);
    m->active = UMICOM_FALSE;
    return status;
}
UmicomKernelServiceManagerStatus UmicomKernelServiceManagerQuery(UmicomKernelServiceManager *m,
    UmicomSize index, UmicomKernelManagedServiceInfo *out)
{
    if (!m || m->self != m || !m->initialised || !out || index >= m->count || index >= UMICOM_MANAGED_SERVICE_LIMIT)
        return UMICOM_MANAGER_INVALID_ARGUMENT;
    if (m->active) return UMICOM_MANAGER_BUSY;
    UmicomManagedCopy(out, &m->records[index].info, sizeof(*out));
    return UMICOM_MANAGER_OK;
}
const char *UmicomKernelManagedServiceStateName(UmicomKernelManagedServiceState state)
{
    switch (state) {
        case UMICOM_MANAGED_WAITING: return "waiting";
        case UMICOM_MANAGED_STARTING: return "starting";
        case UMICOM_MANAGED_HEALTHY: return "healthy";
        case UMICOM_MANAGED_STOPPING: return "stopping";
        case UMICOM_MANAGED_BACKOFF: return "backoff";
        case UMICOM_MANAGED_FAILED: return "failed";
        case UMICOM_MANAGED_STOPPED: return "stopped";
        default: return "invalid";
    }
}

UmicomBoolean UmicomKernelServiceTaskEligible(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    if (!scheduler->services) return UMICOM_TRUE;
    UmicomKernelServiceManager *m = scheduler->services;
    UmicomKernelManagedServiceRecord *r = UmicomManagedRecord(m, task);
    return r && m == umicomReportingManager && m->active && !m->closed && !m->stopping && !m->recovery &&
        (r->info.state == UMICOM_MANAGED_STARTING || r->info.state == UMICOM_MANAGED_HEALTHY) &&
        m->now >= r->info.resumeAt ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomBoolean UmicomKernelServiceCheckpointPending(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    UmicomKernelManagedServiceRecord *r = UmicomManagedRecord(scheduler->services, task);
    return r && r->checkpointPending ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomBoolean UmicomKernelServiceCheckpointCommit(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTask *task)
{
    UmicomKernelServiceManager *m = scheduler->services;
    UmicomKernelManagedServiceRecord *r = UmicomManagedRecord(m, task);
    /* This runs only after the existing adapter verified the hardware return.
     * Rebuilding a frame or fabricating a timer expiry would obscure that proof. */
    if (!r || m != umicomReportingManager || !m->active || !r->checkpointPending ||
        task->frame.mcause != 8U || task->frame.mepc != r->reportNextPc ||
        task->frame.x10_a0 != UMICOM_SERVICE_REPORT_OK || task->process.report.stopReason != UMICOM_USER_STOP_NONE ||
        !UmicomKernelUserFrameValid(&task->process.report.memory, &task->frame) ||
        r->reportTime > ~(UmicomU64)0U - r->spec.healthTicks ||
        r->info.sequence == ~(UmicomU64)0U || r->reportSequence != r->info.sequence + 1U ||
        r->reportTime >= r->info.deadline ||
        (r->reportOperation == UMICOM_SERVICE_REPORT_READY && r->info.state != UMICOM_MANAGED_STARTING) ||
        (r->reportOperation == UMICOM_SERVICE_REPORT_HEARTBEAT && r->info.state != UMICOM_MANAGED_HEALTHY) ||
        (r->reportOperation != UMICOM_SERVICE_REPORT_READY && r->reportOperation != UMICOM_SERVICE_REPORT_HEARTBEAT))
        return UMICOM_FALSE;
    r->checkpointPending = UMICOM_FALSE;
    r->info.sequence = r->reportSequence; ++r->info.reports;
    if (r->reportOperation == UMICOM_SERVICE_REPORT_READY) r->info.readyAt = r->reportTime;
    r->info.deadline = r->reportTime + r->spec.healthTicks;
    r->info.resumeAt = r->reportTime + r->spec.reportTicks;
    r->info.state = UMICOM_MANAGED_HEALTHY;
    return UMICOM_TRUE;
}
UmicomU64 UmicomKernelServiceReportDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc)
{
    UmicomKernelServiceManager *m = umicomReportingManager;
    UmicomKernelManagedServiceRecord *r = 0;
    if (m && m->active && m->supervisor.scheduler.active)
        for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
            UmicomKernelUserTask *task = &m->supervisor.scheduler.tasks[i];
            if (task->state == UMICOM_USER_TASK_RUNNING && session == &task->process.report)
                r = UmicomManagedRecord(m, task);
        }
    UmicomU64 result = UMICOM_SERVICE_REPORT_UNBOUND;
    if (r) {
        const UmicomU64 operation = frame->x10_a0;
        const UmicomU64 sequence = frame->x11_a1;
        const UmicomU64 now = UmicomPlatformTimerRead();
        result = UMICOM_SERVICE_REPORT_OK;
        if ((operation != UMICOM_SERVICE_REPORT_READY && operation != UMICOM_SERVICE_REPORT_HEARTBEAT) ||
            frame->x12_a2 || frame->x13_a3) result = UMICOM_SERVICE_REPORT_BAD_OPERATION;
        else if (r->checkpointPending || (operation == UMICOM_SERVICE_REPORT_READY && r->info.state != UMICOM_MANAGED_STARTING) ||
            (operation == UMICOM_SERVICE_REPORT_HEARTBEAT && r->info.state != UMICOM_MANAGED_HEALTHY)) result = UMICOM_SERVICE_REPORT_BAD_STATE;
        else if (r->info.sequence == ~(UmicomU64)0U || sequence != r->info.sequence + 1U ||
            r->info.reports == ~(UmicomU64)0U) result = UMICOM_SERVICE_REPORT_BAD_SEQUENCE;
        else if (now < m->now || now >= r->info.deadline || now > ~(UmicomU64)0U - r->spec.healthTicks)
            result = UMICOM_SERVICE_REPORT_TOO_LATE;
        if (result == UMICOM_SERVICE_REPORT_OK) {
            r->reportOperation = operation; r->reportSequence = sequence;
            r->reportTime = now; r->reportNextPc = nextPc;
            r->checkpointPending = UMICOM_TRUE;
        }
    }
    frame->x10_a0 = result; frame->mepc = nextPc;
    if (result != UMICOM_SERVICE_REPORT_OK) { ++session->rejectedCalls; return 1U; }
    /* The accepted report has a retained continuation, not a terminal exit.
     * Only its post-restoration commit publishes dependency readiness. */
    session->stopReason = UMICOM_USER_STOP_NONE;
    return 0U;
}

const char *UmicomKernelManagedServiceReasonName(UmicomKernelManagedServiceReason reason)
{
    switch (reason) {
        case UMICOM_MANAGED_NO_FAILURE: return "none";
        case UMICOM_MANAGED_LOAD_FAILURE: return "load-refused";
        case UMICOM_MANAGED_SETUP_FAILURE: return "setup-refused";
        case UMICOM_MANAGED_EXITED: return "process-exited";
        case UMICOM_MANAGED_FAULTED: return "process-fault";
        case UMICOM_MANAGED_BUDGET: return "execution-budget";
        case UMICOM_MANAGED_NOT_READY: return "readiness-deadline";
        case UMICOM_MANAGED_HEALTH_EXPIRED: return "health-expired";
        case UMICOM_MANAGED_DEPENDENCY_LOST: return "dependency-lost";
        case UMICOM_MANAGED_RESTART_REQUEST: return "restart-request";
        case UMICOM_MANAGED_SHUTDOWN: return "shutdown";
        default: return "invalid";
    }
}
