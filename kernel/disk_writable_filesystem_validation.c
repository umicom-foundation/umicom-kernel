/*-----------------------------------------------------------------------------
 * Umicom Kernel persistent VFS and actual U-mode file-service qualification
 *
 * The writer runs an independently linked executable through the existing
 * supervised scheduler and copied file ABI. A reduced-rights process observes
 * the committed result, then a separate read-only boot reopens the same disk.
 * The raw-media oracle checks every byte, including allocation, removed-entry
 * storage and retained free/slack bytes. Ordered acceptance is not a claim of
 * journalling, rollback, power-loss atomicity or physical-hardware qualification.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_writable_filesystem_validation.h"
#include "umicom/kernel/disk_writable_filesystem.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/user_files.h"
#include "umicom/kernel/process_supervisor.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/disk_writable_filesystem/guest_fixture.h"

#if (defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_TEST) + \
     defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_TEST) + \
     defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READ_ONLY_TEST)) != 1
#error "Select exactly one dedicated writable-filesystem qualification role"
#endif

static void UmicomKernelDiskWritableClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static void UmicomKernelDiskWritableRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("disk-writable.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x96U); UmicomPlatformHalt();
    for (;;) {}
}
static UmicomBoolean UmicomKernelDiskWritableMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomKernelDiskWritableDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomKernelDiskWritableRequire(UmicomPlatformBlockDomainGet(&domain) == UMICOM_BLOCK_OK,
        "qualified platform block catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomKernelDiskWritableClear(&info, sizeof(info));
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) {
            *outSlot = i; ++found;
        } else UmicomKernelDiskWritableRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "bounded transport identity probe");
    }
    UmicomKernelDiskWritableRequire(found == 1U, "exactly one disposable FAT16 test device");
    return domain;
}
#if !defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READ_ONLY_TEST)
static void UmicomKernelDiskWritableGeometry(UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomBoolean writable)
{
    UmicomKernelBlockInfo info;
    UmicomKernelDiskWritableClear(&info, sizeof(info));
    /* The harmless pre-admission identity probe does not negotiate capacity.
     * Observe geometry only after this role owns a successfully opened lease. */
    UmicomKernelDiskWritableRequire(UmicomKernelBlockProbe(domain, slot, &info) == UMICOM_BLOCK_OK &&
        info.sectors == UMICOM_DISK_FIXTURE_SECTORS && info.writable == writable,
        "dedicated synthetic geometry and requested device authority");
}
#endif
static void UmicomKernelDiskWritableRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomKernelDiskWritableClear(&after, sizeof(after));
    UmicomKernelDiskWritableClear(&machineAfter, sizeof(machineAfter));
    UmicomKernelDiskWritableRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomKernelDiskWritableRequire(UmicomKernelDiskWritableMachineEqual(machineBefore, &machineAfter),
        "privileged machine state restored");
    UmicomKernelConsoleWriteLine("disk-writable.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("disk-writable.machine-state=restored");
}

#if defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_TEST)
extern const UmicomU8 UmicomKernelDiskFileExecutableStart[];
extern const UmicomU8 UmicomKernelDiskFileExecutableEnd[];
static UmicomKernelDiskWritableMount umicomKernelDiskWritableMount;
static UmicomKernelProcessSupervisor umicomKernelDiskWritableSupervisor;
static UmicomKernelUserFiles umicomKernelDiskWritableFiles;

