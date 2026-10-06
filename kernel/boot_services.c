/*-----------------------------------------------------------------------------
 * Umicom Kernel startup dependency, retry and collection policy
 * File: kernel/boot_services.c
 *
 * This Master Controller chooses when a native one-shot service may run. The
 * existing supervisor, stream owner, scheduler and allocator still perform all
 * execution and storage work. Failed cleanup is not converted into permission
 * to discard a handle or to start another copy of the same service.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/boot_services.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/object_cache.h"

/* Packer addresses are detached staging values, never dereferenced. When a
 * real job is admitted, checked offsets rebase strings into this owner's copy. */
#define UMICOM_SERVICE_STAGING_BASE ((UmicomAddress)0x10000U)

/* Aggregate assignments may lower to hosted memcpy/memset even in a
 * freestanding compilation. Keep these bounded owner-record operations as
 * explicit volatile byte accesses; no new allocator or C runtime is implied. */
static void UmicomServiceClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static void UmicomServiceCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    const volatile UmicomU8 *in = (const volatile UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
static UmicomBoolean UmicomServiceZero(const void *data, UmicomSize bytes)
{
    const UmicomU8 *p = (const UmicomU8 *)data;
    for (UmicomSize i = 0U; i < bytes; ++i) if (p[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomSize UmicomServiceNameLength(const char *name)
{
    if (!name) return 0U;
    for (UmicomSize i = 0U; i < UMICOM_BOOT_SERVICE_NAME_BYTES; ++i) {
        const char c = name[i];
        if (!c) return i;
        /* Configuration labels reach the console. Reject control bytes rather
         * than let a service name become a terminal escape sequence. */
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return 0U;
    }
    return 0U;
}
static UmicomBoolean UmicomServiceEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < UMICOM_BOOT_SERVICE_NAME_BYTES; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomBoolean UmicomServiceTerminal(UmicomKernelBootServiceState state)
{
    return state == UMICOM_SERVICE_SUCCEEDED || state == UMICOM_SERVICE_FAILED ||
        state == UMICOM_SERVICE_SKIPPED || state == UMICOM_SERVICE_CANCELLED ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelBootServiceStatus UmicomServicePoison(UmicomKernelBootServices *s)
{
    /* An unverified machine return is not a retryable program failure. */
    s->phase = UMICOM_BOOT_UNSAFE;
    return UMICOM_BOOT_SERVICE_UNSAFE;
}
static UmicomKernelBootServiceStatus UmicomServiceGate(UmicomKernelBootServices *s)
{
    if (!s || s->self != s || !s->initialised) return UMICOM_BOOT_SERVICE_BAD_STATE;
    if (s->active) return UMICOM_BOOT_SERVICE_BUSY;
    if (s->phase == UMICOM_BOOT_UNSAFE) return UMICOM_BOOT_SERVICE_UNSAFE;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_BOOT_SERVICE_ENTRY_REFUSED;
    if (!s->count || s->count > UMICOM_BOOT_SERVICE_LIMIT || s->next >= s->count ||
        (s->running != UMICOM_BOOT_SERVICE_NO_JOB && s->running >= s->count)) return UmicomServicePoison(s);
    return UMICOM_BOOT_SERVICE_OK;
}
static UmicomKernelBootServiceStatus UmicomServiceClock(UmicomKernelBootServices *s)
{
    const UmicomU64 now = UmicomPlatformTimerRead();
    if (now < s->now) { s->phase = UMICOM_BOOT_UNSAFE; return UMICOM_BOOT_SERVICE_CLOCK_REVERSED; }
    s->now = now;
    return UMICOM_BOOT_SERVICE_OK;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesInitialize(UmicomKernelBootServices *s,
    const UmicomKernelBootServiceSpec *specs, UmicomSize count,
    UmicomKernelBootServiceOutput output, void *context)
{
    if (!s || !specs || !count || count > UMICOM_BOOT_SERVICE_LIMIT) return UMICOM_BOOT_SERVICE_INVALID_ARGUMENT;
    if (!UmicomServiceZero(s, sizeof(*s))) return UMICOM_BOOT_SERVICE_BAD_STATE;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_BOOT_SERVICE_ENTRY_REFUSED;
    const UmicomU32 all = ((UmicomU32)1U << (unsigned)count) - 1U;
    for (UmicomSize i = 0U; i < count; ++i) {
        const UmicomKernelBootServiceSpec *p = &specs[i];
        if (!UmicomServiceNameLength(p->name) || !p->image || !p->imageBytes ||
            p->imageBytes > UMICOM_EXECUTABLE_MAX_FILE_BYTES || !p->attempts ||
            p->attempts > UMICOM_BOOT_SERVICE_ATTEMPT_LIMIT || !p->timeoutTicks ||
            p->timeoutTicks > UMICOM_BOOT_SERVICE_TIME_LIMIT || p->retryTicks > UMICOM_BOOT_SERVICE_TIME_LIMIT ||
            !p->sliceLimit || p->sliceLimit > UMICOM_USER_TASK_SLICE_LIMIT ||
            (p->dependencies & ~all) || (p->dependencies & ((UmicomU32)1U << (unsigned)i)) ||
            (p->required != UMICOM_FALSE && p->required != UMICOM_TRUE)) return UMICOM_BOOT_SERVICE_INVALID_PLAN;
        for (UmicomSize j = 0U; j < i; ++j)
            if (UmicomServiceEqual(p->name, specs[j].name)) return UMICOM_BOOT_SERVICE_INVALID_PLAN;
        /* Reuse the canonical launch packer instead of implementing a second
         * environment/name/size grammar. Bad late input changes no owner. */
        UmicomKernelLaunchImage packed;
        if (UmicomKernelProgramLaunchPack(&p->launch, UMICOM_SERVICE_STAGING_BASE, &packed) != UMICOM_LAUNCH_OK)
            return UMICOM_BOOT_SERVICE_INVALID_PLAN;
    }
    UmicomU32 visited = 0U;
    /* Bounded topological elimination finds disconnected cycles too. A plan
     * need not already be sorted in the order in which its jobs will execute. */
    for (UmicomSize pass = 0U; pass < count; ++pass)
        for (UmicomSize i = 0U; i < count; ++i)
            if (!(specs[i].dependencies & ~visited)) visited |= (UmicomU32)1U << (unsigned)i;
    if (visited != all) return UMICOM_BOOT_SERVICE_INVALID_PLAN;

    s->self = s; s->initialised = UMICOM_TRUE; s->count = count;
    s->running = UMICOM_BOOT_SERVICE_NO_JOB; s->now = UmicomPlatformTimerRead();
    s->output = output; s->outputContext = context;
    for (UmicomSize i = 0U; i < count; ++i) {
        UmicomKernelBootServiceRecord *r = &s->records[i];
        UmicomServiceCopy(&r->spec, &specs[i], sizeof(r->spec)); r->info.required = specs[i].required;
        const UmicomSize n = UmicomServiceNameLength(specs[i].name);
        for (UmicomSize j = 0U; j < n; ++j) r->info.name[j] = specs[i].name[j];
        r->spec.name = r->info.name;
        if (UmicomKernelProgramLaunchPack(&specs[i].launch, UMICOM_SERVICE_STAGING_BASE, &r->launch) != UMICOM_LAUNCH_OK)
            return UmicomServicePoison(s);
        /* The caller's argument arrays and strings need not outlive this call. */
        r->spec.launch.arguments = (const UmicomKernelLaunchString *)0;
        r->spec.launch.environment = (const UmicomKernelLaunchString *)0;
    }
    if (UmicomKernelProcessSupervisorInitialize(&s->supervisor) != UMICOM_SUPERVISION_OK ||
        UmicomKernelUserStreamsAttach(&s->streams, &s->supervisor.scheduler) != UMICOM_STREAM_OK)
        return UmicomServicePoison(s);
    return UMICOM_BOOT_SERVICE_OK;
}
static UmicomBoolean UmicomServiceLaunch(UmicomKernelBootServiceRecord *r,
    UmicomKernelLaunchString *arguments, UmicomKernelLaunchString *environment,
    UmicomKernelProgramLaunchSpec *spec)
{
    const UmicomU64 base = UMICOM_SERVICE_STAGING_BASE + __builtin_offsetof(UmicomKernelLaunchImage, text);
    if (!r->launch.info.argumentCount || r->launch.info.argumentCount > UMICOM_LAUNCH_ARGUMENT_LIMIT ||
        r->launch.info.environmentCount > UMICOM_LAUNCH_ENVIRONMENT_LIMIT) return UMICOM_FALSE;
    for (unsigned list = 0U; list < 2U; ++list) {
        const UmicomSize count = list ? r->launch.info.environmentCount : r->launch.info.argumentCount;
        UmicomKernelLaunchString *out = list ? environment : arguments;
        const UmicomU64 *addresses = list ? r->launch.environment : r->launch.arguments;
        for (UmicomSize i = 0U; i < count; ++i) {
            if (addresses[i] < base || addresses[i] - base >= sizeof(r->launch.text)) return UMICOM_FALSE;
            const UmicomSize offset = addresses[i] - base;
            UmicomSize bytes = 0U;
            while (offset + bytes < sizeof(r->launch.text) && r->launch.text[offset + bytes]) ++bytes;
            if (offset + bytes == sizeof(r->launch.text)) return UMICOM_FALSE;
            out[i].data = (const char *)&r->launch.text[offset]; out[i].bytes = bytes;
        }
    }
    spec->arguments = arguments; spec->argumentCount = r->launch.info.argumentCount;
    spec->environment = environment; spec->environmentCount = r->launch.info.environmentCount;
    return UMICOM_TRUE;
}
static void UmicomServiceFailure(UmicomKernelBootServices *s, UmicomKernelBootServiceRecord *r)
{
    /* No live handle or unpublished failed load may remain at this point. */
    if (s->stopping) r->info.state = UMICOM_SERVICE_CANCELLED;
    else if (r->info.attempts < r->spec.attempts && s->now <= ~(UmicomU64)0U - r->spec.retryTicks) {
        r->info.retryAt = s->now + r->spec.retryTicks; r->info.state = UMICOM_SERVICE_BACKOFF;
    } else r->info.state = UMICOM_SERVICE_FAILED;
    s->running = UMICOM_BOOT_SERVICE_NO_JOB;
}
static UmicomKernelBootServiceStatus UmicomServiceDrain(UmicomKernelBootServices *s,
    UmicomKernelBootServiceRecord *r)
{
    if (!r->streamsGranted) return UMICOM_BOOT_SERVICE_OK;
    for (UmicomSize i = 0U; i < UMICOM_STREAM_OUTPUT_RECORDS; ++i) {
        UmicomKernelStreamPacket packet;
    UmicomServiceClear(&packet, sizeof(packet));
        const UmicomKernelStreamStatus result = UmicomKernelUserStreamsDrain(&s->streams, r->handle, &packet);
        if (result == UMICOM_STREAM_WOULD_BLOCK) return UMICOM_BOOT_SERVICE_OK;
        if (result != UMICOM_STREAM_OK) return UmicomServicePoison(s);
        if (s->output) s->output(s->outputContext, r->info.name, &packet);
        /* A null sink explicitly drops already drained bytes. Reap is never
         * used to silently discard output which still belongs to a task. */
    }
    return UMICOM_BOOT_SERVICE_OK;
}
static UmicomKernelBootServiceStatus UmicomServiceSettle(UmicomKernelBootServices *s)
{
    if (s->running == UMICOM_BOOT_SERVICE_NO_JOB) return UMICOM_BOOT_SERVICE_OK;
    UmicomKernelBootServiceRecord *r = &s->records[s->running];
    if (r->retainedLoad) {
        UmicomSize count = 0U;
        if (UmicomKernelProcessSupervisorReapRetained(&s->supervisor, &count) != UMICOM_SUPERVISION_OK)
            return UMICOM_BOOT_SERVICE_CLEANUP_FAILED;
        r->retainedLoad = UMICOM_FALSE; UmicomServiceFailure(s, r); return UMICOM_BOOT_SERVICE_OK;
    }
    if (!r->handle) return UmicomServicePoison(s);
    UmicomKernelSupervisedProcessInfo info;
    UmicomServiceClear(&info, sizeof(info));
    if (UmicomKernelProcessSupervisorQuery(&s->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &info)
        != UMICOM_SUPERVISION_OK || info.unsafe) return UmicomServicePoison(s);
    if (!info.terminal && (s->stopping || r->forcedStop || s->now >= r->info.deadline)) {
        if (!r->forcedStop) r->info.reason = s->stopping ? UMICOM_SERVICE_STOP_REQUEST : UMICOM_SERVICE_DEADLINE;
        r->forcedStop = UMICOM_TRUE;
        if (UmicomKernelProcessSupervisorCancel(&s->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle)
            != UMICOM_SUPERVISION_OK) return UmicomServicePoison(s);
        if (UmicomKernelProcessSupervisorQuery(&s->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &info)
            != UMICOM_SUPERVISION_OK) return UmicomServicePoison(s);
    }
    const UmicomKernelBootServiceStatus drained = UmicomServiceDrain(s, r);
    if (drained != UMICOM_BOOT_SERVICE_OK || !info.terminal) return drained;
    if (!r->terminalObserved) {
        /* Completion time is when the terminal result is first observed, not
         * when a later retry finally releases its pages. A refused collection
         * must not turn an on-time exit into a timeout on the next attempt. */
        r->terminalObserved = UMICOM_TRUE;
        r->successful = !r->forcedStop && info.completion.state == UMICOM_USER_TASK_EXITED &&
            info.completion.exitValue == r->spec.successfulExit && s->now < r->info.deadline
            ? UMICOM_TRUE : UMICOM_FALSE;
        if (r->successful) r->info.reason = UMICOM_SERVICE_NO_FAILURE;
        else if (!r->forcedStop) r->info.reason = s->now >= r->info.deadline ? UMICOM_SERVICE_DEADLINE :
            info.completion.state == UMICOM_USER_TASK_FAULTED ? UMICOM_SERVICE_PROGRAM_FAULT :
            info.completion.state == UMICOM_USER_TASK_EXHAUSTED ? UMICOM_SERVICE_BUDGET : UMICOM_SERVICE_EXIT_ERROR;
    }
    /* The terminal queue is now fully drained. The lower reaper may remove
     * its stream record and then fail while freeing image pages. A retry must
     * resume collection, not try to drain an already retired stream token. */
    r->streamsGranted = UMICOM_FALSE;
    UmicomKernelProcessCompletion result;
    UmicomServiceClear(&result, sizeof(result));
    const UmicomKernelSupervisionStatus collected = UmicomKernelProcessSupervisorCollect(
        &s->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &result);
    if (collected == UMICOM_SUPERVISION_CLEANUP_FAILED) return UMICOM_BOOT_SERVICE_CLEANUP_FAILED;
    if (collected != UMICOM_SUPERVISION_OK) return UmicomServicePoison(s);
    /* Commit terminal evidence only after successful lower collection. */
    r->handle = 0U; r->streamsGranted = UMICOM_FALSE; UmicomServiceCopy(&r->info.completion, &result, sizeof(result));
    if (r->successful) {
        r->info.state = UMICOM_SERVICE_SUCCEEDED;
        s->running = UMICOM_BOOT_SERVICE_NO_JOB;
    } else UmicomServiceFailure(s, r);
    return UMICOM_BOOT_SERVICE_OK;
}
static UmicomKernelBootServiceStatus UmicomServiceStart(UmicomKernelBootServices *s, UmicomSize index)
{
    UmicomKernelBootServiceRecord *r = &s->records[index];
    s->running = index; s->next = (index + 1U) % s->count; ++r->info.attempts;
    r->info.state = UMICOM_SERVICE_RUNNING; r->forcedStop = UMICOM_FALSE;
    r->terminalObserved = UMICOM_FALSE; r->successful = UMICOM_FALSE;
    if (s->now > ~(UmicomU64)0U - r->spec.timeoutTicks) {
        r->info.reason = UMICOM_SERVICE_DEADLINE; UmicomServiceFailure(s, r); return UMICOM_BOOT_SERVICE_OK;
    }
    r->info.deadline = s->now + r->spec.timeoutTicks;
    const UmicomKernelSupervisionStatus admitted = UmicomKernelProcessSupervisorSpawn(&s->supervisor,
        UMICOM_SUPERVISION_GUARDIAN, r->spec.image, r->spec.imageBytes, 0U, r->spec.sliceLimit,
        UMICOM_CHILDREN_CANCEL_TREE, &r->handle);
    if (admitted == UMICOM_SUPERVISION_LOAD_FAILED) {
        /* Even unpublished images can own frames after a failed rollback.
         * Settle must finish that cleanup before retrying the loader. */
        r->info.reason = UMICOM_SERVICE_LOAD_ERROR; r->retainedLoad = UMICOM_TRUE;
        return UmicomServiceSettle(s);
    }
    if (admitted != UMICOM_SUPERVISION_OK) return UmicomServicePoison(s);
    UmicomKernelSupervisedProcessInfo info;
    UmicomServiceClear(&info, sizeof(info));
    if (UmicomKernelProcessSupervisorQuery(&s->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &info)
        != UMICOM_SUPERVISION_OK) return UmicomServicePoison(s);
    r->info.identity = info.identity;
    UmicomKernelLaunchString args[UMICOM_LAUNCH_ARGUMENT_LIMIT];
    UmicomServiceClear(args, sizeof(args));
    UmicomKernelLaunchString env[UMICOM_LAUNCH_ENVIRONMENT_LIMIT];
    UmicomServiceClear(env, sizeof(env));
    UmicomKernelProgramLaunchSpec launch;
    UmicomServiceClear(&launch, sizeof(launch));
    if (!UmicomServiceLaunch(r, args, env, &launch) ||
        UmicomKernelProcessSupervisorSetLaunch(&s->supervisor, UMICOM_SUPERVISION_GUARDIAN, r->handle, &launch)
            != UMICOM_SUPERVISION_OK ||
        UmicomKernelUserStreamsGrant(&s->streams, r->handle) != UMICOM_STREAM_OK) {
        r->forcedStop = UMICOM_TRUE; r->info.reason = UMICOM_SERVICE_SETUP_ERROR;
        return UmicomServiceSettle(s);
    }
    r->streamsGranted = UMICOM_TRUE;
    /* A boot job receives EOF instead of holding startup hostage to a prompt
     * which does not exist yet. Stdout/stderr remain usable and are drained. */
    if (UmicomKernelUserStreamsEndInput(&s->streams, r->handle) != UMICOM_STREAM_OK) return UmicomServicePoison(s);
    return UMICOM_BOOT_SERVICE_OK;
}
static void UmicomServicePhase(UmicomKernelBootServices *s)
{
    if (s->phase == UMICOM_BOOT_UNSAFE) return;
    /* Propagate failed prerequisites to a fixed point before any further
     * admission. Array order cannot hide a failed transitive dependency. */
    for (UmicomSize pass = 0U; pass < s->count; ++pass)
        for (UmicomSize i = 0U; i < s->count; ++i) {
            UmicomKernelBootServiceRecord *r = &s->records[i];
            if (r->info.state != UMICOM_SERVICE_WAITING && r->info.state != UMICOM_SERVICE_BACKOFF) continue;
            for (UmicomSize j = 0U; j < s->count; ++j)
                if ((r->spec.dependencies & ((UmicomU32)1U << (unsigned)j)) &&
                    UmicomServiceTerminal(s->records[j].info.state) && s->records[j].info.state != UMICOM_SERVICE_SUCCEEDED) {
                    r->info.state = UMICOM_SERVICE_SKIPPED; r->info.reason = UMICOM_SERVICE_DEPENDENCY;
                }
        }
    UmicomBoolean requiredFailed = UMICOM_FALSE, complete = UMICOM_TRUE;
    for (UmicomSize i = 0U; i < s->count; ++i) {
        const UmicomKernelBootServiceRecord *r = &s->records[i];
        if (r->spec.required && UmicomServiceTerminal(r->info.state) && r->info.state != UMICOM_SERVICE_SUCCEEDED)
            requiredFailed = UMICOM_TRUE;
        if (!UmicomServiceTerminal(r->info.state)) complete = UMICOM_FALSE;
    }
    if ((requiredFailed || s->stopping) && s->running == UMICOM_BOOT_SERVICE_NO_JOB) {
        for (UmicomSize i = 0U; i < s->count; ++i) if (!UmicomServiceTerminal(s->records[i].info.state)) {
            s->records[i].info.state = UMICOM_SERVICE_CANCELLED;
            s->records[i].info.reason = UMICOM_SERVICE_STOP_REQUEST;
        }
        s->phase = requiredFailed && !s->stopping ? UMICOM_BOOT_RECOVERY : UMICOM_BOOT_STOPPED;
    } else if (complete && s->running == UMICOM_BOOT_SERVICE_NO_JOB) s->phase = UMICOM_BOOT_READY;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesStep(UmicomKernelBootServices *s, UmicomU64 quantum)
{
    UmicomKernelBootServiceStatus status = UmicomServiceGate(s);
    if (status != UMICOM_BOOT_SERVICE_OK) return status;
    if (s->closed || s->stopping) return UMICOM_BOOT_SERVICE_BAD_STATE;
    if (quantum < UMICOM_USER_QUANTUM_MIN_TICKS || quantum > UMICOM_USER_QUANTUM_MAX_TICKS)
        return UMICOM_BOOT_SERVICE_INVALID_ARGUMENT;
    if (s->phase != UMICOM_BOOT_STARTING) return UMICOM_BOOT_SERVICE_OK;
    s->active = UMICOM_TRUE;
    status = UmicomServiceClock(s);
    if (status == UMICOM_BOOT_SERVICE_OK) status = UmicomServiceSettle(s);
    if (status == UMICOM_BOOT_SERVICE_OK) UmicomServicePhase(s);
    if (status == UMICOM_BOOT_SERVICE_OK && s->phase == UMICOM_BOOT_STARTING && s->running == UMICOM_BOOT_SERVICE_NO_JOB) {
        for (UmicomSize scan = 0U; scan < s->count; ++scan) {
            const UmicomSize i = (s->next + scan) % s->count;
            UmicomKernelBootServiceRecord *r = &s->records[i];
            if (r->info.state != UMICOM_SERVICE_WAITING && r->info.state != UMICOM_SERVICE_BACKOFF) continue;
            if (r->info.state == UMICOM_SERVICE_BACKOFF && s->now < r->info.retryAt) continue;
            UmicomBoolean eligible = UMICOM_TRUE;
            for (UmicomSize j = 0U; j < s->count; ++j)
                if ((r->spec.dependencies & ((UmicomU32)1U << (unsigned)j)) &&
                    s->records[j].info.state != UMICOM_SERVICE_SUCCEEDED) eligible = UMICOM_FALSE;
            if (eligible) { status = UmicomServiceStart(s, i); break; }
        }
    }
    if (status == UMICOM_BOOT_SERVICE_OK && s->running != UMICOM_BOOT_SERVICE_NO_JOB) {
        UmicomKernelSupervisedProcessHandle selected = 0U;
        const UmicomKernelSupervisionStatus run = UmicomKernelProcessSupervisorRunOne(&s->supervisor, quantum, &selected);
        if (run == UMICOM_SUPERVISION_ENTRY_REFUSED) status = UMICOM_BOOT_SERVICE_ENTRY_REFUSED;
        else if (run != UMICOM_SUPERVISION_OK && run != UMICOM_SUPERVISION_IDLE) status = UmicomServicePoison(s);
        if (status == UMICOM_BOOT_SERVICE_OK) status = UmicomServiceClock(s);
        if (status == UMICOM_BOOT_SERVICE_OK) status = UmicomServiceSettle(s);
    }
    if (status == UMICOM_BOOT_SERVICE_OK) UmicomServicePhase(s);
    s->active = UMICOM_FALSE;
    return status;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesClose(UmicomKernelBootServices *s)
{
    UmicomKernelBootServiceStatus status = UmicomServiceGate(s);
    if (status != UMICOM_BOOT_SERVICE_OK) return status;
    if (s->closed) return UMICOM_BOOT_SERVICE_OK;
    s->active = UMICOM_TRUE; s->stopping = UMICOM_TRUE;
    status = UmicomServiceSettle(s);
    if (status == UMICOM_BOOT_SERVICE_OK) {
        UmicomSize reaped = 0U;
        if (UmicomKernelProcessSupervisorBeginShutdown(&s->supervisor) != UMICOM_SUPERVISION_OK ||
            UmicomKernelProcessSupervisorReapRetained(&s->supervisor, &reaped) != UMICOM_SUPERVISION_OK ||
            UmicomKernelUserStreamsClose(&s->streams) != UMICOM_STREAM_OK) status = UMICOM_BOOT_SERVICE_CLEANUP_FAILED;
    }
    if (status == UMICOM_BOOT_SERVICE_OK) {
        if (s->phase == UMICOM_BOOT_STARTING) UmicomServicePhase(s);
        s->closed = UMICOM_TRUE; /* Keep reports; do not reset generations. */
    }
    s->active = UMICOM_FALSE;
    return status;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesQuery(UmicomKernelBootServices *s,
    UmicomSize index, UmicomKernelBootServiceInfo *out)
{
    if (!s || s->self != s || !s->initialised || !out || index >= s->count || index >= UMICOM_BOOT_SERVICE_LIMIT)
        return UMICOM_BOOT_SERVICE_INVALID_ARGUMENT;
    if (s->active) return UMICOM_BOOT_SERVICE_BUSY;
    UmicomServiceCopy(out, &s->records[index].info, sizeof(*out)); /* Snapshot survives collection and Close. */
    return UMICOM_BOOT_SERVICE_OK;
}
const char *UmicomKernelBootServiceStateName(UmicomKernelBootServiceState state)
{
    switch (state) {
        case UMICOM_SERVICE_WAITING: return "waiting";
        case UMICOM_SERVICE_RUNNING: return "running";
        case UMICOM_SERVICE_BACKOFF: return "backoff";
        case UMICOM_SERVICE_SUCCEEDED: return "succeeded";
        case UMICOM_SERVICE_FAILED: return "failed";
        case UMICOM_SERVICE_SKIPPED: return "skipped";
        case UMICOM_SERVICE_CANCELLED: return "cancelled";
        default: return "invalid";
    }
}
