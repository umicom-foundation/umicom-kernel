/*-----------------------------------------------------------------------------
 * Umicom Kernel native startup-service tests
 * File: tests/boot_services/boot_services_tests.c
 *
 * Reuse the existing synthetic ELF, physical allocator and CSR/timer model.
 * The actual service controller, supervisor, scheduler, launch packer, streams,
 * trap dispatcher and memory owners are compiled. Only hardware entry and its
 * observations are modelled. A release wrapper injects an explicit failure.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#define main UmicomBootPreviousModelMain
#define UmicomRiscvUserExecuteFrame UmicomBootPreviousExecute
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#include "umicom/kernel/boot_services.h"
#define CHECK(x) UmicomModelRequire((x) ? 1 : 0, #x)

static UmicomKernelBootServices services;
static UmicomKernelBootServiceSpec plans[UMICOM_BOOT_SERVICE_LIMIT + 1U];
static UmicomKernelLaunchString arguments[2], environment[1];
static unsigned behaviour;
static unsigned releaseRefusals;
static unsigned collectedOutput;
static unsigned reentryChecks;
static unsigned expectedLaunchChecks;
static UmicomU64 entryIdentities[32];
static unsigned entries;
static char copiedName[32] = "first";
static char copiedArgument[32] = "snapshot-value";

UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    return umicomModelAllowed && !umicomModelHart && !umicomModelMachine.mie &&
        !(umicomModelMachine.mstatus & 0x20008U) && !umicomModelMachine.satp ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelMemoryStatus __real_UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress address);
UmicomKernelMemoryStatus __wrap_UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress address)
{
    if (releaseRefusals) { --releaseRefusals; return UMICOM_KERNEL_MEMORY_RESERVED_FRAME; }
    return __real_UmicomKernelPhysicalMemoryFreeFrame(address);
}
static void Capture(void *context, const char *name, const UmicomKernelStreamPacket *packet)
{
    (void)context;
    CHECK(name && packet->selector == UMICOM_STREAM_OUTPUT && packet->bytes == 4U);
    CHECK(!memcmp(packet->data, "boot", 4U)); ++collectedOutput;
    CHECK(UmicomModelMemory().allocatedFrames != 0U); /* Drained before process collection. */
    if (reentryChecks) {
        UmicomKernelBootServiceInfo info = {0};
        CHECK(UmicomKernelBootServicesStep(&services, 1000U) == UMICOM_BOOT_SERVICE_BUSY);
        CHECK(UmicomKernelBootServicesClose(&services) == UMICOM_BOOT_SERVICE_BUSY);
        CHECK(UmicomKernelBootServicesQuery(&services, 0U, &info) == UMICOM_BOOT_SERVICE_BUSY);
    }
}
UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    CHECK(entries < sizeof(entryIdentities) / sizeof(entryIdentities[0]));
    entryIdentities[entries++] = session->identity;
    if (behaviour == 2U) { umicomModelMode = UMICOM_MODEL_TIMER; return UmicomBootPreviousExecute(request, session, frame); }
    if (behaviour == 3U) { umicomModelMode = UMICOM_MODEL_FAULT; return UmicomBootPreviousExecute(request, session, frame); }
    if (behaviour == 4U) return 1U; /* Architecture refusal before any instruction. */
    ++umicomModelEntries;
    CHECK(request->rootTablePhysicalAddress == session->memory.space->rootTablePhysicalAddress);
    UmicomRiscvTrapFrame live = *frame;
    live.mstatus = (UmicomU64)2U << 32U; live.mcause = 8U; live.reserved = 0U;
    if (expectedLaunchChecks) {
        UmicomU64 secondAddress = 0U;
        char snapshot[15] = {0};
        CHECK(UmicomKernelUserMemoryRead(&session->memory, live.x11_a1 + 8U, (UmicomU8 *)&secondAddress, 8U) == UMICOM_USER_RESULT_OK);
        CHECK(UmicomKernelUserMemoryRead(&session->memory, secondAddress, (UmicomU8 *)snapshot, 15U) == UMICOM_USER_RESULT_OK);
        CHECK(!strcmp(snapshot, "snapshot-value"));
    }
    if (behaviour == 5U) {
        static const UmicomU8 message[] = "boot";
        CHECK(UmicomKernelUserMemoryWrite(&session->memory, 0x600000U, message, 4U) == UMICOM_USER_RESULT_OK);
        live.x17_a7 = UMICOM_USER_CALL_STREAM_WRITE;
        live.x10_a0 = UMICOM_STREAM_OUTPUT; live.x11_a1 = 0x600000U;
        live.x12_a2 = 4U; live.x13_a3 = 0U;
        CHECK(UmicomKernelUserTrapDispatch(session, &live) == 1U);
        CHECK(live.x10_a0 == UMICOM_STREAM_OK && live.x11_a1 == 4U);
    }
    live.mcause = 8U; live.x17_a7 = UMICOM_USER_CALL_EXIT;
    live.x10_a0 = behaviour == 1U ? 7U : 0U;
    CHECK(UmicomKernelUserTrapDispatch(session, &live) == 0U);
    *frame = live;
    return 0U;
}
static void Setup(void)
{
    UmicomModelSetup();
    arguments[0] = (UmicomKernelLaunchString){"/boot/check", 11U};
    arguments[1] = (UmicomKernelLaunchString){copiedArgument, 14U};
    environment[0] = (UmicomKernelLaunchString){"LANG=C", 6U};
    for (UmicomSize i = 0U; i < UMICOM_BOOT_SERVICE_LIMIT + 1U; ++i) {
        plans[i] = (UmicomKernelBootServiceSpec){"job", umicomModelElf, sizeof(umicomModelElf),
            {arguments, 2U, environment, 1U}, 0U, 1U, 100U, 100000U, 16U, 0U, UMICOM_TRUE};
    }
    plans[0].name = copiedName;
    plans[1].name = "second"; plans[2].name = "third"; plans[3].name = "fourth";
    plans[4].name = "fifth"; plans[5].name = "sixth"; plans[6].name = "seventh"; plans[7].name = "eighth";
}
static void Initialise(UmicomSize count)
{
    CHECK(UmicomKernelBootServicesInitialize(&services, plans, count, Capture, NULL) == UMICOM_BOOT_SERVICE_OK);
    CHECK(UmicomModelMemory().allocatedFrames == 0U);
}
static void Step(void) { CHECK(UmicomKernelBootServicesStep(&services, 1000U) == UMICOM_BOOT_SERVICE_OK); }
static void Finish(void)
{
    for (unsigned i = 0U; i < 24U && services.phase == UMICOM_BOOT_STARTING; ++i) { Step(); umicomModelNow += 100U; }
    CHECK(services.phase != UMICOM_BOOT_STARTING);
    CHECK(UmicomKernelBootServicesClose(&services) == UMICOM_BOOT_SERVICE_OK);
    CHECK(UmicomKernelBootServicesClose(&services) == UMICOM_BOOT_SERVICE_OK);
    UmicomModelNoLeaks();
}
static void InvalidPlan(const char *name)
{
    UmicomSize count = 1U;
    UmicomKernelBootServiceSpec *input = plans;
    UmicomKernelBootServiceStatus expected = UMICOM_BOOT_SERVICE_INVALID_PLAN;
    if (!strcmp(name,"null-owner")) { CHECK(UmicomKernelBootServicesInitialize(NULL,plans,1U,NULL,NULL)==UMICOM_BOOT_SERVICE_INVALID_ARGUMENT); return; }
    if (!strcmp(name,"zero-count")) { count=0;expected=UMICOM_BOOT_SERVICE_INVALID_ARGUMENT; }
    else if (!strcmp(name,"excess-count")) { count=UMICOM_BOOT_SERVICE_LIMIT+1U;expected=UMICOM_BOOT_SERVICE_INVALID_ARGUMENT; }
    else if (!strcmp(name,"null-plan")) { input=NULL;expected=UMICOM_BOOT_SERVICE_INVALID_ARGUMENT; }
    else if (!strcmp(name,"duplicate-name")) { count=2; plans[1].name=plans[0].name; }
    else if (!strcmp(name,"empty-name")) plans[0].name="";
    else if (!strcmp(name,"bad-name")) plans[0].name="bad\nname";
    else if (!strcmp(name,"long-name")) plans[0].name="abcdefghijklmnopqrstuvwxyz123456";
    else if (!strcmp(name,"null-image")) plans[0].image=NULL;
    else if (!strcmp(name,"empty-image")) plans[0].imageBytes=0;
    else if (!strcmp(name,"huge-image")) plans[0].imageBytes=UMICOM_EXECUTABLE_MAX_FILE_BYTES+1U;
    else if (!strcmp(name,"invalid-launch")) { count=2;plans[1].launch.argumentCount=0; }
    else if (!strcmp(name,"invalid-environment")) environment[0]=(UmicomKernelLaunchString){"INVALID",7U};
    else if (!strcmp(name,"self-dependency")) plans[0].dependencies=1U;
    else if (!strcmp(name,"unknown-dependency")) plans[0].dependencies=4U;
    else if (!strcmp(name,"cycle")) { count=2;plans[0].dependencies=2U;plans[1].dependencies=1U; }
    else if (!strcmp(name,"disconnected-cycle")) { count=3;plans[1].dependencies=4U;plans[2].dependencies=2U; }
    else if (!strcmp(name,"zero-attempts")) plans[0].attempts=0;
    else if (!strcmp(name,"excess-attempts")) plans[0].attempts=5;
    else if (!strcmp(name,"zero-timeout")) plans[0].timeoutTicks=0;
    else if (!strcmp(name,"excess-timeout")) plans[0].timeoutTicks=UMICOM_BOOT_SERVICE_TIME_LIMIT+1U;
    else if (!strcmp(name,"excess-retry")) plans[0].retryTicks=UMICOM_BOOT_SERVICE_TIME_LIMIT+1U;
    else if (!strcmp(name,"zero-slices")) plans[0].sliceLimit=0;
    else if (!strcmp(name,"excess-slices")) plans[0].sliceLimit=UMICOM_USER_TASK_SLICE_LIMIT+1U;
    else CHECK(0);
    CHECK(UmicomKernelBootServicesInitialize(&services,input,count,Capture,NULL)==expected);
    const UmicomU8 *bytes=(const UmicomU8 *)&services;
    for(UmicomSize i=0;i<sizeof(services);++i)CHECK(bytes[i]==0U);
    UmicomModelNoLeaks();
}
int main(int argc, char **argv)
{
    if(argc!=2)return EXIT_FAILURE;
    Setup();const char *name=argv[1];
    if(!strcmp(name,"normal")){Initialise(1);Finish();CHECK(services.records[0].info.state==UMICOM_SERVICE_SUCCEEDED && entries==1U);}
    else if(!strcmp(name,"duplicate-initialise")){Initialise(1);CHECK(UmicomKernelBootServicesInitialize(&services,plans,1,NULL,NULL)==UMICOM_BOOT_SERVICE_BAD_STATE);Finish();}
    else if(!strcmp(name,"copied-owner")){Initialise(1);UmicomKernelBootServices *copy=malloc(sizeof(*copy));CHECK(copy);*copy=services;
        CHECK(UmicomKernelBootServicesStep(copy,1000)==UMICOM_BOOT_SERVICE_BAD_STATE);free(copy);Finish();}
    else if(!strcmp(name,"unsafe-admission")){umicomModelAllowed=UMICOM_FALSE;CHECK(UmicomKernelBootServicesInitialize(&services,plans,1,NULL,NULL)==UMICOM_BOOT_SERVICE_ENTRY_REFUSED);UmicomModelNoLeaks();}
    else if(!strcmp(name,"unsafe-step")){Initialise(1);umicomModelAllowed=UMICOM_FALSE;CHECK(UmicomKernelBootServicesStep(&services,1000)==UMICOM_BOOT_SERVICE_ENTRY_REFUSED);CHECK(!entries);umicomModelAllowed=UMICOM_TRUE;Finish();}
    else if(!strcmp(name,"invalid-quantum")){Initialise(1);CHECK(UmicomKernelBootServicesStep(&services,0)==UMICOM_BOOT_SERVICE_INVALID_ARGUMENT);CHECK(!entries);Finish();}
    else if(!strcmp(name,"dependency-order")){plans[0].dependencies=2;Initialise(2);Step();CHECK(services.records[1].info.state==UMICOM_SERVICE_SUCCEEDED && services.records[0].info.attempts==0);Finish();CHECK(services.records[0].info.identity>services.records[1].info.identity);}
    else if(!strcmp(name,"multiple-prerequisites")){plans[0].dependencies=6;Initialise(3);Step();Step();CHECK(!services.records[0].info.attempts);Finish();CHECK(services.records[0].info.identity==3);}
    else if(!strcmp(name,"optional-failure")){plans[0].required=UMICOM_FALSE;Initialise(2);behaviour=1;Step();behaviour=0;Finish();CHECK(services.phase==UMICOM_BOOT_READY && services.records[0].info.state==UMICOM_SERVICE_FAILED && services.records[1].info.state==UMICOM_SERVICE_SUCCEEDED);}
    else if(!strcmp(name,"required-failure")){Initialise(2);behaviour=1;Step();CHECK(services.phase==UMICOM_BOOT_RECOVERY && !services.records[1].info.attempts);Finish();}
    else if(!strcmp(name,"transitive-skip")){plans[0].dependencies=2;plans[1].dependencies=4;plans[2].required=UMICOM_FALSE;Initialise(3);behaviour=1;Step();CHECK(services.records[0].info.state==UMICOM_SERVICE_SKIPPED && services.records[1].info.state==UMICOM_SERVICE_SKIPPED);Finish();}
    else if(!strcmp(name,"backoff")||!strcmp(name,"exact-retry-boundary")||!strcmp(name,"retry-new-identity")){
        plans[0].attempts=2;Initialise(1);behaviour=1;Step();CHECK(services.records[0].info.state==UMICOM_SERVICE_BACKOFF && entries==1);
        const UmicomU64 first=services.records[0].info.identity;behaviour=0;umicomModelNow=services.records[0].info.retryAt-1;Step();CHECK(entries==1);
        umicomModelNow++;Step();CHECK(entries==2 && services.records[0].info.identity>first);Finish();}
    else if(!strcmp(name,"snapshot-inputs")){Initialise(1);memset(copiedArgument,'x',14);memset(copiedName,'z',5);expectedLaunchChecks=1;Finish();CHECK(!strcmp(services.records[0].info.name,"first"));}
    else if(!strcmp(name,"output-before-collection")||!strcmp(name,"callback-reentry")){Initialise(1);behaviour=5;reentryChecks=!strcmp(name,"callback-reentry")?1U:0U;Finish();CHECK(collectedOutput==1);}
    else if(!strcmp(name,"malformed-image")){umicomModelElf[0]=0;plans[0].attempts=2;Initialise(1);Finish();CHECK(services.records[0].info.attempts==2 && services.records[0].info.reason==UMICOM_SERVICE_LOAD_ERROR && !entries);}
    else if(!strcmp(name,"deadline")){plans[0].timeoutTicks=1500;Initialise(1);behaviour=2;Step();Step();Finish();CHECK(services.records[0].info.reason==UMICOM_SERVICE_DEADLINE);}
    else if(!strcmp(name,"budget")){plans[0].sliceLimit=1;Initialise(1);behaviour=2;Finish();CHECK(services.records[0].info.reason==UMICOM_SERVICE_BUDGET);}
    else if(!strcmp(name,"entry-refusal")){Initialise(1);behaviour=4;CHECK(UmicomKernelBootServicesStep(&services,1000)==UMICOM_BOOT_SERVICE_ENTRY_REFUSED);CHECK(services.records[0].info.attempts==1 && services.records[0].handle);behaviour=0;Finish();CHECK(services.records[0].info.attempts==1);}
    else if(!strcmp(name,"shutdown-running")){Initialise(2);behaviour=2;Step();CHECK(UmicomKernelBootServicesClose(&services)==UMICOM_BOOT_SERVICE_OK);CHECK(services.phase==UMICOM_BOOT_STOPPED && !services.records[1].info.attempts);UmicomModelNoLeaks();}
    else if(!strcmp(name,"shutdown-backoff")){plans[0].attempts=4;Initialise(1);behaviour=1;Step();CHECK(UmicomKernelBootServicesClose(&services)==UMICOM_BOOT_SERVICE_OK);CHECK(services.records[0].info.attempts==1);UmicomModelNoLeaks();}
    else if(!strcmp(name,"clock-reversal")){Initialise(1);umicomModelNow=99;CHECK(UmicomKernelBootServicesStep(&services,1000)==UMICOM_BOOT_SERVICE_CLOCK_REVERSED);CHECK(services.phase==UMICOM_BOOT_UNSAFE && !entries);UmicomModelNoLeaks();}
    else if(!strcmp(name,"deadline-overflow")){umicomModelNow=~(UmicomU64)0U-50U;Initialise(1);Step();CHECK(!entries && services.phase==UMICOM_BOOT_RECOVERY);CHECK(UmicomKernelBootServicesClose(&services)==UMICOM_BOOT_SERVICE_OK);UmicomModelNoLeaks();}
    else if(!strcmp(name,"query-invalid")){Initialise(1);UmicomKernelBootServiceInfo info={0};CHECK(UmicomKernelBootServicesQuery(&services,8,&info)==UMICOM_BOOT_SERVICE_INVALID_ARGUMENT);Finish();CHECK(UmicomKernelBootServicesQuery(&services,0,&info)==UMICOM_BOOT_SERVICE_OK && info.state==UMICOM_SERVICE_SUCCEEDED);}
    else if(!strcmp(name,"cleanup-retry")||!strcmp(name,"cleanup-outcome-stable")){Initialise(1);releaseRefusals=1;CHECK(UmicomKernelBootServicesStep(&services,1000)==UMICOM_BOOT_SERVICE_CLEANUP_FAILED);CHECK(services.records[0].handle && entries==1 && services.records[0].info.state==UMICOM_SERVICE_RUNNING);
        if (!strcmp(name,"cleanup-outcome-stable")) {
            umicomModelNow = services.records[0].info.deadline + 1U;
        }
        Step();
        CHECK(services.records[0].info.state == UMICOM_SERVICE_SUCCEEDED && entries == 1U);
        Finish();
    }
    else if(!strcmp(name,"repeated-lifetimes")){for(unsigned i=0;i<200;++i){UmicomKernelBootServices *owner=calloc(1,sizeof(*owner));CHECK(owner);entries=0;
        CHECK(UmicomKernelBootServicesInitialize(owner,plans,1,NULL,NULL)==UMICOM_BOOT_SERVICE_OK);CHECK(UmicomKernelBootServicesStep(owner,1000)==UMICOM_BOOT_SERVICE_OK);
        CHECK(owner->phase==UMICOM_BOOT_READY && UmicomKernelBootServicesClose(owner)==UMICOM_BOOT_SERVICE_OK);free(owner);UmicomModelNoLeaks();}}
    else if (!strcmp(name, "program-fault")) {
        Initialise(1U);
        behaviour = 3U;
        Finish();
        CHECK(services.records[0].info.reason == UMICOM_SERVICE_PROGRAM_FAULT);
    }
    else if (!strcmp(name, "retained-load-retry") || !strcmp(name, "stop-retained-load")) {
        /* Leave only two pages available so the real loader must roll back.
         * Refuse its first two frees, including the unpublished-owner cleanup,
         * then permit the controller to resume that same cleanup on retry. */
        CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)umicomModelRam,
            sizeof(umicomModelRam) - 8192U) == UMICOM_KERNEL_MEMORY_OK);
        Initialise(1U);
        releaseRefusals = 2U;
        CHECK(UmicomKernelBootServicesStep(&services, 1000U) == UMICOM_BOOT_SERVICE_CLEANUP_FAILED);
        CHECK(services.records[0].retainedLoad && services.records[0].info.attempts == 1U);
        if (!strcmp(name, "stop-retained-load")) {
            CHECK(UmicomKernelBootServicesClose(&services) == UMICOM_BOOT_SERVICE_OK);
        } else {
            Step();
            CHECK(services.phase == UMICOM_BOOT_RECOVERY);
            Finish();
        }
        CHECK(!entries && services.records[0].info.attempts == 1U);
        UmicomModelNoLeaks();
    }
    else if (!strcmp(name, "independent-during-backoff")) {
        plans[0].attempts = 2U;
        plans[0].retryTicks = 1000U;
        Initialise(2U);
        behaviour = 1U;
        Step();
        CHECK(services.records[0].info.state == UMICOM_SERVICE_BACKOFF);
        behaviour = 0U;
        Step();
        CHECK(services.records[1].info.state == UMICOM_SERVICE_SUCCEEDED);
        CHECK(services.records[0].info.attempts == 1U);
        umicomModelNow = services.records[0].info.retryAt;
        Finish();
    }
    else if (!strcmp(name, "maximum-plan")) {
        Initialise(UMICOM_BOOT_SERVICE_LIMIT);
        Finish();
        CHECK(entries == UMICOM_BOOT_SERVICE_LIMIT);
        for (UmicomSize i = 0U; i < UMICOM_BOOT_SERVICE_LIMIT; ++i) {
            CHECK(services.records[i].info.state == UMICOM_SERVICE_SUCCEEDED);
        }
    }
    else InvalidPlan(name);
    printf("PASS %s (%u checks)\n",name,umicomModelChecks);return EXIT_SUCCESS;
}
