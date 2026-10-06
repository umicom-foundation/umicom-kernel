/*-----------------------------------------------------------------------------
 * Umicom Kernel file-service guest acceptance
 * File: kernel/user_files_validation.c
 *
 * Load real user programs, run their requests through the scheduler and verify
 * that terminal lifecycle cleanup closes descriptors without asking the program
 * to cooperate. This is separate from host tests that model privileged entry.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_files.h"
#include "vfs_internal.h"
#include "umicom/kernel/process_supervisor.h"
#include "umicom/kernel/ramfs.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"

extern const UmicomU8 UmicomFileExecutableStart[];
extern const UmicomU8 UmicomFileExecutableEnd[];
static UmicomKernelProcessSupervisor umicomFileSupervisor;
static UmicomKernelUserFiles umicomFileService;
static UmicomKernelRamfs umicomFileRamfs;
static UmicomKernelVfs umicomFileVfs;
static UmicomKernelVfsClient umicomFileAdmin;
static UmicomU64 umicomFileChecks;
static void UmicomFilesExpect(UmicomBoolean condition, const char *reason)
{
    ++umicomFileChecks;
    if (condition) return;
    UmicomKernelConsoleWrite("file-services.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x91U);
    UmicomPlatformHalt();
}
static UmicomKernelSupervisedProcessHandle UmicomFilesSpawn(UmicomU64 mode, UmicomU64 budget,
    UmicomBoolean granted, UmicomKernelVfsRights rights)
{
    UmicomKernelSupervisedProcessHandle handle = 0U;
    UmicomFilesExpect(UmicomKernelProcessSupervisorSpawn(&umicomFileSupervisor, UMICOM_SUPERVISION_GUARDIAN,
        UmicomFileExecutableStart, (UmicomSize)(UmicomFileExecutableEnd-UmicomFileExecutableStart),
        mode, budget, UMICOM_CHILDREN_ADOPT, &handle) == UMICOM_SUPERVISION_OK, "load independent file client");
    if (granted) UmicomFilesExpect(UmicomKernelUserFilesGrant(&umicomFileService, handle, rights) == UMICOM_VFS_OK,
        "grant only Kernel-selected descriptor rights");
    return handle;
}
static UmicomKernelSupervisedProcessInfo UmicomFilesInfo(UmicomKernelSupervisedProcessHandle handle)
{
    UmicomKernelSupervisedProcessInfo info;
    UmicomVfsClear(&info, sizeof(info));
    UmicomFilesExpect(UmicomKernelProcessSupervisorQuery(&umicomFileSupervisor, UMICOM_SUPERVISION_GUARDIAN,
        handle, &info) == UMICOM_SUPERVISION_OK, "query task evidence");
    return info;
}
static void UmicomFilesRun(void)
{
    UmicomKernelSupervisedProcessHandle selected = 0U;
    UmicomFilesExpect(UmicomKernelProcessSupervisorRunOne(&umicomFileSupervisor, 100000U, &selected)
        == UMICOM_SUPERVISION_OK, "run a captured user quantum or file continuation");
}
static UmicomKernelProcessCompletion UmicomFilesCollect(UmicomKernelSupervisedProcessHandle handle)
{
    UmicomKernelProcessCompletion result;
    UmicomVfsClear(&result, sizeof(result));
    UmicomFilesExpect(umicomFileService.records[(UmicomU32)handle-1U].task == 0U,
        "descriptors closed before process memory collection");
    UmicomFilesExpect(UmicomKernelProcessSupervisorCollect(&umicomFileSupervisor, UMICOM_SUPERVISION_GUARDIAN,
        handle, &result) == UMICOM_SUPERVISION_OK, "collect terminal image once");
    return result;
}
static void UmicomFilesUntilTerminal(UmicomKernelSupervisedProcessHandle handle)
{
    for (UmicomSize i = 0U; i < 512U && !UmicomFilesInfo(handle).terminal; ++i) UmicomFilesRun();
    UmicomFilesExpect(UmicomFilesInfo(handle).terminal, "bounded diagnostic program terminates");
}
static UmicomBoolean UmicomFilesMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec && a->mscratch == b->mscratch &&
        a->medeleg == b->medeleg && a->mideleg == b->mideleg && a->satp == b->satp &&
        a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 && a->mepc == b->mepc &&
        a->mcause == b->mcause && a->mtval == b->mtval ? UMICOM_TRUE : UMICOM_FALSE;
}
void UmicomKernelUserFilesValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("file-services-test=begin");
    UmicomKernelPhysicalMemorySnapshot baseline, final;
    UmicomVfsClear(&baseline, sizeof(baseline));
    UmicomVfsClear(&final, sizeof(final));
    UmicomRiscvSupervisorMachineState before, after;
    UmicomVfsClear(&before, sizeof(before));
    UmicomVfsClear(&after, sizeof(after));
    UmicomRiscvSupervisorMachineStateRead(&before);
    UmicomFilesExpect(UmicomKernelPhysicalMemorySnapshotRead(&baseline) == UMICOM_KERNEL_MEMORY_OK, "baseline accounting");
    UmicomFilesExpect(UmicomKernelRamfsInitialize(&umicomFileRamfs) == UMICOM_VFS_OK, "create RAMFS");
    UmicomFilesExpect(UmicomKernelVfsMount(&umicomFileVfs, UmicomKernelRamfsOperations(), &umicomFileRamfs)
        == UMICOM_VFS_OK, "mount file namespace");
    UmicomFilesExpect(UmicomKernelVfsClientOpen(&umicomFileAdmin, &umicomFileVfs, 900U, UMICOM_VFS_RIGHT_ALL)
        == UMICOM_VFS_OK, "trusted setup client");
    UmicomFilesExpect(UmicomKernelVfsCreate(&umicomFileAdmin, "/records", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_OK,
        "create records directory");
    UmicomFilesExpect(UmicomKernelVfsCreate(&umicomFileAdmin, "/shared", UMICOM_VFS_DIRECTORY) == UMICOM_VFS_OK,
        "create shared directory");
    UmicomFilesExpect(UmicomKernelVfsCreate(&umicomFileAdmin, "/shared/report", UMICOM_VFS_FILE) == UMICOM_VFS_OK,
        "create read-only-client input");
    UmicomKernelFileDescriptor input = 0U;
    UmicomSize count = 0U;
    UmicomFilesExpect(UmicomKernelVfsOpen(&umicomFileAdmin, "/shared/report", UMICOM_VFS_RIGHT_WRITE,
        UMICOM_FALSE, &input) == UMICOM_VFS_OK, "open setup file");
    UmicomFilesExpect(UmicomKernelVfsWrite(&umicomFileAdmin, input, "Umicom records", 14U, &count) == UMICOM_VFS_OK &&
        count == 14U, "write shared input");
    UmicomFilesExpect(UmicomKernelVfsClose(&umicomFileAdmin, input) == UMICOM_VFS_OK, "close setup descriptor");
    UmicomFilesExpect(UmicomKernelProcessSupervisorInitialize(&umicomFileSupervisor) == UMICOM_SUPERVISION_OK,
        "create supervised scheduler and IPC");
    UmicomFilesExpect(UmicomKernelUserFilesAttach(&umicomFileService, &umicomFileSupervisor.scheduler, &umicomFileVfs)
        == UMICOM_VFS_OK, "attach file ownership before task admission");

    UmicomKernelConsoleWriteLine("file-services.case=independent-writer-and-reader");
    const UmicomKernelSupervisedProcessHandle writer = UmicomFilesSpawn(0U, 256U, UMICOM_TRUE, UMICOM_VFS_RIGHT_ALL);
    const UmicomKernelSupervisedProcessHandle reader = UmicomFilesSpawn(1U, 256U, UMICOM_TRUE,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY);
    const UmicomU64 writerId = UmicomFilesInfo(writer).identity, readerId = UmicomFilesInfo(reader).identity;
    for (UmicomSize i = 0U; i < 512U && (!UmicomFilesInfo(writer).terminal || !UmicomFilesInfo(reader).terminal); ++i)
        UmicomFilesRun();
    UmicomKernelProcessCompletion completion = UmicomFilesCollect(writer);
    UmicomFilesExpect(completion.state == UMICOM_USER_TASK_EXITED && completion.exitValue == 0x6000U+writerId,
        "writer verifies copied bytes, rights, offsets and unlink-open lifetime");
    completion = UmicomFilesCollect(reader);
    UmicomFilesExpect(completion.state == UMICOM_USER_TASK_EXITED && completion.exitValue == 0x6100U+readerId,
        "reader cannot acquire writer or namespace authority");
    UmicomSize reaped = 0U;
    UmicomFilesExpect(UmicomKernelRamfsReap(&umicomFileRamfs, &reaped) == UMICOM_VFS_OK && reaped == 1U,
        "unlinked journal loses its final pins at writer exit");
    UmicomKernelConsoleWriteLine("file-services.user-file-operations=pass");
    UmicomKernelConsoleWriteLine("file-services.exit-closes-descriptors=pass");

    UmicomKernelConsoleWriteLine("file-services.case=fault-closes-descriptors");
    UmicomKernelSupervisedProcessHandle task = UmicomFilesSpawn(2U, 128U, UMICOM_TRUE, UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY);
    UmicomFilesUntilTerminal(task); completion = UmicomFilesCollect(task);
    UmicomFilesExpect(completion.state == UMICOM_USER_TASK_FAULTED && completion.trapCause == 13U, "fault remains a real user fault");

    UmicomKernelConsoleWriteLine("file-services.case=budget-closes-descriptors");
    task = UmicomFilesSpawn(3U, 16U, UMICOM_TRUE, UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY);
    UmicomFilesUntilTerminal(task); completion = UmicomFilesCollect(task);
    UmicomFilesExpect(completion.state == UMICOM_USER_TASK_EXHAUSTED, "CPU-bound file owner exhausts its slices");

    UmicomKernelConsoleWriteLine("file-services.case=cancel-and-reuse");
    task = UmicomFilesSpawn(4U, 256U, UMICOM_TRUE, UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY);
    UmicomBoolean openedBeforeCancel = UMICOM_FALSE;
    for (UmicomSize i = 0U; i < 128U; ++i) {
        UmicomFilesRun();
        /* A live supervisor report is not a running syscall counter. Check
         * the scheduler's public view, then verify the admitted file client's
         * successful OPEN result before cancellation is allowed to count. */
        UmicomKernelUserTaskInfo running;
        UmicomVfsClear(&running, sizeof(running));
        UmicomFilesExpect(UmicomKernelUserTaskQuery(&umicomFileSupervisor.scheduler, task, &running) == UMICOM_USER_SCHEDULE_OK,
            "query live syscall count");
        if (running.systemCalls != 0U) {
            openedBeforeCancel = running.state == UMICOM_USER_TASK_PAUSED ? UMICOM_TRUE : UMICOM_FALSE;
            break;
        }
    }
    UmicomFilesExpect(openedBeforeCancel &&
        umicomFileSupervisor.scheduler.tasks[(UmicomU32)task-1U].frame.x10_a0 == UMICOM_VFS_OK,
        "file OPEN completed before the cancellation checkpoint");
    UmicomFilesExpect(UmicomKernelProcessSupervisorCancel(&umicomFileSupervisor, UMICOM_SUPERVISION_GUARDIAN, task)
        == UMICOM_SUPERVISION_OK, "cancel without running user close code");
    completion = UmicomFilesCollect(task);
    UmicomFilesExpect(completion.state == UMICOM_USER_TASK_CANCELLED, "cancelled image is collectable after descriptor cleanup");
    const UmicomKernelSupervisedProcessHandle old = task;
    task = UmicomFilesSpawn(5U, 128U, UMICOM_FALSE, 0U);
    UmicomFilesExpect(task != old, "slot reuse does not reuse task authority");
    UmicomKernelConsoleWriteLine("file-services.case=ungranted-replacement");
    UmicomFilesUntilTerminal(task); completion = UmicomFilesCollect(task);
    UmicomFilesExpect(completion.state == UMICOM_USER_TASK_EXITED && completion.exitValue == 0x6500U,
        "replacement does not inherit the previous client");

    UmicomFilesExpect(UmicomKernelUserFilesClose(&umicomFileService) == UMICOM_VFS_OK, "release client cache and mount anchor");
    UmicomFilesExpect(UmicomKernelVfsClientClose(&umicomFileAdmin, &count) == UMICOM_VFS_OK, "close setup authority");
    UmicomFilesExpect(UmicomKernelVfsUnmount(&umicomFileVfs) == UMICOM_VFS_OK, "unmount after every task client is gone");
    UmicomFilesExpect(UmicomKernelRamfsClose(&umicomFileRamfs) == UMICOM_VFS_OK, "return all file and metadata frames");
    UmicomRiscvSupervisorMachineStateRead(&after);
    UmicomFilesExpect(UmicomFilesMachineEqual(&before, &after), "unchanged machine controls");
    UmicomFilesExpect(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK &&
        UmicomKernelPhysicalMemorySnapshotRead(&final) == UMICOM_KERNEL_MEMORY_OK &&
        final.allocatedFrames == baseline.allocatedFrames && final.freeFrames == baseline.freeFrames &&
        final.reservedFrames == baseline.reservedFrames, "all service and process frames returned");
    UmicomKernelConsoleWriteLine("file-services.machine-state=restored");
    UmicomKernelConsoleWriteLine("file-services.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("file-services.completed-cases=5");
    UmicomKernelConsoleWrite("file-services.completed-checks=");
    UmicomKernelConsoleWriteUnsigned(umicomFileChecks);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("file-services-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FILE_SERVICES_READY");
}
