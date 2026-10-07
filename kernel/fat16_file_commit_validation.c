/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_file_commit_validation.c
 *
 * Qualify ordered FAT16 data, ARCHIVE and explicit write-calendar updates.
 * A complete writer stages and finishes the update on an initially unarchived
 * file; a fresh read-only process checks every disk byte and VFS behaviour.
 * A second writer ends after Stage and Close; another fresh process verifies
 * data, directory bytes and the original provider's refusal of dirty FATs.
 *
 * These are real file-backed flush/restart observations. Deliberately omitting
 * Finish is not a physical power cut, atomic-write guarantee or repair test.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_file_commit.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_file_commit/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST) + \
     defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 commit qualification image"
#endif

#define UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES 32U
static UmicomU8 umicomFatFileCommitReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatFileCommitExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatFileCommitFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatFileCommitRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-file-commit.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatFileCommitBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-file-commit.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatFileCommitRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatFileCommitEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatFileCommitMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatFileCommitDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatFileCommitBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatFileCommitFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outSlot = i; ++found; }
        else UmicomFatFileCommitRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatFileCommitRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatFileCommitDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomFat16FileCommitFixtureState state)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatFileCommitBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatFileCommitReadback, sizeof(umicomFatFileCommitReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16FileCommitFixtureSector(first + sector, umicomFatFileCommitExpected, state);
            UmicomFatFileCommitRequire(UmicomFatFileCommitEqual(umicomFatFileCommitExpected,
                umicomFatFileCommitReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "complete disk agrees with exact original, staged or committed fixture");
        }
    }
}
static void UmicomFatFileCommitRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatFileCommitFill(&after, sizeof(after), 0U);
    UmicomFatFileCommitFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatFileCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatFileCommitRequire(UmicomFatFileCommitMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-file-commit.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-file-commit.machine-state=unchanged");
    UmicomFatFileCommitFill(umicomFatFileCommitReadback, sizeof(umicomFatFileCommitReadback), 0U);
    UmicomFatFileCommitFill(umicomFatFileCommitExpected, sizeof(umicomFatFileCommitExpected), 0U);
}

#if defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST) || defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_TEST)
static UmicomKernelFat16FileCommitter umicomFatFileCommitOwner;
static const UmicomKernelFat16FileTime umicomFatFileCommitTime = {
    UMICOM_FAT16_FILE_COMMIT_FIXTURE_YEAR, UMICOM_FAT16_FILE_COMMIT_FIXTURE_MONTH,
    UMICOM_FAT16_FILE_COMMIT_FIXTURE_DAY, UMICOM_FAT16_FILE_COMMIT_FIXTURE_HOUR,
    UMICOM_FAT16_FILE_COMMIT_FIXTURE_MINUTE, UMICOM_FAT16_FILE_COMMIT_FIXTURE_SECOND
};
static UmicomU8 umicomFatFileCommitInput[UMICOM_FAT16_UPDATE_FIXTURE_BYTES +
    2U * UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES];