static void UmicomKernelDiskWritableProcess(UmicomU64 mode, UmicomKernelVfsRights rights)
{
    UmicomKernelSupervisedProcessHandle task = 0U;
    UmicomKernelDiskWritableRequire(UmicomKernelProcessSupervisorSpawn(&umicomKernelDiskWritableSupervisor,
        UMICOM_SUPERVISION_GUARDIAN, UmicomKernelDiskFileExecutableStart,
        (UmicomSize)(UmicomKernelDiskFileExecutableEnd - UmicomKernelDiskFileExecutableStart),
        mode, 512U, UMICOM_CHILDREN_ADOPT, &task) == UMICOM_SUPERVISION_OK,
        "load independently linked persistent-file process");
    UmicomKernelDiskWritableRequire(UmicomKernelUserFilesGrant(&umicomKernelDiskWritableFiles, task, rights)
        == UMICOM_VFS_OK, "grant selected process file rights");
    UmicomKernelSupervisedProcessInfo info;
    UmicomKernelDiskWritableClear(&info, sizeof(info));
    for (UmicomSize iteration = 0U; iteration < 2048U; ++iteration) {
        UmicomKernelDiskWritableRequire(UmicomKernelProcessSupervisorQuery(&umicomKernelDiskWritableSupervisor,
            UMICOM_SUPERVISION_GUARDIAN, task, &info) == UMICOM_SUPERVISION_OK, "observe task lifetime");
        if (info.terminal) break;
        UmicomKernelSupervisedProcessHandle selected = 0U;
        const UmicomKernelSupervisionStatus run = UmicomKernelProcessSupervisorRunOne(
            &umicomKernelDiskWritableSupervisor, 100000U, &selected);
        if (run != UMICOM_SUPERVISION_OK) {
            UmicomKernelConsoleWrite("disk-writable.supervision-status=");
            UmicomKernelConsoleWriteLine(UmicomKernelSupervisionStatusName(run));
        }
        UmicomKernelDiskWritableRequire(run == UMICOM_SUPERVISION_OK,
            "run user quantum or copied file continuation");
    }
    UmicomKernelDiskWritableRequire(info.terminal, "bounded file diagnostic terminates");
    UmicomKernelDiskWritableRequire(umicomKernelDiskWritableFiles.records[(UmicomU32)task - 1U].task == 0U,
        "terminal cleanup closes descriptors before process collection");
    UmicomKernelProcessCompletion completion;
    UmicomKernelDiskWritableClear(&completion, sizeof(completion));
    UmicomKernelDiskWritableRequire(UmicomKernelProcessSupervisorCollect(&umicomKernelDiskWritableSupervisor,
        UMICOM_SUPERVISION_GUARDIAN, task, &completion) == UMICOM_SUPERVISION_OK,
        "collect private executable memory after descriptor release");
    UmicomKernelConsoleWrite("disk-writable.user-exit=");
    UmicomKernelConsoleWriteHex64(completion.exitValue); UmicomKernelConsoleWriteLine("");
    if (completion.state != UMICOM_USER_TASK_EXITED || completion.exitValue != 0x7200U + mode) {
        UmicomKernelConsoleWrite("disk-writable.user-state=");
        UmicomKernelConsoleWriteUnsigned((UmicomU64)completion.state);
        UmicomKernelConsoleWrite(" cause="); UmicomKernelConsoleWriteHex64(completion.trapCause);
        UmicomKernelConsoleWrite(" calls="); UmicomKernelConsoleWriteUnsigned(completion.systemCalls);
        UmicomKernelConsoleWrite(" slices="); UmicomKernelConsoleWriteUnsigned(completion.slices);
        UmicomKernelConsoleWriteLine("");
    }
    UmicomKernelDiskWritableRequire(completion.state == UMICOM_USER_TASK_EXITED &&
        completion.exitValue == 0x7200U + mode, "user program verified every copied file operation");
}
void UmicomKernelDiskWritableFilesystemValidate(void)
{
    UmicomKernelConsoleWriteLine("disk-writable-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomKernelDiskWritableClear(&before, sizeof(before));
    UmicomKernelDiskWritableClear(&machineBefore, sizeof(machineBefore));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomKernelDiskWritableRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "writer allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomKernelDiskWritableDiscover(&slot);
    const UmicomKernelFat16FileTime time = UmicomKernelDiskWritableFixtureTime();
    UmicomKernelDiskWritableRequire(UmicomKernelDiskWritableMountOpen(&umicomKernelDiskWritableMount,
        domain, slot, 0U, 10000000U, &time) == UMICOM_VFS_OK, "exclusive writable VFS lifetime");
    UmicomKernelDiskWritableGeometry(domain, slot, UMICOM_TRUE);
    UmicomKernelDiskWritableRequire(UmicomKernelProcessSupervisorInitialize(&umicomKernelDiskWritableSupervisor)
        == UMICOM_SUPERVISION_OK, "initialise supervised process scheduler");
    UmicomKernelDiskWritableRequire(UmicomKernelUserFilesAttach(&umicomKernelDiskWritableFiles,
        &umicomKernelDiskWritableSupervisor.scheduler, &umicomKernelDiskWritableMount.vfs) == UMICOM_VFS_OK,
        "attach existing file services before task admission");
    UmicomKernelDiskWritableRequire(UmicomKernelDiskWritableMountClose(&umicomKernelDiskWritableMount)
        == UMICOM_VFS_BUSY, "empty file-service anchor prevents unmount");
    UmicomKernelDiskWritableProcess(0U, UMICOM_VFS_RIGHT_ALL);
    UmicomKernelDiskWritableRequire(umicomKernelDiskWritableMount.lifecycle.committedOperations == 7U,
        "data process accepts seven mutations within its native call budget");
    /* A fresh task and private descriptor table continue the same explicitly
     * mounted namespace without increasing the existing per-process budget. */
    UmicomKernelDiskWritableProcess(2U, UMICOM_VFS_RIGHT_ALL);
    UmicomKernelDiskWritableRequire(umicomKernelDiskWritableMount.lifecycle.committedOperations ==
        UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_COMMITS, "twelve accepted persistent mutations");
    UmicomKernelConsoleWriteLine("disk-writable.user-write-seek-append-resize=verified");
    UmicomKernelConsoleWriteLine("disk-writable.directory-epochs-and-reuse=verified");
    UmicomKernelDiskWritableProcess(1U, UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY);
    UmicomKernelDiskWritableRequire(umicomKernelDiskWritableMount.lifecycle.committedOperations ==
        UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_COMMITS, "reduced rights and queries do not mutate media");
    UmicomKernelConsoleWriteLine("disk-writable.reduced-process-rights=verified");
    UmicomKernelConsoleWriteLine("disk-writable.exit-closes-descriptors=verified");
    UmicomKernelDiskWritableRequire(UmicomKernelUserFilesClose(&umicomKernelDiskWritableFiles) == UMICOM_VFS_OK,
        "release file clients and mount anchor");
    UmicomKernelDiskWritableRequire(UmicomKernelDiskWritableMountClose(&umicomKernelDiskWritableMount)
        == UMICOM_VFS_OK && umicomKernelDiskWritableMount.state == UMICOM_VFS_CLOSED,
        "retire namespace before transport reset and frame release");
    UmicomKernelDiskWritableRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("disk-writable.accepted-commits=12");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READY");
}
#elif defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_TEST)
static UmicomKernelDiskMount umicomKernelDiskWritableReadMount;
static UmicomKernelVfsClient umicomKernelDiskWritableReadClient;
static UmicomU8 umicomKernelDiskWritableReadBytes[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomKernelDiskWritableExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomKernelDiskWritableWholeImage(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomKernelDiskWritableRequire(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomKernelDiskWritableReadBytes, sizeof(umicomKernelDiskWritableReadBytes)) == UMICOM_BLOCK_OK,
            "read full disposable image independently of VFS");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomKernelDiskWritableFixtureSector(first + sector, umicomKernelDiskWritableExpected, UMICOM_TRUE);
            for (UmicomSize byte = 0U; byte < UMICOM_BLOCK_SECTOR_BYTES; ++byte)
                UmicomKernelDiskWritableRequire(umicomKernelDiskWritableReadBytes[sector * UMICOM_BLOCK_SECTOR_BYTES + byte]
                    == umicomKernelDiskWritableExpected[byte], "complete independent media oracle");
        }
    }
    UmicomKernelConsoleWriteLine("disk-writable.whole-image=8388608-bytes-verified");
}
void UmicomKernelDiskWritableFilesystemReadbackValidate(void)
{
    UmicomKernelConsoleWriteLine("disk-writable-readback-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomKernelDiskWritableClear(&before, sizeof(before));
    UmicomKernelDiskWritableClear(&machineBefore, sizeof(machineBefore));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomKernelDiskWritableRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "fresh reader allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomKernelDiskWritableDiscover(&slot);
    UmicomKernelDiskWritableRequire(UmicomKernelDiskMountOpen(&umicomKernelDiskWritableReadMount,
        domain, slot, 0U, 10000000U) == UMICOM_VFS_OK, "fresh immutable FAT16 mount");
    UmicomKernelDiskWritableGeometry(domain, slot, UMICOM_FALSE);
    UmicomKernelDiskWritableWholeImage(domain, umicomKernelDiskWritableReadMount.handle);
    UmicomKernelDiskWritableRequire(UmicomKernelDiskMountClientOpen(&umicomKernelDiskWritableReadMount,
        &umicomKernelDiskWritableReadClient, 0x96U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK,
        "fresh read-only descriptor authority");
    UmicomKernelFileDescriptor file = 0U;
    UmicomKernelDiskWritableRequire(UmicomKernelVfsOpen(&umicomKernelDiskWritableReadClient,
        "/WORK/LOG.BIN", UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &file) == UMICOM_VFS_OK,
        "reopen persisted nested file");
    UmicomKernelVfsNodeInfo info;
    UmicomKernelDiskWritableClear(&info, sizeof(info));
    UmicomKernelDiskWritableRequire(UmicomKernelVfsQuery(&umicomKernelDiskWritableReadClient, file, &info)
        == UMICOM_VFS_OK && info.kind == UMICOM_VFS_FILE && info.bytes == UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_BYTES,
        "fresh file size and type");
    UmicomSize count = 0U;
    UmicomKernelDiskWritableRequire(UmicomKernelVfsRead(&umicomKernelDiskWritableReadClient, file,
        umicomKernelDiskWritableReadBytes, UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_BYTES, &count) == UMICOM_VFS_OK &&
        count == UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_BYTES, "reboot reads every committed logical byte");
    for (UmicomSize i = 0U; i < count; ++i)
        UmicomKernelDiskWritableRequire(umicomKernelDiskWritableReadBytes[i] == UmicomKernelDiskWritableFixtureByte(i),
            "reboot payload includes positional replacement and appended prefix");
    UmicomKernelDiskWritableRequire(UmicomKernelVfsRead(&umicomKernelDiskWritableReadClient, file,
        umicomKernelDiskWritableReadBytes, 1U, &count) == UMICOM_VFS_OK && count == 0U,
        "persisted EOF follows the accepted shrink");
    UmicomKernelDiskWritableRequire(UmicomKernelVfsClose(&umicomKernelDiskWritableReadClient, file) == UMICOM_VFS_OK,
        "release fresh file pin");
    UmicomKernelFileDescriptor directory = 0U;
    UmicomKernelDiskWritableRequire(UmicomKernelVfsOpen(&umicomKernelDiskWritableReadClient,
        "/WORK", UMICOM_VFS_RIGHT_ENUMERATE, UMICOM_FALSE, &directory) == UMICOM_VFS_OK,
        "reopen persisted directory");
    UmicomKernelVfsDirectoryEntry entry;
    UmicomKernelDiskWritableClear(&entry, sizeof(entry));
    UmicomKernelDiskWritableRequire(UmicomKernelVfsReadDirectory(&umicomKernelDiskWritableReadClient,
        directory, &entry) == UMICOM_VFS_OK && entry.name[0] == 'L' && entry.info.bytes == 900U,
        "one surviving directory entry");
    UmicomKernelDiskWritableRequire(UmicomKernelVfsReadDirectory(&umicomKernelDiskWritableReadClient,
        directory, &entry) == UMICOM_VFS_END, "removed file and child directory remain absent");
    UmicomKernelDiskWritableRequire(UmicomKernelVfsClose(&umicomKernelDiskWritableReadClient, directory)
        == UMICOM_VFS_OK, "release fresh directory pin");
    UmicomKernelDiskWritableRequire(UmicomKernelVfsClientClose(&umicomKernelDiskWritableReadClient, &count)
        == UMICOM_VFS_OK && count == 0U, "close fresh descriptor authority");
    UmicomKernelDiskWritableRequire(UmicomKernelDiskMountClose(&umicomKernelDiskWritableReadMount)
        == UMICOM_VFS_OK, "release read-only transport");
    UmicomKernelDiskWritableRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("disk-writable.reboot-paths-and-bytes=verified");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_READY");
}
#else
static UmicomKernelDiskWritableMount umicomKernelDiskWritableRefusedMount;
void UmicomKernelDiskWritableFilesystemReadOnlyValidate(void)
{
    UmicomKernelConsoleWriteLine("disk-writable-read-only-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomKernelDiskWritableClear(&before, sizeof(before));
    UmicomKernelDiskWritableClear(&machineBefore, sizeof(machineBefore));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomKernelDiskWritableRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "read-only refusal allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomKernelDiskWritableDiscover(&slot);
    const UmicomKernelFat16FileTime time = UmicomKernelDiskWritableFixtureTime();
    UmicomKernelDiskWritableRequire(UmicomKernelDiskWritableMountOpen(&umicomKernelDiskWritableRefusedMount,
        domain, slot, 0U, 10000000U, &time) == UMICOM_VFS_READ_ONLY,
        "read-only hardware cannot acquire writable filesystem authority");
    UmicomKernelDiskWritableRequire(UmicomKernelDiskWritableMountClose(&umicomKernelDiskWritableRefusedMount)
        == UMICOM_VFS_OK, "refused admission has no retained resources");
    UmicomKernelDiskWritableRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("disk-writable.read-only-admission=refused");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READ_ONLY_READY");
}
#endif
