/*-----------------------------------------------------------------------------
 * Umicom Kernel structured-launch native tests
 * File: tests/program_launch/program_launch_tests.c
 *
 * Reuse the established synthetic ELF, allocator and CSR/timer model. The
 * actual launch packer, scheduler, supervisor, parser, file and stream owners
 * are compiled. Instruction execution is modelled, not claimed as a guest run.
 * The separately compiled argument program is also tested directly with a
 * host-rebased block and an explicit stream-output substitute.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#define main UmicomLaunchPreviousModelMain
#include "../user_scheduling/user_scheduling_tests.c"
#undef main
#include "umicom/kernel/program_launch.h"
#include "umicom/kernel/console_terminal.h"

#define CHECK(x) UmicomModelRequire((x) ? 1 : 0, #x)
#define PACK_BASE ((UmicomAddress)0x300000f800ULL)
static UmicomKernelLaunchString arguments[UMICOM_LAUNCH_ARGUMENT_LIMIT + 1U];
static UmicomKernelLaunchString environment[UMICOM_LAUNCH_ENVIRONMENT_LIMIT + 1U];
static UmicomKernelProgramLaunchSpec spec;
static UmicomKernelLaunchImage packed;
static UmicomKernelConsoleShell shell;
static UmicomKernelConsoleTerminal terminal;
static char captured[8192];
static UmicomSize capturedBytes;

UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    return umicomModelAllowed && !umicomModelHart && !umicomModelMachine.mie &&
        !(umicomModelMachine.mstatus & 0x20008U) && !umicomModelMachine.satp ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomU64 UmicomLaunchProgramMain(UmicomU64 argc, const char *const *argv,
    const char *const *envp, const UmicomProgramLaunchInfo *info);
UmicomU64 UmicomLaunchProgramWrite(UmicomAddress address, UmicomSize bytes, UmicomSize *outBytes)
{
    CHECK(bytes <= UMICOM_STREAM_TRANSFER_BYTES && bytes <= sizeof(captured) - capturedBytes);
    memcpy(captured + capturedBytes, (const void *)address, (size_t)bytes);
    capturedBytes += bytes;
    *outBytes = bytes;
    return UMICOM_STREAM_OK;
}
static void Capture(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    CHECK(bytes < sizeof(captured) - capturedBytes);
    memcpy(captured + capturedBytes, text, (size_t)bytes);
    capturedBytes += bytes;
    captured[capturedBytes] = 0;
}
static UmicomKernelLaunchString Span(const char *value)
{
    const UmicomKernelLaunchString result = {value, (UmicomSize)strlen(value)};
    return result;
}
static void Setup(void)
{
    UmicomModelSetup();
    memset(arguments, 0, sizeof(arguments)); memset(environment, 0, sizeof(environment));
    arguments[0] = Span("/bin/launch.elf"); arguments[1] = Span("Account one"); arguments[2] = Span("");
    environment[0] = Span("LANG=C"); environment[1] = Span("REPORT=ready=approved");
    spec = (UmicomKernelProgramLaunchSpec){arguments, 3U, environment, 2U};
    memset(&packed, 0xa5, sizeof(packed));
}
static void CheckPacked(UmicomAddress base)
{
    CHECK(packed.info.cookie == UMICOM_LAUNCH_COOKIE && packed.info.bytes == sizeof(packed));
    CHECK(packed.info.argumentCount == spec.argumentCount && packed.info.environmentCount == spec.environmentCount);
    CHECK(packed.info.arguments == base + __builtin_offsetof(UmicomKernelLaunchImage, arguments));
    CHECK(packed.info.environment == base + __builtin_offsetof(UmicomKernelLaunchImage, environment));
    CHECK(packed.info.text == base + __builtin_offsetof(UmicomKernelLaunchImage, text));
    for (UmicomSize i=0; i<spec.argumentCount; ++i) {
        const UmicomSize offset=packed.arguments[i]-packed.info.text;
        CHECK(offset < packed.info.textBytes);
        CHECK(strlen((char *)packed.text+offset) == spec.arguments[i].bytes);
        if (spec.arguments[i].bytes) CHECK(!memcmp(packed.text+offset,spec.arguments[i].data,(size_t)spec.arguments[i].bytes));
    }
    for (UmicomSize i=0; i<spec.environmentCount; ++i) {
        const UmicomSize offset=packed.environment[i]-packed.info.text;
        CHECK(offset < packed.info.textBytes);
        CHECK(!memcmp(packed.text+offset,spec.environment[i].data,(size_t)spec.environment[i].bytes));
    }
    for (UmicomSize i=spec.argumentCount; i<=UMICOM_LAUNCH_ARGUMENT_LIMIT; ++i) CHECK(!packed.arguments[i]);
    for (UmicomSize i=spec.environmentCount; i<=UMICOM_LAUNCH_ENVIRONMENT_LIMIT; ++i) CHECK(!packed.environment[i]);
    for (UmicomSize i=packed.info.textBytes; i<sizeof(packed.text); ++i) CHECK(!packed.text[i]);
    for (UmicomSize i=0; i<sizeof(packed.padding); ++i) CHECK(!packed.padding[i]);
}
static void ReadBlock(UmicomKernelUserTask *task, UmicomKernelLaunchImage *out)
{
    const UmicomAddress base=UMICOM_EXECUTABLE_STACK_TOP-UMICOM_LAUNCH_BLOCK_BYTES;
    for (UmicomSize i=0; i<sizeof(*out); i+=UMICOM_USER_COPY_LIMIT)
        CHECK(UmicomKernelUserMemoryRead(&task->process.report.memory, base+i,
            (UmicomU8 *)out+i, UMICOM_USER_COPY_LIMIT) == UMICOM_USER_RESULT_OK);
}
static void CancelReap(UmicomKernelUserScheduler *scheduler, UmicomKernelUserTaskHandle handle)
{
    CHECK(UmicomKernelUserTaskCancel(scheduler,handle) == UMICOM_USER_SCHEDULE_OK);
    CHECK(UmicomKernelUserTaskReap(scheduler,handle) == UMICOM_USER_SCHEDULE_OK);
}
static void TestPack(const char *name)
{
    Setup();
    UmicomKernelLaunchStatus expected=UMICOM_LAUNCH_OK;
    UmicomAddress base=PACK_BASE;
    const UmicomKernelProgramLaunchSpec *input=&spec;
    const char embedded[]={'A',0,'B'};
    char longValue[UMICOM_LAUNCH_STRING_LIMIT+2U]; memset(longValue,'x',sizeof(longValue));
    char names[UMICOM_LAUNCH_ENVIRONMENT_LIMIT][16];
    if (!strcmp(name,"null-spec")) { input=NULL; expected=UMICOM_LAUNCH_INVALID_ARGUMENT; }
    else if (!strcmp(name,"null-output")) { CHECK(UmicomKernelProgramLaunchPack(&spec,base,NULL)==UMICOM_LAUNCH_INVALID_ARGUMENT); return; }
    else if (!strcmp(name,"no-arguments")) {spec.argumentCount=0;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"null-arguments")) {spec.arguments=NULL;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"empty-program-name")) {arguments[0]=Span("");expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"null-string")) {arguments[1].data=NULL;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"empty-null-string")) {arguments[1]=(UmicomKernelLaunchString){NULL,0};}
    else if (!strcmp(name,"embedded-nul")) {arguments[1]=(UmicomKernelLaunchString){embedded,3};expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"long-string")) {arguments[1]=(UmicomKernelLaunchString){longValue,256};expected=UMICOM_LAUNCH_TOO_LARGE;}
    else if (!strcmp(name,"max-string")) arguments[1]=(UmicomKernelLaunchString){longValue,255};
    else if (!strcmp(name,"too-many-arguments")) {spec.argumentCount=17;expected=UMICOM_LAUNCH_TOO_LARGE;}
    else if (!strcmp(name,"maximum-arguments")) {spec.argumentCount=16;for(unsigned i=3;i<16;++i)arguments[i]=Span("item");}
    else if (!strcmp(name,"null-environment")) {spec.environment=NULL;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"no-environment")) {spec.environment=NULL;spec.environmentCount=0;}
    else if (!strcmp(name,"too-many-environment")) {spec.environmentCount=9;expected=UMICOM_LAUNCH_TOO_LARGE;}
    else if (!strcmp(name,"maximum-environment")) {spec.environmentCount=8;for(unsigned i=0;i<8;++i){snprintf(names[i],sizeof(names[i]),"KEY%u=value",i);environment[i]=Span(names[i]);}}
    else if (!strcmp(name,"duplicate-environment")) {environment[1]=Span("LANG=other");expected=UMICOM_LAUNCH_DUPLICATE_ENVIRONMENT;}
    else if (!strcmp(name,"case-sensitive-environment")) environment[1]=Span("lang=other");
    else if (!strcmp(name,"prefix-environment")) {environment[0]=Span("AB=value");environment[1]=Span("A=value");}
    else if (!strcmp(name,"bad-environment-name")) {environment[0]=Span("A-B=value");expected=UMICOM_LAUNCH_INVALID_ENVIRONMENT;}
    else if (!strcmp(name,"numeric-environment-name")) {environment[0]=Span("1KEY=value");expected=UMICOM_LAUNCH_INVALID_ENVIRONMENT;}
    else if (!strcmp(name,"empty-environment-name")) {environment[0]=Span("=value");expected=UMICOM_LAUNCH_INVALID_ENVIRONMENT;}
    else if (!strcmp(name,"missing-equals")) {environment[0]=Span("VALUE");expected=UMICOM_LAUNCH_INVALID_ENVIRONMENT;}
    else if (!strcmp(name,"empty-environment-value")) environment[0]=Span("KEY=");
    else if (!strcmp(name,"text-capacity") || !strcmp(name,"text-overflow")) {
        spec.argumentCount=6;spec.environmentCount=0;
        for(unsigned i=0;i<6;++i)arguments[i]=(UmicomKernelLaunchString){longValue,255};
        if(!strcmp(name,"text-overflow")){spec.argumentCount=7;arguments[6]=Span("");expected=UMICOM_LAUNCH_TOO_LARGE;}
    }
    else if (!strcmp(name,"misaligned-base")) {base++;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"zero-base")) {base=0;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"overflow-base")) {base=~(UmicomAddress)15U;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"noncanonical-base")) {base=(UmicomAddress)1U<<39U;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"canonical-boundary")) {base=((UmicomAddress)1U<<38U)-1024U;expected=UMICOM_LAUNCH_INVALID_ARGUMENT;}
    else if (!strcmp(name,"upper-canonical")) base=(UmicomAddress)0xffffffc000100000ULL;
    const UmicomKernelLaunchImage before=packed;
    CHECK(UmicomKernelProgramLaunchPack(input,base,&packed)==expected);
    if(expected==UMICOM_LAUNCH_OK)CheckPacked(base);else CHECK(!memcmp(&before,&packed,sizeof(packed)));
    UmicomModelNoLeaks();
}
static void TestTask(const char *name)
{
    Setup(); const UmicomKernelUserTaskHandle token=UmicomModelCreate(8U);
    UmicomKernelUserTask *task=UmicomModelRecord(token);
    UmicomKernelLaunchImage before,after; ReadBlock(task,&before);
    const UmicomRiscvTrapFrame frame=task->frame;
    UmicomKernelUserTaskHandle supplied=token;
    UmicomKernelUserScheduleStatus expected=UMICOM_USER_SCHEDULE_OK;
    if(!strcmp(name,"stale-task")){supplied^=(UmicomU64)1U<<32U;expected=UMICOM_USER_SCHEDULE_INVALID_HANDLE;}
    if(!strcmp(name,"invalid-plan-atomic")){spec.argumentCount=0;expected=UMICOM_USER_SCHEDULE_INVALID_ARGUMENT;}
    if(!strcmp(name,"unsafe-context")){umicomModelAllowed=UMICOM_FALSE;expected=UMICOM_USER_SCHEDULE_ENTRY_REFUSED;}
    if(!strcmp(name,"active-refused")){umicomModelScheduler.active=UMICOM_TRUE;expected=UMICOM_USER_SCHEDULE_BUSY;}
    UmicomKernelUserPage *page=NULL;UmicomAddress savedPhysical=0U;
    if(!strcmp(name,"wrong-backing")) {
        for(UmicomSize i=0;i<task->process.pageCount;++i)
            if(task->process.pages[i].virtualBase==UMICOM_EXECUTABLE_STACK_TOP-4096U) page=&task->process.pages[i];
        CHECK(page);savedPhysical=page->physicalBase;page->physicalBase+=4096U;
        expected=UMICOM_USER_SCHEDULE_INVALID_CONTEXT;
    }
    UmicomKernelPhysicalMemorySnapshot counts={0},countsAfter={0};
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&counts)==UMICOM_KERNEL_MEMORY_OK);
    CHECK(UmicomKernelUserTaskSetLaunch(&umicomModelScheduler,supplied,&spec)==expected);
    umicomModelAllowed=UMICOM_TRUE;umicomModelScheduler.active=UMICOM_FALSE;if(page)page->physicalBase=savedPhysical;
    ReadBlock(task,&after);
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&countsAfter)==UMICOM_KERNEL_MEMORY_OK);
    CHECK(counts.allocatedFrames==countsAfter.allocatedFrames);
    if(expected!=UMICOM_USER_SCHEDULE_OK) {
        CHECK(!memcmp(&before,&after,sizeof(before))&&!memcmp(&frame,&task->frame,sizeof(frame))&&!task->structuredLaunch);
    } else {
        const UmicomAddress base=UMICOM_EXECUTABLE_STACK_TOP-UMICOM_LAUNCH_BLOCK_BYTES;
        packed=after;CheckPacked(base);CHECK(task->frame.mepc==frame.mepc&&task->frame.mstatus==frame.mstatus);
        CHECK(task->frame.x2_sp==base&&!(base&15U)&&task->frame.x10_a0==3U&&task->frame.x13_a3==base);
        CHECK(task->frame.x11_a1==after.info.arguments&&task->frame.x12_a2==after.info.environment);
        CHECK(task->structuredLaunch);
        if(!strcmp(name,"input-snapshot")) {arguments[1]=Span("changed");ReadBlock(task,&before);CHECK(!memcmp(&before,&after,sizeof(after)));}
        if(!strcmp(name,"numeric-after-structured")) CHECK(UmicomKernelUserTaskSetArgument(&umicomModelScheduler,token,77U)==UMICOM_USER_SCHEDULE_BAD_STATE);
        if(!strcmp(name,"double-prepare")) CHECK(UmicomKernelUserTaskSetLaunch(&umicomModelScheduler,token,&spec)==UMICOM_USER_SCHEDULE_BAD_STATE);
        if(!strcmp(name,"paused-prepare")) {UmicomModelRun();CHECK(UmicomKernelUserTaskSetLaunch(&umicomModelScheduler,token,&spec)==UMICOM_USER_SCHEDULE_BAD_STATE);}
    }
    CancelReap(&umicomModelScheduler,token);UmicomModelNoLeaks();
}
static void TestRepeated(void)
{
    Setup();
    for(unsigned i=0;i<1000U;++i) {
        const UmicomKernelUserTaskHandle task=UmicomModelCreate(4U);
        CHECK(!UmicomModelRecord(task)->structuredLaunch);
        CHECK(UmicomKernelUserTaskSetLaunch(&umicomModelScheduler,task,&spec)==UMICOM_USER_SCHEDULE_OK);
        CancelReap(&umicomModelScheduler,task);
        CHECK(UmicomKernelUserTaskSetLaunch(&umicomModelScheduler,task,&spec)==UMICOM_USER_SCHEDULE_INVALID_HANDLE);
    }
    UmicomModelNoLeaks();
}
static void TestParser(const char *name)
{
    char text[UMICOM_SHELL_LINE_BYTES];UmicomSize offsets[UMICOM_SHELL_TOKEN_LIMIT],count=99U;
    memset(text,0xa5,sizeof(text));memset(offsets,0xa5,sizeof(offsets));
    const char *line="exec /bin/a a b c d e f g h i j k l m n o";
    UmicomKernelShellStatus expected=UMICOM_SHELL_OK;
    if(!strcmp(name,"tokenizer-overflow")){line="exec /bin/a a b c d e f g h i j k l m n o p";expected=UMICOM_SHELL_SYNTAX;}
    if(!strcmp(name,"tokenizer-quotes"))line="exec /bin/a \"Account one\" ''";
    if(!strcmp(name,"tokenizer-late-quote")){line="exec /bin/a a b c d \"late";expected=UMICOM_SHELL_SYNTAX;}
    CHECK(UmicomKernelShellTokenize(line,(UmicomSize)strlen(line),text,offsets,UMICOM_SHELL_TOKEN_LIMIT,&count)==expected);
    if(expected!=UMICOM_SHELL_OK){CHECK(count==99U&&(unsigned char)text[0]==0xa5U);return;}
    if(!strcmp(name,"tokenizer-quotes")){CHECK(count==4U&&!strcmp(text+offsets[2],"Account one")&&!text[offsets[3]]);}
    else CHECK(count==17U&&!strcmp(text+offsets[16],"o"));
    UmicomKernelShellCommand legacy;
    CHECK(UmicomKernelShellParse(line,(UmicomSize)strlen(line),&legacy)==(count>4U?UMICOM_SHELL_SYNTAX:UMICOM_SHELL_OK));
}
static void TestAuthority(const char *name)
{
    Setup();static UmicomKernelProcessSupervisor owner;
    CHECK(UmicomKernelProcessSupervisorInitialize(&owner)==UMICOM_SUPERVISION_OK);
    UmicomKernelSupervisedProcessHandle parent=0U,child=0U;
    CHECK(UmicomKernelProcessSupervisorSpawn(&owner,0U,umicomModelElf,sizeof(umicomModelElf),0,8U,UMICOM_CHILDREN_ADOPT,&parent)==UMICOM_SUPERVISION_OK);
    UmicomKernelSupervisedProcessInfo info={0};
    CHECK(UmicomKernelProcessSupervisorQuery(&owner,0U,parent,&info)==UMICOM_SUPERVISION_OK);
    CHECK(UmicomKernelProcessSupervisorSpawn(&owner,info.identity,umicomModelElf,sizeof(umicomModelElf),0,8U,UMICOM_CHILDREN_ADOPT,&child)==UMICOM_SUPERVISION_OK);
    if(!strcmp(name,"wrong-parent"))CHECK(UmicomKernelProcessSupervisorSetLaunch(&owner,999U,child,&spec)==UMICOM_SUPERVISION_WRONG_PARENT);
    CHECK(UmicomKernelProcessSupervisorSetLaunch(&owner,info.identity,child,&spec)==UMICOM_SUPERVISION_OK);
    CHECK(UmicomKernelProcessSupervisorBeginShutdown(&owner)==UMICOM_SUPERVISION_OK);
    UmicomKernelProcessCompletion result={0};
    CHECK(UmicomKernelProcessSupervisorCollect(&owner,0U,child,&result)==UMICOM_SUPERVISION_OK);
    CHECK(UmicomKernelProcessSupervisorCollect(&owner,0U,parent,&result)==UMICOM_SUPERVISION_OK);
    UmicomModelNoLeaks();
}
static void TestShell(const char *name)
{
    Setup();const UmicomKernelShellImage image={"/bin/a",umicomModelElf,sizeof(umicomModelElf)};
    CHECK(UmicomKernelConsoleShellInitialize(&shell,Capture,NULL,&image,1U)==UMICOM_SHELL_OK);
    CHECK(UmicomKernelConsoleTerminalAttach(&terminal,&shell)==UMICOM_SHELL_OK);
    const char *command=!strcmp(name,"shell-writable")?"execrw /bin/a one \"two three\" \"\"":"exec /bin/a one \"two three\" \"\"";
    if(!strcmp(name,"shell-invalid"))command="exec /bin/a \"unterminated";
    const UmicomKernelShellStatus status=UmicomKernelConsoleShellExecute(&shell,command,(UmicomSize)strlen(command));
    CHECK(!shell.launchSpec);
    if(!strcmp(name,"shell-invalid")){CHECK(status==UMICOM_SHELL_SYNTAX&&!shell.foreground);}
    else {
        CHECK(status==UMICOM_SHELL_OK&&shell.foreground);
        const UmicomSize slot=(UmicomU32)shell.foreground-1U;
        UmicomKernelUserTask *task=&shell.supervisor.scheduler.tasks[slot];
        ReadBlock(task,&packed);CHECK(packed.info.argumentCount==4U&&packed.info.environmentCount==2U);
        CHECK(!strcmp((char *)packed.text+packed.arguments[2]-packed.info.text,"two three"));
        CHECK(!strcmp((char *)packed.text+packed.environment[1]-packed.info.text,"UMICOM_CONSOLE=serial"));
        umicomModelMode=UMICOM_MODEL_EXIT;
        CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_OK&&!shell.foreground);
        CHECK(strstr(captured,"umicom> ")!=NULL);
    }
    CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_OK);UmicomModelNoLeaks();
}
static void TestProgram(const char *name)
{
    Setup();CHECK(UmicomKernelProgramLaunchPack(&spec,PACK_BASE,&packed)==UMICOM_LAUNCH_OK);
    /* Rebase only this host copy; this is not a RISC-V execution claim. */
    const UmicomAddress base=(UmicomAddress)&packed;
    for(UmicomSize i=0;i<spec.argumentCount;++i)packed.arguments[i]=base+(packed.arguments[i]-PACK_BASE);
    for(UmicomSize i=0;i<spec.environmentCount;++i)packed.environment[i]=base+(packed.environment[i]-PACK_BASE);
    packed.info.arguments=(UmicomAddress)packed.arguments;packed.info.environment=(UmicomAddress)packed.environment;
    packed.info.text=(UmicomAddress)packed.text;
    if(!strcmp(name,"program-bad-cookie"))packed.info.cookie=0U;
    if(!strcmp(name,"program-missing-sentinel"))packed.arguments[spec.argumentCount]=1U;
    const UmicomU64 result=UmicomLaunchProgramMain(!strcmp(name,"program-numeric")?0U:spec.argumentCount,
        (const char *const *)packed.arguments,(const char *const *)packed.environment,&packed.info);
    if(!strcmp(name,"program-actual-c")){CHECK(result==0U);captured[capturedBytes]=0;CHECK(strstr(captured,"argv[1]=<Account one>")&&strstr(captured,"argv[2]=<>")&&strstr(captured,"launch-context=valid"));}
    else CHECK(result!=0U);
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *name=argv[1];
    if(!strncmp(name,"task-",5U))TestTask(name+5U);
    else if(!strncmp(name,"tokenizer-",10U))TestParser(name);
    else if(!strncmp(name,"shell-",6U))TestShell(name);
    else if(!strncmp(name,"program-",8U))TestProgram(name);
    else if(!strcmp(name,"parent-authority")||!strcmp(name,"wrong-parent"))TestAuthority(name);
    else if(!strcmp(name,"repeated-lifetimes"))TestRepeated();
    else TestPack(name);
    printf("PASS %s (%u checks)\n",name,umicomModelChecks);return 0;
}
