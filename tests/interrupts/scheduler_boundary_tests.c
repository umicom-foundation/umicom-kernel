/*-----------------------------------------------------------------------------
 * Umicom Kernel native scheduler/interrupt boundary tests
 *
 * Reuse the existing Linux alternate-stack adapter and the actual scheduler.
 * Linker wrappers model the two readiness checks inserted in the RV64 adapter.
 * They do not claim that this host executes machine CSR instructions or UNIMP.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "model.h"
#include "umicom/kernel/threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "scheduler-boundary:%d: %s\n", __LINE__, #expression); exit(1); \
} } while (0)

static UmicomKernelScheduler scheduler;
static UmicomBoolean abandonScope;
extern UmicomBoolean __real_UmicomKernelThreadMachineReady(void);
extern void __real_UmicomKernelThreadContextSwitch(UmicomKernelThreadContext *from,
    const UmicomKernelThreadContext *to);

UmicomBoolean __wrap_UmicomKernelThreadMachineReady(void)
{
    /* The main target implements this branch in thread_context.S. Keep the
     * normal host adapter's own admission result rather than replacing it. */
    return UmicomKernelInterruptContextSwitchAllowed() != UMICOM_FALSE &&
        __real_UmicomKernelThreadMachineReady() != UMICOM_FALSE ? UMICOM_TRUE : UMICOM_FALSE;
}
void __wrap_UmicomKernelThreadContextSwitch(UmicomKernelThreadContext *from,
    const UmicomKernelThreadContext *to)
{
    /* Only the explicit abandoned-ownership test expects this controlled exit.
     * It models the production adapter's fail-stop, not recovery from a fault. */
    if (UmicomKernelInterruptContextSwitchAllowed() == UMICOM_FALSE) _Exit(77);
    __real_UmicomKernelThreadContextSwitch(from, to);
}
static UmicomU64 Worker(void *argument)
{
    (void)argument;
    UmicomKernelCriticalSection section = 0U;
    CHECK(UmicomKernelCriticalSectionEnter(11U, &section) == UMICOM_INTERRUPT_OK);
    if (abandonScope != UMICOM_FALSE) return 99U;
    volatile UmicomU64 local = 0x12345678U;
    CHECK(UmicomKernelThreadYield(&scheduler) == UMICOM_THREAD_UNSAFE_MACHINE);
    CHECK(UmicomKernelThreadWait(&scheduler) == UMICOM_THREAD_UNSAFE_MACHINE);
    CHECK(UmicomKernelThreadSleepUntil(&scheduler, 1U) == UMICOM_THREAD_UNSAFE_MACHINE);
    CHECK(UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_OK);
    CHECK(UmicomKernelThreadYield(&scheduler) == UMICOM_THREAD_OK);
    return local;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    UmicomInterruptModelPrepare();
    CHECK(UmicomKernelInterruptInitialize(0x80200100U, 0x80230000U) == UMICOM_INTERRUPT_OK);
    CHECK(UmicomKernelSchedulerInitialize(&scheduler) == UMICOM_THREAD_OK);
    UmicomKernelThreadHandle thread = 0U;
    CHECK(UmicomKernelThreadCreate(&scheduler, Worker, NULL, &thread) == UMICOM_THREAD_OK);
    if (strcmp(argv[1], "abandoned-completion") == 0) {
        /* A separate child allows us to check the precise stop code without
         * treating every crash or signal as successful refusal evidence. */
        const pid_t child = fork(); CHECK(child >= 0);
        if (child == 0) {
            abandonScope = UMICOM_TRUE;
            (void)UmicomKernelSchedulerRunOne(&scheduler);
            _Exit(1); /* The guarded context switch must not return. */
        }
        int status = 0;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
        puts("scheduler-boundary.abandoned-completion=pass"); return 0;
    }
    if (strcmp(argv[1], "dispatcher-section") == 0 || strcmp(argv[1], "dispatcher-lease") == 0) {
        UmicomKernelCriticalSection section = 0U;
        UmicomKernelInterruptLease lease = 0U;
        CHECK(UmicomKernelCriticalSectionEnter(11U, &section) == UMICOM_INTERRUPT_OK);
        if (strcmp(argv[1], "dispatcher-lease") == 0) {
            CHECK(UmicomKernelInterruptSourceAcquire(11U, 0x80U, &lease) == UMICOM_INTERRUPT_OK);
            CHECK(UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_OK);
        }
        CHECK(UmicomKernelSchedulerRunOne(&scheduler) == UMICOM_THREAD_UNSAFE_MACHINE);
        if (lease != 0U) {
            CHECK(UmicomKernelCriticalSectionEnter(11U, &section) == UMICOM_INTERRUPT_OK);
            CHECK(UmicomKernelInterruptSourceRelease(11U, lease) == UMICOM_INTERRUPT_OK);
        }
        CHECK(UmicomKernelCriticalSectionLeave(11U, section) == UMICOM_INTERRUPT_OK);
    } else CHECK(strcmp(argv[1], "callback-suspension") == 0);
    CHECK(UmicomKernelSchedulerRunOne(&scheduler) == UMICOM_THREAD_OK);
    CHECK(UmicomKernelSchedulerRunOne(&scheduler) == UMICOM_THREAD_OK);
    UmicomKernelThreadInfo info;
    CHECK(UmicomKernelThreadReap(&scheduler, thread, &info) == UMICOM_THREAD_OK);
    CHECK(info.exitValue == 0x12345678U && info.yields == 1U && info.dispatches == 2U);
    CHECK(UmicomKernelSchedulerValidate(&scheduler) == UMICOM_THREAD_OK);
    printf("scheduler-boundary.%s=pass\n", argv[1]);
    return 0;
}
