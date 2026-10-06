/*-----------------------------------------------------------------------------
 * Umicom Kernel file-service host tests
 * File: tests/file_services/file_services_tests.c
 *
 * Reuse the established ELF/RAM/CSR model without editing its source. The actual
 * scheduler, restoration adapter, file boundary, VFS, RAMFS and allocators are
 * linked below. Only privileged entry, machine admission and deliberate failure
 * injection are models. A passing host test is not a RISC-V guest execution.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#define main UmicomFileBaselineMain
#define UmicomRiscvUserExecuteFrame UmicomFileBaselineEntry
#include "../user_scheduling/user_scheduling_tests.c"
#undef UmicomRiscvUserExecuteFrame
#undef main
#include "umicom/kernel/user_files.h"
#include "umicom/kernel/ramfs.h"
#include "umicom/kernel/user_ipc.h"
#include "umicom/kernel/process_supervisor.h"

#define REQUIRE(x) UmicomModelRequire((x), #x)
#define VFS(x) REQUIRE((x) == UMICOM_VFS_OK)
#define FILE_MODEL_CALL 100U
#define FILE_REQUEST ((UmicomAddress)0x600040U)
#define FILE_REPLY ((UmicomAddress)0x6000a0U)
#define FILE_PATH ((UmicomAddress)0x600100U)
#define FILE_DATA ((UmicomAddress)0x600ff0U)
#define FILE_RW (UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE)
static UmicomKernelRamfs fileStore;
static UmicomKernelVfs fileVfs;
static UmicomKernelVfsClient fileAdmin;
static UmicomKernelUserFiles fileOwner;
static UmicomKernelUserIpc fileIpc;
static UmicomKernelVfsOperations fileOperations;
static UmicomBoolean fileInsideTrap;
static UmicomBoolean fileUnsafe;
static UmicomBoolean fileFailUnpin;
static int fileAllocationBudget = -1;
static unsigned fileMutation;
static UmicomAddress fileRequestAddress = FILE_REQUEST;
static UmicomAddress fileReplyAddress = FILE_REPLY;
static UmicomU64 fileRequestBytes = sizeof(UmicomKernelFileRequest);
static UmicomU64 fileReplyBytes = sizeof(UmicomKernelFileResult);
static UmicomBoolean fileWrongSession;
static UmicomU64 fileCallNumber = UMICOM_USER_CALL_FILE;
static UmicomKernelMessageHandle fileEndpoint;
static UmicomBoolean fileBlockCall;
static UmicomU64 fileLastStatus;
static UmicomKernelFileResult fileLastResult;

/* File allocation must not be made legal merely because a syscall is active.
 * During the simulated trap this gate is false, just as the real satp/mie gate
 * is unavailable. Successful calls therefore prove deferred VFS execution. */
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    return !fileInsideTrap && !fileUnsafe && umicomModelAllowed ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelMemoryStatus UmicomFileActualAllocate(UmicomAddress *out);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *out)
{
    if (fileAllocationBudget == 0) return UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY;
    const UmicomKernelMemoryStatus status = UmicomFileActualAllocate(out);
    if (status == UMICOM_KERNEL_MEMORY_OK && fileAllocationBudget > 0) --fileAllocationBudget;
    return status;
}
static UmicomKernelVfsStatus FileUnpin(void *context, UmicomKernelVfsNodeId node)
{
    if (fileFailUnpin) return UMICOM_VFS_RELEASE_FAILED;
    return UmicomKernelRamfsOperations()->unpin(context, node);
}
static void FilePut(UmicomKernelUserTaskHandle task, UmicomAddress address, const void *bytes, UmicomSize size)
{
    for (UmicomSize offset = 0U; offset < size;) {
        const UmicomSize chunk = size-offset < 64U ? size-offset : 64U;
        REQUIRE(UmicomKernelUserMemoryWrite(&UmicomModelRecord(task)->process.report.memory,
            address+offset, (const UmicomU8 *)bytes+offset, chunk) == UMICOM_USER_RESULT_OK);
        offset += chunk;
    }
}
static void FileGet(UmicomKernelUserTaskHandle task, UmicomAddress address, void *bytes, UmicomSize size)
{
    for (UmicomSize offset = 0U; offset < size;) {
        const UmicomSize chunk = size-offset < 64U ? size-offset : 64U;
        REQUIRE(UmicomKernelUserMemoryRead(&UmicomModelRecord(task)->process.report.memory,
            address+offset, (UmicomU8 *)bytes+offset, chunk) == UMICOM_USER_RESULT_OK);
        offset += chunk;
    }
}
UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame)
{
    if (umicomModelMode != FILE_MODEL_CALL) return UmicomFileBaselineEntry(request, session, frame);
    ++umicomModelEntries;
    REQUIRE(request->rootTablePhysicalAddress == session->memory.space->rootTablePhysicalAddress);
    frame->mstatus = (UmicomU64)2U << 32U;
    frame->reserved = 0U; frame->mcause = 8U; frame->mtval = 0U;
    frame->x17_a7 = fileBlockCall ? UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT : fileCallNumber;
    frame->x10_a0 = fileBlockCall ? fileEndpoint : fileRequestAddress;
    frame->x11_a1 = fileBlockCall ? FILE_DATA : fileReplyAddress;
    frame->x12_a2 = fileBlockCall ? 280U : fileRequestBytes;
    frame->x13_a3 = fileBlockCall ? UMICOM_IPC_WAIT_FOREVER : fileReplyBytes;
    UmicomKernelUserSession fake = *session;
    fileInsideTrap = UMICOM_TRUE;
    const UmicomU64 resumed = UmicomKernelUserTrapDispatch(fileWrongSession ? &fake : session, frame);
    fileInsideTrap = UMICOM_FALSE;
    if (!resumed) {
        /* Change user input after capture. A correct deferred operation uses
         * its own snapshot, not a second read of this now-different input. */
        if (fileMutation == 1U || fileMutation == 2U || fileMutation == 3U) {
            UmicomU8 change[64]; memset(change, 'X', sizeof(change));
            const UmicomAddress address = fileMutation == 1U ? FILE_PATH : fileMutation == 2U ? FILE_DATA : FILE_REQUEST;
            REQUIRE(UmicomKernelUserMemoryWrite(&session->memory, address, change, sizeof(change)) == UMICOM_USER_RESULT_OK);
        }
        if (fileMutation == 4U) {
            REQUIRE(UmicomKernelVirtualMemoryUnmapPage((UmicomKernelVirtualAddressSpace *)session->memory.space,
                (UmicomAddress)0x600000U) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
        }
        return 0U;
    }
    /* A synchronous refusal resumes user code, then encounters a model timer.
     * Accepted requests above return immediately with the ECALL frame retained. */
    umicomModelNow = umicomModelCompare;
    frame->mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
    REQUIRE(UmicomKernelUserTrapDispatch(session, frame) == 0U);
    return 0U;
}
static void FileSetup(void)
{
    UmicomModelSetup();
    /* Two writable pages let inputs and outputs cross a real virtual boundary. */
    UmicomModelPut(64U+2U*56U+40U, 8192U, 8U);
    VFS(UmicomKernelRamfsInitialize(&fileStore));
    fileOperations = *UmicomKernelRamfsOperations();
    fileOperations.unpin = FileUnpin;
    VFS(UmicomKernelVfsMount(&fileVfs, &fileOperations, &fileStore));
    VFS(UmicomKernelVfsClientOpen(&fileAdmin, &fileVfs, 900U, UMICOM_VFS_RIGHT_ALL));
    VFS(UmicomKernelVfsCreate(&fileAdmin, "/base", UMICOM_VFS_DIRECTORY));
    VFS(UmicomKernelVfsCreate(&fileAdmin, "/base/input", UMICOM_VFS_FILE));
    UmicomKernelFileDescriptor fd = 0U;
    UmicomSize count = 0U;
    VFS(UmicomKernelVfsOpen(&fileAdmin, "/base/input", FILE_RW, UMICOM_FALSE, &fd));
    VFS(UmicomKernelVfsWrite(&fileAdmin, fd, "abcdefghijklmnop", 16U, &count));
    VFS(UmicomKernelVfsClose(&fileAdmin, fd));
    REQUIRE(UmicomKernelUserIpcAttach(&fileIpc, &umicomModelScheduler) == UMICOM_USER_SCHEDULE_OK);
    VFS(UmicomKernelUserFilesAttach(&fileOwner, &umicomModelScheduler, &fileVfs));
}
static UmicomKernelUserTaskHandle FileTask(UmicomU64 budget, UmicomKernelVfsRights rights)
{
    const UmicomKernelUserTaskHandle task = UmicomModelCreate(budget);
    VFS(UmicomKernelUserFilesGrant(&fileOwner, task, rights));
    return task;
}
static UmicomU64 FileCall(UmicomKernelUserTaskHandle task, UmicomKernelFileRequest request)
{
    FilePut(task, FILE_REQUEST, &request, sizeof(request));
    const UmicomKernelFileResult empty = {0xeeeeU, 0xddddU};
    FilePut(task, FILE_REPLY, &empty, sizeof(empty));
    const UmicomAddress oldPc = UmicomModelInfo(task).resumePc;
    const UmicomU64 oldCalls = UmicomModelInfo(task).systemCalls;
    umicomModelMode = FILE_MODEL_CALL;
    umicomModelScheduler.next = (UmicomU32)task-1U;
    REQUIRE(UmicomModelRun() == task);
    fileLastStatus = UmicomModelRecord(task)->frame.x10_a0;
    if (fileMutation != 4U) FileGet(task, FILE_REPLY, &fileLastResult, sizeof(fileLastResult));
    REQUIRE(UmicomModelInfo(task).resumePc == oldPc+4U);
    /* A forged copy is refused without changing the real session's counter. */
    REQUIRE(UmicomModelInfo(task).systemCalls == oldCalls+(fileWrongSession ? 0U : 1U));
    return fileLastStatus;
}
static UmicomKernelFileRequest FileRequest(UmicomU64 op, UmicomU64 fd, UmicomU64 address,
    UmicomU64 bytes, UmicomU64 argument)
{
    return (UmicomKernelFileRequest){op,fd,address,bytes,argument,0U};
}
static UmicomKernelFileDescriptor FileOpen(UmicomKernelUserTaskHandle task, const char *path, UmicomU64 rights)
{
    FilePut(task, FILE_PATH, path, strlen(path));
    REQUIRE(FileCall(task, FileRequest(UMICOM_FILE_OPEN,0U,FILE_PATH,strlen(path),rights)) == UMICOM_VFS_OK);
    REQUIRE(fileLastResult.status == UMICOM_VFS_OK && fileLastResult.value != 0U);
    return fileLastResult.value;
}
static void FileFinish(void)
{
    fileUnsafe = UMICOM_FALSE; fileFailUnpin = UMICOM_FALSE; fileAllocationBudget = -1;
    for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
        UmicomKernelUserTask *task = &umicomModelScheduler.tasks[i];
        if (task->state == UMICOM_USER_TASK_EMPTY) continue;
        const UmicomKernelUserTaskHandle token = ((UmicomU64)task->generation<<32U)|(i+1U);
        if (task->state == UMICOM_USER_TASK_READY || task->state == UMICOM_USER_TASK_PAUSED || task->state == UMICOM_USER_TASK_BLOCKED)
            REQUIRE(UmicomKernelUserTaskCancel(&umicomModelScheduler, token) == UMICOM_USER_SCHEDULE_OK);
        REQUIRE(UmicomKernelUserTaskReap(&umicomModelScheduler, token) == UMICOM_USER_SCHEDULE_OK);
    }
    VFS(UmicomKernelUserFilesClose(&fileOwner));
    UmicomSize count = 0U;
    VFS(UmicomKernelVfsClientClose(&fileAdmin, &count));
    VFS(UmicomKernelVfsUnmount(&fileVfs));
    VFS(UmicomKernelRamfsClose(&fileStore));
    REQUIRE(!umicomModelScheduler.files);
    UmicomModelNoLeaks();
}
static void FileCase(const char *name)
{
    FileSetup();
    if (!strcmp(name,"attach-lifetime")) {
        REQUIRE(fileVfs.clients == 2U);
        REQUIRE(UmicomKernelVfsUnmount(&fileVfs) == UMICOM_VFS_BUSY);
    } else if (!strcmp(name,"repeat-attach")) {
        REQUIRE(UmicomKernelUserFilesAttach(&fileOwner,&umicomModelScheduler,&fileVfs) == UMICOM_VFS_BAD_STATE);
    } else if (!strcmp(name,"copied-owner")) {
        UmicomKernelUserFiles *copy = malloc(sizeof(*copy)); REQUIRE(copy != NULL); *copy = fileOwner;
        REQUIRE(UmicomKernelUserFilesClose(copy) == UMICOM_VFS_BAD_STATE); free(copy);
    } else if (!strcmp(name,"invalid-rights")) {
        UmicomKernelUserTaskHandle a = UmicomModelCreate(100U);
        REQUIRE(UmicomKernelUserFilesGrant(&fileOwner,a,0x80000000U) == UMICOM_VFS_INVALID_ARGUMENT);
        REQUIRE(fileOwner.records[0].task == 0U);
    } else if (!strcmp(name,"duplicate-grant")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_READ);
        REQUIRE(UmicomKernelUserFilesGrant(&fileOwner,a,UMICOM_VFS_RIGHT_ALL) == UMICOM_VFS_BUSY);
    } else if (!strcmp(name,"late-grant")) {
        UmicomKernelUserTaskHandle a = UmicomModelCreate(100U); (void)UmicomModelRun();
        REQUIRE(UmicomKernelUserFilesGrant(&fileOwner,a,UMICOM_VFS_RIGHT_READ) == UMICOM_VFS_BAD_STATE);
    } else if (!strcmp(name,"no-grant")) {
        UmicomKernelUserTaskHandle a = UmicomModelCreate(100U);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_CLOSE,1U,0U,0U,0U)) == UMICOM_FILE_SERVICE_UNBOUND);
    } else if (!strcmp(name,"wrong-session")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_ALL); fileWrongSession = UMICOM_TRUE;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_CLOSE,1U,0U,0U,0U)) == UMICOM_FILE_SERVICE_UNBOUND);
    } else if (!strcmp(name,"late-attach")) {
        UmicomKernelUserFiles other = {0};
        (void)UmicomModelCreate(100U);
        REQUIRE(UmicomKernelUserFilesAttach(&other,&umicomModelScheduler,&fileVfs) == UMICOM_VFS_BAD_STATE);
    } else if (!strcmp(name,"read-only-client")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_READ);
        UmicomKernelFileDescriptor fd = FileOpen(a,"/base/input",UMICOM_VFS_RIGHT_READ);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_WRITE,fd,FILE_DATA,1U,0U)) == UMICOM_VFS_ACCESS_DENIED);
        FilePut(a,FILE_PATH,"/denied",7U);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_CREATE,0U,FILE_PATH,7U,UMICOM_VFS_FILE)) == UMICOM_VFS_ACCESS_DENIED);
    } else if (!strcmp(name,"path-snapshot") || !strcmp(name,"request-snapshot")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        fileMutation = !strcmp(name,"path-snapshot") ? 1U : 3U;
        (void)FileOpen(a,"/base/input",FILE_RW); REQUIRE(fileOwner.completed == 1U);
    } else if (!strcmp(name,"write-snapshot") || !strcmp(name,"input-cross-page")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        UmicomKernelFileDescriptor fd = FileOpen(a,"/base/input",FILE_RW);
        UmicomU8 bytes[32],readback[32]; memset(bytes,0x5a,sizeof(bytes)); FilePut(a,FILE_DATA,bytes,sizeof(bytes));
        fileMutation = !strcmp(name,"write-snapshot") ? 2U : 0U;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_WRITE,fd,FILE_DATA,32U,0U)) == UMICOM_VFS_OK);
        fileMutation=0U;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_SEEK,fd,0U,0U,0U)) == UMICOM_VFS_OK);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,fd,FILE_DATA,32U,0U)) == UMICOM_VFS_OK);
        FileGet(a,FILE_DATA,readback,sizeof(readback)); REQUIRE(!memcmp(bytes,readback,sizeof(bytes)));
    } else if (!strcmp(name,"invalid-request-address") || !strcmp(name,"invalid-result-address") ||
        !strcmp(name,"malformed-wire-size") || !strcmp(name,"result-recheck")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        FilePut(a,FILE_PATH,"/base/input",11U);
        if (!strcmp(name,"invalid-request-address")) fileRequestAddress=0U;
        if (!strcmp(name,"invalid-result-address")) fileReplyAddress=0U;
        if (!strcmp(name,"malformed-wire-size")) fileRequestBytes=47U;
        if (!strcmp(name,"result-recheck")) fileMutation=4U;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_OPEN,0U,FILE_PATH,11U,FILE_RW)) ==
            (!strcmp(name,"malformed-wire-size") ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_FILE_BAD_USER_BUFFER));
        UmicomKernelVfsClient *client = fileOwner.records[0].client.address;
        REQUIRE(client->descriptions[0].references == 0U);
    } else if (!strcmp(name,"missing-input-tail") || !strcmp(name,"read-invalid-tail") ||
        !strcmp(name,"read-result-overlap") || !strcmp(name,"huge-transfer")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        UmicomKernelFileDescriptor fd = FileOpen(a,"/base/input",FILE_RW);
        UmicomU64 op=!strcmp(name,"missing-input-tail") ? UMICOM_FILE_WRITE : UMICOM_FILE_READ;
        UmicomAddress addr=!strcmp(name,"read-result-overlap") ? FILE_REPLY : 0x601ff8U;
        UmicomSize length=!strcmp(name,"huge-transfer") ? 4097U : 32U;
        UmicomU64 expected=!strcmp(name,"huge-transfer") ? UMICOM_FILE_TOO_LARGE :
            !strcmp(name,"read-result-overlap") ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_FILE_BAD_USER_BUFFER;
        REQUIRE(FileCall(a,FileRequest(op,fd,addr,length,0U)) == expected);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,fd,FILE_DATA,16U,0U)) == UMICOM_VFS_OK);
        UmicomU8 data[16]; FileGet(a,FILE_DATA,data,16U); REQUIRE(!memcmp(data,"abcdefghijklmnop",16U));
    } else if (!strcmp(name,"embedded-nul") || !strcmp(name,"reserved-fields") || !strcmp(name,"unknown-operation")) {
        UmicomKernelUserTaskHandle a = FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        const UmicomU8 path[8]={'/','b','a','s','e',0,'x','x'}; FilePut(a,FILE_PATH,path,8U);
        UmicomKernelFileRequest r=FileRequest(UMICOM_FILE_OPEN,0U,FILE_PATH,8U,FILE_RW);
        if (!strcmp(name,"reserved-fields")) r.options=2U;
        if (!strcmp(name,"unknown-operation")) r.operation=99U;
        REQUIRE(FileCall(a,r) == (!strcmp(name,"embedded-nul") ? UMICOM_VFS_INVALID_PATH : UMICOM_VFS_INVALID_ARGUMENT));
    } else if (!strcmp(name,"null-zero-io")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL); UmicomKernelFileDescriptor fd=FileOpen(a,"/base/input",FILE_RW);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,fd,0U,0U,0U))==UMICOM_VFS_OK && fileLastResult.value==0U);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_WRITE,fd,0U,0U,0U))==UMICOM_VFS_OK && fileLastResult.value==0U);
    } else if (!strcmp(name,"descriptor-locality") || !strcmp(name,"independent-position")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL), b=FileTask(100U,UMICOM_VFS_RIGHT_READ);
        UmicomKernelFileDescriptor fa=FileOpen(a,"/base/input",FILE_RW), fb=FileOpen(b,"/base/input",UMICOM_VFS_RIGHT_READ);
        REQUIRE(fa==fb); /* Equal tokens select only their own session's client. */
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,fa,FILE_DATA,8U,0U))==UMICOM_VFS_OK);
        REQUIRE(FileCall(b,FileRequest(UMICOM_FILE_READ,fa,FILE_DATA,1U,0U))==UMICOM_VFS_OK);
        UmicomU8 value=0U; FileGet(b,FILE_DATA,&value,1U); REQUIRE(value=='a');
        REQUIRE(FileCall(b,FileRequest(UMICOM_FILE_WRITE,fa,FILE_DATA,1U,0U))==UMICOM_VFS_ACCESS_DENIED);
    } else if (!strcmp(name,"duplicate-position") || !strcmp(name,"duplicate-rights")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL); UmicomKernelFileDescriptor fd=FileOpen(a,"/base/input",FILE_RW);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_DUPLICATE,fd,0U,0U,UMICOM_VFS_RIGHT_READ))==UMICOM_VFS_OK);
        UmicomKernelFileDescriptor dup=fileLastResult.value;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,fd,FILE_DATA,5U,0U))==UMICOM_VFS_OK);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,dup,FILE_DATA,1U,0U))==UMICOM_VFS_OK);
        UmicomU8 value=0U; FileGet(a,FILE_DATA,&value,1U); REQUIRE(value=='f');
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_DUPLICATE,dup,0U,0U,FILE_RW))==UMICOM_VFS_ACCESS_DENIED);
    } else if (!strcmp(name,"stat-wire")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL); UmicomKernelFileDescriptor fd=FileOpen(a,"/base/input",FILE_RW);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_QUERY,fd,FILE_DATA,sizeof(UmicomKernelFileInfo),0U))==UMICOM_VFS_OK);
        UmicomKernelFileInfo info={0}; FileGet(a,FILE_DATA,&info,sizeof(info));
        REQUIRE(info.identity>1U && info.kind==UMICOM_VFS_FILE && info.bytes==16U && info.maximumBytes==UMICOM_RAMFS_FILE_BYTES);
    } else if (!strcmp(name,"directory-wire") || !strcmp(name,"directory-change")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL); UmicomKernelFileDescriptor fd=FileOpen(a,"/base",UMICOM_VFS_RIGHT_ENUMERATE);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ_DIRECTORY,fd,FILE_DATA,sizeof(UmicomKernelFileEntry),0U))==UMICOM_VFS_OK);
        UmicomKernelFileEntry entry={0}; FileGet(a,FILE_DATA,&entry,sizeof(entry)); REQUIRE(!strcmp(entry.name,"input") && entry.info.bytes==16U);
        VFS(UmicomKernelVfsCreate(&fileAdmin,"/base/new",UMICOM_VFS_FILE));
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ_DIRECTORY,fd,FILE_DATA,sizeof(entry),0U))==UMICOM_VFS_CHANGED);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_REWIND_DIRECTORY,fd,0U,0U,0U))==UMICOM_VFS_OK);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ_DIRECTORY,fd,FILE_DATA,sizeof(entry),0U))==UMICOM_VFS_OK);
    } else if (!strcmp(name,"resize-zero-fill")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL); UmicomKernelFileDescriptor fd=FileOpen(a,"/base/input",FILE_RW);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_RESIZE,fd,0U,0U,0U))==UMICOM_VFS_OK);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_RESIZE,fd,0U,0U,32U))==UMICOM_VFS_OK);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_READ,fd,FILE_DATA,32U,0U))==UMICOM_VFS_OK);
        UmicomU8 data[32]; FileGet(a,FILE_DATA,data,32U); for(unsigned i=0U;i<32U;++i) REQUIRE(data[i]==0U);
    } else if (!strcmp(name,"append")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL); FilePut(a,FILE_PATH,"/base/input",11U);
        UmicomKernelFileRequest r=FileRequest(UMICOM_FILE_OPEN,0U,FILE_PATH,11U,FILE_RW);r.options=1U;
        REQUIRE(FileCall(a,r)==UMICOM_VFS_OK); UmicomKernelFileDescriptor fd=fileLastResult.value;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_SEEK,fd,0U,0U,0U))==UMICOM_VFS_OK);
        FilePut(a,FILE_DATA,"Z",1U); REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_WRITE,fd,FILE_DATA,1U,0U))==UMICOM_VFS_OK);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_QUERY,fd,FILE_DATA,32U,0U))==UMICOM_VFS_OK);
        UmicomKernelFileInfo info={0};FileGet(a,FILE_DATA,&info,sizeof(info));REQUIRE(info.bytes==17U);
    } else if (!strcmp(name,"partial-write")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        VFS(UmicomKernelVfsCreate(&fileAdmin,"/base/empty",UMICOM_VFS_FILE));
        UmicomKernelFileDescriptor fd=FileOpen(a,"/base/empty",FILE_RW);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_SEEK,fd,0U,0U,4090U))==UMICOM_VFS_OK);
        UmicomU8 data[32];memset(data,0x77,32U);FilePut(a,FILE_DATA,data,32U);
        fileAllocationBudget=1;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_WRITE,fd,FILE_DATA,32U,0U))==UMICOM_VFS_NO_MEMORY);
        REQUIRE(fileLastResult.status==UMICOM_VFS_NO_MEMORY && fileLastResult.value==6U);
        fileAllocationBudget=-1;
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_QUERY,fd,FILE_DATA,32U,0U))==UMICOM_VFS_OK);
        UmicomKernelFileInfo info={0};FileGet(a,FILE_DATA,&info,sizeof(info));REQUIRE(info.bytes==4096U);
    } else if (!strcmp(name,"close-stale-token")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);UmicomKernelFileDescriptor fd=FileOpen(a,"/base/input",FILE_RW);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_CLOSE,fd,0U,0U,0U))==UMICOM_VFS_OK);
        UmicomKernelFileDescriptor replacement=FileOpen(a,"/base/input",FILE_RW);REQUIRE(replacement!=fd);
        REQUIRE(FileCall(a,FileRequest(UMICOM_FILE_CLOSE,fd,0U,0U,0U))==UMICOM_VFS_INVALID_DESCRIPTOR);
    } else if (!strcmp(name,"terminal-exit") || !strcmp(name,"terminal-fault") || !strcmp(name,"cancellation") ||
        !strcmp(name,"paused-file-lifetime") || !strcmp(name,"slice-budget") || !strcmp(name,"last-slice-open")) {
        UmicomU64 budget=!strcmp(name,"last-slice-open") ? 1U : !strcmp(name,"slice-budget") ? 2U : 100U;
        UmicomKernelUserTaskHandle a=FileTask(budget,UMICOM_VFS_RIGHT_ALL);(void)FileOpen(a,"/base/input",FILE_RW);
        if (!strcmp(name,"last-slice-open")) { REQUIRE(UmicomModelInfo(a).state==UMICOM_USER_TASK_EXHAUSTED); }
        else if (!strcmp(name,"cancellation")) REQUIRE(UmicomKernelUserTaskCancel(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_OK);
        else {
            umicomModelMode=!strcmp(name,"terminal-exit") ? UMICOM_MODEL_EXIT :
                !strcmp(name,"terminal-fault") ? UMICOM_MODEL_FAULT : UMICOM_MODEL_TIMER;
            (void)UmicomModelRun();
            if (!strcmp(name,"paused-file-lifetime")) {
                REQUIRE(fileOwner.records[0].task==a);
                REQUIRE(UmicomKernelUserTaskReap(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_BAD_STATE);
                REQUIRE(UmicomKernelUserTaskCancel(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_OK);
            }
        }
        REQUIRE(fileOwner.records[0].task==0U && fileVfs.clients==2U);
    } else if (!strcmp(name,"close-before-reap")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);(void)FileOpen(a,"/base/input",FILE_RW);
        REQUIRE(UmicomKernelUserFilesClose(&fileOwner)==UMICOM_VFS_BUSY);
    } else if (!strcmp(name,"grant-allocation-refusal")) {
        UmicomKernelUserTaskHandle a=UmicomModelCreate(100U);
        fileAllocationBudget=0;
        REQUIRE(UmicomKernelUserFilesGrant(&fileOwner,a,FILE_RW)==UMICOM_VFS_NO_MEMORY);
        REQUIRE(fileOwner.records[0].task==0U && fileVfs.clients==2U);
        fileAllocationBudget=-1;
        VFS(UmicomKernelUserFilesGrant(&fileOwner,a,FILE_RW));
        (void)FileOpen(a,"/base/input",FILE_RW);
    } else if (!strcmp(name,"grant-client-limit")) {
        UmicomKernelVfsClient *others=calloc(UMICOM_VFS_CLIENT_LIMIT,sizeof(*others));
        REQUIRE(others!=NULL);
        for(UmicomSize i=0U;i<UMICOM_VFS_CLIENT_LIMIT-2U;++i)
            VFS(UmicomKernelVfsClientOpen(&others[i],&fileVfs,1000U+i,0U));
        UmicomKernelUserTaskHandle a=UmicomModelCreate(100U);
        REQUIRE(UmicomKernelUserFilesGrant(&fileOwner,a,FILE_RW)!=UMICOM_VFS_OK);
        REQUIRE(fileOwner.records[0].task==0U && fileVfs.clients==UMICOM_VFS_CLIENT_LIMIT);
        UmicomSize closed=0U;
        for(UmicomSize i=0U;i<UMICOM_VFS_CLIENT_LIMIT-2U;++i)
            VFS(UmicomKernelVfsClientClose(&others[i],&closed));
        free(others);
        VFS(UmicomKernelUserFilesGrant(&fileOwner,a,FILE_RW));
        (void)FileOpen(a,"/base/input",FILE_RW);
    } else if (!strcmp(name,"cleanup-retry")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);(void)FileOpen(a,"/base/input",FILE_RW);
        fileFailUnpin=UMICOM_TRUE;
        REQUIRE(UmicomKernelUserTaskCancel(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_CLEANUP_FAILED);
        REQUIRE(fileOwner.records[0].task==a && UmicomModelInfo(a).state==UMICOM_USER_TASK_CANCELLED);
        REQUIRE(UmicomKernelUserTaskReap(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_CLEANUP_FAILED);
        fileFailUnpin=UMICOM_FALSE;
        REQUIRE(UmicomKernelUserTaskReap(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_OK);
        REQUIRE(fileOwner.records[0].task==0U);
    } else if (!strcmp(name,"reuse-no-inheritance") || !strcmp(name,"repeated-lifetimes")) {
        const unsigned lifetimes=!strcmp(name,"repeated-lifetimes") ? 1000U : 1U;
        for(unsigned i=0U;i<lifetimes;++i) {
            UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);(void)FileOpen(a,"/base/input",FILE_RW);
            REQUIRE(UmicomKernelUserTaskCancel(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_OK);
            REQUIRE(UmicomKernelUserTaskReap(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_OK);
        }
        UmicomKernelUserTaskHandle b=UmicomModelCreate(100U);
        REQUIRE(FileCall(b,FileRequest(UMICOM_FILE_CLOSE,0x100000001ULL,0U,0U,0U))==UMICOM_FILE_SERVICE_UNBOUND);
    } else if (!strcmp(name,"ipc-blocked-cancel") || !strcmp(name,"ipc-final-slice")) {
        const UmicomU64 budget = !strcmp(name,"ipc-final-slice") ? 2U : 100U;
        UmicomKernelUserTaskHandle a=FileTask(budget,UMICOM_VFS_RIGHT_ALL), b=UmicomModelCreate(100U);
        UmicomKernelMessageHandle send=0U;
        REQUIRE(UmicomKernelUserIpcConnect(&fileIpc,a,UMICOM_MESSAGE_RIGHT_RECEIVE,b,UMICOM_MESSAGE_RIGHT_SEND,
            &fileEndpoint,&send)==UMICOM_MESSAGE_OK);
        /* Endpoints are admission-time authority; open the file only after
         * both fresh tasks have received their communication references. */
        (void)FileOpen(a,"/base/input",FILE_RW);
        fileBlockCall=UMICOM_TRUE;umicomModelScheduler.next=0U;(void)UmicomModelRun();
        if (!strcmp(name,"ipc-final-slice")) {
            REQUIRE(UmicomModelInfo(a).state==UMICOM_USER_TASK_EXHAUSTED);
        } else {
            REQUIRE(UmicomModelInfo(a).state==UMICOM_USER_TASK_BLOCKED && fileOwner.records[0].task==a);
            REQUIRE(UmicomKernelUserTaskCancel(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_OK);
        }
        REQUIRE(fileOwner.records[0].task==0U);fileBlockCall=UMICOM_FALSE;
    } else if (!strcmp(name,"context-refusal") || !strcmp(name,"wrong-hart-refusal")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);
        if (!strcmp(name,"context-refusal")) umicomModelAllowed=UMICOM_FALSE;else umicomModelHart=1U;
        UmicomKernelUserTaskHandle out=99U;
        REQUIRE(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler,1000U,&out)!=UMICOM_USER_SCHEDULE_OK && out==99U);
        REQUIRE(UmicomModelInfo(a).slices==0U);umicomModelAllowed=UMICOM_TRUE;umicomModelHart=0U;
    } else if (!strcmp(name,"unverified-machine-retention")) {
        UmicomKernelUserTaskHandle a=FileTask(100U,UMICOM_VFS_RIGHT_ALL);(void)FileOpen(a,"/base/input",FILE_RW);
        umicomModelMode=UMICOM_MODEL_CORRUPT_MACHINE;UmicomKernelUserTaskHandle out=0U;
        REQUIRE(UmicomKernelUserSchedulerRunOne(&umicomModelScheduler,1000U,&out)==UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR);
        REQUIRE(umicomModelScheduler.poisoned && fileOwner.records[0].task==a);
        REQUIRE(UmicomKernelUserTaskReap(&umicomModelScheduler,a)==UMICOM_USER_SCHEDULE_MACHINE_STATE_ERROR);
        return; /* Intentional containment: no fake successful cleanup is reported. */
    } else { fprintf(stderr,"Unknown file test: %s\n",name);exit(1); }
    FileFinish();
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    FileCase(argv[1]);
    printf("PASS %s (%u checks)\n",argv[1],umicomModelChecks);
    return 0;
}
