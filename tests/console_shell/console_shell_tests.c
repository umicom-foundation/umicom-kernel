/*-----------------------------------------------------------------------------
 * Umicom Kernel console host checks
 * File: tests/console_shell/console_shell_tests.c
 *
 * Reuse the existing explicit ELF/RAM/CSR model without changing it. The real
 * command engine, supervisor, scheduler, file services, VFS, RAMFS, page tables
 * and allocators are linked. Only hardware entry/clock and selected allocator
 * refusals are models. This is not evidence of a RISC-V guest or a physical UART.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#define main UmicomConsoleEarlierSchedulingMain
#define UmicomRiscvUserExecuteFrame UmicomConsoleEarlierEntry
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#include "umicom/kernel/console_shell.h"

#define CHECK(x) UmicomModelRequire((x) ? 1 : 0, #x)
static UmicomKernelConsoleShell shell;
static char transcript[32768];
static UmicomSize transcriptBytes;
static int allocationBudget = -1;
static unsigned releaseCalls;
static unsigned refuseReleaseAt;
static UmicomBoolean insideFileTrap;
static UmicomBoolean fileProgram;
static UmicomBoolean fileProgramFault;
static UmicomBoolean fileProgramSpin;
static UmicomU64 fileProgramIdentity;
static unsigned fileProgramStep;
static UmicomBoolean tryReentry;
static unsigned refusedReentries;

UmicomKernelMemoryStatus UmicomConsoleActualAllocate(UmicomAddress *out);
UmicomKernelMemoryStatus UmicomConsoleActualFree(UmicomAddress address);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *out)
{
    if (allocationBudget == 0) return UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY;
    const UmicomKernelMemoryStatus status = UmicomConsoleActualAllocate(out);
    if (status == UMICOM_KERNEL_MEMORY_OK && allocationBudget > 0) --allocationBudget;
    return status;
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress address)
{
    ++releaseCalls;
    if (refuseReleaseAt != 0U && releaseCalls == refuseReleaseAt) return UMICOM_KERNEL_MEMORY_NOT_ALLOCATED;
    return UmicomConsoleActualFree(address);
}
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    return !insideFileTrap && umicomModelAllowed ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    if (!fileProgram) return UmicomConsoleEarlierEntry(request, session, frame);
    if (fileProgramIdentity != session->identity) { fileProgramIdentity = session->identity; fileProgramStep = 0U; }
    if (fileProgramStep++ == 0U) {
        static const char path[] = "/shared/report";
        const UmicomKernelFileRequest call = {UMICOM_FILE_OPEN, 0U, 0x600100U,
            sizeof(path)-1U, UMICOM_VFS_RIGHT_READ, 0U};
        CHECK(UmicomKernelUserMemoryWrite(&session->memory, 0x600100U, (const UmicomU8 *)path, sizeof(path)-1U) == UMICOM_USER_RESULT_OK);
        CHECK(UmicomKernelUserMemoryWrite(&session->memory, 0x600000U, (const UmicomU8 *)&call, sizeof(call)) == UMICOM_USER_RESULT_OK);
        frame->mstatus = (UmicomU64)2U << 32U;
        frame->mcause = 8U; frame->mtval = 0U; frame->reserved = 0U;
        frame->x17_a7 = UMICOM_USER_CALL_FILE;
        frame->x10_a0 = 0x600000U; frame->x11_a1 = 0x600080U;
        frame->x12_a2 = sizeof(UmicomKernelFileRequest); frame->x13_a3 = sizeof(UmicomKernelFileResult);
        insideFileTrap = UMICOM_TRUE;
        CHECK(UmicomKernelUserTrapDispatch(session, frame) == 0U);
        insideFileTrap = UMICOM_FALSE;
        return 0U; /* Actual file execution happens after this model trap returns. */
    }
    const unsigned saved = umicomModelMode;
    umicomModelMode = fileProgramFault ? UMICOM_MODEL_FAULT : fileProgramSpin ? UMICOM_MODEL_TIMER : UMICOM_MODEL_EXIT;
    const UmicomU64 result = UmicomConsoleEarlierEntry(request, session, frame);
    umicomModelMode = saved;
    return result;
}
static void Output(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    if (tryReentry) {
        CHECK(UmicomKernelConsoleShellExecute(&shell, "help", 4U) == UMICOM_SHELL_BUSY);
        ++refusedReentries;
    }
    for (UmicomSize i = 0U; i < bytes && transcriptBytes < sizeof(transcript)-1U; ++i)
        transcript[transcriptBytes++] = text[i];
    transcript[transcriptBytes] = '\0';
}
static void ResetOutput(void) { transcriptBytes = 0U; transcript[0] = '\0'; }
static UmicomKernelShellStatus Command(const char *text)
{
    return UmicomKernelConsoleShellExecute(&shell, text, (UmicomSize)strlen(text));
}
static void Feed(const char *text)
{
    for (size_t i = 0U; text[i] != '\0'; ++i) (void)UmicomKernelConsoleShellFeed(&shell, (UmicomU8)text[i]);
}
static void Initialise(void)
{
    /* Expand the fixture's writable segment to hold real copied file requests.
     * It remains a synthetic ELF and is never presented as native execution. */
    UmicomModelSetup();
    UmicomModelPut(64U + 2U*56U + 40U, 8192U, 8U);
    const UmicomKernelShellImage seed = {"/bin/model.elf", umicomModelElf, sizeof(umicomModelElf)};
    CHECK(UmicomKernelConsoleShellInitialize(&shell, Output, NULL, &seed, 1U) == UMICOM_SHELL_OK);
}
static UmicomKernelVfsStatus Open(const char *path, UmicomKernelFileDescriptor *fd)
{
    return UmicomKernelVfsOpen(&shell.client, path, UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY,
        UMICOM_FALSE, fd);
}
static void Contents(const char *path, const char *expected)
{
    UmicomKernelFileDescriptor fd = 0U; UmicomSize got = 0U; char data[1024] = {0};
    CHECK(Open(path, &fd) == UMICOM_VFS_OK);
    CHECK(UmicomKernelVfsRead(&shell.client, fd, data, sizeof(data), &got) == UMICOM_VFS_OK);
    CHECK(got == strlen(expected) && memcmp(data, expected, (size_t)got) == 0);
    CHECK(UmicomKernelVfsClose(&shell.client, fd) == UMICOM_VFS_OK);
}
static void Missing(const char *path)
{
    UmicomKernelFileDescriptor fd = 0U;
    CHECK(Open(path, &fd) == UMICOM_VFS_NOT_FOUND);
}
static void RunToPrompt(void)
{
    for (unsigned i = 0U; shell.foreground != 0U && i < UMICOM_SHELL_SLICE_LIMIT + 8U; ++i)
        CHECK(UmicomKernelConsoleShellStep(&shell) == UMICOM_SHELL_OK);
    CHECK(shell.foreground == 0U);
}
static void Close(void)
{
    CHECK(UmicomKernelConsoleShellClose(&shell) == UMICOM_SHELL_OK);
    CHECK(shell.state == UMICOM_VFS_CLOSED);
    UmicomModelNoLeaks();
}
static void TestLine(const char *name)
{
    UmicomKernelConsoleLine line = {0};
    if (!strcmp(name,"line-crlf")) {
        CHECK(UmicomKernelConsoleLineFeed(&line, 'a') == UMICOM_CONSOLE_LINE_NONE);
        CHECK(UmicomKernelConsoleLineFeed(&line, 13U) == UMICOM_CONSOLE_LINE_READY);
        UmicomKernelConsoleLineConsume(&line);
        CHECK(UmicomKernelConsoleLineFeed(&line, 10U) == UMICOM_CONSOLE_LINE_NONE);
    } else if (!strcmp(name,"line-backspace")) {
        (void)UmicomKernelConsoleLineFeed(&line,8U);
        (void)UmicomKernelConsoleLineFeed(&line,'a'); (void)UmicomKernelConsoleLineFeed(&line,'b');
        (void)UmicomKernelConsoleLineFeed(&line,127U);
        CHECK(line.length == 1U && line.bytes[0] == 'a' && line.bytes[1] == 0);
    } else if (!strcmp(name,"line-controls")) {
        for (unsigned b=0U; b<32U; ++b) {
            if(b==3U || b==8U || b==9U || b==10U || b==13U || b==21U) continue;
            memset(&line,0,sizeof(line)); (void)UmicomKernelConsoleLineFeed(&line,'x');
            (void)UmicomKernelConsoleLineFeed(&line,(UmicomU8)b);
            CHECK(UmicomKernelConsoleLineFeed(&line,10U)==UMICOM_CONSOLE_LINE_REJECTED);
        }
    } else if (!strcmp(name,"line-overflow")) {
        for(unsigned i=0U;i<UMICOM_SHELL_LINE_BYTES;++i)(void)UmicomKernelConsoleLineFeed(&line,'x');
        (void)UmicomKernelConsoleLineFeed(&line,8U);
        CHECK(UmicomKernelConsoleLineFeed(&line,10U)==UMICOM_CONSOLE_LINE_REJECTED);
        CHECK(line.length==0U);
    } else if (!strcmp(name,"line-limit")) {
        for(unsigned i=0U;i<UMICOM_SHELL_LINE_BYTES-1U;++i)(void)UmicomKernelConsoleLineFeed(&line,'x');
        CHECK(UmicomKernelConsoleLineFeed(&line,10U)==UMICOM_CONSOLE_LINE_READY);
        CHECK(line.length==UMICOM_SHELL_LINE_BYTES-1U);
    } else if (!strcmp(name,"line-clear-cancel")) {
        (void)UmicomKernelConsoleLineFeed(&line,'x');(void)UmicomKernelConsoleLineFeed(&line,21U);
        CHECK(line.length==0U);(void)UmicomKernelConsoleLineFeed(&line,'x');
        CHECK(UmicomKernelConsoleLineFeed(&line,3U)==UMICOM_CONSOLE_LINE_CANCEL && line.length==0U);
    } else if (!strcmp(name,"line-invalid-length")) {
        line.length=~(UmicomSize)0U;
        CHECK(UmicomKernelConsoleLineFeed(&line,10U)==UMICOM_CONSOLE_LINE_REJECTED);
    } else if (!strcmp(name,"line-byte-fuzz")) {
        unsigned random=17U;
        for(unsigned i=0U;i<100000U;++i){
            random=random*1664525U+1013904223U;
            const UmicomKernelConsoleLineEvent event=UmicomKernelConsoleLineFeed(&line,(UmicomU8)(random>>16U));
            CHECK(line.length<UMICOM_SHELL_LINE_BYTES);
            if(event==UMICOM_CONSOLE_LINE_READY)UmicomKernelConsoleLineConsume(&line);
        }
    } else { CHECK(0); }
}
static void TestParse(const char *name)
{
    UmicomKernelShellCommand command = {0};
    if (!strcmp(name,"parse-quotes")) {
        const char *line="write '/notes/a b' \"some text\"";
        CHECK(UmicomKernelShellParse(line,strlen(line),&command)==UMICOM_SHELL_OK);
        CHECK(command.count==3U && !strcmp(command.bytes+command.offsets[1],"/notes/a b"));
        CHECK(!strcmp(command.bytes+command.offsets[2],"some text"));
    } else if (!strcmp(name,"parse-empty-token")) {
        const char *line="write /notes/a \"\"";
        CHECK(UmicomKernelShellParse(line,strlen(line),&command)==UMICOM_SHELL_OK);
        CHECK(command.count==3U && command.bytes[command.offsets[2]]==0);
    } else if (!strcmp(name,"parse-refusals")) {
        const char *cases[]={"a b c d e","a\"b","'unfinished","\"a\"b","a\n","a\x1b"};
        for(size_t i=0U;i<sizeof(cases)/sizeof(cases[0]);++i){
            memset(&command,0x5a,sizeof(command));
            CHECK(UmicomKernelShellParse(cases[i],strlen(cases[i]),&command)!=UMICOM_SHELL_OK);
            CHECK((unsigned char)command.bytes[0]==0x5aU);
        }
        const char nul[]={'a',0,'b'};
        CHECK(UmicomKernelShellParse(nul,sizeof(nul),&command)==UMICOM_SHELL_SYNTAX);
    } else if (!strcmp(name,"parse-limits")) {
        char line[UMICOM_SHELL_LINE_BYTES];memset(line,'a',sizeof(line));
        CHECK(UmicomKernelShellParse(line,sizeof(line)-1U,&command)==UMICOM_SHELL_OK);
        CHECK(UmicomKernelShellParse(line,sizeof(line),&command)!=UMICOM_SHELL_OK);
        CHECK(UmicomKernelShellParse(NULL,0U,&command)==UMICOM_SHELL_OK && command.count==0U);
        CHECK(UmicomKernelShellParse(NULL,1U,&command)!=UMICOM_SHELL_OK);
    } else if (!strcmp(name,"parse-no-expansion")) {
        const char *line="write /notes/a '$(poweroff); *'";
        CHECK(UmicomKernelShellParse(line,strlen(line),&command)==UMICOM_SHELL_OK);
        CHECK(!strcmp(command.bytes+command.offsets[2],"$(poweroff); *"));
    } else if (!strcmp(name,"parse-numbers")) {
        UmicomU64 n=123U;
        CHECK(UmicomKernelShellUnsigned("18446744073709551615",&n)&&n==~(UmicomU64)0U);
        CHECK(!UmicomKernelShellUnsigned("18446744073709551616",&n));
        CHECK(!UmicomKernelShellUnsigned("-1",&n)); CHECK(!UmicomKernelShellUnsigned("1x",&n));
        CHECK(!UmicomKernelShellUnsigned("",&n)); CHECK(UmicomKernelShellUnsigned("0007",&n)&&n==7U);
    } else if (!strcmp(name,"parse-byte-fuzz")) {
        unsigned random=79U;char line[UMICOM_SHELL_LINE_BYTES];
        for(unsigned i=0U;i<10000U;++i){
            random=random*1664525U+1013904223U;const size_t n=random%sizeof(line);
            for(size_t j=0U;j<n;++j){random=random*1664525U+1013904223U;line[j]=(char)(random>>16U);}
            const UmicomKernelShellStatus s=UmicomKernelShellParse(line,n,&command);
            if(s==UMICOM_SHELL_OK){CHECK(command.count<=UMICOM_SHELL_ARGUMENT_LIMIT);
                for(UmicomSize j=0U;j<command.count;++j)CHECK(command.offsets[j]<UMICOM_SHELL_LINE_BYTES);}
        }
    } else { CHECK(0); }
}
int main(int argc,char **argv)
{
    if(argc!=2)return EXIT_FAILURE;
    const char *name=argv[1];
    if(!strncmp(name,"line-",5U)){TestLine(name);return EXIT_SUCCESS;}
    if(!strncmp(name,"parse-",6U)){TestParse(name);return EXIT_SUCCESS;}
    if(!strcmp(name,"initialisation-refusals")){
        CHECK(UmicomKernelConsoleShellInitialize(NULL,Output,NULL,NULL,0U)==UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(UmicomKernelConsoleShellInitialize(&shell,NULL,NULL,NULL,0U)==UMICOM_SHELL_INVALID_ARGUMENT);
        UmicomModelSetup();umicomModelAllowed=UMICOM_FALSE;
        CHECK(UmicomKernelConsoleShellInitialize(&shell,Output,NULL,NULL,0U)==UMICOM_SHELL_UNSAFE);
        return EXIT_SUCCESS;
    }
    if(!strcmp(name,"initialisation-budgets")){
        UmicomModelSetup();
        for(int budget=0;budget<24;++budget){
            UmicomKernelConsoleShell *local=calloc(1,sizeof(*local));CHECK(local!=NULL);
            allocationBudget=budget;
            const UmicomKernelShellImage seed={"/bin/model.elf",umicomModelElf,sizeof(umicomModelElf)};
            (void)UmicomKernelConsoleShellInitialize(local,Output,NULL,&seed,1U);
            allocationBudget=-1;
            CHECK(UmicomKernelConsoleShellClose(local)==UMICOM_SHELL_OK);
            free(local);UmicomModelNoLeaks();
        }
        return EXIT_SUCCESS;
    }
    Initialise();
    if(!strcmp(name,"seed-files")){
        Contents("/shared/report","Umicom report\n");CHECK(Command("ls /bin")==UMICOM_SHELL_OK);
        CHECK(strstr(transcript,"model.elf")!=NULL);
    }else if(!strcmp(name,"reinitialisation")){
        CHECK(UmicomKernelConsoleShellInitialize(&shell,Output,NULL,NULL,0U)==UMICOM_SHELL_BAD_STATE);
    }else if(!strcmp(name,"copied-owner")){
        UmicomKernelConsoleShell *copy=malloc(sizeof(*copy));CHECK(copy!=NULL);memcpy(copy,&shell,sizeof(*copy));
        CHECK(UmicomKernelConsoleShellExecute(copy,"help",4U)==UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelConsoleShellClose(copy)==UMICOM_SHELL_BAD_STATE);free(copy);
    }else if(!strcmp(name,"basic-file-roundtrip")){
        CHECK(Command("mkdir /notes/accounts")==UMICOM_SHELL_OK);
        CHECK(Command("create /notes/accounts/journal")==UMICOM_SHELL_OK);
        CHECK(Command("write /notes/accounts/journal \"Deposit reviewed\"")==UMICOM_SHELL_OK);
        CHECK(Command("append /notes/accounts/journal ' and approved'")==UMICOM_SHELL_OK);
        Contents("/notes/accounts/journal","Deposit reviewed and approved");
        CHECK(Command("cat /notes/accounts/journal")==UMICOM_SHELL_OK);
        CHECK(strstr(transcript,"Deposit reviewed and approved")!=NULL);
    }else if(!strcmp(name,"quoted-file-name")){
        CHECK(Command("create '/notes/a b'")==UMICOM_SHELL_OK);
        CHECK(Command("write '/notes/a b' ''")==UMICOM_SHELL_OK);Contents("/notes/a b","");
    }else if(!strcmp(name,"namespace-refusals")){
        const char *paths[]={"create relative","create /notes//a","create /notes/../a","rm /","rmdir /notes/missing"};
        for(size_t i=0U;i<sizeof(paths)/sizeof(paths[0]);++i)CHECK(Command(paths[i])!=UMICOM_SHELL_OK);
        CHECK(Command("rmdir /bin")==UMICOM_SHELL_IO_ERROR);CHECK(shell.descriptor==0U);
    }else if(!strcmp(name,"unknown-and-arguments")){
        CHECK(Command("help extra")==UMICOM_SHELL_UNKNOWN_COMMAND);
        CHECK(Command("write /notes/x")==UMICOM_SHELL_UNKNOWN_COMMAND);
        CHECK(Command("whatever")==UMICOM_SHELL_UNKNOWN_COMMAND);
        CHECK(Command("create /notes/a extra extra extra")==UMICOM_SHELL_SYNTAX);Missing("/notes/a");
    }else if(!strcmp(name,"overwrite-existing")){
        CHECK(Command("write /notes/absent text")==UMICOM_SHELL_IO_ERROR);Missing("/notes/absent");
        CHECK(Command("create /notes/a")==UMICOM_SHELL_OK);CHECK(Command("write /notes/a long")==UMICOM_SHELL_OK);
        CHECK(Command("write /notes/a x")==UMICOM_SHELL_OK);Contents("/notes/a","x");
    }else if(!strcmp(name,"binary-display")){
        CHECK(Command("create /notes/binary")==UMICOM_SHELL_OK);
        UmicomKernelFileDescriptor fd=0U;UmicomSize n=0U;const UmicomU8 data[]={0U,27U,'[','2','J',255U,10U};
        CHECK(UmicomKernelVfsOpen(&shell.client,"/notes/binary",UMICOM_VFS_RIGHT_WRITE,UMICOM_FALSE,&fd)==UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsWrite(&shell.client,fd,data,sizeof(data),&n)==UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsClose(&shell.client,fd)==UMICOM_VFS_OK);ResetOutput();
        CHECK(Command("cat /notes/binary")==UMICOM_SHELL_OK);CHECK(strchr(transcript,27)==NULL);
        CHECK(strstr(transcript,"\\x00\\x1b[2J\\xff")!=NULL);
    }else if(!strcmp(name,"directory-and-types")){
        CHECK(Command("cat /notes")==UMICOM_SHELL_IO_ERROR);
        CHECK(Command("stat /notes")==UMICOM_SHELL_OK);
        CHECK(Command("ls /shared/report")==UMICOM_SHELL_IO_ERROR);
        CHECK(Command("rm /notes")==UMICOM_SHELL_IO_ERROR);CHECK(shell.descriptor==0U);
    }else if(!strcmp(name,"remove-and-reap")){
        CHECK(Command("create /notes/a")==UMICOM_SHELL_OK);CHECK(Command("write /notes/a text")==UMICOM_SHELL_OK);
        CHECK(Command("rm /notes/a")==UMICOM_SHELL_OK);Missing("/notes/a");
        CHECK(Command("reap")==UMICOM_SHELL_OK);CHECK(Command("mkdir /notes/empty")==UMICOM_SHELL_OK);
        CHECK(Command("rmdir /notes/empty")==UMICOM_SHELL_OK);
    }else if(!strcmp(name,"editor-crlf-once")){
        const UmicomU64 before=shell.commands;Feed("create /notes/a\r\n");CHECK(shell.commands==before+1U);
        Contents("/notes/a","");
    }else if(!strcmp(name,"editor-backspace-clear")){
        Feed("create /notes/ax\bb\r");Contents("/notes/ab","");
        Feed("create /notes/unwanted\x15help\r");Missing("/notes/unwanted");
    }else if(!strcmp(name,"editor-nul-and-escape")){
        Feed("create /notes/nul");(void)UmicomKernelConsoleShellFeed(&shell,0U);Feed("tail\r");Missing("/notes/nul");
        Feed("create /notes/esc\x1b[A\r");Missing("/notes/esc");
    }else if(!strcmp(name,"editor-overflow-no-prefix")){
        Feed("create /notes/long");for(unsigned i=0U;i<600U;++i)(void)UmicomKernelConsoleShellFeed(&shell,' ');
        Feed("\r");Missing("/notes/long");
    }else if(!strcmp(name,"editor-input-loss")){
        Feed("create /notes/lost");UmicomKernelConsoleShellInputLost(&shell);Feed("\r");Missing("/notes/lost");
        Feed("create /notes/next\r");Contents("/notes/next","");
    }else if(!strcmp(name,"editor-cancel")){
        Feed("rm /shared/report");(void)UmicomKernelConsoleShellFeed(&shell,3U);Feed("\r");
        Contents("/shared/report","Umicom report\n");
    }else if(!strcmp(name,"numeric-range")){
        CHECK(Command("run /bin/model.elf -1")==UMICOM_SHELL_RANGE);
        CHECK(Command("run /bin/model.elf 18446744073709551616")==UMICOM_SHELL_RANGE);CHECK(shell.foreground==0U);
    }else if(!strcmp(name,"malformed-executable")){
        CHECK(Command("run /shared/report")==UMICOM_SHELL_PROCESS_ERROR);CHECK(shell.foreground==0U);
        CHECK(Command("help")==UMICOM_SHELL_OK);
    }else if(!strcmp(name,"load-refusal-rollback")){
        const UmicomU64 baseline=UmicomModelMemory().allocatedFrames;
        allocationBudget=0;CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_PROCESS_ERROR);allocationBudget=-1;
        CHECK(UmicomModelMemory().allocatedFrames==baseline && shell.foreground==0U);
    }else if(!strcmp(name,"run-file-snapshot")){
        umicomModelMode=UMICOM_MODEL_EXIT;CHECK(Command("run /bin/model.elf 7")==UMICOM_SHELL_OK);
        CHECK(shell.foreground!=0U && shell.descriptor==0U);
        for(size_t i=0U;i<sizeof(shell.image);++i)CHECK(shell.image[i]==0U);
        /* Removing the named source after Spawn cannot revoke its private image. */
        CHECK(UmicomKernelVfsRemove(&shell.client,"/bin/model.elf",UMICOM_VFS_FILE)==UMICOM_VFS_OK);
        RunToPrompt();CHECK(shell.lastCompletion.state==UMICOM_USER_TASK_EXITED);
    }else if(!strcmp(name,"run-busy-rejects-command")){
        CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);
        CHECK(Command("create /notes/no")==UMICOM_SHELL_BUSY);Missing("/notes/no");
    }else if(!strcmp(name,"run-fault-then-retry")){
        umicomModelMode=UMICOM_MODEL_FAULT;CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);RunToPrompt();
        CHECK(shell.lastCompletion.state==UMICOM_USER_TASK_FAULTED);
        umicomModelMode=UMICOM_MODEL_EXIT;CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);RunToPrompt();
        CHECK(shell.lastCompletion.state==UMICOM_USER_TASK_EXITED);
    }else if(!strcmp(name,"run-budget")){
        umicomModelMode=UMICOM_MODEL_TIMER;CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);RunToPrompt();
        CHECK(shell.lastCompletion.state==UMICOM_USER_TASK_EXHAUSTED && shell.lastCompletion.slices==UMICOM_SHELL_SLICE_LIMIT);
    }else if(!strcmp(name,"run-cancel")){
        CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);
        CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_OK);
        CHECK(UmicomKernelConsoleShellFeed(&shell,3U)==UMICOM_SHELL_OK);
        CHECK(shell.foreground==0U && shell.lastCompletion.state==UMICOM_USER_TASK_CANCELLED);
    }else if(!strcmp(name,"foreground-typeahead")){
        umicomModelMode=UMICOM_MODEL_EXIT;CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);
        Feed("create /notes/unwanted");RunToPrompt();Feed("tail\r");Missing("/notes/unwantedtail");
        Feed("create /notes/fresh\r");Contents("/notes/fresh","");
    }else if(!strcmp(name,"file-client-lifetime") || !strcmp(name,"file-client-fault") || !strcmp(name,"file-client-cancel")){
        fileProgram=UMICOM_TRUE;fileProgramFault=!strcmp(name,"file-client-fault")?UMICOM_TRUE:UMICOM_FALSE;
        fileProgramSpin=!strcmp(name,"file-client-cancel")?UMICOM_TRUE:UMICOM_FALSE;
        const UmicomU64 baseline=UmicomModelMemory().allocatedFrames;
        CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);
        CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_OK);
        CHECK(shell.foreground!=0U && shell.files.completed==1U);
        if(fileProgramSpin)CHECK(UmicomKernelConsoleShellFeed(&shell,3U)==UMICOM_SHELL_OK);else RunToPrompt();
        CHECK(shell.foreground==0U && UmicomModelMemory().allocatedFrames>=baseline);
        CHECK(UmicomKernelUserFilesValidate(&shell.supervisor.scheduler));
        Contents("/shared/report","Umicom report\n");
    }else if(!strcmp(name,"read-only-grant") || !strcmp(name,"explicit-writable-grant")){
        const UmicomBoolean writable=!strcmp(name,"explicit-writable-grant")?UMICOM_TRUE:UMICOM_FALSE;
        CHECK(Command(writable?"runrw /bin/model.elf":"run /bin/model.elf")==UMICOM_SHELL_OK);
        const UmicomSize slot=(UmicomU32)shell.foreground-1U;void *object=NULL;
        CHECK(UmicomKernelObjectCacheResolve(&shell.files.clients,shell.files.records[slot].client,&object)==UMICOM_OBJECT_OK);
        const UmicomKernelVfsClient *client=object;
        CHECK(client->rights==(writable?UMICOM_VFS_RIGHT_ALL:UMICOM_VFS_RIGHT_READ|UMICOM_VFS_RIGHT_QUERY|UMICOM_VFS_RIGHT_ENUMERATE));
    }else if(!strcmp(name,"file-grant-refusal")){
        /* Find a budget which loads the image but refuses client backing. */
        const UmicomU64 baseline=UmicomModelMemory().allocatedFrames;
        for(int i=0;i<16;++i){allocationBudget=i;const UmicomKernelShellStatus s=Command("run /bin/model.elf");allocationBudget=-1;
            if(shell.foreground)CHECK(UmicomKernelConsoleShellFeed(&shell,3U)==UMICOM_SHELL_OK);
            CHECK(s==UMICOM_SHELL_OK||s==UMICOM_SHELL_PROCESS_ERROR||s==UMICOM_SHELL_IO_ERROR);
            CHECK(UmicomModelMemory().allocatedFrames<=baseline+UMICOM_USER_TASK_LIMIT);}
    }else if(!strcmp(name,"unsafe-command")){
        umicomModelAllowed=UMICOM_FALSE;CHECK(Command("mkdir /unsafe")==UMICOM_SHELL_UNSAFE);
        umicomModelAllowed=UMICOM_TRUE;Missing("/unsafe");
    }else if(!strcmp(name,"unsafe-return")){
        umicomModelMode=UMICOM_MODEL_CORRUPT_MACHINE;CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);
        CHECK(UmicomKernelConsoleShellStep(&shell)==UMICOM_SHELL_UNSAFE);
        CHECK(shell.state==UMICOM_VFS_POISONED && shell.foreground!=0U);
        CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_UNSAFE);
        return EXIT_SUCCESS; /* Retention is deliberate after an unverified return. */
    }else if(!strcmp(name,"reentrant-output")){
        tryReentry=UMICOM_TRUE;CHECK(Command("help")==UMICOM_SHELL_OK);tryReentry=UMICOM_FALSE;CHECK(refusedReentries!=0U);
    }else if(!strcmp(name,"partial-close-retry")){
        refuseReleaseAt=releaseCalls+2U;CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_CLEANUP_FAILED);
        CHECK(shell.state==UMICOM_VFS_CLOSING);CHECK(Command("create /notes/no")==UMICOM_SHELL_BAD_STATE);
        refuseReleaseAt=0U;CHECK(Command("poweroff")==UMICOM_SHELL_OK);CHECK(shell.exitRequested);UmicomModelNoLeaks();return EXIT_SUCCESS;
    }else if(!strcmp(name,"shutdown-cancels-live")){
        CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);
        CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_OK);CHECK(shell.foreground==0U);UmicomModelNoLeaks();return EXIT_SUCCESS;
    }else if(!strcmp(name,"close-idempotent")){
        Close();CHECK(UmicomKernelConsoleShellClose(&shell)==UMICOM_SHELL_OK);CHECK(Command("help")==UMICOM_SHELL_BAD_STATE);return EXIT_SUCCESS;
    }else if(!strcmp(name,"poweroff")){
        Feed("poweroff\r\n");CHECK(shell.exitRequested && shell.state==UMICOM_VFS_CLOSED);UmicomModelNoLeaks();return EXIT_SUCCESS;
    }else if(!strcmp(name,"repeated-commands")){
        for(unsigned i=0U;i<1000U;++i){CHECK(Command("create /notes/reused")==UMICOM_SHELL_OK);
            CHECK(Command("write /notes/reused 'a record'")==UMICOM_SHELL_OK);
            CHECK(Command("rm /notes/reused")==UMICOM_SHELL_OK);ResetOutput();}
    }else if(!strcmp(name,"repeated-processes")){
        umicomModelMode=UMICOM_MODEL_EXIT;
        for(unsigned i=0U;i<200U;++i){CHECK(Command("run /bin/model.elf")==UMICOM_SHELL_OK);RunToPrompt();ResetOutput();}
    }else if(!strcmp(name,"memory-and-status")){
        CHECK(Command("mem")==UMICOM_SHELL_OK);CHECK(Command("status")==UMICOM_SHELL_OK);
        CHECK(Command("pwd")==UMICOM_SHELL_OK);CHECK(Command("about")==UMICOM_SHELL_OK);
        CHECK(strstr(transcript,"RAM-only")!=NULL);
    }else{CHECK(0);}
    Close();
    printf("console host case passed: %s\n",name);
    return EXIT_SUCCESS;
}
