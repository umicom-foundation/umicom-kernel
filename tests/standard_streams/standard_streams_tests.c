/*-----------------------------------------------------------------------------
 * Umicom Kernel standard-stream boundary and terminal tests
 * File: tests/standard_streams/standard_streams_tests.c
 *
 * These tests reuse the earlier synthetic ELF, real host-backed frame allocator
 * and explicit CSR/timer model. The actual scheduler, syscall policy, copies,
 * queues, editor and command engine are linked. Only instruction execution is
 * substituted. Model ECALLs are not evidence of RISC-V Assembly execution.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#define main UmicomStreamPreviousMain
#define UmicomRiscvUserExecuteFrame UmicomStreamPreviousEntry
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#include "umicom/kernel/user_streams.h"
#include "umicom/kernel/console_terminal.h"

#define CHECK(x) UmicomModelRequire((x) ? 1 : 0, #x)
#define STREAM(x) CHECK((x) == UMICOM_STREAM_OK)
#define DATA ((UmicomAddress)0x600ff0U)
#define STREAM_MODEL_CALL 100U
static UmicomKernelUserStreams streams;
static UmicomBoolean insideTrap;
static UmicomBoolean unsafeContext;
static UmicomBoolean copiedSession;
static UmicomBoolean corruptReturn;
static UmicomU64 callNumber = UMICOM_USER_CALL_STREAM_READ;
static UmicomU64 callSelector;
static UmicomAddress callAddress = DATA;
static UmicomSize callBytes = 32U;
static UmicomU64 callFlags;
static UmicomKernelConsoleShell shell;
static UmicomKernelConsoleTerminal terminal;
static char transcript[32768U];
static UmicomSize transcriptBytes;

UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    /* No filesystem/cache allocation becomes legal inside our model trap. The
     * stream path needs only its preallocated storage and checked user copies. */
    return !insideTrap && !unsafeContext && umicomModelAllowed ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    if (umicomModelMode != STREAM_MODEL_CALL) return UmicomStreamPreviousEntry(request, session, frame);
    ++umicomModelEntries;
    CHECK(request->rootTablePhysicalAddress == session->memory.space->rootTablePhysicalAddress);
    frame->mstatus = (UmicomU64)2U << 32U;
    frame->reserved = 0U; frame->mcause = 8U; frame->mtval = 0U;
    frame->x17_a7 = callNumber; frame->x10_a0 = callSelector;
    frame->x11_a1 = callAddress; frame->x12_a2 = callBytes; frame->x13_a3 = callFlags;
    UmicomKernelUserSession fake = *session;
    insideTrap = UMICOM_TRUE;
    const UmicomU64 resumed = UmicomKernelUserTrapDispatch(copiedSession ? &fake : session, frame);
    insideTrap = UMICOM_FALSE;
    if (resumed) {
        /* An immediate result resumes user code. We model a subsequent timer,
         * retaining both result registers so their contract can be inspected. */
        umicomModelNow = umicomModelCompare;
        frame->mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
        CHECK(UmicomKernelUserTrapDispatch(session, frame) == 0U);
    }
    if (corruptReturn) umicomModelMachine.mtvec ^= 16U;
    return 0U;
}
static void Put(UmicomKernelUserTask *task, UmicomAddress address, const void *data, UmicomSize bytes)
{
    for (UmicomSize done = 0U; done < bytes;) {
        const UmicomSize chunk = bytes-done < 64U ? bytes-done : 64U;
        CHECK(UmicomKernelUserMemoryWrite(&task->process.report.memory, address+done,
            (const UmicomU8 *)data+done, chunk) == UMICOM_USER_RESULT_OK);
        done += chunk;
    }
}
static void Get(UmicomKernelUserTask *task, UmicomAddress address, void *data, UmicomSize bytes)
{
    for (UmicomSize done = 0U; done < bytes;) {
        const UmicomSize chunk = bytes-done < 64U ? bytes-done : 64U;
        CHECK(UmicomKernelUserMemoryRead(&task->process.report.memory, address+done,
            (UmicomU8 *)data+done, chunk) == UMICOM_USER_RESULT_OK);
        done += chunk;
    }
}
static void Setup(void)
{
    UmicomModelSetup();
    UmicomModelPut(64U+2U*56U+40U, 8192U, 8U); /* Two real writable pages. */
    STREAM(UmicomKernelUserStreamsAttach(&streams, &umicomModelScheduler));
}
static UmicomKernelUserTaskHandle Task(UmicomU64 budget)
{
    const UmicomKernelUserTaskHandle task = UmicomModelCreate(budget);
    STREAM(UmicomKernelUserStreamsGrant(&streams, task));
    return task;
}
static UmicomKernelStreamInfo Info(UmicomKernelUserTaskHandle task)
{
    UmicomKernelStreamInfo info = {0};
    STREAM(UmicomKernelUserStreamsQuery(&streams, task, &info));
    return info;
}
static UmicomU64 Call(UmicomKernelUserTaskHandle task, UmicomU64 number, UmicomU64 selector,
    UmicomAddress address, UmicomSize bytes, UmicomU64 flags)
{
    callNumber = number; callSelector = selector; callAddress = address; callBytes = bytes; callFlags = flags;
    const UmicomAddress pc = UmicomModelInfo(task).resumePc;
    const UmicomU64 calls = UmicomModelInfo(task).systemCalls;
    umicomModelMode = STREAM_MODEL_CALL;
    umicomModelScheduler.next = (UmicomU32)task-1U;
    CHECK(UmicomModelRun() == task);
    CHECK(UmicomModelInfo(task).resumePc == pc+4U);
    CHECK(UmicomModelInfo(task).systemCalls == calls+(copiedSession ? 0U : 1U));
    return UmicomModelRecord(task)->frame.x10_a0;
}
static UmicomU64 Read(UmicomKernelUserTaskHandle task, UmicomSize bytes, UmicomU64 flags)
{
    return Call(task, UMICOM_USER_CALL_STREAM_READ, UMICOM_STREAM_INPUT, DATA, bytes, flags);
}
static UmicomU64 Write(UmicomKernelUserTaskHandle task, UmicomSize bytes, UmicomU64 flags)
{
    return Call(task, UMICOM_USER_CALL_STREAM_WRITE, UMICOM_STREAM_OUTPUT, DATA, bytes, flags);
}
static void Idle(void)
{
    const UmicomU64 dispatches = umicomModelScheduler.dispatches;
    UmicomKernelUserTaskHandle output = 0xeeeeU;
    CHECK(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler, 1000U, &output) == UMICOM_USER_SCHEDULE_IDLE);
    CHECK(output == 0xeeeeU && umicomModelScheduler.dispatches == dispatches);
}
static UmicomKernelStreamPacket Drain(UmicomKernelUserTaskHandle task)
{
    UmicomKernelStreamPacket packet = {0};
    STREAM(UmicomKernelUserStreamsDrain(&streams, task, &packet));
    return packet;
}
static void End(UmicomKernelUserTaskHandle token, unsigned mode)
{
    umicomModelMode = mode;
    umicomModelScheduler.next = (UmicomU32)token-1U;
    CHECK(UmicomModelRun() == token);
}
static void Finish(void)
{
    for (UmicomSize slot = 0U; slot < UMICOM_USER_TASK_LIMIT; ++slot) {
        UmicomKernelUserTask *task = &umicomModelScheduler.tasks[slot];
        if (task->state == UMICOM_USER_TASK_EMPTY) continue;
        const UmicomKernelUserTaskHandle token = ((UmicomU64)task->generation<<32U)|(slot+1U);
        if (task->state == UMICOM_USER_TASK_READY || task->state == UMICOM_USER_TASK_PAUSED || task->state == UMICOM_USER_TASK_BLOCKED)
            CHECK(UmicomKernelUserTaskCancel(&umicomModelScheduler, token) == UMICOM_USER_SCHEDULE_OK);
        if (streams.records[slot].task) STREAM(UmicomKernelUserStreamsDiscard(&streams, token));
        CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler, token) == UMICOM_USER_SCHEDULE_OK);
    }
    CHECK(UmicomKernelUserSchedulerValidate(&umicomModelScheduler) == UMICOM_USER_SCHEDULE_OK);
    STREAM(UmicomKernelUserStreamsClose(&streams));
    STREAM(UmicomKernelUserStreamsClose(&streams));
    CHECK(!umicomModelScheduler.streams);
    UmicomModelNoLeaks();
}
static void FillOutput(UmicomKernelUserTaskHandle task)
{
    const UmicomU8 text[32] = "Original output chunk";
    Put(UmicomModelRecord(task), DATA, text, sizeof(text));
    for (unsigned i = 0U; i < UMICOM_STREAM_OUTPUT_RECORDS; ++i) CHECK(Write(task,32U,0U) == UMICOM_STREAM_OK);
    CHECK(Info(task).outputRecords == UMICOM_STREAM_OUTPUT_RECORDS);
}
static void TestOwnership(const char *name)
{
    if (!strcmp(name,"attach-null")) {
        CHECK(UmicomKernelUserStreamsAttach(NULL,&umicomModelScheduler) == UMICOM_STREAM_INVALID_ARGUMENT);
        CHECK(UmicomKernelUserStreamsAttach(&streams,NULL) == UMICOM_STREAM_INVALID_ARGUMENT);
        return;
    }
    if (!strcmp(name,"attach-dirty") || !strcmp(name,"attach-late")) {
        UmicomModelSetup();
        if (!strcmp(name,"attach-dirty")) streams.closed = UMICOM_TRUE;
        UmicomKernelUserTaskHandle admitted = 0U;
        if (!strcmp(name,"attach-late")) admitted = UmicomModelCreate(4U);
        CHECK(UmicomKernelUserStreamsAttach(&streams,&umicomModelScheduler) == UMICOM_STREAM_BAD_STATE);
        CHECK(!umicomModelScheduler.streams);
        if (admitted) {
            CHECK(UmicomKernelUserTaskCancel(&umicomModelScheduler,admitted) == UMICOM_USER_SCHEDULE_OK);
            CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,admitted) == UMICOM_USER_SCHEDULE_OK);
        }
        UmicomModelNoLeaks();
        return; /* This refusal-only model has no published stream owner. */
    }
    Setup();
    if (!strcmp(name,"attach-twice")) {
        static UmicomKernelUserStreams other;
        CHECK(UmicomKernelUserStreamsAttach(&streams,&umicomModelScheduler) == UMICOM_STREAM_BAD_STATE);
        CHECK(UmicomKernelUserStreamsAttach(&other,&umicomModelScheduler) == UMICOM_STREAM_BAD_STATE);
    } else if (!strcmp(name,"copied-owner")) {
        static UmicomKernelUserStreams other;
        other = streams;
        CHECK(UmicomKernelUserStreamsClose(&other) == UMICOM_STREAM_BAD_STATE);
    } else if (!strcmp(name,"grant-invalid")) {
        CHECK(UmicomKernelUserStreamsGrant(&streams,0U) == UMICOM_STREAM_INVALID_HANDLE);
        CHECK(UmicomKernelUserStreamsGrant(&streams,0x100000008ULL) == UMICOM_STREAM_INVALID_HANDLE);
    } else if (!strcmp(name,"grant-twice")) {
        const UmicomKernelUserTaskHandle token = Task(16U);
        CHECK(UmicomKernelUserStreamsGrant(&streams,token) == UMICOM_STREAM_BAD_STATE);
    } else if (!strcmp(name,"grant-late")) {
        const UmicomKernelUserTaskHandle token = UmicomModelCreate(16U);
        End(token,UMICOM_MODEL_TIMER);
        CHECK(UmicomKernelUserStreamsGrant(&streams,token) == UMICOM_STREAM_BAD_STATE);
    } else if (!strcmp(name,"close-live")) {
        (void)Task(16U);
        CHECK(UmicomKernelUserStreamsClose(&streams) == UMICOM_STREAM_BAD_STATE);
    } else if (!strcmp(name,"context-refusal")) {
        const UmicomKernelUserTaskHandle token = Task(16U);
        unsafeContext = UMICOM_TRUE;
        CHECK(UmicomKernelUserStreamsEndInput(&streams,token) == UMICOM_STREAM_BAD_STATE);
        UmicomKernelUserTaskHandle out=0U;
        CHECK(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler,1000U,&out) == UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR);
        unsafeContext = UMICOM_FALSE;
        CHECK(UmicomModelInfo(token).slices == 0U);
    } else if (!strcmp(name,"refused-entry")) {
        const UmicomKernelUserTaskHandle token = Task(16U);
        umicomModelMode = UMICOM_MODEL_REFUSE;
        UmicomKernelUserTaskHandle out=0U;
        CHECK(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler,1000U,&out) == UMICOM_USER_SCHEDULE_ENTRY_REFUSED);
        CHECK(UmicomModelInfo(token).slices == 0U);
        End(token,UMICOM_MODEL_EXIT); /* Exact service binding must have been removed. */
    } else if (!strcmp(name,"attach-lifetime")) {
        (void)Task(16U);
    } else { CHECK(0); }
    Finish();
}
static void TestCalls(const char *name)
{
    Setup();
    const UmicomKernelUserTaskHandle task = !strcmp(name,"no-grant") ? UmicomModelCreate(128U) : Task(128U);
    UmicomU8 bytes[256];
    for (UmicomSize i=0U;i<sizeof(bytes);++i) bytes[i]=(UmicomU8)i;
    Put(UmicomModelRecord(task),DATA,bytes,sizeof(bytes));
    if (!strcmp(name,"no-grant")) CHECK(Read(task,32U,UMICOM_STREAM_NONBLOCK)==UMICOM_STREAM_UNBOUND);
    else if (!strcmp(name,"exact-session")) {
        copiedSession=UMICOM_TRUE;
        CHECK(Read(task,32U,UMICOM_STREAM_NONBLOCK)==UMICOM_STREAM_UNBOUND);
        copiedSession=UMICOM_FALSE;
        CHECK(Info(task).wait==UMICOM_STREAM_WAIT_NONE);
    } else if (!strcmp(name,"wrong-direction")) {
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_READ,UMICOM_STREAM_OUTPUT,DATA,32U,0U)==UMICOM_STREAM_WRONG_DIRECTION);
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_WRITE,UMICOM_STREAM_INPUT,DATA,32U,0U)==UMICOM_STREAM_WRONG_DIRECTION);
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_WRITE,99U,DATA,32U,0U)==UMICOM_STREAM_WRONG_DIRECTION);
    } else if (!strcmp(name,"flags")) CHECK(Read(task,32U,2U)==UMICOM_STREAM_INVALID_ARGUMENT);
    else if (!strcmp(name,"zero-transfer")) {
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_READ,0U,0U,0U,0U)==UMICOM_STREAM_OK);
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_WRITE,1U,0U,0U,0U)==UMICOM_STREAM_OK);
        CHECK(!Info(task).outputRecords && Info(task).wait==UMICOM_STREAM_WAIT_NONE);
    } else if (!strcmp(name,"excessive-transfer")) {
        CHECK(Read(task,257U,0U)==UMICOM_STREAM_TOO_LARGE);
        CHECK(Write(task,257U,0U)==UMICOM_STREAM_TOO_LARGE);
    } else if (!strcmp(name,"invalid-input-buffer")) {
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_READ,0U,~(UmicomAddress)0U-7U,32U,0U)==UMICOM_STREAM_BAD_BUFFER);
    } else if (!strcmp(name,"invalid-output-buffer")) {
        CHECK(Call(task,UMICOM_USER_CALL_STREAM_WRITE,1U,0U,32U,0U)==UMICOM_STREAM_BAD_BUFFER);
    } else if (!strcmp(name,"cross-page-input")) {
        STREAM(UmicomKernelUserStreamsInput(&streams,task,bytes,sizeof(bytes)));
        CHECK(Read(task,256U,0U)==UMICOM_STREAM_OK);
        UmicomU8 result[256]; Get(UmicomModelRecord(task),DATA,result,sizeof(result));
        CHECK(!memcmp(result,bytes,sizeof(bytes)) && UmicomModelRecord(task)->frame.x11_a1==256U);
    } else if (!strcmp(name,"cross-page-output")) {
        CHECK(Write(task,256U,0U)==UMICOM_STREAM_OK);
        const UmicomKernelStreamPacket p=Drain(task);
        CHECK(p.bytes==256U && !memcmp(p.data,bytes,256U));
    } else if (!strcmp(name,"invalid-second-input-page") || !strcmp(name,"invalid-second-output-page")) {
        UmicomU8 incoming[32]; memset(incoming,0xa5,sizeof(incoming));
        /* Distinct source/destination bytes reveal an unwanted partial write. */
        STREAM(UmicomKernelUserStreamsInput(&streams,task,incoming,sizeof(incoming)));
        CHECK(UmicomKernelVirtualMemoryUnmapPage(&UmicomModelRecord(task)->process.space,0x601000U)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
        CHECK((!strcmp(name,"invalid-second-input-page") ? Read(task,32U,0U) : Write(task,32U,0U))==UMICOM_STREAM_BAD_BUFFER);
        UmicomU8 result[16]; Get(UmicomModelRecord(task),DATA,result,sizeof(result));
        CHECK(!memcmp(result,bytes,sizeof(result)) && Info(task).inputBytes==32U && !Info(task).outputRecords);
    } else if (!strcmp(name,"nonblocking-empty")) {
        CHECK(Read(task,32U,UMICOM_STREAM_NONBLOCK)==UMICOM_STREAM_WOULD_BLOCK);
        CHECK(Info(task).wait==UMICOM_STREAM_WAIT_NONE);
    } else if (!strcmp(name,"nonblocking-full")) {
        FillOutput(task);
        CHECK(Write(task,32U,UMICOM_STREAM_NONBLOCK)==UMICOM_STREAM_WOULD_BLOCK);
        CHECK(Info(task).wait==UMICOM_STREAM_WAIT_NONE && Info(task).outputRecords==8U);
    } else if (!strcmp(name,"input-copy-ownership")) {
        STREAM(UmicomKernelUserStreamsInput(&streams,task,bytes,32U)); memset(bytes,0xee,32U);
        CHECK(Read(task,32U,0U)==UMICOM_STREAM_OK); Get(UmicomModelRecord(task),DATA,bytes,32U);
        for(unsigned i=0U;i<32U;++i) CHECK(bytes[i]==i);
    } else if (!strcmp(name,"output-copy-ownership")) {
        CHECK(Write(task,32U,0U)==UMICOM_STREAM_OK); memset(bytes,0xee,32U); Put(UmicomModelRecord(task),DATA,bytes,32U);
        const UmicomKernelStreamPacket p=Drain(task);
        for(unsigned i=0U;i<32U;++i) CHECK(p.data[i]==i);
    } else if (!strcmp(name,"input-ring-wrap")) {
        UmicomU8 all[1024]; for(unsigned i=0U;i<1024U;++i) all[i]=(UmicomU8)i;
        STREAM(UmicomKernelUserStreamsInput(&streams,task,all,1000U));
        for(unsigned i=0U;i<3U;++i) CHECK(Read(task,256U,0U)==UMICOM_STREAM_OK);
        STREAM(UmicomKernelUserStreamsInput(&streams,task,all,700U));
        CHECK(Info(task).inputBytes==932U);
        CHECK(Read(task,256U,0U)==UMICOM_STREAM_OK);
        Get(UmicomModelRecord(task),DATA,bytes,256U);
        for(unsigned i=0U;i<232U;++i) CHECK(bytes[i]==(UmicomU8)(768U+i));
        for(unsigned i=232U;i<256U;++i) CHECK(bytes[i]==(UmicomU8)(i-232U));
    } else if (!strcmp(name,"input-capacity")) {
        UmicomU8 all[1024]; memset(all,'a',sizeof(all));
        STREAM(UmicomKernelUserStreamsInput(&streams,task,all,sizeof(all)));
        CHECK(UmicomKernelUserStreamsInput(&streams,task,bytes,1U)==UMICOM_STREAM_WOULD_BLOCK);
        CHECK(Info(task).inputBytes==1024U);
    } else if (!strcmp(name,"output-order")) {
        for(unsigned i=0U;i<8U;++i) {
            bytes[0]=(UmicomU8)i; Put(UmicomModelRecord(task),DATA,bytes,1U);
            CHECK(Call(task,UMICOM_USER_CALL_STREAM_WRITE,1U+(i%2U),DATA,1U,0U)==UMICOM_STREAM_OK);
        }
        for(unsigned i=0U;i<8U;++i) { const UmicomKernelStreamPacket p=Drain(task); CHECK(p.selector==1U+(i%2U) && p.data[0]==i); }
    } else if (!strcmp(name,"output-tail-zero")) {
        CHECK(Write(task,1U,0U)==UMICOM_STREAM_OK); const UmicomKernelStreamPacket p=Drain(task);
        for(unsigned i=1U;i<sizeof(p.data);++i) CHECK(p.data[i]==0U);
    } else if (!strcmp(name,"task-isolation")) {
        const UmicomKernelUserTaskHandle other=Task(64U);
        STREAM(UmicomKernelUserStreamsInput(&streams,task,bytes,32U));
        CHECK(Read(other,32U,UMICOM_STREAM_NONBLOCK)==UMICOM_STREAM_WOULD_BLOCK);
        CHECK(Read(task,32U,0U)==UMICOM_STREAM_OK);
        CHECK(Info(other).inputBytes==0U);
    } else { CHECK(0); }
    Finish();
}
static void TestWaiting(const char *name)
{
    Setup(); const UmicomKernelUserTaskHandle task=Task(!strcmp(name,"last-slice-wait")?1U:128U);
    UmicomU8 data[32]; memset(data,'A',sizeof(data)); Put(UmicomModelRecord(task),DATA,data,sizeof(data));
    if (!strcmp(name,"wait-output") || !strcmp(name,"output-snapshot") || !strcmp(name,"cancel-output")) {
        FillOutput(task);
        (void)Write(task,32U,0U);
        CHECK(Info(task).wait==UMICOM_STREAM_WAIT_OUTPUT); Idle();
        if (!strcmp(name,"cancel-output")) {
            CHECK(UmicomKernelUserTaskCancel(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
            CHECK(Info(task).wait==UMICOM_STREAM_WAIT_NONE && Info(task).outputRecords==8U);
        } else {
            memset(data,'Z',sizeof(data)); Put(UmicomModelRecord(task),DATA,data,sizeof(data));
            (void)Drain(task);
            CHECK(UmicomKernelUserStreamsPump(&umicomModelScheduler));
            CHECK(Info(task).wait==UMICOM_STREAM_WAIT_NONE && Info(task).outputRecords==8U);
            for(unsigned i=0U;i<7U;++i)(void)Drain(task);
            const UmicomKernelStreamPacket p=Drain(task);
            CHECK(!memcmp(p.data,"Original output chunk",21U));
            CHECK(UmicomModelRecord(task)->frame.x11_a1==32U);
        }
    } else if (!strcmp(name,"remaining-output-after-close") || !strcmp(name,"explicit-discard")) {
        CHECK(Write(task,32U,0U)==UMICOM_STREAM_OK);
        End(task,UMICOM_MODEL_EXIT);
        CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_CLEANUP_FAILED);
        CHECK(Info(task).outputRecords==1U && Info(task).stopped);
        if (!strcmp(name,"explicit-discard")) STREAM(UmicomKernelUserStreamsDiscard(&streams,task));
        else { const UmicomKernelStreamPacket p=Drain(task); CHECK(!memcmp(p.data,data,32U)); }
        CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
    } else if (!strcmp(name,"buffered-eof")) {
        STREAM(UmicomKernelUserStreamsInput(&streams,task,data,32U));
        STREAM(UmicomKernelUserStreamsEndInput(&streams,task));
        STREAM(UmicomKernelUserStreamsEndInput(&streams,task));
        CHECK(Read(task,16U,0U)==UMICOM_STREAM_OK && Info(task).inputBytes==16U);
        CHECK(Read(task,32U,0U)==UMICOM_STREAM_OK && UmicomModelRecord(task)->frame.x11_a1==16U);
        CHECK(Read(task,32U,0U)==UMICOM_STREAM_EOF && UmicomModelRecord(task)->frame.x11_a1==0U);
        CHECK(UmicomKernelUserStreamsInput(&streams,task,data,1U)==UMICOM_STREAM_BAD_STATE);
    } else {
        (void)Read(task,32U,0U);
        if (!strcmp(name,"last-slice-wait")) {
            CHECK(UmicomModelInfo(task).state==UMICOM_USER_TASK_EXHAUSTED && Info(task).stopped);
        } else {
            CHECK(Info(task).wait==UMICOM_STREAM_WAIT_INPUT);
            const UmicomKernelUserTaskInfo before=UmicomModelInfo(task);
            for(unsigned i=0U;i<20U;++i) Idle();
            CHECK(UmicomModelInfo(task).systemCalls==before.systemCalls && UmicomModelInfo(task).slices==before.slices);
            if (!strcmp(name,"cancel-input") || !strcmp(name,"stale-replacement")) {
                CHECK(UmicomKernelUserTaskCancel(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
                CHECK(Info(task).wait==UMICOM_STREAM_WAIT_NONE && Info(task).stopped);
                CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
                const UmicomKernelUserTaskHandle replacement=UmicomModelCreate(8U);
                CHECK(replacement!=task && UmicomKernelUserStreamsInput(&streams,task,data,32U)==UMICOM_STREAM_INVALID_HANDLE);
                CHECK(Read(replacement,32U,UMICOM_STREAM_NONBLOCK)==UMICOM_STREAM_UNBOUND);
            } else if (!strcmp(name,"wait-eof")) {
                STREAM(UmicomKernelUserStreamsEndInput(&streams,task));
                CHECK(UmicomKernelUserStreamsPump(&umicomModelScheduler));
                CHECK(UmicomModelRecord(task)->frame.x10_a0==UMICOM_STREAM_EOF && Info(task).wait==UMICOM_STREAM_WAIT_NONE);
            } else if (!strcmp(name,"wait-revalidate")) {
                CHECK(UmicomKernelVirtualMemoryUnmapPage(&UmicomModelRecord(task)->process.space,0x601000U)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
                memset(data,'B',sizeof(data)); /* Original user destination still contains A. */
                STREAM(UmicomKernelUserStreamsInput(&streams,task,data,32U));
                CHECK(UmicomKernelUserStreamsPump(&umicomModelScheduler));
                CHECK(UmicomModelRecord(task)->frame.x10_a0==UMICOM_STREAM_BAD_BUFFER && Info(task).inputBytes==32U);
                UmicomU8 original[16];Get(UmicomModelRecord(task),DATA,original,16U);
                for(unsigned i=0U;i<16U;++i)CHECK(original[i]=='A');
            } else if (!strcmp(name,"wait-input") || !strcmp(name,"no-user-spin") || !strcmp(name,"repeated-pump")) {
                STREAM(UmicomKernelUserStreamsInput(&streams,task,data,32U));
                CHECK(UmicomKernelUserStreamsPump(&umicomModelScheduler));
                const UmicomRiscvTrapFrame saved=UmicomModelRecord(task)->frame;
                for(unsigned i=0U;i<20U;++i)CHECK(UmicomKernelUserStreamsPump(&umicomModelScheduler));
                CHECK(!memcmp(&saved,&UmicomModelRecord(task)->frame,sizeof(saved)));
                CHECK(saved.x10_a0==UMICOM_STREAM_OK && saved.x11_a1==32U && saved.mepc==before.resumePc);
                CHECK(UmicomModelInfo(task).systemCalls==before.systemCalls && !Info(task).inputBytes);
            } else { CHECK(0); }
        }
    }
    Finish();
}
static void TestFailure(const char *name)
{
    Setup(); const UmicomKernelUserTaskHandle task=Task(128U);
    if (!strcmp(name,"unsafe-return")) {
        corruptReturn=UMICOM_TRUE; callNumber=UMICOM_USER_CALL_STREAM_READ;
        umicomModelMode=STREAM_MODEL_CALL; UmicomKernelUserTaskHandle out=0U;
        CHECK(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler,1000U,&out)==UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR);
        CHECK(umicomModelScheduler.poisoned && !UmicomModelRecord(task)->process.quiesced);
        CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)!=UMICOM_USER_SCHEDULE_OK);
        return; /* Deliberate retained unsafe image: no claim of safe reclamation. */
    }
    if (!strcmp(name,"corrupt-count")) streams.records[0].inputCount=UMICOM_STREAM_INPUT_BYTES+1U;
    else if (!strcmp(name,"corrupt-packet")) {
        const UmicomU8 data[32]={1U};Put(UmicomModelRecord(task),DATA,data,sizeof(data));
        CHECK(Write(task,32U,0U)==UMICOM_STREAM_OK);
        streams.records[0].output[0].selector=99U;
    } else if (!strcmp(name,"cumulative-call-limit")) {
        for(unsigned i=0U;i<UMICOM_USER_CALL_LIMIT;++i)CHECK(Read(task,0U,0U)==UMICOM_STREAM_OK);
        umicomModelMode=STREAM_MODEL_CALL;CHECK(UmicomModelRun()==task);
        CHECK(UmicomModelInfo(task).state==UMICOM_USER_TASK_EXHAUSTED && Info(task).stopped);
        Finish(); return;
    } else { CHECK(0); }
    CHECK(!UmicomKernelUserStreamsPump(&umicomModelScheduler) && streams.poisoned);
    CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)!=UMICOM_USER_SCHEDULE_OK);
}
static void TestStress(const char *name)
{
    Setup();
    if (!strcmp(name,"repeated-lifetimes")) {
        UmicomKernelUserTaskHandle previous=0U;
        for(unsigned i=0U;i<1000U;++i) {
            const UmicomKernelUserTaskHandle task=Task(4U);
            CHECK(task!=previous); previous=task;
            const UmicomU8 data=(UmicomU8)i;Put(UmicomModelRecord(task),DATA,&data,1U);
            CHECK(Write(task,1U,0U)==UMICOM_STREAM_OK);End(task,UMICOM_MODEL_EXIT);
            const UmicomKernelStreamPacket p=Drain(task);CHECK(p.data[0]==data);
            CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
            CHECK(UmicomKernelUserSchedulerValidate(&umicomModelScheduler)==UMICOM_USER_SCHEDULE_OK);
        }
    } else {
        /* Each lifetime exercises varied lengths and ring movement without
         * resetting an execution budget or overwriting a live owner. */
        unsigned random=19U;
        for(unsigned i=0U;i<100U;++i) {
            const UmicomKernelUserTaskHandle task=Task(64U);
            for(unsigned turn=0U;turn<12U;++turn) {
                random=random*1664525U+1013904223U;
                const UmicomSize count=(random%256U)+1U;
                UmicomU8 data[256];for(UmicomSize k=0U;k<count;++k)data[k]=(UmicomU8)(random+k);
                STREAM(UmicomKernelUserStreamsInput(&streams,task,data,count));
                CHECK(Read(task,count,0U)==UMICOM_STREAM_OK);
                CHECK(Write(task,count,0U)==UMICOM_STREAM_OK);
                const UmicomKernelStreamPacket p=Drain(task);CHECK(p.bytes==count && !memcmp(p.data,data,(size_t)count));
            }
            CHECK(UmicomKernelUserTaskCancel(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
            CHECK(UmicomKernelUserTaskReap(&umicomModelScheduler,task)==UMICOM_USER_SCHEDULE_OK);
        }
    }
    Finish();
}
static void Output(void *context, const char *text, UmicomSize bytes)
{
    (void)context; CHECK(!insideTrap);
    for(UmicomSize i=0U;i<bytes && transcriptBytes<sizeof(transcript)-1U;++i) transcript[transcriptBytes++]=text[i];
    transcript[transcriptBytes]='\0';
}
static void Feed(const char *text)
{
    for(size_t i=0U;text[i];++i)(void)UmicomKernelConsoleShellFeed(&shell,(UmicomU8)text[i]);
}
static void TerminalSetup(void)
{
    UmicomModelSetup(); UmicomModelPut(64U+2U*56U+40U,8192U,8U);
    const UmicomKernelShellImage seed={"/bin/model.elf",umicomModelElf,sizeof(umicomModelElf)};
    CHECK(UmicomKernelConsoleShellInitialize(&shell,Output,NULL,&seed,1U)==UMICOM_SHELL_OK);
    CHECK(UmicomKernelConsoleTerminalAttach(&terminal,&shell)==UMICOM_SHELL_OK);
}
static void TerminalReadWait(void)
{
    callNumber=UMICOM_USER_CALL_STREAM_READ;callSelector=0U;callAddress=DATA;callBytes=64U;callFlags=0U;
    umicomModelMode=STREAM_MODEL_CALL;
    CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_OK);
    CHECK(terminal.streams.records[0].wait==UMICOM_STREAM_WAIT_INPUT);
}
static void TerminalExit(void)
{
    umicomModelMode=UMICOM_MODEL_EXIT;
    for(unsigned i=0U;shell.foreground && i<3U;++i)CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_OK);
    CHECK(!shell.foreground && strstr(transcript,"umicom>")!=NULL);
}
static void TestTerminal(const char *name)
{
    TerminalSetup();
    if (!strcmp(name,"terminal-attach")) {
        CHECK(UmicomKernelConsoleTerminalAttach(&terminal,&shell)==UMICOM_SHELL_BAD_STATE);
    } else {
        Feed("run /bin/model.elf 0\r\n");
        CHECK(shell.foreground!=0U && terminal.input.length==0U && terminal.streams.records[0].inputCount==0U);
        if (!strcmp(name,"terminal-control-output")) {
            const UmicomU8 control[]={ 'A',27U,'[','2','J',0U,10U,'B' };
            Put(&shell.supervisor.scheduler.tasks[0],DATA,control,sizeof(control));
            callNumber=UMICOM_USER_CALL_STREAM_WRITE;callSelector=2U;callAddress=DATA;callBytes=sizeof(control);callFlags=0U;
            umicomModelMode=STREAM_MODEL_CALL; CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_OK);
            CHECK(strstr(transcript,"[stderr] A\\x1b[2J\\x00\r\nB")!=NULL);
            CHECK(strchr(transcript,27)==NULL); TerminalExit();
        } else {
            TerminalReadWait();
            if (!strcmp(name,"terminal-cancel")) {
                Feed("discard this");(void)UmicomKernelConsoleShellFeed(&shell,3U);
                CHECK(!shell.foreground && terminal.input.length==0U);
            } else if (!strcmp(name,"terminal-shutdown")) {
                CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_OK);
                CHECK(terminal.closed && !shell.foreground);UmicomModelNoLeaks();return;
            } else if (!strcmp(name,"terminal-eof")) {
                (void)UmicomKernelConsoleShellFeed(&shell,4U);
                CHECK(terminal.streams.records[0].inputEnded);
                CHECK(UmicomKernelUserStreamsPump(&shell.supervisor.scheduler));
                CHECK(shell.supervisor.scheduler.tasks[0].frame.x10_a0==UMICOM_STREAM_EOF);TerminalExit();
            } else if (!strcmp(name,"terminal-partial-eof")) {
                Feed("partial");(void)UmicomKernelConsoleShellFeed(&shell,4U);
                CHECK(!terminal.streams.records[0].inputEnded && terminal.streams.records[0].inputCount==7U);
                CHECK(UmicomKernelUserStreamsPump(&shell.supervisor.scheduler));
                UmicomU8 copy[7];Get(&shell.supervisor.scheduler.tasks[0],DATA,copy,sizeof(copy));CHECK(!memcmp(copy,"partial",7U));
                (void)UmicomKernelConsoleShellFeed(&shell,4U);CHECK(terminal.streams.records[0].inputEnded);TerminalExit();
            } else if (!strcmp(name,"terminal-edit")) {
                Feed("mistake");(void)UmicomKernelConsoleShellFeed(&shell,21U);
                Feed("accountX");(void)UmicomKernelConsoleShellFeed(&shell,8U);Feed(" verified\n");
                CHECK(terminal.streams.records[0].inputCount==17U);TerminalExit();
            } else if (!strcmp(name,"terminal-rejected-line") || !strcmp(name,"terminal-overlong") || !strcmp(name,"terminal-input-loss")) {
                Feed("unsafe");
                if(!strcmp(name,"terminal-rejected-line"))(void)UmicomKernelConsoleShellFeed(&shell,27U);
                else if(!strcmp(name,"terminal-input-loss"))UmicomKernelConsoleShellInputLost(&shell);
                else for(unsigned i=0U;i<512U;++i)(void)UmicomKernelConsoleShellFeed(&shell,'x');
                Feed("tail\n");CHECK(terminal.streams.records[0].inputCount==0U);
                Feed("safe\n");CHECK(terminal.streams.records[0].inputCount==5U);TerminalExit();
            } else if (!strcmp(name,"terminal-input-full")) {
                UmicomU8 data[1024];memset(data,'q',sizeof(data));
                STREAM(UmicomKernelUserStreamsInput(&terminal.streams,shell.foreground,data,sizeof(data)));
                Feed("not partial\n");CHECK(terminal.streams.records[0].inputCount==1024U);
                CHECK(strstr(transcript,"stdin line not accepted")!=NULL);TerminalExit();
            } else {
                CHECK(!strcmp(name,"terminal-line") || !strcmp(name,"terminal-crlf") || !strcmp(name,"terminal-restart"));
                Feed("approved\r\n");CHECK(terminal.streams.records[0].inputCount==9U);
                CHECK(UmicomKernelUserStreamsPump(&shell.supervisor.scheduler));
                UmicomU8 copy[9];Get(&shell.supervisor.scheduler.tasks[0],DATA,copy,sizeof(copy));CHECK(!memcmp(copy,"approved\n",9U));
                TerminalExit();
                if (!strcmp(name,"terminal-restart")) {
                    Feed("run /bin/model.elf 0\n");TerminalReadWait();
                    CHECK(terminal.streams.records[0].inputCount==0U && !terminal.streams.records[0].inputEnded);
                    (void)UmicomKernelConsoleShellFeed(&shell,4U);TerminalExit();
                }
            }
        }
    }
    CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_OK);
    CHECK(terminal.closed);UmicomModelNoLeaks();
}
int main(int argc, char **argv)
{
    CHECK(argc==2);const char *name=argv[1];
    if(!strncmp(name,"terminal-",9U))TestTerminal(name);
    else if(!strcmp(name,"repeated-lifetimes") || !strcmp(name,"mixed-stream-operations"))TestStress(name);
    else if(!strcmp(name,"unsafe-return") || !strncmp(name,"corrupt-",8U) || !strcmp(name,"cumulative-call-limit"))TestFailure(name);
    else if(!strncmp(name,"attach-",7U) || !strncmp(name,"grant-",6U) || !strcmp(name,"copied-owner") ||
        !strcmp(name,"close-live") || !strcmp(name,"context-refusal") || !strcmp(name,"refused-entry"))TestOwnership(name);
    else if(!strncmp(name,"wait-",5U) || !strncmp(name,"cancel-",7U) || !strcmp(name,"no-user-spin") ||
        !strcmp(name,"buffered-eof") || !strcmp(name,"output-snapshot") || !strcmp(name,"repeated-pump") ||
        !strcmp(name,"remaining-output-after-close") || !strcmp(name,"explicit-discard") ||
        !strcmp(name,"last-slice-wait") || !strcmp(name,"stale-replacement"))TestWaiting(name);
    else TestCalls(name);
    printf("PASS %s (%u checks)\n",name,umicomModelChecks);return 0;
}
