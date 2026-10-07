/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_update_validation.c
 *
 * Qualify a bounded update spanning three non-contiguous FAT16 data sectors.
 * Every byte of the original disposable disk is checked before mutation. The
 * writer then checks the result, explicitly flushes and releases its lease.
 * A separately started read-only guest verifies all disk bytes and reads the
 * changed file through the existing VFS provider. This is orderly persistence
 * evidence; it does not simulate a power cut or provide an atomic-write claim.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_update.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_update/guest_fixture.h"

#if defined(UMICOM_KERNEL_FAT16_UPDATE_TEST) == defined(UMICOM_KERNEL_FAT16_UPDATE_READBACK_TEST)
#error "Select exactly one dedicated FAT16 update qualification image"
#endif

#define UMICOM_FAT16_UPDATE_GUARD_BYTES 32U
static UmicomU8 umicomFatValidationReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatValidationExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatValidationFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatValidationRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-update.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x89U); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatValidationBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-update.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatValidationRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatValidationEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatValidationMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatValidationDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatValidationBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatValidationFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) {
            *outSlot = i; ++found;
        } else UmicomFatValidationRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatValidationRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatValidationDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomBoolean changed)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatValidationBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatValidationReadback, sizeof(umicomFatValidationReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16UpdateFixtureSector(first + sector, umicomFatValidationExpected, changed);
            UmicomFatValidationRequire(UmicomFatValidationEqual(umicomFatValidationExpected,
                umicomFatValidationReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                changed ? "only the intended existing file bytes changed anywhere on disk" :
                "every original fixture byte before the first write");
        }
    }
}
static void UmicomFatValidationFinish(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatValidationFill(&after, sizeof(after), 0U);
    UmicomFatValidationFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatValidationRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatValidationRequire(UmicomFatValidationMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-update.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-update.machine-state=unchanged");
    UmicomFatValidationFill(umicomFatValidationReadback, sizeof(umicomFatValidationReadback), 0U);
    UmicomFatValidationFill(umicomFatValidationExpected, sizeof(umicomFatValidationExpected), 0U);
}

#ifdef UMICOM_KERNEL_FAT16_UPDATE_TEST
static UmicomKernelFat16Updater umicomFatValidationUpdater;
static UmicomU8 umicomFatValidationInput[UMICOM_FAT16_UPDATE_FIXTURE_BYTES +
    2U * UMICOM_FAT16_UPDATE_GUARD_BYTES];

static void UmicomFatValidationStatus(UmicomKernelFat16UpdateStatus actual,
    UmicomKernelFat16UpdateStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-update.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWrite("fat16-update.disk-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatValidationUpdater.lastDiskStatus));
    UmicomKernelConsoleWrite("fat16-update.transport-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(umicomFatValidationUpdater.lastBlockStatus));
    UmicomFatValidationRequire(UMICOM_FALSE, reason);
}
static void UmicomFatValidationInputCheck(void)
{
    for (UmicomSize i = 0U; i < sizeof(umicomFatValidationInput); ++i) {
        const UmicomU8 expected = i >= UMICOM_FAT16_UPDATE_GUARD_BYTES &&
            i - UMICOM_FAT16_UPDATE_GUARD_BYTES < UMICOM_FAT16_UPDATE_FIXTURE_BYTES ?
            UmicomFat16UpdateFixturePattern(i - UMICOM_FAT16_UPDATE_GUARD_BYTES) : 0xa7U;
        UmicomFatValidationRequire(umicomFatValidationInput[i] == expected,
            "const caller input and both guards unchanged");
    }
}
static void UmicomFatValidationRefused(const char *path, UmicomU64 offset,
    UmicomKernelFat16UpdateStatus expected)
{
    UmicomKernelFat16UpdateResult result;
    UmicomFatValidationFill(&result, sizeof(result), 0U);
    const UmicomU8 *const input = umicomFatValidationInput + UMICOM_FAT16_UPDATE_GUARD_BYTES;
    UmicomFatValidationStatus(UmicomKernelFat16UpdateWrite(&umicomFatValidationUpdater,
        path, offset, input, 1U, &result), expected, "file refusal before mutation");
    UmicomFatValidationRequire(result.outcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED &&
        result.requestedBytes == 1U && result.confirmedBytes == 0U && result.submittedBytes == 0U &&
        result.completedSectors == 0U && result.submittedSectors == 0U &&
        result.uncertainBytes == 0U && !result.needsFlush && !result.writeUncertain &&
        !umicomFatValidationUpdater.needsFlush && !umicomFatValidationUpdater.writeUncertain,
        "refused attempt publishes no write or flush uncertainty");
}
void UmicomKernelFat16UpdateValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-update-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatValidationFill(&before, sizeof(before), 0U);
    UmicomFatValidationFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatValidationRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatValidationDiscover(&slot);
    UmicomKernelFat16Updater *const owner = &umicomFatValidationUpdater;
    UmicomFatValidationStatus(UmicomKernelFat16UpdateOpen(owner, domain, slot, 0U, 10000000U),
        UMICOM_FAT16_UPDATE_OK, "exclusive writable FAT16 lease admitted");
    UmicomFatValidationRequire(owner->state == UMICOM_FAT16_UPDATER_OPEN && owner->handle &&
        owner->sectors == UMICOM_DISK_FIXTURE_SECTORS && owner->info.clusters == 12159U &&
        !owner->needsFlush && !owner->writeUncertain,
        "qualified fixture geometry and clean initial mutation evidence");
    UmicomFatValidationDisk(domain, owner->handle, UMICOM_FALSE);
    UmicomKernelConsoleWriteLine("fat16-update.original-disk-bytes=8388608-verified");
    UmicomFatValidationFill(umicomFatValidationInput, sizeof(umicomFatValidationInput), 0xa7U);
    UmicomU8 *const input = umicomFatValidationInput + UMICOM_FAT16_UPDATE_GUARD_BYTES;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_UPDATE_FIXTURE_BYTES; ++i)
        input[i] = UmicomFat16UpdateFixturePattern(i);
    UmicomFatValidationRefused("/README.TXT", 0U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatValidationRefused("/DOCS/GUIDE.TXT", 0U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatValidationRefused("/FRAG.BIN", UMICOM_DISK_FIXTURE_FRAGMENT_BYTES, UMICOM_FAT16_UPDATE_RANGE);
    UmicomFatValidationRefused("/EMPTY.TXT", 0U, UMICOM_FAT16_UPDATE_RANGE);
    UmicomKernelConsoleWriteLine("fat16-update.read-only-and-fixed-size-refusals=verified");

    UmicomKernelFat16UpdateResult result;
    UmicomFatValidationFill(&result, sizeof(result), 0U);
    UmicomFatValidationStatus(UmicomKernelFat16UpdateWrite(owner, "/frag.bin",
        UMICOM_FAT16_UPDATE_FIXTURE_OFFSET, input, UMICOM_FAT16_UPDATE_FIXTURE_BYTES, &result),
        UMICOM_FAT16_UPDATE_OK, "unaligned fragmented existing-file update");
    UmicomFatValidationRequire(result.outcome == UMICOM_FAT16_UPDATE_COMPLETED &&
        result.lastBlockOutcome == UMICOM_BLOCK_COMPLETED && result.offset == UMICOM_FAT16_UPDATE_FIXTURE_OFFSET &&
        result.requestedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.confirmedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.submittedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.completedSectors == 3U && result.submittedSectors == 3U &&
        result.uncertainBytes == 0U && result.needsFlush && !result.writeUncertain &&
        owner->needsFlush && !owner->writeUncertain, "explicit complete write and pending flush evidence");
    UmicomFatValidationInputCheck();
    UmicomFatValidationDisk(domain, owner->handle, UMICOM_TRUE);
    UmicomFatValidationRequire(owner->needsFlush && !owner->writeUncertain,
        "successful readback does not imply a flush barrier");
    UmicomKernelConsoleWriteLine("fat16-update.fragmented-three-sector-write=completed");
    UmicomKernelConsoleWriteLine("fat16-update.metadata-other-files-slack-and-surrounding-bytes=unchanged");
    UmicomKernelConsoleWriteLine("fat16-update.input-and-guards=unchanged");
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_NOT_SUBMITTED;
    UmicomFatValidationStatus(UmicomKernelFat16UpdateFlush(owner, &outcome), UMICOM_FAT16_UPDATE_OK,
        "explicit FLUSH after every file WRITE completion");
    UmicomFatValidationRequire(outcome == UMICOM_BLOCK_COMPLETED && !owner->needsFlush &&
        !owner->writeUncertain && owner->lastWrite.needsFlush &&
        owner->lastWrite.confirmedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES,
        "flush clears current pending state and preserves historical write record");
    UmicomKernelConsoleWriteLine("fat16-update.flush=completed");
    UmicomFatValidationStatus(UmicomKernelFat16UpdateClose(owner), UMICOM_FAT16_UPDATE_OK,
        "reset acknowledgement before owned DMA release");
    UmicomFatValidationRequire(owner->state == UMICOM_FAT16_UPDATER_CLOSED && !owner->handle,
        "writer lifetime ended");
    UmicomFatValidationStatus(UmicomKernelFat16UpdateClose(owner), UMICOM_FAT16_UPDATE_OK,
        "completed close is idempotent");
    UmicomFatValidationFinish(&before, &machineBefore);
    UmicomFatValidationFill(umicomFatValidationInput, sizeof(umicomFatValidationInput), 0U);
    UmicomKernelConsoleWriteLine("fat16-update-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_UPDATE_READY");
}
#else
static UmicomKernelDiskMount umicomFatReadbackMount;
static UmicomKernelVfsClient umicomFatReadbackClient;

static void UmicomFatReadbackFile(const char *path, const char *literal, UmicomSize expectedBytes)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomFatValidationFill(&info, sizeof(info), 0U);
    UmicomFatValidationRequire(UmicomKernelVfsOpen(&umicomFatReadbackClient, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "fresh file descriptor over unchanged read-only provider");
    UmicomFatValidationRequire(UmicomKernelVfsQuery(&umicomFatReadbackClient, descriptor, &info) == UMICOM_VFS_OK &&
        info.kind == UMICOM_VFS_FILE && info.bytes == expectedBytes, "existing file size unchanged");
    UmicomFatValidationFill(umicomFatValidationReadback, sizeof(umicomFatValidationReadback), 0xa5U);
    UmicomSize count = 0U;
    UmicomFatValidationRequire(UmicomKernelVfsRead(&umicomFatReadbackClient, descriptor,
        umicomFatValidationReadback, sizeof(umicomFatValidationReadback), &count) == UMICOM_VFS_OK &&
        count == expectedBytes, "complete file read through VFS after a fresh guest start");
    for (UmicomSize i = 0U; i < sizeof(umicomFatValidationReadback); ++i) {
        const UmicomU8 expected = i < expectedBytes ?
            (literal ? (UmicomU8)literal[i] : UmicomFat16UpdateFixtureFileByte(i, UMICOM_TRUE)) : 0xa5U;
        UmicomFatValidationRequire(umicomFatValidationReadback[i] == expected,
            "changed file content, other file bytes and unused caller output");
    }
    count = 99U;
    UmicomFatValidationRequire(UmicomKernelVfsRead(&umicomFatReadbackClient, descriptor,
        umicomFatValidationReadback, 1U, &count) == UMICOM_VFS_OK && !count,
        "unchanged EOF after bounded overwrite");
    UmicomFatValidationRequire(UmicomKernelVfsClose(&umicomFatReadbackClient, descriptor) == UMICOM_VFS_OK,
        "file description and provider pin released");
}
void UmicomKernelFat16UpdateReadbackValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-update-readback-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatValidationFill(&before, sizeof(before), 0U);
    UmicomFatValidationFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatValidationRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatValidationDiscover(&slot);
    UmicomFatValidationRequire(UmicomKernelDiskMountOpen(&umicomFatReadbackMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_OK, "fresh read-only mount after writer process ended");
    UmicomKernelBlockInfo info;
    UmicomFatValidationFill(&info, sizeof(info), 0U);
    UmicomFatValidationBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh read-only transport geometry");
    UmicomFatValidationRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "complete fixture capacity and exclusive read-only DMA lease");
    UmicomFatValidationDisk(domain, umicomFatReadbackMount.handle, UMICOM_TRUE);
    UmicomKernelConsoleWriteLine("fat16-update-readback.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-update-readback.only-intended-file-range=changed");
    UmicomFatValidationRequire(UmicomKernelDiskMountClientOpen(&umicomFatReadbackMount,
        &umicomFatReadbackClient, 42U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK,
        "independent read-only file client");
    UmicomFatReadbackFile("/FRAG.BIN", (const char *)0, UMICOM_DISK_FIXTURE_FRAGMENT_BYTES);
    UmicomFatReadbackFile("/README.TXT", UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    UmicomFatReadbackFile("/DOCS/GUIDE.TXT", UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    UmicomKernelFileDescriptor refused = 0U;
    UmicomFatValidationRequire(UmicomKernelVfsOpen(&umicomFatReadbackClient, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE, &refused) == UMICOM_VFS_ACCESS_DENIED && !refused,
        "existing VFS provider still denies write rights");
    UmicomKernelConsoleWriteLine("fat16-update-readback.vfs-file-size-content-and-read-only-rights=verified");
    UmicomSize closed = 0U;
    UmicomFatValidationRequire(UmicomKernelVfsClientClose(&umicomFatReadbackClient, &closed) == UMICOM_VFS_OK &&
        !closed && UmicomKernelDiskMountClose(&umicomFatReadbackMount) == UMICOM_VFS_OK &&
        !umicomFatReadbackMount.handle, "client and mount retired before transport release");
    UmicomFatValidationFinish(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-update-readback-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_UPDATE_READBACK_READY");
}
#endif