static void UmicomFatFileCommitCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static void UmicomFatFileCommitStatus(UmicomKernelFat16UpdateStatus actual,
    UmicomKernelFat16UpdateStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-file-commit.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWrite("fat16-file-commit.phase=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16CommitPhaseName(umicomFatFileCommitOwner.lastResult.commit.phase));
    UmicomKernelConsoleWrite("fat16-file-commit.disk-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatFileCommitOwner.commit.updater.lastDiskStatus));
    UmicomKernelConsoleWrite("fat16-file-commit.transport-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(umicomFatFileCommitOwner.commit.updater.lastBlockStatus));
    UmicomFatFileCommitRequire(UMICOM_FALSE, reason);
}
static void UmicomFatFileCommitInputCheck(void)
{
    for (UmicomSize i = 0U; i < sizeof(umicomFatFileCommitInput); ++i) {
        const UmicomU8 expected = i >= UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES &&
            i - UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES < UMICOM_FAT16_UPDATE_FIXTURE_BYTES ?
            UmicomFat16UpdateFixturePattern(i - UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES) : 0xa7U;
        UmicomFatFileCommitRequire(umicomFatFileCommitInput[i] == expected,
            "const caller input and both guards unchanged");
    }
}
static void UmicomFatFileCommitMetadataCheck(const UmicomKernelFat16FileCommitResult *result)
{
    UmicomFatFileCommitRequire(result->directoryPlanned && result->directorySubmitted &&
        result->directoryCompleted && result->directoryDurable && result->directoryVerified &&
        result->directorySector == UMICOM_DISK_FIXTURE_ROOT && result->entryOffset == 96U &&
        result->originalAttributes == 0U && result->updatedAttributes == 0x20U &&
        result->encodedTime.writeTime == UMICOM_FAT16_FILE_COMMIT_FIXTURE_TIME &&
        result->encodedTime.writeDate == UMICOM_FAT16_FILE_COMMIT_FIXTURE_DATE &&
        result->encodedTime.storedSecond == 58U &&
        result->requestedTime.year == 2037U && result->requestedTime.month == 11U &&
        result->requestedTime.day == 23U && result->requestedTime.hour == 14U &&
        result->requestedTime.minute == 35U && result->requestedTime.second == 59U,
        "archive-clear file gained only ARCHIVE and the explicit two-second write timestamp");
}
static void UmicomFatFileCommitRefused(const char *path, UmicomU64 offset,
    UmicomKernelFat16UpdateStatus expected)
{
    UmicomKernelFat16FileCommitResult result;
    UmicomFatFileCommitFill(&result, sizeof(result), 0U);
    const UmicomU8 *const input = umicomFatFileCommitInput + UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES;
    UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitStage(&umicomFatFileCommitOwner,
        path, offset, input, 1U, &umicomFatFileCommitTime, &result), expected, "file refusal before any dirty marker");
    UmicomFatFileCommitRequire(umicomFatFileCommitOwner.commit.state == UMICOM_FAT16_COMMIT_READY &&
        result.commit.dataOutcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && result.commit.requestedBytes == 1U &&
        !result.commit.confirmedBytes && !result.commit.submittedBytes && !result.commit.submittedMetadataSectors &&
        !result.commit.completedMetadataSectors && !result.commit.submittedDataSectors && !result.commit.completedDataSectors &&
        !result.commit.completedFlushes && !result.commit.mediaTouched && !result.commit.uncertainSectorValid &&
        !result.commit.needsFlush && !result.commit.writeUncertain && !result.directorySubmitted &&
        !result.directoryCompleted && !result.directoryDurable && !result.directoryVerified,
        "refusal leaves a ready owner and unchanged media");
}
static void UmicomFatFileCommitWriter(UmicomBoolean complete)
{
    UmicomKernelConsoleWriteLine(complete ? "fat16-file-commit-test=begin" : "fat16-file-commit-interrupted-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatFileCommitFill(&before, sizeof(before), 0U);
    UmicomFatFileCommitFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatFileCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatFileCommitDiscover(&slot);
    UmicomKernelFat16FileCommitter *const owner = &umicomFatFileCommitOwner;
    UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitOpen(owner, domain, slot, 0U, 10000000U),
        UMICOM_FAT16_UPDATE_OK, "exclusive writable commit lease admitted");
    UmicomFatFileCommitRequire(owner->commit.state == UMICOM_FAT16_COMMIT_READY && owner->commit.updater.handle &&
        owner->commit.updater.sectors == UMICOM_DISK_FIXTURE_SECTORS && owner->commit.updater.info.clusters == 12159U &&
        !owner->commit.updater.needsFlush && !owner->commit.updater.writeUncertain, "clean admission and fixture geometry");
    UmicomFatFileCommitDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_FILE_FIXTURE_ORIGINAL);
    UmicomKernelConsoleWriteLine("fat16-file-commit.original-disk-bytes=8388608-verified");
    UmicomFatFileCommitFill(umicomFatFileCommitInput, sizeof(umicomFatFileCommitInput), 0xa7U);
    UmicomU8 *const input = umicomFatFileCommitInput + UMICOM_FAT16_FILE_COMMIT_GUARD_BYTES;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_UPDATE_FIXTURE_BYTES; ++i)
        input[i] = UmicomFat16UpdateFixturePattern(i);
    UmicomFatFileCommitRefused("/README.TXT", 0U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatFileCommitRefused("/DOCS/GUIDE.TXT", 0U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatFileCommitRefused("/FRAG.BIN", UMICOM_DISK_FIXTURE_FRAGMENT_BYTES, UMICOM_FAT16_UPDATE_RANGE);
    UmicomFatFileCommitRefused("/EMPTY.TXT", 0U, UMICOM_FAT16_UPDATE_RANGE);
    UmicomKernelConsoleWriteLine("fat16-file-commit.preflight-refusals-before-metadata=verified");

    UmicomKernelFat16FileCommitResult result, historical;
    UmicomFatFileCommitFill(&result, sizeof(result), 0U);
    UmicomFatFileCommitFill(&historical, sizeof(historical), 0U);
    UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitStage(owner, "/frag.bin",
        UMICOM_FAT16_UPDATE_FIXTURE_OFFSET, input, UMICOM_FAT16_UPDATE_FIXTURE_BYTES,
        &umicomFatFileCommitTime, &result),
        UMICOM_FAT16_UPDATE_OK, "dirty guards precede unaligned fragmented data update");
    UmicomFatFileCommitRequire(owner->commit.state == UMICOM_FAT16_COMMIT_STAGED &&
        result.commit.dataOutcome == UMICOM_FAT16_UPDATE_COMPLETED && result.commit.offset == UMICOM_FAT16_UPDATE_FIXTURE_OFFSET &&
        result.commit.requestedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.commit.confirmedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.commit.submittedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
        result.commit.completedDataSectors == 3U && result.commit.submittedDataSectors == 3U &&
        result.commit.completedMetadataSectors == 3U && result.commit.submittedMetadataSectors == 3U &&
        result.commit.completedFlushes == 4U && result.commit.mediaTouched && result.commit.dirtyDurable && result.commit.dirtyVerified &&
        result.commit.dataDurable && result.commit.dataVerified && !result.commit.cleanFinalisationStarted && !result.commit.cleanDurable &&
        !result.commit.cleanVerified && !result.commit.commitAccepted && !result.commit.uncertainSectorValid &&
        !result.commit.needsFlush && !result.commit.writeUncertain, "staged data and dirty flags have distinct complete evidence");
    UmicomFatFileCommitMetadataCheck(&result);
    UmicomFatFileCommitInputCheck();
    UmicomFatFileCommitDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_FILE_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-file-commit.staged-disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-file-commit.staged-changes=700-file-bytes-five-directory-bytes-and-two-clean-bits");
    UmicomKernelConsoleWriteLine("fat16-file-commit.data-sectors=3 metadata-sectors=3 flushes=4");
    UmicomKernelConsoleWriteLine("fat16-file-commit.write-calendar=requested-2037-11-23T14:35:59-stored-2037-11-23T14:35:58");
    UmicomKernelConsoleWriteLine("fat16-file-commit.directory-sector=2145 entry-offset=96 archive=clear-to-set");
    UmicomFatFileCommitCopy(&historical, &owner->lastResult, sizeof(historical));
    UmicomFatFileCommitFill(&result, sizeof(result), 0xa5U);
    UmicomKernelFat16FileCommitResult untouched;
    UmicomFatFileCommitFill(&untouched, sizeof(untouched), 0xa5U);
    UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitStage(owner, "/FRAG.BIN", 0U, input, 1U,
        &umicomFatFileCommitTime, &result),
        UMICOM_FAT16_UPDATE_BAD_STATE, "a staged owner cannot begin another update");
    UmicomFatFileCommitRequire(UmicomFatFileCommitEqual(&result, &untouched, sizeof(result)) &&
        UmicomFatFileCommitEqual(&historical, &owner->lastResult, sizeof(historical)),
        "refused second Stage preserves caller output and historical evidence");

    if (complete) {
        UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitFinish(owner, &result), UMICOM_FAT16_UPDATE_OK,
            "explicit clean finalisation after complete staged re-verification");
        UmicomFatFileCommitRequire(owner->commit.state == UMICOM_FAT16_COMMIT_COMMITTED &&
            result.commit.phase == UMICOM_FAT16_COMMIT_COMPLETE && result.commit.commitAccepted &&
            result.commit.dirtyDurable && result.commit.dirtyVerified && result.commit.dataDurable && result.commit.dataVerified &&
            result.commit.cleanFinalisationStarted && result.commit.cleanDurable && result.commit.cleanVerified &&
            result.commit.confirmedBytes == UMICOM_FAT16_UPDATE_FIXTURE_BYTES &&
            result.commit.completedDataSectors == 3U && result.commit.submittedDataSectors == 3U &&
            result.commit.completedMetadataSectors == 5U && result.commit.submittedMetadataSectors == 5U &&
            result.commit.completedFlushes == 6U && !result.commit.uncertainSectorValid &&
            !result.commit.needsFlush && !result.commit.writeUncertain, "clean commit retains all ordered evidence");
        UmicomFatFileCommitMetadataCheck(&result);
        UmicomFatFileCommitDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_FILE_FIXTURE_COMMITTED);
        UmicomKernelConsoleWriteLine("fat16-file-commit.committed-disk-bytes=8388608-verified");
        UmicomKernelConsoleWriteLine("fat16-file-commit.committed-changes=700-file-bytes-and-five-directory-bytes");
        UmicomKernelConsoleWriteLine("fat16-file-commit.data-sectors=3 metadata-sectors=5 flushes=6");
        UmicomFatFileCommitCopy(&historical, &owner->lastResult, sizeof(historical));
    } else {
        UmicomKernelConsoleWriteLine("fat16-file-commit.finish=deliberately-omitted");
    }
    UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitClose(owner), UMICOM_FAT16_UPDATE_OK,
        "resource-only close after accepted or deliberately unfinished update");
    UmicomFatFileCommitRequire(owner->commit.state == UMICOM_FAT16_COMMIT_CLOSED && !owner->commit.updater.handle &&
        UmicomFatFileCommitEqual(&historical, &owner->lastResult, sizeof(historical)),
        "close releases the lease without editing commit evidence");
    UmicomFatFileCommitStatus(UmicomKernelFat16FileCommitClose(owner), UMICOM_FAT16_UPDATE_OK,
        "completed close remains idempotent");
    UmicomFatFileCommitInputCheck();
    UmicomFatFileCommitRestore(&before, &machineBefore);
    UmicomFatFileCommitFill(umicomFatFileCommitInput, sizeof(umicomFatFileCommitInput), 0U);
    UmicomKernelConsoleWriteLine(complete ? "fat16-file-commit-test=pass" : "fat16-file-commit-interrupted-test=pass");
    UmicomKernelConsoleWriteLine(complete ? "UMICOM_KERNEL_FAT16_FILE_COMMIT_READY" :
        "UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_READY");
}
#ifdef UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST
void UmicomKernelFat16FileCommitValidate(void) { UmicomFatFileCommitWriter(UMICOM_TRUE); }
#else
void UmicomKernelFat16FileCommitInterruptedValidate(void) { UmicomFatFileCommitWriter(UMICOM_FALSE); }
#endif

