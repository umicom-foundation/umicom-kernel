/* Umicom Kernel live-service acceptance through the real native execution path.
 * No C mock declares a process ready: the separately linked program must issue
 * the report syscall and return through the existing saved-frame adapter.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/service_manager.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"
extern const UmicomU8 UmicomHealthExecutableStart[];
extern const UmicomU8 UmicomHealthExecutableEnd[];
static UmicomKernelServiceManager umicomManagedProofs[6];
static UmicomSize umicomManagedChecks;
static void UmicomManagedRequire(UmicomBoolean condition, const char *what)
{
    ++umicomManagedChecks;
    if (condition) return;
    UmicomKernelConsoleWrite("managed-services.check="); UmicomKernelConsoleWriteLine(what);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0xb5U); UmicomPlatformHalt();
}
#define UMICOM_MANAGED_CHECK(x) UmicomManagedRequire((x) ? UMICOM_TRUE : UMICOM_FALSE, #x)
static void UmicomManagedProofStart(UmicomSize index, const char *mode, UmicomSize count)
{
    UmicomSize bytes = 0U; while (mode[bytes]) ++bytes;
    const UmicomKernelLaunchString a[] = {{"/services/sample", 16U}, {mode, bytes}};
    const UmicomKernelLaunchString b[] = {{"/services/consumer", 18U}, {"healthy", 7U}};
    UmicomKernelManagedServiceSpec specs[2];
    volatile UmicomU8 *clear = (volatile UmicomU8 *)specs;
    for (UmicomSize i = 0U; i < sizeof(specs); ++i) clear[i] = 0U;
    for (UmicomSize i = 0U; i < count; ++i) {
        specs[i].name = i ? "consumer" : "provider";
        specs[i].image = UmicomHealthExecutableStart;
        specs[i].imageBytes = (UmicomSize)(UmicomHealthExecutableEnd - UmicomHealthExecutableStart);
        specs[i].launch.arguments = i ? b : a; specs[i].launch.argumentCount = 2U;
        specs[i].dependencies = i ? 1U : 0U; specs[i].attempts = 2U;
        specs[i].startupTicks = 2000000U; specs[i].healthTicks = 2000000U;
        /* Keep the healthy consumer's unchanged syscall budget longer than
         * the silent provider's health window. Otherwise this test would
         * exercise consumer budget exhaustion before the intended deadline. */
        specs[i].reportTicks = 100000U; specs[i].retryTicks = 1000U;
        specs[i].sliceLimit = 4096U; specs[i].required = UMICOM_TRUE;
    }
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerInitialize(&umicomManagedProofs[index], specs, count, 0, 0) == UMICOM_MANAGER_OK);
}
static void UmicomManagedProofStep(UmicomKernelServiceManager *m)
{
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerStep(m, 10000U) == UMICOM_MANAGER_OK);
}
static void UmicomManagedProofUntil(UmicomKernelServiceManager *m, UmicomBoolean healthy)
{
    /* Loading and host emulation speed can vary. Bound the diagnostic by the
     * platform clock as well as a generous loop ceiling; do not mistake a fast
     * poller for a failed health interval before enough time has elapsed. */
    const UmicomU64 begin = UmicomPlatformTimerRead();
    for (UmicomSize n = 0U; n < 1000000U; ++n) {
        if (UmicomPlatformTimerRead() - begin > 50000000U) break;
        UmicomManagedProofStep(m);
        if ((healthy && m->phase == UMICOM_MANAGER_HEALTHY) || (!healthy && m->phase == UMICOM_MANAGER_RECOVERY)) return;
    }
    UMICOM_MANAGED_CHECK(0); /* The clock/runner must not hang the diagnostic image. */
}
void UmicomKernelManagedServicesValidateExecution(void)
{
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomRiscvTrapSnapshot trapBefore, trapAfter;
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UMICOM_MANAGED_CHECK(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK);
    UmicomKernelConsoleWriteLine("managed-services-test=begin");
    UmicomKernelConsoleWriteLine("managed-services.case=explicit-ready-and-heartbeat");
    UmicomManagedProofStart(0U, "healthy", 2U);
    UmicomManagedProofUntil(&umicomManagedProofs[0], UMICOM_TRUE);
    UmicomKernelServiceManager *m = &umicomManagedProofs[0];
    UMICOM_MANAGED_CHECK(m->records[0].info.identity != m->records[1].info.identity);
    UMICOM_MANAGED_CHECK(m->records[0].info.sequence >= 1U && m->records[1].info.sequence >= 1U);
    for (UmicomSize n = 0U; n < 1000000U && (m->records[0].info.sequence < 2U || m->records[1].info.sequence < 2U); ++n)
        UmicomManagedProofStep(m);
    UMICOM_MANAGED_CHECK(m->records[0].info.sequence >= 2U && m->records[1].info.sequence >= 2U);
    UmicomKernelConsoleWriteLine("managed-services.readiness=reported-by-user-programs");
    UmicomKernelConsoleWriteLine("managed-services.case=dependency-safe-restart");
    const UmicomU64 oldFirst = m->records[0].info.identity, oldSecond = m->records[1].info.identity;
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerRestart(m, 0U) == UMICOM_MANAGER_OK);
    UMICOM_MANAGED_CHECK(m->records[0].info.state == UMICOM_MANAGED_STOPPING && m->records[1].info.state == UMICOM_MANAGED_STOPPING);
    UmicomManagedProofUntil(m, UMICOM_TRUE);
    UMICOM_MANAGED_CHECK(m->records[0].info.identity > oldSecond && m->records[1].info.identity > oldFirst);
    UMICOM_MANAGED_CHECK(m->records[0].info.attempts == 2U && m->records[1].info.attempts == 2U);
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerClose(m) == UMICOM_MANAGER_OK);
    UmicomKernelConsoleWriteLine("managed-services.replacement=old-instances-collected-first");

    UmicomKernelConsoleWriteLine("managed-services.case=missing-heartbeat");
    UmicomManagedProofStart(1U, "silent", 2U);
    UmicomManagedProofUntil(&umicomManagedProofs[1], UMICOM_FALSE);
    UMICOM_MANAGED_CHECK(umicomManagedProofs[1].records[0].info.reason == UMICOM_MANAGED_HEALTH_EXPIRED);
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerClose(&umicomManagedProofs[1]) == UMICOM_MANAGER_OK);
    UmicomKernelConsoleWriteLine("managed-services.health-expiry=contained");

    UmicomKernelConsoleWriteLine("managed-services.case=no-readiness-report");
    UmicomManagedProofStart(2U, "unready", 2U);
    UmicomManagedProofUntil(&umicomManagedProofs[2], UMICOM_FALSE);
    UMICOM_MANAGED_CHECK(umicomManagedProofs[2].records[0].info.reason == UMICOM_MANAGED_NOT_READY);
    UMICOM_MANAGED_CHECK(umicomManagedProofs[2].records[1].info.attempts == 0U);
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerClose(&umicomManagedProofs[2]) == UMICOM_MANAGER_OK);

    UmicomKernelConsoleWriteLine("managed-services.case=zero-exit-is-not-live-readiness");
    UmicomManagedProofStart(3U, "exit", 1U);
    UmicomManagedProofUntil(&umicomManagedProofs[3], UMICOM_FALSE);
    UMICOM_MANAGED_CHECK(umicomManagedProofs[3].records[0].info.reason == UMICOM_MANAGED_EXITED);
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerClose(&umicomManagedProofs[3]) == UMICOM_MANAGER_OK);

    UmicomKernelConsoleWriteLine("managed-services.case=fault-and-budget-remain-terminal");
    UmicomManagedProofStart(4U, "fault", 1U);
    UmicomManagedProofUntil(&umicomManagedProofs[4], UMICOM_FALSE);
    UMICOM_MANAGED_CHECK(umicomManagedProofs[4].records[0].info.reason == UMICOM_MANAGED_FAULTED);
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerClose(&umicomManagedProofs[4]) == UMICOM_MANAGER_OK);
    UmicomManagedProofStart(5U, "healthy", 1U);
    UmicomManagedProofUntil(&umicomManagedProofs[5], UMICOM_FALSE);
    UMICOM_MANAGED_CHECK(umicomManagedProofs[5].records[0].info.reason == UMICOM_MANAGED_BUDGET);
    UMICOM_MANAGED_CHECK(UmicomKernelServiceManagerClose(&umicomManagedProofs[5]) == UMICOM_MANAGER_OK);

    UmicomRiscvTrapSnapshotRead(&trapBefore); UmicomRiscvTriggerMachineEcall(); UmicomRiscvTrapSnapshotRead(&trapAfter);
    UMICOM_MANAGED_CHECK(trapAfter.exceptionCount == trapBefore.exceptionCount + 1U);
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UMICOM_MANAGED_CHECK(machineBefore.mstatus == machineAfter.mstatus && machineBefore.mie == machineAfter.mie &&
        machineBefore.satp == machineAfter.satp && machineBefore.mtvec == machineAfter.mtvec &&
        machineBefore.mscratch == machineAfter.mscratch && machineBefore.pmpcfg0 == machineAfter.pmpcfg0);
    UMICOM_MANAGED_CHECK(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK);
    UMICOM_MANAGED_CHECK(before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames);
    UmicomKernelConsoleWriteLine("managed-services.original-trap-handler=pass");
    UmicomKernelConsoleWriteLine("managed-services.machine-state=restored");
    UmicomKernelConsoleWriteLine("managed-services.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("managed-services.completed-cases=6");
    UmicomKernelConsoleWrite("managed-services.completed-checks="); UmicomKernelConsoleWriteUnsigned(umicomManagedChecks); UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("managed-services-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_MANAGED_SERVICES_READY");
}
