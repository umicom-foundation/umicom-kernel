/*-----------------------------------------------------------------------------
 * Umicom Kernel managed-service native tests
 * File: tests/managed_services/service_manager_tests.c
 *
 * Use the established synthetic ELF and hardware model, but compile the actual
 * new controller, monitor hook and scheduler additions with their real lower
 * C services. No native test claims to run RISC-V instructions. The release
 * wrapper injects failure without replacing the allocator's normal behaviour.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#define UmicomPlatformTimerRead UmicomManagedPreviousTimerRead
#define main UmicomManagedPreviousModelMain
#define UmicomRiscvUserExecuteFrame UmicomManagedPreviousExecute
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#undef UmicomPlatformTimerRead
#include "umicom/kernel/service_manager.h"
#define CHECK(x) UmicomModelRequire((x) ? 1 : 0, #x)
#include "umicom/kernel/service_console.h"
/* Mirror the immutable carrier's start/end symbols with writable test storage.
 * Setup fills this region with the established synthetic ELF; the actual
 * adapter still receives a byte span and uses the real loader. This does not
 * execute or substitute host code for the RISC-V program's instructions. */
__asm__(".pushsection .bss\n.balign 16\n.global UmicomHealthExecutableStart\n"
        "UmicomHealthExecutableStart:\n.zero 16384\n.global UmicomHealthExecutableEnd\n"
        "UmicomHealthExecutableEnd:\n.popsection\n");
extern UmicomU8 UmicomHealthExecutableStart[16384U];
static UmicomKernelConsoleShell testShell;
static UmicomKernelConsoleShell otherShell;
static char consoleText[8192];
static UmicomSize consoleUsed;
static void ConsoleOutput(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    CHECK(consoleUsed + bytes < sizeof(consoleText));
    memcpy(consoleText + consoleUsed, text, (size_t)bytes);
    consoleUsed += bytes;
    consoleText[consoleUsed] = 0;
}
static UmicomKernelShellStatus Command(UmicomKernelConsoleShell *shell, const char *line)
{
    UmicomKernelShellCommand command = {0};
    UmicomBoolean handled = UMICOM_FALSE;
    CHECK(UmicomKernelShellParse(line, strlen(line), &command) == UMICOM_SHELL_OK);
    UmicomKernelShellStatus status = UmicomKernelServiceConsoleCommand(shell, &command, &handled);
    CHECK(handled == UMICOM_TRUE);
    return status;
}
static void ConsoleResetOutput(void)
{
    consoleUsed = 0U;
    consoleText[0] = 0;
}
static UmicomKernelServiceManager manager;
static UmicomKernelManagedServiceSpec plans[UMICOM_MANAGED_SERVICE_LIMIT + 1U];
static UmicomKernelLaunchString arguments[2];
static char sourceName[32] = "provider";
static char sourceArgument[32] = "snapshot-value";
static unsigned behaviour, peerBehaviour, entries, refusedFrees, outputCount;
static UmicomU64 clockStep;
static UmicomU64 trapProbeCount;
UmicomU64 UmicomPlatformTimerRead(void)
{
    /* Only the exact guest C-sequence test advances time on observation. Other
     * policy tests retain explicit deterministic clock control. */
    umicomModelNow += clockStep;
    return umicomModelNow;
}
void UmicomKernelConsoleWrite(const char *text) { fputs(text, stdout); }
void UmicomKernelConsoleWriteLine(const char *text) { puts(text); }
void UmicomKernelConsoleWriteUnsigned(UmicomU64 value) { printf("%llu", value); }
void UmicomPlatformFinishFailure(UmicomU32 code) { fprintf(stderr, "guest model failure: %u\n", code); exit(EXIT_FAILURE); }
void UmicomPlatformHalt(void) { exit(EXIT_FAILURE); }
void UmicomRiscvTriggerMachineEcall(void) { ++trapProbeCount; }
void UmicomRiscvTrapSnapshotRead(UmicomRiscvTrapSnapshot *out)
{
    memset(out, 0, sizeof(*out)); out->exceptionCount = trapProbeCount;
}
static unsigned verifySnapshot, copiedSession, reentry, corruptReturn;
static UmicomU64 lastResult, lastPc, oldPc;
static UmicomU64 enteredIdentities[256];