#elif defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_TEST)
static UmicomKernelDiskMount umicomFatFileCommitMount;
static UmicomKernelVfsClient umicomFatFileCommitClient;

static void UmicomFatFileCommitFile(const char *path, const char *literal, UmicomSize expectedBytes)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomFatFileCommitFill(&info, sizeof(info), 0U);
    UmicomFatFileCommitRequire(UmicomKernelVfsOpen(&umicomFatFileCommitClient, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "fresh file descriptor over unchanged read-only provider");
    UmicomFatFileCommitRequire(UmicomKernelVfsQuery(&umicomFatFileCommitClient, descriptor, &info) == UMICOM_VFS_OK &&
        info.kind == UMICOM_VFS_FILE && info.bytes == expectedBytes, "existing file size unchanged");
    UmicomFatFileCommitFill(umicomFatFileCommitReadback, sizeof(umicomFatFileCommitReadback), 0xa5U);
    UmicomSize count = 0U;
    UmicomFatFileCommitRequire(UmicomKernelVfsRead(&umicomFatFileCommitClient, descriptor,
        umicomFatFileCommitReadback, sizeof(umicomFatFileCommitReadback), &count) == UMICOM_VFS_OK &&
        count == expectedBytes, "complete file read through VFS after a fresh guest start");
    for (UmicomSize i = 0U; i < sizeof(umicomFatFileCommitReadback); ++i) {
        const UmicomU8 expected = i < expectedBytes ?
            (literal ? (UmicomU8)literal[i] : UmicomFat16UpdateFixtureFileByte(i, UMICOM_TRUE)) : 0xa5U;
        UmicomFatFileCommitRequire(umicomFatFileCommitReadback[i] == expected,
            "file contents and unused caller output are exact");
    }
    count = 99U;
    UmicomFatFileCommitRequire(UmicomKernelVfsRead(&umicomFatFileCommitClient, descriptor,
        umicomFatFileCommitReadback, 1U, &count) == UMICOM_VFS_OK && !count, "unchanged EOF after bounded update");
    UmicomFatFileCommitRequire(UmicomKernelVfsClose(&umicomFatFileCommitClient, descriptor) == UMICOM_VFS_OK,
        "file description and provider pin released");
}
void UmicomKernelFat16FileCommitReadbackValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-file-commit-readback-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatFileCommitFill(&before, sizeof(before), 0U);
    UmicomFatFileCommitFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatFileCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatFileCommitDiscover(&slot);
    UmicomFatFileCommitRequire(UmicomKernelDiskMountOpen(&umicomFatFileCommitMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_OK, "fresh read-only mount after clean finalisation");
    UmicomKernelBlockInfo info;
    UmicomFatFileCommitFill(&info, sizeof(info), 0U);
    UmicomFatFileCommitBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh read-only transport geometry");
    UmicomFatFileCommitRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "fixture capacity and exclusive read-only DMA lease");
    UmicomFatFileCommitDisk(domain, umicomFatFileCommitMount.handle, UMICOM_FAT16_FILE_FIXTURE_COMMITTED);
    UmicomKernelConsoleWriteLine("fat16-file-commit-readback.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-file-commit-readback.changes=700-file-bytes-and-five-directory-bytes");
    UmicomFatFileCommitRequire(UmicomKernelDiskMountClientOpen(&umicomFatFileCommitMount,
        &umicomFatFileCommitClient, 42U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK,
        "independent read-only file client");
    UmicomFatFileCommitFile("/FRAG.BIN", (const char *)0, UMICOM_DISK_FIXTURE_FRAGMENT_BYTES);
    UmicomFatFileCommitFile("/README.TXT", UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    UmicomFatFileCommitFile("/DOCS/GUIDE.TXT", UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    UmicomKernelFileDescriptor refused = 0U;
    UmicomFatFileCommitRequire(UmicomKernelVfsOpen(&umicomFatFileCommitClient, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE, &refused) == UMICOM_VFS_ACCESS_DENIED && !refused,
        "the existing VFS provider still denies write rights");
    UmicomKernelConsoleWriteLine("fat16-file-commit-readback.vfs-size-content-eof-and-read-only-rights=verified");
    UmicomSize closed = 0U;
    UmicomFatFileCommitRequire(UmicomKernelVfsClientClose(&umicomFatFileCommitClient, &closed) == UMICOM_VFS_OK &&
        !closed && UmicomKernelDiskMountClose(&umicomFatFileCommitMount) == UMICOM_VFS_OK &&
        !umicomFatFileCommitMount.handle, "client and mount retired before transport release");
    UmicomFatFileCommitRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-file-commit-readback-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_READY");
}

#else
static UmicomKernelDiskMount umicomFatFileCommitRefusedMount;

void UmicomKernelFat16FileCommitRejectedValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-file-commit-rejected-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatFileCommitFill(&before, sizeof(before), 0U);
    UmicomFatFileCommitFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatFileCommitRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh refusal guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatFileCommitDiscover(&slot);
    UmicomKernelBlockHandle handle = 0U;
    UmicomFatFileCommitBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &handle), UMICOM_BLOCK_OK,
        "fresh read-only lease can inspect raw interruption evidence");
    UmicomKernelBlockInfo info;
    UmicomFatFileCommitFill(&info, sizeof(info), 0U);
    UmicomFatFileCommitBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh refusal transport geometry");
    UmicomFatFileCommitRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "read-only interruption fixture and owned DMA frames");
    UmicomFatFileCommitDisk(domain, handle, UMICOM_FAT16_FILE_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-file-commit-rejected.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-file-commit-rejected.changes=700-file-bytes-five-directory-bytes-and-two-clean-bits");
    UmicomFatFileCommitBlock(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK,
        "raw verification lease released before filesystem admission");
    UmicomFatFileCommitRequire(UmicomKernelDiskMountOpen(&umicomFatFileCommitRefusedMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_CORRUPT_FILESYSTEM &&
        umicomFatFileCommitRefusedMount.provider.lastDiskStatus == UMICOM_DISK_DIRTY &&
        !umicomFatFileCommitRefusedMount.admitted && !umicomFatFileCommitRefusedMount.provider.volume.open,
        "unchanged read-only filesystem rejects persistent dirty flags before publication");
    UmicomFatFileCommitRequire(UmicomKernelDiskMountClose(&umicomFatFileCommitRefusedMount) == UMICOM_VFS_OK &&
        !umicomFatFileCommitRefusedMount.handle, "refused mount releases transport without repairing flags");
    UmicomKernelConsoleWriteLine("fat16-file-commit-rejected.filesystem=dirty-refused");
    UmicomKernelConsoleWriteLine("fat16-file-commit-rejected.repair=not-performed");
    UmicomFatFileCommitRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-file-commit-rejected-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_FILE_COMMIT_REJECTED_READY");
}
#endif
