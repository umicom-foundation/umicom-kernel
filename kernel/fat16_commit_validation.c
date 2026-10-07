/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_commit_validation.c
 *
 * Qualify ordered FAT16 file data updates using four separate guest roles.
 * A complete writer stages and finishes an update; a fresh read-only process
 * verifies its complete disk and existing VFS behaviour. A second writer ends
 * after Stage and Close; another fresh process verifies every retained byte
 * and the original provider's refusal of both dirty FAT copies.
 *
 * These are real file-backed flush/restart observations. Deliberately omitting
 * Finish is not a physical power cut, atomic-write guarantee or repair test.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_commit.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_commit/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_COMMIT_TEST) + \
     defined(UMICOM_KERNEL_FAT16_COMMIT_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_COMMIT_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_COMMIT_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 commit qualification image"
#endif

#define UMICOM_FAT16_COMMIT_GUARD_BYTES 32U
static UmicomU8 umicomFatCommitReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatCommitExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatCommitFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatCommitRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-commit.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatCommitBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-commit.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatCommitRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatCommitEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatCommitMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatCommitDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatCommitBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatCommitFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outSlot = i; ++found; }
        else UmicomFatCommitRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatCommitRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatCommitDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomFat16CommitFixtureState state)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatCommitBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatCommitReadback, sizeof(umicomFatCommitReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16CommitFixtureSector(first + sector, umicomFatCommitExpected, state);
            UmicomFatCommitRequire(UmicomFatCommitEqual(umicomFatCommitExpected,
                umicomFatCommitReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "complete disk agrees with exact original, staged or committed fixture");
        }
    }
}
static void UmicomFatCommitRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatCommitFill(&after, sizeof(after), 0U);
    UmicomFatCommitFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatCommitRequire(UmicomFatCommitMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-commit.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-commit.machine-state=unchanged");
    UmicomFatCommitFill(umicomFatCommitReadback, sizeof(umicomFatCommitReadback), 0U);
    UmicomFatCommitFill(umicomFatCommitExpected, sizeof(umicomFatCommitExpected), 0U);
}

#if defined(UMICOM_KERNEL_FAT16_COMMIT_TEST) || defined(UMICOM_KERNEL_FAT16_COMMIT_INTERRUPTED_TEST)
static UmicomKernelFat16Committer umicomFatCommitOwner;
static UmicomU8 umicomFatCommitInput[UMICOM_FAT16_UPDATE_FIXTURE_BYTES +
    2U * UMICOM_FAT16_COMMIT_GUARD_BYTES];

