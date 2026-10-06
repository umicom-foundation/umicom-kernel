/*-----------------------------------------------------------------------------
 * Umicom Kernel supervised startup jobs
 * File: include/umicom/kernel/boot_services.h
 *
 * A startup service is not ready merely because its ELF loaded or printed a
 * line. This controller waits for a verified successful exit and collection
 * before satisfying its dependents. Failed attempts are collected before a
 * replacement identity is admitted. Launch strings and names are copied;
 * immutable executable buffers remain borrowed for the owner's lifetime.
 *
 * These are bounded ONE-SHOT jobs, not continuously running daemons. A future
 * daemon manager needs explicit readiness and health protocols. It must not
 * mistake these terminal-success dependencies for those protocols.
 *
 * All operations are trusted, serial Kernel work on hart zero between quanta.
 * Initialise stable, zero-filled storage once. Do not copy it or dispatch its
 * embedded supervisor behind the controller's back. One job runs at a time;
 * backoff does not prevent an independent eligible job from starting.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_BOOT_SERVICES_H
#define UMICOM_KERNEL_BOOT_SERVICES_H
#include "umicom/kernel/process_supervisor.h"
#include "umicom/kernel/user_streams.h"

#define UMICOM_BOOT_SERVICE_LIMIT 8U
#define UMICOM_BOOT_SERVICE_NAME_BYTES 32U
#define UMICOM_BOOT_SERVICE_ATTEMPT_LIMIT 4U
#define UMICOM_BOOT_SERVICE_TIME_LIMIT 100000000ULL
#define UMICOM_BOOT_SERVICE_NO_JOB UMICOM_BOOT_SERVICE_LIMIT

typedef enum UmicomKernelBootServiceState {
    UMICOM_SERVICE_WAITING, UMICOM_SERVICE_RUNNING, UMICOM_SERVICE_BACKOFF,
    UMICOM_SERVICE_SUCCEEDED, UMICOM_SERVICE_FAILED, UMICOM_SERVICE_SKIPPED,
    UMICOM_SERVICE_CANCELLED
} UmicomKernelBootServiceState;
typedef enum UmicomKernelBootPhase {
    UMICOM_BOOT_STARTING, UMICOM_BOOT_READY, UMICOM_BOOT_RECOVERY,
    UMICOM_BOOT_STOPPED, UMICOM_BOOT_UNSAFE
} UmicomKernelBootPhase;
typedef enum UmicomKernelBootServiceReason {
    UMICOM_SERVICE_NO_FAILURE, UMICOM_SERVICE_LOAD_ERROR, UMICOM_SERVICE_SETUP_ERROR,
    UMICOM_SERVICE_EXIT_ERROR, UMICOM_SERVICE_PROGRAM_FAULT, UMICOM_SERVICE_DEADLINE,
    UMICOM_SERVICE_BUDGET, UMICOM_SERVICE_DEPENDENCY, UMICOM_SERVICE_STOP_REQUEST
} UmicomKernelBootServiceReason;
typedef enum UmicomKernelBootServiceStatus {
    UMICOM_BOOT_SERVICE_OK, UMICOM_BOOT_SERVICE_INVALID_ARGUMENT,
    UMICOM_BOOT_SERVICE_INVALID_PLAN, UMICOM_BOOT_SERVICE_BAD_STATE,
    UMICOM_BOOT_SERVICE_BUSY, UMICOM_BOOT_SERVICE_CLOCK_REVERSED,
    UMICOM_BOOT_SERVICE_CLEANUP_FAILED, UMICOM_BOOT_SERVICE_UNSAFE,
    UMICOM_BOOT_SERVICE_ENTRY_REFUSED
} UmicomKernelBootServiceStatus;

/* Dependencies are bits naming other indices in this complete plan. The
 * whole graph, including disconnected components, is checked before admission.
 * Every attempt has both a wall-time deadline and a scheduler slice budget. */