enum { HEALTHY, TIMER, EXITED, FAULTED, SILENT, BAD_READY, BAD_SEQUENCE, BAD_OPERATION,
    RESERVED_ARGUMENT, REFUSED_ENTRY, OUTPUT, LATE_REPORT, GUEST_SEQUENCE };
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    return umicomModelAllowed && !umicomModelHart && !umicomModelMachine.mie &&
        !(umicomModelMachine.mstatus & 0x20008U) && !umicomModelMachine.satp ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelMemoryStatus __real_UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress address);
UmicomKernelMemoryStatus __wrap_UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress address)
{
    if (refusedFrees) { --refusedFrees; return UMICOM_KERNEL_MEMORY_RESERVED_FRAME; }
    return __real_UmicomKernelPhysicalMemoryFreeFrame(address);
}
static void Capture(void *context, const char *name, const UmicomKernelStreamPacket *packet)
{
    (void)context;
    CHECK(name && packet->bytes == 4U && !memcmp(packet->data, "work", 4U));
    ++outputCount;
    CHECK(UmicomModelMemory().allocatedFrames != 0U);
    if (reentry) {
        UmicomKernelManagedServiceInfo info;
        CHECK(UmicomKernelServiceManagerStep(&manager, 1000U) == UMICOM_MANAGER_BUSY);
        CHECK(UmicomKernelServiceManagerClose(&manager) == UMICOM_MANAGER_BUSY);
        CHECK(UmicomKernelServiceManagerRestart(&manager, 0U) == UMICOM_MANAGER_BUSY);
        CHECK(UmicomKernelServiceManagerQuery(&manager, 0U, &info) == UMICOM_MANAGER_BUSY);
    }
}
UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    ++entries;
    if (entries <= 256U) enteredIdentities[entries - 1U] = session->identity;
    CHECK(request->rootTablePhysicalAddress == session->memory.space->rootTablePhysicalAddress);
    unsigned mode = peerBehaviour && session->identity == 2U ? peerBehaviour : behaviour;
    if (mode == REFUSED_ENTRY) return 1U;
    UmicomRiscvTrapFrame live = *frame;
    live.mstatus = (UmicomU64)2U << 32U; live.reserved = 0U;
    if (mode == GUEST_SEQUENCE) {
        /* Interpret only the diagnostic mode from its real packed argv. Retain
         * that choice in a modelled callee-saved register across checkpoints.
         * This models the payload; it does not execute the guest's Assembly. */
        if (live.x18_s2 == 0U) {
            UmicomU64 address = 0U; char text[16] = {0};
            CHECK(UmicomKernelUserMemoryRead(&session->memory, live.x11_a1 + 8U, (UmicomU8 *)&address, 8U) == UMICOM_USER_RESULT_OK);
            CHECK(UmicomKernelUserMemoryRead(&session->memory, address, (UmicomU8 *)text, 15U) == UMICOM_USER_RESULT_OK);
            live.x19_s3 = !strcmp(text, "silent") ? SILENT : (!strcmp(text, "unready") ? TIMER :
                (!strcmp(text, "fault") ? FAULTED : (!strcmp(text, "exit") ? EXITED : HEALTHY)));
        }
        mode = (unsigned)live.x19_s3;
        if (mode == FAULTED && !live.x18_s2) mode = HEALTHY;
    }
    if (verifySnapshot && live.x18_s2 == 0U) {
        UmicomU64 address = 0U; char text[15] = {0};
        CHECK(UmicomKernelUserMemoryRead(&session->memory, live.x11_a1 + 8U, (UmicomU8 *)&address, 8U) == UMICOM_USER_RESULT_OK);
        CHECK(UmicomKernelUserMemoryRead(&session->memory, address, (UmicomU8 *)text, 15U) == UMICOM_USER_RESULT_OK);
        CHECK(!strcmp(text, "snapshot-value"));
    }
    if (mode == TIMER || (mode == SILENT && live.x18_s2 != 0U)) {
        umicomModelNow = umicomModelCompare;
        live.mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
        CHECK(UmicomKernelUserTrapDispatch(session, &live) == 0U);
    } else if (mode == FAULTED) {
        live.mcause = 2U; CHECK(UmicomKernelUserTrapDispatch(session, &live) == 0U);
    } else if (mode == EXITED) {
        live.mcause = 8U; live.x17_a7 = UMICOM_USER_CALL_EXIT; live.x10_a0 = 0U;
        CHECK(UmicomKernelUserTrapDispatch(session, &live) == 0U);
    } else {
        if (mode == OUTPUT) {
            const UmicomU8 bytes[] = "work";
            CHECK(UmicomKernelUserMemoryWrite(&session->memory, 0x600000U, bytes, 4U) == UMICOM_USER_RESULT_OK);
            live.mcause = 8U; live.x17_a7 = UMICOM_USER_CALL_STREAM_WRITE;
            live.x10_a0 = UMICOM_STREAM_OUTPUT; live.x11_a1 = 0x600000U; live.x12_a2 = 4U; live.x13_a3 = 0U;
            CHECK(UmicomKernelUserTrapDispatch(session, &live) == 1U && live.x10_a0 == UMICOM_STREAM_OK);
        }
        live.mcause = 8U; live.x17_a7 = UMICOM_USER_CALL_SERVICE_REPORT;
        live.x10_a0 = live.x18_s2 ? UMICOM_SERVICE_REPORT_HEARTBEAT : UMICOM_SERVICE_REPORT_READY;
        live.x11_a1 = live.x18_s2 + 1U; live.x12_a2 = 0U; live.x13_a3 = 0U;
        if (mode == BAD_READY) live.x10_a0 = UMICOM_SERVICE_REPORT_READY;
        if (mode == BAD_SEQUENCE) live.x11_a1 += 1U;
        if (mode == BAD_OPERATION) live.x10_a0 = 99U;
        if (mode == RESERVED_ARGUMENT) live.x13_a3 = 1U;
        if (mode == LATE_REPORT) umicomModelNow += 10000U;
        oldPc = live.mepc;
        UmicomU64 result;
        if (copiedSession) {
            UmicomKernelUserSession copy = *session;
            result = UmicomKernelUserTrapDispatch(&copy, &live);
            CHECK(copy.rejectedCalls == session->rejectedCalls + 1U);
        } else result = UmicomKernelUserTrapDispatch(session, &live);
        lastResult = live.x10_a0; lastPc = live.mepc;
        if (result == 0U && session->stopReason == UMICOM_USER_STOP_NONE) {
            ++live.x18_s2; /* The model retains this per-process local across parks. */
        } else if (result == 1U) {
            /* A rejected report leaves the program running; model its next
             * timer interruption so the real scheduler can return to policy. */
            live.mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
            umicomModelNow = umicomModelCompare > umicomModelNow ? umicomModelCompare : umicomModelNow;
            CHECK(UmicomKernelUserTrapDispatch(session, &live) == 0U);
        }
    }
    if (corruptReturn) live.reserved = 1U;
    *frame = live;
    return 0U;
}
static void Setup(void)
{
    UmicomModelSetup();
    memcpy(UmicomHealthExecutableStart, umicomModelElf, sizeof(umicomModelElf));
    testShell.output = ConsoleOutput; otherShell.output = ConsoleOutput;
    arguments[0] = (UmicomKernelLaunchString){"/services/sample",16U};
    arguments[1] = (UmicomKernelLaunchString){sourceArgument,14U};
    for (UmicomSize i=0U;i<UMICOM_MANAGED_SERVICE_LIMIT+1U;++i) {
        plans[i] = (UmicomKernelManagedServiceSpec){sourceName, umicomModelElf, sizeof(umicomModelElf),
            {arguments,2U,0,0U},0U,2U,10000U,10000U,100U,100U,64U,UMICOM_TRUE};
    }
    plans[1].name="consumer";plans[2].name="independent";plans[3].name="fourth";
}
static void Init(UmicomSize count)
{
    CHECK(UmicomKernelServiceManagerInitialize(&manager,plans,count,Capture,0)==UMICOM_MANAGER_OK);
    CHECK(UmicomModelMemory().allocatedFrames==0U);
}
static void Step(void){CHECK(UmicomKernelServiceManagerStep(&manager,1000U)==UMICOM_MANAGER_OK);}
static void Close(void)
{
    CHECK(UmicomKernelServiceManagerClose(&manager)==UMICOM_MANAGER_OK);
    CHECK(UmicomKernelServiceManagerClose(&manager)==UMICOM_MANAGER_OK);
    UmicomModelNoLeaks();
}
static void Invalid(const char *name)
{
    UmicomSize count=1U;const UmicomKernelManagedServiceSpec *input=plans;
    UmicomKernelServiceManagerStatus status=UMICOM_MANAGER_INVALID_PLAN;
    if(!strcmp(name,"null-owner")){CHECK(UmicomKernelServiceManagerInitialize(0,plans,1U,0,0)==UMICOM_MANAGER_INVALID_ARGUMENT);return;}
    if(!strcmp(name,"null-plan")){input=0;status=UMICOM_MANAGER_INVALID_ARGUMENT;}
    else if(!strcmp(name,"zero-count")){count=0;status=UMICOM_MANAGER_INVALID_ARGUMENT;}
    else if(!strcmp(name,"excess-count")){count=UMICOM_MANAGED_SERVICE_LIMIT+1U;status=UMICOM_MANAGER_INVALID_ARGUMENT;}
    else if(!strcmp(name,"empty-name"))plans[0].name="";
    else if(!strcmp(name,"bad-name"))plans[0].name="bad\nname";
    else if(!strcmp(name,"duplicate-name")){count=2;plans[1].name=plans[0].name;}
    else if(!strcmp(name,"empty-image"))plans[0].imageBytes=0;
    else if(!strcmp(name,"null-image"))plans[0].image=0;
    else if(!strcmp(name,"large-image"))plans[0].imageBytes=UMICOM_EXECUTABLE_MAX_FILE_BYTES+1U;
    else if(!strcmp(name,"invalid-launch"))plans[0].launch.argumentCount=0;
    else if(!strcmp(name,"self-dependency"))plans[0].dependencies=1;
    else if(!strcmp(name,"unknown-dependency"))plans[0].dependencies=4;
    else if(!strcmp(name,"cycle")){count=2;plans[0].dependencies=2;plans[1].dependencies=1;}
    else if(!strcmp(name,"disconnected-cycle")){count=3;plans[1].dependencies=4;plans[2].dependencies=2;}
    else if(!strcmp(name,"zero-attempts"))plans[0].attempts=0;
    else if(!strcmp(name,"excess-attempts"))plans[0].attempts=5;
    else if(!strcmp(name,"zero-startup"))plans[0].startupTicks=0;
    else if(!strcmp(name,"excess-startup"))plans[0].startupTicks=UMICOM_MANAGED_SERVICE_TIME_LIMIT+1U;
    else if(!strcmp(name,"zero-health"))plans[0].healthTicks=0;
    else if(!strcmp(name,"excess-health"))plans[0].healthTicks=UMICOM_MANAGED_SERVICE_TIME_LIMIT+1U;
    else if(!strcmp(name,"zero-interval"))plans[0].reportTicks=0;
    else if(!strcmp(name,"interval-equals-health"))plans[0].reportTicks=plans[0].healthTicks;
    else if(!strcmp(name,"excess-retry"))plans[0].retryTicks=UMICOM_MANAGED_SERVICE_TIME_LIMIT+1U;
    else if(!strcmp(name,"zero-slices"))plans[0].sliceLimit=0;
    else if(!strcmp(name,"excess-slices"))plans[0].sliceLimit=UMICOM_USER_TASK_SLICE_LIMIT+1U;
    else if(!strcmp(name,"invalid-required"))plans[0].required=(UmicomBoolean)2;
    else CHECK(0);
    CHECK(UmicomKernelServiceManagerInitialize(&manager,input,count,0,0)==status);
    const UmicomU8 *p=(const UmicomU8 *)&manager;for(UmicomSize i=0;i<sizeof(manager);++i)CHECK(p[i]==0U);
    UmicomModelNoLeaks();
}
int main(int argc,char **argv)
{
    if(argc!=2)return EXIT_FAILURE;
    Setup();const char *name=argv[1];
    if(!strcmp(name,"ready-and-heartbeat")){Init(1);Step();CHECK(manager.phase==UMICOM_MANAGER_HEALTHY && manager.records[0].handle && manager.records[0].info.sequence==1U);
        umicomModelNow+=100U;Step();CHECK(manager.records[0].info.sequence==2U && entries==2U);Close();}
    else if(!strcmp(name,"park-no-spin")){Init(1);Step();const UmicomU64 dispatches=manager.supervisor.scheduler.dispatches;
        for(unsigned i=0;i<30;++i) { Step(); }
        CHECK(entries==1U && manager.supervisor.scheduler.dispatches==dispatches);Close();}
    else if(!strcmp(name,"continuation-once")){Init(1);Step();CHECK(lastPc==oldPc+4U);umicomModelNow+=100U;Step();CHECK(lastPc==oldPc+4U && manager.records[0].info.sequence==2U);Close();}
    else if(!strcmp(name,"dependent-waits-for-ready")){plans[1].dependencies=1U;Init(2);behaviour=TIMER;Step();CHECK(!manager.records[1].info.attempts);behaviour=HEALTHY;Step();Step();CHECK(manager.records[1].info.attempts==1U);Close();}
    else if(!strcmp(name,"restart-revokes-consumers")){plans[1].dependencies=1U;Init(2);Step();Step();const UmicomU64 old=manager.records[1].info.identity;
        CHECK(UmicomKernelServiceManagerRestart(&manager,0U)==UMICOM_MANAGER_OK);
        CHECK(manager.records[0].info.state==UMICOM_MANAGED_STOPPING && manager.records[1].info.state==UMICOM_MANAGED_STOPPING);
        Step();umicomModelNow+=100U;Step();Step();CHECK(manager.records[0].info.identity>old && manager.records[1].info.identity>old);Close();}
    else if(!strcmp(name,"new-identity")){Init(1);Step();const UmicomU64 old=manager.records[0].info.identity;CHECK(UmicomKernelServiceManagerRestart(&manager,0)==UMICOM_MANAGER_OK);
        Step();umicomModelNow+=100U;Step();CHECK(manager.records[0].info.identity>old && manager.records[0].info.sequence==1U);Close();}
    else if(!strcmp(name,"readiness-deadline")){plans[0].attempts=1;Init(1);behaviour=TIMER;Step();umicomModelNow=manager.records[0].info.deadline;Step();CHECK(manager.recovery && manager.records[0].info.reason==UMICOM_MANAGED_NOT_READY);Close();}
    else if(!strcmp(name,"health-deadline")){plans[0].attempts=1;Init(1);Step();umicomModelNow=manager.records[0].info.deadline;Step();CHECK(manager.recovery && manager.records[0].info.reason==UMICOM_MANAGED_HEALTH_EXPIRED && entries==1U);Close();}
    else if(!strcmp(name,"health-renewal")){Init(1);Step();const UmicomU64 old=manager.records[0].info.deadline;umicomModelNow+=100U;Step();CHECK(manager.records[0].info.deadline==old+100U);Close();}
    else if(!strcmp(name,"exit-zero-not-ready")){plans[0].attempts=1;Init(1);behaviour=EXITED;Step();CHECK(manager.recovery && manager.records[0].info.reason==UMICOM_MANAGED_EXITED);Close();}
    else if(!strcmp(name,"fault-retry")){Init(1);behaviour=FAULTED;Step();CHECK(manager.records[0].info.state==UMICOM_MANAGED_BACKOFF);behaviour=HEALTHY;umicomModelNow+=100U;Step();CHECK(manager.records[0].info.state==UMICOM_MANAGED_HEALTHY);Close();}
    else if(!strcmp(name,"optional-failure")){plans[0].attempts=1;plans[0].required=UMICOM_FALSE;Init(2);behaviour=EXITED;Step();behaviour=HEALTHY;Step();CHECK(!manager.recovery && manager.phase==UMICOM_MANAGER_DEGRADED && manager.records[1].info.state==UMICOM_MANAGED_HEALTHY);Close();}
    else if(!strcmp(name,"transitive-loss")){plans[1].dependencies=1;plans[2].dependencies=2;Init(3);Step();Step();Step();CHECK(manager.records[2].handle);CHECK(UmicomKernelServiceManagerRestart(&manager,0)==UMICOM_MANAGER_OK);CHECK(manager.records[2].info.state==UMICOM_MANAGED_STOPPING);Close();}
    else if(!strcmp(name,"cleanup-retry")){Init(1);Step();CHECK(UmicomKernelServiceManagerRestart(&manager,0)==UMICOM_MANAGER_OK);refusedFrees=1;CHECK(UmicomKernelServiceManagerStep(&manager,1000)==UMICOM_MANAGER_CLEANUP_FAILED);CHECK(manager.records[0].handle && manager.records[0].info.attempts==1);Step();CHECK(!manager.records[0].handle);umicomModelNow+=100;Step();CHECK(manager.records[0].info.attempts==2);Close();}
    else if(!strcmp(name,"close-retry")){Init(1);Step();refusedFrees=1;CHECK(UmicomKernelServiceManagerClose(&manager)==UMICOM_MANAGER_CLEANUP_FAILED);CHECK(manager.records[0].handle);Close();}
    else if(!strcmp(name,"shutdown-no-restart")){Init(2);Step();Close();CHECK(manager.records[0].info.attempts==1 && !manager.records[1].info.attempts);CHECK(UmicomKernelServiceManagerStep(&manager,1000)==UMICOM_MANAGER_BAD_STATE);}
    else if(!strcmp(name,"slice-budget")){plans[0].attempts=1;plans[0].sliceLimit=1;Init(1);Step();CHECK(manager.recovery && manager.records[0].info.reason==UMICOM_MANAGED_BUDGET);Close();}
    else if(!strcmp(name,"call-budget")){plans[0].attempts=1;Init(1);for(unsigned i=0;i<40 && !manager.recovery;++i){Step();umicomModelNow+=100U;}CHECK(manager.records[0].info.reason==UMICOM_MANAGED_BUDGET && manager.records[0].info.completion.systemCalls==UMICOM_USER_CALL_LIMIT+1U);Close();}
    else if(!strcmp(name,"bad-sequence")||!strcmp(name,"bad-operation")||!strcmp(name,"reserved-register")||!strcmp(name,"copy-session")){
        Init(1);behaviour=!strcmp(name,"bad-sequence")?BAD_SEQUENCE:(!strcmp(name,"bad-operation")?BAD_OPERATION:RESERVED_ARGUMENT);if(!strcmp(name,"copy-session")){behaviour=HEALTHY;copiedSession=1;}
        Step();CHECK(!manager.records[0].info.sequence);CHECK(lastResult==(!strcmp(name,"bad-sequence")?UMICOM_SERVICE_REPORT_BAD_SEQUENCE:(!strcmp(name,"copy-session")?UMICOM_SERVICE_REPORT_UNBOUND:UMICOM_SERVICE_REPORT_BAD_OPERATION)));Close();}
    else if(!strcmp(name,"duplicate-ready")){Init(1);Step();const UmicomU64 deadline=manager.records[0].info.deadline;umicomModelNow+=100;behaviour=BAD_READY;Step();CHECK(lastResult==UMICOM_SERVICE_REPORT_BAD_STATE && manager.records[0].info.deadline==deadline);Close();}
    else if(!strcmp(name,"late-report")){plans[0].attempts=1;Init(1);behaviour=LATE_REPORT;Step();CHECK(lastResult==UMICOM_SERVICE_REPORT_TOO_LATE && !manager.records[0].info.sequence);Close();}
    else if(!strcmp(name,"snapshot-plan")){Init(1);memset(sourceName,'z',8);memset(sourceArgument,'x',14);verifySnapshot=1;Step();CHECK(!strcmp(manager.records[0].info.name,"provider"));Close();}
    else if(!strcmp(name,"callback-reentry")||!strcmp(name,"accepted-output")){Init(1);behaviour=OUTPUT;reentry=1;Step();CHECK(outputCount==1U);Close();}
    else if(!strcmp(name,"entry-refusal")){Init(1);behaviour=REFUSED_ENTRY;CHECK(UmicomKernelServiceManagerStep(&manager,1000)==UMICOM_MANAGER_ENTRY_REFUSED);CHECK(manager.records[0].info.attempts==1U);behaviour=HEALTHY;Step();CHECK(manager.records[0].info.attempts==1U && manager.records[0].info.state==UMICOM_MANAGED_HEALTHY);Close();}
    else if(!strcmp(name,"unsafe-context")){Init(1);umicomModelAllowed=UMICOM_FALSE;CHECK(UmicomKernelServiceManagerStep(&manager,1000)==UMICOM_MANAGER_ENTRY_REFUSED && !entries);umicomModelAllowed=UMICOM_TRUE;Close();}
    else if(!strcmp(name,"invalid-quantum")){Init(1);CHECK(UmicomKernelServiceManagerStep(&manager,0)==UMICOM_MANAGER_INVALID_ARGUMENT);Close();}
    else if(!strcmp(name,"reinitialise")){Init(1);CHECK(UmicomKernelServiceManagerInitialize(&manager,plans,1,0,0)==UMICOM_MANAGER_BAD_STATE);Close();}
    else if(!strcmp(name,"copied-owner")){Init(1);UmicomKernelServiceManager *copy=malloc(sizeof(*copy));CHECK(copy);*copy=manager;CHECK(UmicomKernelServiceManagerStep(copy,1000)==UMICOM_MANAGER_BAD_STATE);free(copy);Close();}
    else if(!strcmp(name,"clock-reversal")){Init(1);umicomModelNow--;CHECK(UmicomKernelServiceManagerStep(&manager,1000)==UMICOM_MANAGER_CLOCK_REVERSED);CHECK(manager.phase==UMICOM_MANAGER_UNSAFE);UmicomModelNoLeaks();}
    else if(!strcmp(name,"overflow-deadline")){umicomModelNow=~(UmicomU64)0U-10U;plans[0].attempts=1;Init(1);Step();CHECK(manager.recovery && !entries);Close();}
    else if(!strcmp(name,"budgeted-restart")){plans[0].attempts=1;Init(1);Step();CHECK(UmicomKernelServiceManagerRestart(&manager,0)==UMICOM_MANAGER_ATTEMPTS_EXHAUSTED);CHECK(manager.records[0].info.state==UMICOM_MANAGED_HEALTHY);Close();}
    else if(!strcmp(name,"out-of-order-plan")){plans[0].dependencies=2U;Init(2);Step();CHECK(!manager.records[0].handle && manager.records[1].info.state==UMICOM_MANAGED_HEALTHY);Step();CHECK(manager.phase==UMICOM_MANAGER_HEALTHY);Close();}
    else if(!strcmp(name,"maximum-plan")){Init(4);for(unsigned i=0;i<4;++i)Step();CHECK(manager.phase==UMICOM_MANAGER_HEALTHY);Close();}
    else if(!strcmp(name,"malformed-image")){plans[0].attempts=1;umicomModelElf[0]=0;Init(1);Step();CHECK(manager.recovery && !entries && manager.records[0].info.reason==UMICOM_MANAGED_LOAD_FAILURE);Close();}
    else if(!strcmp(name,"stale-session-unbound")){Init(1);Step();UmicomKernelUserSession session=manager.supervisor.scheduler.tasks[0].process.report;UmicomRiscvTrapFrame frame=manager.supervisor.scheduler.tasks[0].frame;
        frame.x10_a0=UMICOM_SERVICE_REPORT_HEARTBEAT;frame.x11_a1=2;CHECK(UmicomKernelServiceReportDispatch(&session,&frame,frame.mepc+4)==1U && frame.x10_a0==UMICOM_SERVICE_REPORT_UNBOUND);Close();}
    else if(!strcmp(name,"corrupt-return")){Init(1);corruptReturn=1;CHECK(UmicomKernelServiceManagerStep(&manager,1000)==UMICOM_MANAGER_STATE_UNSAFE);CHECK(!manager.records[0].info.sequence && manager.records[0].handle && UmicomModelMemory().allocatedFrames);}
    else if(!strcmp(name,"repeated-lifetimes")){for(unsigned i=0;i<200;++i){UmicomKernelServiceManager *m=calloc(1,sizeof(*m));CHECK(m);CHECK(UmicomKernelServiceManagerInitialize(m,plans,1,0,0)==UMICOM_MANAGER_OK);CHECK(UmicomKernelServiceManagerStep(m,1000)==UMICOM_MANAGER_OK);CHECK(UmicomKernelServiceManagerClose(m)==UMICOM_MANAGER_OK);free(m);UmicomModelNoLeaks();}}
    else if (!strcmp(name, "guest-controller-sequence")) {
        behaviour = GUEST_SEQUENCE; clockStep = 500U;
        UmicomKernelManagedServicesValidateExecution(); UmicomModelNoLeaks();
    }
    else if (!strcmp(name, "console-not-started")) {
        CHECK(Command(&testShell, "daemons status") == UMICOM_SHELL_OK);
        CHECK(strstr(consoleText, "No managed service plan")); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-invalid-mode")) {
        CHECK(Command(&testShell, "daemons start arbitrary") == UMICOM_SHELL_BAD_STATE);
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsoleClose(&testShell) == UMICOM_SHELL_OK); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-live-reports")) {
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        ConsoleResetOutput(); CHECK(Command(&testShell, "daemons status") == UMICOM_SHELL_OK);
        CHECK(strstr(consoleText, "clock-sample: healthy") && strstr(consoleText, "consumer-sample: healthy"));
        CHECK(UmicomKernelServiceConsoleClose(&testShell) == UMICOM_SHELL_OK); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-restart")) {
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        CHECK(Command(&testShell, "daemons restart 0") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        umicomModelNow += 10000000U;
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        ConsoleResetOutput(); CHECK(Command(&testShell, "daemons status") == UMICOM_SHELL_OK);
        CHECK(strstr(consoleText, "identity=3") && strstr(consoleText, "identity=4"));
        CHECK(UmicomKernelServiceConsoleClose(&testShell) == UMICOM_SHELL_OK); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-stop-retains-report")) {
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        CHECK(Command(&testShell, "daemons stop") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        ConsoleResetOutput(); CHECK(Command(&testShell, "daemons status") == UMICOM_SHELL_OK);
        CHECK(strstr(consoleText, "stopped") && strstr(consoleText, "attempts=1"));
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_BAD_STATE); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-domain-selection")) {
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&otherShell) == UMICOM_SHELL_OK && entries == 0U);
        CHECK(Command(&otherShell, "daemons restart 0") == UMICOM_SHELL_OK && entries == 0U);
        CHECK(UmicomKernelServiceConsoleClose(&otherShell) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK && entries == 1U);
        CHECK(UmicomKernelServiceConsoleClose(&testShell) == UMICOM_SHELL_OK); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-cleanup-retry")) {
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        refusedFrees = 1U;
        CHECK(Command(&testShell, "daemons stop") == UMICOM_SHELL_CLEANUP_FAILED);
        const unsigned before = entries;
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK && entries == before);
        CHECK(Command(&testShell, "daemons stop") == UMICOM_SHELL_OK); UmicomModelNoLeaks();
    } else if (!strcmp(name, "console-restart-input-refusal")) {
        CHECK(Command(&testShell, "daemons start") == UMICOM_SHELL_OK);
        CHECK(UmicomKernelServiceConsolePoll(&testShell) == UMICOM_SHELL_OK);
        CHECK(Command(&testShell, "daemons restart 00") == UMICOM_SHELL_BAD_STATE);
        CHECK(Command(&testShell, "daemons restart 3") == UMICOM_SHELL_BAD_STATE);
        CHECK(Command(&testShell, "daemons restart -1") == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelServiceConsoleClose(&testShell) == UMICOM_SHELL_OK); UmicomModelNoLeaks();
    } else Invalid(name);
    printf("PASS %s (%u checks)\n",name,umicomModelChecks);return EXIT_SUCCESS;
}