static void UmicomFatCommitCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static void UmicomFatCommitStatus(UmicomKernelFat16UpdateStatus actual,
    UmicomKernelFat16UpdateStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-commit.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWrite("fat16-commit.phase=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16CommitPhaseName(umicomFatCommitOwner.lastResult.phase));
    UmicomKernelConsoleWrite("fat16-commit.disk-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatCommitOwner.updater.lastDiskStatus));
    UmicomKernelConsoleWrite("fat16-commit.transport-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(umicomFatCommitOwner.updater.lastBlockStatus));
    UmicomFatCommitRequire(UMICOM_FALSE, reason);
}
static void UmicomFatCommitInputCheck(void)
{
    for (UmicomSize i = 0U; i < sizeof(umicomFatCommitInput); ++i) {
        const UmicomU8 expected = i >= UMICOM_FAT16_COMMIT_GUARD_BYTES &&
            i - UMICOM_FAT16_COMMIT_GUARD_BYTES < UMICOM_FAT16_UPDATE_FIXTURE_BYTES ?
            UmicomFat16UpdateFixturePattern(i - UMICOM_FAT16_COMMIT_GUARD_BYTES) : 0xa7U;
        UmicomFatCommitRequire(umicomFatCommitInput[i] == expected,
            "const caller input and both guards unchanged");
    }
}
static void UmicomFatCommitRefused(const char *path, UmicomU64 offset,
    UmicomKernelFat16UpdateStatus expected)
{
    UmicomKernelFat16CommitResult result;
    UmicomFatCommitFill(&result, sizeof(result), 0U);
    const UmicomU8 *const input = umicomFatCommitInput + UMICOM_FAT16_COMMIT_GUARD_BYTES;
    UmicomFatCommitStatus(UmicomKernelFat16CommitStage(&umicomFatCommitOwner,
        path, offset, input, 1U, &result), expected, "file refusal before any dirty marker");
    UmicomFatCommitRequire(umicomFatCommitOwner.state == UMICOM_FAT16_COMMIT_READY &&
        result.dataOutcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && result.requestedBytes == 1U &&
        !result.confirmedBytes && !result.submittedBytes && !result.submittedMetadataSectors &&
        !result.completedMetadataSectors && !result.submittedDataSectors && !result.completedDataSectors &&
        !result.completedFlushes && !result.mediaTouched && !result.uncertainSectorValid &&
        !result.needsFlush && !result.writeUncertain, "refusal leaves a ready owner and unchanged media");
}
static void UmicomFatCommitWriter(UmicomBoolean complete)
{
    UmicomKernelConsoleWriteLine(complete ? "fat16-commit-test=begin" : "fat16-commit-interrupted-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatCommitFill(&before, sizeof(before), 0U);
    UmicomFatCommitFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatCommitDiscover(&slot);
    UmicomKernelFat16Committer *const owner = &umicomFatCommitOwner;
    UmicomFatCommitStatus(UmicomKernelFat16CommitOpen(owner, domain, slot, 0U, 10000000U),
        UMICOM_FAT16_UPDATE_OK, "exclusive writable commit lease admitted");
    UmicomFatCommitRequire(owner->state == UMICOM_FAT16_COMMIT_READY && owner->updater.handle &&
        owner->updater.sectors == UMICOM_DISK_FIXTURE_SECTORS && owner->updater.info.clusters == 12159U &&
        !owner->updater.needsFlush && !owner->updater.writeUncertain, "clean admission and fixture geometry");
    UmicomFatCommitDisk(domain, owner->updater.handle, UMICOM_FAT16_FIXTURE_ORIGINAL);
    UmicomKernelConsoleWriteLine("fat16-commit.original-disk-bytes=8388608-verified");
    UmicomFatCommitFill(umicomFatCommitInput, sizeof(umicomFatCommitInput), 0xa7U);
    UmicomU8 *const input = umicomFatCommitInput + UMICOM_FAT16_COMMIT_GUARD_BYTES;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_UPDATE_FIXTURE_BYTES; ++i)
        input[i] = UmicomFat16UpdateFixturePattern(i);
    UmicomFatCommitRefused("/README.TXT", 0U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatCommitRefused("/DOCS/GUIDE.TXT", 0U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatCommitRefused("/FRAG.BIN", UMICOM_DISK_FIXTURE_FRAGMENT_BYTES, UMICOM_FAT16_UPDATE_RANGE);
    UmicomFatCommitRefused("/EMPTY.TXT", 0U, UMICOM_FAT16_UPDATE_RANGE);
    UmicomKernelConsoleWriteLine("fat16-commit.preflight-refusals-before-metadata=verified");

    UmicomKernelFat16CommitResult result, historical;
    UmicomFatCommitFill(&result, sizeof(result), 0U);
    UmicomFatCommitFill(&historical, sizeof(historical), 0U);
    UmicomFatCommitStatus(UmicomKernelFat16CommitStage(owner, "/frag.bin",
        UMICOM_FAT16_UPDATE_FIXTURE_OFFSET, input, UMICOM_FAT16_UPDATE_FIXTURE_BYTES, &result),
        UMICOM_FAT16_UPDATE_OK, "dirty guards precede unaligned fragmented data update");
    UmicomFatCommitRequire(owner->state == UMICOM_FAT16_COMMIT_STAGED &&
        result.dataOutcome == UMICOM_FAT16_UPDATE_COMPLETED && result.offset == UMICOM_FAT16_UPDATE_FIXTURE_OFFSET &&
        result.requestedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.confirmedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.submittedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.completedDataSectors == 3U && result.submittedDataSectors == 3U &&
        result.completedMetadataSectors == 2U && result.submittedMetadataSectors == 2U &&
        result.completedFlushes == 3U && result.mediaTouched && result.dirtyDurable && result.dirtyVerified &&
        result.dataDurable && result.dataVerified && !result.cleanFinalisationStarted && !result.cleanDurable &&
        !result.cleanVerified && !result.commitAccepted && !result.uncertainSectorValid &&
        !result.needsFlush && !result.writeUncertain, "staged data and dirty flags have distinct complete evidence");
    UmicomFatCommitInputCheck();
    UmicomFatCommitDisk(domain, owner->updater.handle, UMICOM_FAT16_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-commit.staged-disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-commit.staged-changes=700-file-bytes-and-two-clean-bits");
    UmicomKernelConsoleWriteLine("fat16-commit.data-sectors=3 metadata-sectors=2 flushes=3");
    UmicomFatCommitCopy(&historical, &owner->lastResult, sizeof(historical));
    UmicomFatCommitFill(&result, sizeof(result), 0xa5U);
    UmicomKernelFat16CommitResult untouched;
    UmicomFatCommitFill(&untouched, sizeof(untouched), 0xa5U);
    UmicomFatCommitStatus(UmicomKernelFat16CommitStage(owner, "/FRAG.BIN", 0U, input, 1U, &result),
        UMICOM_FAT16_UPDATE_BAD_STATE, "a staged owner cannot begin another update");
    UmicomFatCommitRequire(UmicomFatCommitEqual(&result, &untouched, sizeof(result)) &&
        UmicomFatCommitEqual(&historical, &owner->lastResult, sizeof(historical)),
        "refused second Stage preserves caller output and historical evidence");

    if (complete) {
        UmicomFatCommitStatus(UmicomKernelFat16CommitFinish(owner, &result), UMICOM_FAT16_UPDATE_OK,
            "explicit clean finalisation after complete staged re-verification");
        UmicomFatCommitRequire(owner->state == UMICOM_FAT16_COMMIT_COMMITTED &&
            result.phase == UMICOM_FAT16_COMMIT_COMPLETE && result.commitAccepted &&
            result.dirtyDurable && result.dirtyVerified && result.dataDurable && result.dataVerified &&
            result.cleanFinalisationStarted && result.cleanDurable && result.cleanVerified &&
            result.confirmedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
            result.completedDataSectors == 3U && result.submittedDataSectors == 3U &&
            result.completedMetadataSectors == 4U && result.submittedMetadataSectors == 4U &&
            result.completedFlushes == 5U && !result.uncertainSectorValid &&
            !result.needsFlush && !result.writeUncertain, "clean commit retains all ordered evidence");
        UmicomFatCommitDisk(domain, owner->updater.handle, UMICOM_FAT16_FIXTURE_COMMITTED);
        UmicomKernelConsoleWriteLine("fat16-commit.committed-disk-bytes=8388608-verified");
        UmicomKernelConsoleWriteLine("fat16-commit.committed-changes=700-file-bytes-only");
        UmicomKernelConsoleWriteLine("fat16-commit.data-sectors=3 metadata-sectors=4 flushes=5");
        UmicomFatCommitCopy(&historical, &owner->lastResult, sizeof(historical));
    } else {
        UmicomKernelConsoleWriteLine("fat16-commit.finish=deliberately-omitted");
    }
    UmicomFatCommitStatus(UmicomKernelFat16CommitClose(owner), UMICOM_FAT16_UPDATE_OK,
        "resource-only close after accepted or deliberately unfinished update");
    UmicomFatCommitRequire(owner->state == UMICOM_FAT16_COMMIT_CLOSED && !owner->updater.handle &&
        UmicomFatCommitEqual(&historical, &owner->lastResult, sizeof(historical)),
        "close releases the lease without editing commit evidence");
    UmicomFatCommitStatus(UmicomKernelFat16CommitClose(owner), UMICOM_FAT16_UPDATE_OK,
        "completed close remains idempotent");
    UmicomFatCommitInputCheck();
    UmicomFatCommitRestore(&before, &machineBefore);
    UmicomFatCommitFill(umicomFatCommitInput, sizeof(umicomFatCommitInput), 0U);
    UmicomKernelConsoleWriteLine(complete ? "fat16-commit-test=pass" : "fat16-commit-interrupted-test=pass");
    UmicomKernelConsoleWriteLine(complete ? "UMICOM_KERNEL_FAT16_COMMIT_READY" :
        "UMICOM_KERNEL_FAT16_COMMIT_INTERRUPTED_READY");
}
#ifdef UMICOM_KERNEL_FAT16_COMMIT_TEST
void UmicomKernelFat16CommitValidate(void) { UmicomFatCommitWriter(UMICOM_TRUE); }
#else
void UmicomKernelFat16CommitInterruptedValidate(void) { UmicomFatCommitWriter(UMICOM_FALSE); }
#endif

#elif defined(UMICOM_KERNEL_FAT16_COMMIT_READBACK_TEST)
static UmicomKernelDiskMount umicomFatCommitMount;
static UmicomKernelVfsClient umicomFatCommitClient;

static void UmicomFatCommitFile(const char *path, const char *literal, UmicomSize expectedBytes)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomFatCommitFill(&info, sizeof(info), 0U);
    UmicomFatCommitRequire(UmicomKernelVfsOpen(&umicomFatCommitClient, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "fresh file descriptor over unchanged read-only provider");
    UmicomFatCommitRequire(UmicomKernelVfsQuery(&umicomFatCommitClient, descriptor, &info) == UMICOM_VFS_OK &&
        info.kind == UMICOM_VFS_FILE && info.bytes == expectedBytes, "existing file size unchanged");
    UmicomFatCommitFill(umicomFatCommitReadback, sizeof(umicomFatCommitReadback), 0xa5U);
    UmicomSize count = 0U;
    UmicomFatCommitRequire(UmicomKernelVfsRead(&umicomFatCommitClient, descriptor,
        umicomFatCommitReadback, sizeof(umicomFatCommitReadback), &count) == UMICOM_VFS_OK &&
        count == expectedBytes, "complete file read through VFS after a fresh guest start");
    for (UmicomSize i = 0U; i < sizeof(umicomFatCommitReadback); ++i) {
        const UmicomU8 expected = i < expectedBytes ?
            (literal ? (UmicomU8)literal[i] : UmicomFat16UpdateFixtureFileByte(i, UMICOM_TRUE)) : 0xa5U;
        UmicomFatCommitRequire(umicomFatCommitReadback[i] == expected,
            "file contents and unused caller output are exact");
    }
    count = 99U;
    UmicomFatCommitRequire(UmicomKernelVfsRead(&umicomFatCommitClient, descriptor,
        umicomFatCommitReadback, 1U, &count) == UMICOM_VFS_OK && !count, "unchanged EOF after bounded update");
    UmicomFatCommitRequire(UmicomKernelVfsClose(&umicomFatCommitClient, descriptor) == UMICOM_VFS_OK,
        "file description and provider pin released");
}
void UmicomKernelFat16CommitReadbackValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-commit-readback-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatCommitFill(&before, sizeof(before), 0U);
    UmicomFatCommitFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatCommitDiscover(&slot);
    UmicomFatCommitRequire(UmicomKernelDiskMountOpen(&umicomFatCommitMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_OK, "fresh read-only mount after clean finalisation");
    UmicomKernelBlockInfo info;
    UmicomFatCommitFill(&info, sizeof(info), 0U);
    UmicomFatCommitBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh read-only transport geometry");
    UmicomFatCommitRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "fixture capacity and exclusive read-only DMA lease");
    UmicomFatCommitDisk(domain, umicomFatCommitMount.handle, UMICOM_FAT16_FIXTURE_COMMITTED);
    UmicomKernelConsoleWriteLine("fat16-commit-readback.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-commit-readback.changes=700-file-bytes-only");
    UmicomFatCommitRequire(UmicomKernelDiskMountClientOpen(&umicomFatCommitMount,
        &umicomFatCommitClient, 42U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK,
        "independent read-only file client");
    UmicomFatCommitFile("/FRAG.BIN", (const char *)0, UMICOM_DISK_FIXTURE_FRAGMENT_BYTES);
    UmicomFatCommitFile("/README.TXT", UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    UmicomFatCommitFile("/DOCS/GUIDE.TXT", UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    UmicomKernelFileDescriptor refused = 0U;
    UmicomFatCommitRequire(UmicomKernelVfsOpen(&umicomFatCommitClient, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE, &refused) == UMICOM_VFS_ACCESS_DENIED && !refused,
        "the existing VFS provider still denies write rights");
    UmicomKernelConsoleWriteLine("fat16-commit-readback.vfs-size-content-eof-and-read-only-rights=verified");
    UmicomSize closed = 0U;
    UmicomFatCommitRequire(UmicomKernelVfsClientClose(&umicomFatCommitClient, &closed) == UMICOM_VFS_OK &&
        !closed && UmicomKernelDiskMountClose(&umicomFatCommitMount) == UMICOM_VFS_OK &&
        !umicomFatCommitMount.handle, "client and mount retired before transport release");
    UmicomFatCommitRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-commit-readback-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_COMMIT_READBACK_READY");
}

#else
static UmicomKernelDiskMount umicomFatCommitRefusedMount;

void UmicomKernelFat16CommitRejectedValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-commit-rejected-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatCommitFill(&before, sizeof(before), 0U);
    UmicomFatCommitFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh refusal guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatCommitDiscover(&slot);
    UmicomKernelBlockHandle handle = 0U;
    UmicomFatCommitBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &handle), UMICOM_BLOCK_OK,
        "fresh read-only lease can inspect raw interruption evidence");
    UmicomKernelBlockInfo info;
    UmicomFatCommitFill(&info, sizeof(info), 0U);
    UmicomFatCommitBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh refusal transport geometry");
    UmicomFatCommitRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "read-only interruption fixture and owned DMA frames");
    UmicomFatCommitDisk(domain, handle, UMICOM_FAT16_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-commit-rejected.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-commit-rejected.changes=700-file-bytes-and-two-clean-bits");
    UmicomFatCommitBlock(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK,
        "raw verification lease released before filesystem admission");
    UmicomFatCommitRequire(UmicomKernelDiskMountOpen(&umicomFatCommitRefusedMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_CORRUPT_FILESYSTEM &&
        umicomFatCommitRefusedMount.provider.lastDiskStatus == UMICOM_DISK_DIRTY &&
        !umicomFatCommitRefusedMount.admitted && !umicomFatCommitRefusedMount.provider.volume.open,
        "unchanged read-only filesystem rejects persistent dirty flags before publication");
    UmicomFatCommitRequire(UmicomKernelDiskMountClose(&umicomFatCommitRefusedMount) == UMICOM_VFS_OK &&
        !umicomFatCommitRefusedMount.handle, "refused mount releases transport without repairing flags");
    UmicomKernelConsoleWriteLine("fat16-commit-rejected.filesystem=dirty-refused");
    UmicomKernelConsoleWriteLine("fat16-commit-rejected.repair=not-performed");
    UmicomFatCommitRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-commit-rejected-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_COMMIT_REJECTED_READY");
}
#endif
