/*-----------------------------------------------------------------------------
 * Umicom Kernel real-guest startup-service acceptance
 * File: kernel/boot_services_validation.c
 *
 * These cases run after the existing diagnostics, not during normal startup.
 * They reuse the actual launch client and diagnostic ELF, the existing user
 * monitor, and the actual timer. A compiled host model is not guest evidence.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/boot_services.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"
#include "console_internal.h"
extern const UmicomU8 UmicomLaunchExecutableStart[];
extern const UmicomU8 UmicomLaunchExecutableEnd[];
extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
static UmicomKernelBootServices umicomBootValidation[6];
static UmicomU64 umicomBootChecks;
static UmicomU64 umicomBootOutputBytes;

static void UmicomBootRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomBootChecks;
    if (condition) return;
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelConsoleWrite("boot-services.reason="); UmicomKernelConsoleWriteLine(reason);
    UmicomPlatformFinishFailure(0xb3U); UmicomPlatformHalt();
}
static void UmicomBootCapture(void *context, const char *name, const UmicomKernelStreamPacket *packet)
{
    (void)context; (void)name;
    UmicomBootRequire(packet->bytes <= UMICOM_STREAM_TRANSFER_BYTES, "output record extent");
    umicomBootOutputBytes += packet->bytes;
}
static void UmicomBootPlan(UmicomKernelBootServiceSpec *spec, const char *name)
{
    static const UmicomKernelLaunchString args[] = {{"/boot/check",11U},{"startup",7U}};
    static const UmicomKernelLaunchString env[] = {{"LANG=C",6U}};
    UmicomConsoleClear(spec, sizeof(*spec));
    spec->name=name; spec->image=UmicomLaunchExecutableStart;
    spec->imageBytes=(UmicomSize)(UmicomLaunchExecutableEnd-UmicomLaunchExecutableStart);
    spec->launch.arguments=args; spec->launch.argumentCount=2U;
    spec->launch.environment=env; spec->launch.environmentCount=1U;
    spec->attempts=1U; spec->timeoutTicks=10000000U; spec->sliceLimit=128U; spec->required=UMICOM_TRUE;
}
static void UmicomBootComplete(UmicomKernelBootServices *owner)
{
    /* A broken dependency transition must not leave CTest waiting indefinitely.
     * The external test timeout is independent of this guest watchdog. */
    const UmicomU64 begun=UmicomPlatformTimerRead();
    while(owner->phase==UMICOM_BOOT_STARTING) {
        UmicomBootRequire(UmicomPlatformTimerRead()-begun<50000000U,"controller watchdog");
        UmicomBootRequire(UmicomKernelBootServicesStep(owner,50000U)==UMICOM_BOOT_SERVICE_OK,"controller step");
    }
    UmicomBootRequire(UmicomKernelBootServicesClose(owner)==UMICOM_BOOT_SERVICE_OK,"controller close");
}
static UmicomBoolean UmicomBootMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus==b->mstatus && a->mie==b->mie && a->mtvec==b->mtvec && a->mscratch==b->mscratch &&
        a->medeleg==b->medeleg && a->mideleg==b->mideleg && a->satp==b->satp &&
        a->pmpcfg0==b->pmpcfg0 && a->pmpaddr0==b->pmpaddr0 && a->mepc==b->mepc &&
        a->mcause==b->mcause && a->mtval==b->mtval ? UMICOM_TRUE:UMICOM_FALSE;
}
void UmicomKernelBootServicesValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("boot-services-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState controls, restored;
    UmicomBootRequire(UmicomKernelPhysicalMemorySnapshotRead(&before)==UMICOM_KERNEL_MEMORY_OK,"initial accounting");
    UmicomRiscvSupervisorMachineStateRead(&controls);
    const UmicomU64 compare=UmicomPlatformTimerCompareRead(0U);
    UmicomKernelBootServiceSpec specs[3];

    UmicomKernelConsoleWriteLine("boot-services.case=dependency-order-and-user-output");
    UmicomBootPlan(&specs[0],"dependent"); UmicomBootPlan(&specs[1],"prerequisite");
    specs[0].dependencies=2U;
    UmicomBootRequire(UmicomKernelBootServicesInitialize(&umicomBootValidation[0],specs,2U,UmicomBootCapture,(void*)0)==UMICOM_BOOT_SERVICE_OK,"valid unsorted graph");
    UmicomBootComplete(&umicomBootValidation[0]);
    UmicomBootRequire(umicomBootValidation[0].phase==UMICOM_BOOT_READY && umicomBootOutputBytes!=0U &&
        umicomBootValidation[0].records[0].info.identity>umicomBootValidation[0].records[1].info.identity,"dependency completion before admission");

    UmicomKernelConsoleWriteLine("boot-services.case=optional-retry-and-dependent-skip");
    static const UmicomU8 invalid[]={0U,1U,2U,3U};
    UmicomBootPlan(&specs[0],"optional"); UmicomBootPlan(&specs[1],"optional-dependent"); UmicomBootPlan(&specs[2],"independent");
    specs[0].image=invalid; specs[0].imageBytes=sizeof(invalid); specs[0].required=UMICOM_FALSE;
    specs[0].attempts=2U; specs[0].retryTicks=20000U;
    specs[1].required=UMICOM_FALSE; specs[1].dependencies=1U;
    UmicomBootRequire(UmicomKernelBootServicesInitialize(&umicomBootValidation[1],specs,3U,(UmicomKernelBootServiceOutput)0,(void*)0)==UMICOM_BOOT_SERVICE_OK,"optional plan");
    UmicomBootComplete(&umicomBootValidation[1]);
    UmicomBootRequire(umicomBootValidation[1].phase==UMICOM_BOOT_READY &&
        umicomBootValidation[1].records[0].info.attempts==2U &&
        umicomBootValidation[1].records[1].info.state==UMICOM_SERVICE_SKIPPED &&
        umicomBootValidation[1].records[2].info.state==UMICOM_SERVICE_SUCCEEDED,"optional failure containment");

    UmicomKernelConsoleWriteLine("boot-services.case=required-failure-selects-recovery");
    specs[0].required=UMICOM_TRUE;
    UmicomBootRequire(UmicomKernelBootServicesInitialize(&umicomBootValidation[2],specs,2U,(UmicomKernelBootServiceOutput)0,(void*)0)==UMICOM_BOOT_SERVICE_OK,"required plan");
    UmicomBootComplete(&umicomBootValidation[2]);
    UmicomBootRequire(umicomBootValidation[2].phase==UMICOM_BOOT_RECOVERY &&
        umicomBootValidation[2].records[0].info.attempts==2U &&
        umicomBootValidation[2].records[1].info.attempts==0U,"no admission after required failure");

    /* The original diagnostic accepts one numeric a0. Structured preparation
     * supplies argc=3 here, selecting its existing endless-loop mode. This is
     * a deliberate adapter test, not a new program ABI or restart mechanism. */
    static const UmicomKernelLaunchString spinArgs[]={{"/boot/spin",10U},{"a",1U},{"b",1U}};
    UmicomBootPlan(&specs[0],"bounded-worker");
    specs[0].image=UmicomEmbeddedExecutableStart;
    specs[0].imageBytes=(UmicomSize)(UmicomEmbeddedExecutableEnd-UmicomEmbeddedExecutableStart);
    specs[0].launch.arguments=spinArgs; specs[0].launch.argumentCount=3U;
    UmicomKernelConsoleWriteLine("boot-services.case=wall-deadline");
    specs[0].timeoutTicks=70000U;
    UmicomBootRequire(UmicomKernelBootServicesInitialize(&umicomBootValidation[3],specs,1U,(UmicomKernelBootServiceOutput)0,(void*)0)==UMICOM_BOOT_SERVICE_OK,"deadline plan");
    UmicomBootComplete(&umicomBootValidation[3]);
    UmicomBootRequire(umicomBootValidation[3].records[0].info.reason==UMICOM_SERVICE_DEADLINE,"wall deadline reported");

    UmicomKernelConsoleWriteLine("boot-services.case=execution-budget");
    specs[0].timeoutTicks=10000000U; specs[0].sliceLimit=1U;
    UmicomBootRequire(UmicomKernelBootServicesInitialize(&umicomBootValidation[4],specs,1U,(UmicomKernelBootServiceOutput)0,(void*)0)==UMICOM_BOOT_SERVICE_OK,"budget plan");
    UmicomBootComplete(&umicomBootValidation[4]);
    UmicomBootRequire(umicomBootValidation[4].records[0].info.reason==UMICOM_SERVICE_BUDGET,"budget remains distinct");

    UmicomKernelConsoleWriteLine("boot-services.case=close-active-service");
    specs[0].sliceLimit=128U;
    UmicomBootRequire(UmicomKernelBootServicesInitialize(&umicomBootValidation[5],specs,1U,(UmicomKernelBootServiceOutput)0,(void*)0)==UMICOM_BOOT_SERVICE_OK,"close plan");
    UmicomBootRequire(UmicomKernelBootServicesStep(&umicomBootValidation[5],50000U)==UMICOM_BOOT_SERVICE_OK &&
        umicomBootValidation[5].records[0].handle!=0U,"live continuation exists");
    UmicomBootRequire(UmicomKernelBootServicesClose(&umicomBootValidation[5])==UMICOM_BOOT_SERVICE_OK &&
        umicomBootValidation[5].phase==UMICOM_BOOT_STOPPED,"close cancels and collects");

    UmicomRiscvSupervisorMachineStateRead(&restored);
    UmicomBootRequire(UmicomBootMachineEqual(&controls,&restored) && UmicomPlatformTimerCompareRead(0U)==compare,"restored control state");
    UmicomBootRequire(UmicomKernelPhysicalMemorySnapshotRead(&after)==UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames==after.allocatedFrames && before.freeFrames==after.freeFrames &&
        before.reservedFrames==after.reservedFrames && UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK,"restored ownership accounting");
    UmicomKernelConsoleWriteLine("boot-services.machine-state=restored");
    UmicomKernelConsoleWriteLine("boot-services.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("boot-services.completed-cases=6");
    UmicomKernelConsoleWrite("boot-services.completed-checks="); UmicomKernelConsoleWriteUnsigned(umicomBootChecks); UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("boot-services-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_BOOT_SERVICES_READY");
}