typedef struct UmicomKernelBootServiceSpec {
    const char *name;
    const UmicomU8 *image;
    UmicomSize imageBytes;
    UmicomKernelProgramLaunchSpec launch;
    UmicomU32 dependencies;
    UmicomU32 attempts; /* Total attempts including the first, not extra retries. */
    UmicomU64 retryTicks;
    UmicomU64 timeoutTicks;
    UmicomU64 sliceLimit;
    UmicomU64 successfulExit;
    UmicomBoolean required;
} UmicomKernelBootServiceSpec;

/* The callback may consume these copied output bytes synchronously. It must
 * not retain pointers, reenter the controller, or run indefinitely. */
typedef void (*UmicomKernelBootServiceOutput)(void *context, const char *name,
    const UmicomKernelStreamPacket *packet);
typedef struct UmicomKernelBootServiceInfo {
    char name[UMICOM_BOOT_SERVICE_NAME_BYTES];
    UmicomKernelBootServiceState state;
    UmicomKernelBootServiceReason reason;
    UmicomU32 attempts;
    UmicomU64 identity;
    UmicomU64 deadline;
    UmicomU64 retryAt;
    UmicomBoolean required;
    UmicomKernelProcessCompletion completion; /* Last collected attempt. */
} UmicomKernelBootServiceInfo;

/* Visible only to permit static storage and deliberate fault-injection tests.
 * No caller should mutate records, handles or saved launch data while live. */
typedef struct UmicomKernelBootServiceRecord {
    UmicomKernelBootServiceInfo info;
    UmicomKernelBootServiceSpec spec;
    UmicomKernelLaunchImage launch;
    UmicomKernelSupervisedProcessHandle handle;
    UmicomBoolean streamsGranted;
    UmicomBoolean forcedStop;
    UmicomBoolean retainedLoad;
    UmicomBoolean terminalObserved; /* Cleanup delay cannot change the observed outcome. */
    UmicomBoolean successful;
} UmicomKernelBootServiceRecord;
typedef struct UmicomKernelBootServices {
    const struct UmicomKernelBootServices *self;
    UmicomBoolean initialised;
    UmicomBoolean active;
    UmicomBoolean closed;
    UmicomBoolean stopping;
    UmicomKernelBootPhase phase;
    UmicomSize count;
    UmicomSize running;
    UmicomSize next;
    UmicomU64 now;
    UmicomKernelBootServiceOutput output;
    void *outputContext;
    UmicomKernelBootServiceRecord records[UMICOM_BOOT_SERVICE_LIMIT];
    UmicomKernelProcessSupervisor supervisor;
    UmicomKernelUserStreams streams;
} UmicomKernelBootServices;

/* Invalid plans leave the zero-filled owner unchanged. No physical frame is
 * acquired until Step admits a job. Malformed ELF bytes remain an observable
 * load failure, subject to the configured attempt limit. */
UmicomKernelBootServiceStatus UmicomKernelBootServicesInitialize(UmicomKernelBootServices *services,
    const UmicomKernelBootServiceSpec *specs, UmicomSize count,
    UmicomKernelBootServiceOutput output, void *context);
/* At most one user quantum runs per Step. Kernel bookkeeping is not pre-empted.
 * A caller services the monotonic clock while backoff/deadlines are pending. */
UmicomKernelBootServiceStatus UmicomKernelBootServicesStep(UmicomKernelBootServices *services,
    UmicomU64 quantumTicks);
/* Cancellation and collection are explicit. Partial cleanup retains authority
 * and evidence for another Close; a closed owner cannot restart or reinitialise. */
UmicomKernelBootServiceStatus UmicomKernelBootServicesClose(UmicomKernelBootServices *services);
UmicomKernelBootServiceStatus UmicomKernelBootServicesQuery(UmicomKernelBootServices *services,
    UmicomSize index, UmicomKernelBootServiceInfo *outInfo);
const char *UmicomKernelBootServiceStateName(UmicomKernelBootServiceState state);
void UmicomKernelBootServicesValidateExecution(void);
#endif /* UMICOM_KERNEL_BOOT_SERVICES_H */
