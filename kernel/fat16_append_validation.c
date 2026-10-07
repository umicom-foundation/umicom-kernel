/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_append_validation.c
 *
 * Qualify bounded FAT16 append in existing final-cluster slack, including
 * the new file size, ARCHIVE and an explicitly supplied write calendar.
 * A complete writer stages and finishes the append on an initially unarchived
 * file; a fresh read-only process checks every disk byte and VFS behaviour.
 * A second writer ends after Stage and Close; another fresh process verifies
 * appended data, new size, directory bytes and the original provider's refusal of dirty FATs.
 *
 * These are real file-backed flush/restart observations. Deliberately omitting
 * Finish is not a physical power cut, atomic-write guarantee or repair test.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_file_append.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/fat16_metadata.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_append/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_APPEND_TEST) + \
     defined(UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_APPEND_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 append qualification image"
#endif

#define UMICOM_FAT16_APPEND_GUARD_BYTES 32U
static UmicomU8 umicomFatAppendReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatAppendExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatAppendFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatAppendRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-append.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatAppendBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-append.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatAppendRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatAppendEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatAppendMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatAppendDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatAppendBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatAppendFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outSlot = i; ++found; }
        else UmicomFatAppendRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatAppendRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatAppendDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomFat16AppendFixtureState state)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatAppendBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatAppendReadback, sizeof(umicomFatAppendReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16AppendFixtureSector(first + sector, umicomFatAppendExpected, state);
            UmicomFatAppendRequire(UmicomFatAppendEqual(umicomFatAppendExpected,
                umicomFatAppendReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "complete disk agrees with exact original, staged or committed fixture");
        }
    }
}
static void UmicomFatAppendRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatAppendFill(&after, sizeof(after), 0U);
    UmicomFatAppendFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatAppendRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatAppendRequire(UmicomFatAppendMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-append.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-append.machine-state=unchanged");
    UmicomFatAppendFill(umicomFatAppendReadback, sizeof(umicomFatAppendReadback), 0U);
    UmicomFatAppendFill(umicomFatAppendExpected, sizeof(umicomFatAppendExpected), 0U);
}

#if defined(UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST) || defined(UMICOM_KERNEL_FAT16_APPEND_REJECTED_TEST)
typedef struct UmicomFatAppendReader {
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
} UmicomFatAppendReader;
static UmicomKernelFat16 umicomFatAppendMetadataVolume;
static UmicomBoolean UmicomFatAppendSectorRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomFatAppendReader *const reader = (UmicomFatAppendReader *)context;
    return UmicomKernelBlockRead(reader->domain, reader->handle, sector, 1U,
        output, UMICOM_BLOCK_SECTOR_BYTES) == UMICOM_BLOCK_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
static void UmicomFatAppendMetadata(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle, UmicomBoolean dirty)
{
    UmicomFatAppendReader source = {domain, handle};
    const UmicomKernelDiskReader reader = {UMICOM_DISK_FIXTURE_SECTORS, UmicomFatAppendSectorRead, &source};
    struct {
        UmicomU8 before[32];
        UmicomKernelFat16Metadata value;
        UmicomU8 after[32];
    } output;
    UmicomFatAppendFill(&output, sizeof(output), 0xa5U);
    if (dirty) {
        UmicomFatAppendRequire(UmicomKernelFat16Open(&umicomFatAppendMetadataVolume, &reader, 0U) == UMICOM_DISK_DIRTY &&
            !umicomFatAppendMetadataVolume.open, "fresh inspector refuses dirty append before metadata publication");
        UmicomFatAppendRequire(UmicomKernelFat16MetadataRead(&umicomFatAppendMetadataVolume, "/FRAG.BIN",
            &output.value) == UMICOM_DISK_BAD_STATE, "unopened metadata owner refuses query");
        const UmicomU8 *const bytes = (const UmicomU8 *)&output;
        for (UmicomSize i = 0U; i < sizeof(output); ++i)
            UmicomFatAppendRequire(bytes[i] == 0xa5U, "dirty refusal preserves result, padding and guards");
        UmicomKernelConsoleWriteLine("fat16-append-rejected.metadata=dirty-refused-output-unchanged");
        return;
    }
    UmicomFatAppendRequire(UmicomKernelFat16Open(&umicomFatAppendMetadataVolume, &reader, 0U) == UMICOM_DISK_OK &&
        UmicomKernelFat16MetadataRead(&umicomFatAppendMetadataVolume, "/frag.bin", &output.value) == UMICOM_DISK_OK,
        "fresh public metadata owner reads the persisted append record");
    for (UmicomSize i = 0U; i < sizeof(output.before); ++i)
        UmicomFatAppendRequire(output.before[i] == 0xa5U && output.after[i] == 0xa5U, "metadata caller guards unchanged");
    const UmicomKernelFat16Metadata *const metadata = &output.value;
    UmicomFatAppendRequire(metadata->directoryEntryPresent && !metadata->entry.directory &&
        UmicomFatAppendEqual(metadata->entry.name, "FRAG.BIN", 9U) &&
        metadata->entry.bytes == 1497U && metadata->entry.firstCluster == 4U && metadata->entry.attributes == 0x20U &&
        metadata->writeTimestamp.state == UMICOM_FAT16_TIMESTAMP_VALID &&
        metadata->writeTimestamp.rawTime == 0xbf5cU && metadata->writeTimestamp.rawDate == 0x805dU &&
        metadata->writeTimestamp.value.year == 2044U && metadata->writeTimestamp.value.month == 2U &&
        metadata->writeTimestamp.value.day == 29U && metadata->writeTimestamp.value.hour == 23U &&
        metadata->writeTimestamp.value.minute == 58U && metadata->writeTimestamp.value.second == 56U,
        "fresh size, unchanged first cluster, ARCHIVE and leap-day calendar agree with independent constants");
    UmicomFatAppendRequire(UmicomKernelFat16Close(&umicomFatAppendMetadataVolume) == UMICOM_DISK_OK,
        "independent metadata inspector releases its borrowed reader");
    UmicomKernelConsoleWriteLine("fat16-append-readback.metadata=size-1497-archive-20-time-2044-02-29T23:58:56");
}
#endif

#if defined(UMICOM_KERNEL_FAT16_APPEND_TEST) || defined(UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_TEST)
static UmicomKernelFat16FileCommitter umicomFatAppendOwner;
static const UmicomKernelFat16FileTime umicomFatAppendTime = {
    UMICOM_FAT16_APPEND_FIXTURE_YEAR, UMICOM_FAT16_APPEND_FIXTURE_MONTH,
    UMICOM_FAT16_APPEND_FIXTURE_DAY, UMICOM_FAT16_APPEND_FIXTURE_HOUR,
    UMICOM_FAT16_APPEND_FIXTURE_MINUTE, UMICOM_FAT16_APPEND_FIXTURE_SECOND
};
static UmicomU8 umicomFatAppendInput[UMICOM_FAT16_APPEND_FIXTURE_SLACK + 1U +
    2U * UMICOM_FAT16_APPEND_GUARD_BYTES];

static void UmicomFatAppendCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static void UmicomFatAppendStatus(UmicomKernelFat16UpdateStatus actual,
    UmicomKernelFat16UpdateStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-append.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWrite("fat16-append.phase=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16CommitPhaseName(umicomFatAppendOwner.lastResult.commit.phase));
    UmicomKernelConsoleWrite("fat16-append.disk-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatAppendOwner.commit.updater.lastDiskStatus));
    UmicomKernelConsoleWrite("fat16-append.transport-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(umicomFatAppendOwner.commit.updater.lastBlockStatus));
    UmicomFatAppendRequire(UMICOM_FALSE, reason);
}
static void UmicomFatAppendInputCheck(void)
{
    for (UmicomSize i = 0U; i < sizeof(umicomFatAppendInput); ++i) {
        const UmicomU8 expected = i >= UMICOM_FAT16_APPEND_GUARD_BYTES &&
            i - UMICOM_FAT16_APPEND_GUARD_BYTES < UMICOM_FAT16_APPEND_FIXTURE_SLACK + 1U ?
            UmicomFat16AppendFixturePattern(i - UMICOM_FAT16_APPEND_GUARD_BYTES) : 0xa7U;
        UmicomFatAppendRequire(umicomFatAppendInput[i] == expected,
            "const caller input and both guards unchanged");
    }
}
static void UmicomFatAppendMetadataCheck(const UmicomKernelFat16FileCommitResult *result)
{
    UmicomFatAppendRequire(result->directoryPlanned && result->directorySubmitted &&
        result->directoryCompleted && result->directoryDurable && result->directoryVerified &&
        result->directorySector == UMICOM_DISK_FIXTURE_ROOT && result->entryOffset == 96U &&
        result->originalAttributes == 0U && result->updatedAttributes == 0x20U &&
        result->encodedTime.writeTime == UMICOM_FAT16_APPEND_FIXTURE_TIME &&
        result->encodedTime.writeDate == UMICOM_FAT16_APPEND_FIXTURE_DATE &&
        result->encodedTime.storedSecond == 56U &&
        result->requestedTime.year == 2044U && result->requestedTime.month == 2U &&
        result->requestedTime.day == 29U && result->requestedTime.hour == 23U &&
        result->requestedTime.minute == 58U && result->requestedTime.second == 57U,
        "archive-clear file gained ARCHIVE, the new EOF and the explicit two-second write timestamp");
}
static void UmicomFatAppendRefused(const char *path, UmicomSize bytes,
    UmicomKernelFat16UpdateStatus expected)
{
    UmicomKernelFat16FileCommitResult result;
    UmicomFatAppendFill(&result, sizeof(result), 0U);
    const UmicomU8 *const input = umicomFatAppendInput + UMICOM_FAT16_APPEND_GUARD_BYTES;
    UmicomFatAppendStatus(UmicomKernelFat16FileCommitAppend(&umicomFatAppendOwner,
        path, input, bytes, &umicomFatAppendTime, &result), expected, "file refusal before any dirty marker");
    UmicomFatAppendRequire(umicomFatAppendOwner.commit.state == UMICOM_FAT16_COMMIT_READY &&
        result.commit.dataOutcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && result.commit.requestedBytes == bytes &&
        !result.commit.confirmedBytes && !result.commit.submittedBytes && !result.commit.submittedMetadataSectors &&
        !result.commit.completedMetadataSectors && !result.commit.submittedDataSectors && !result.commit.completedDataSectors &&
        !result.commit.completedFlushes && !result.commit.mediaTouched && !result.commit.uncertainSectorValid &&
        !result.commit.needsFlush && !result.commit.writeUncertain && !result.directorySubmitted &&
        !result.directoryCompleted && !result.directoryDurable && !result.directoryVerified,
        "refusal leaves a ready owner and unchanged media");
}
static void UmicomFatAppendWriter(UmicomBoolean complete)
{
    UmicomKernelConsoleWriteLine(complete ? "fat16-append-test=begin" : "fat16-append-interrupted-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatAppendFill(&before, sizeof(before), 0U);
    UmicomFatAppendFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatAppendRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatAppendDiscover(&slot);
    UmicomKernelFat16FileCommitter *const owner = &umicomFatAppendOwner;
    UmicomFatAppendStatus(UmicomKernelFat16FileCommitOpen(owner, domain, slot, 0U, 10000000U),
        UMICOM_FAT16_UPDATE_OK, "exclusive writable commit lease admitted");
    UmicomFatAppendRequire(owner->commit.state == UMICOM_FAT16_COMMIT_READY && owner->commit.updater.handle &&
        owner->commit.updater.sectors == UMICOM_DISK_FIXTURE_SECTORS && owner->commit.updater.info.clusters == 12159U &&
        !owner->commit.updater.needsFlush && !owner->commit.updater.writeUncertain, "clean admission and fixture geometry");
    UmicomFatAppendDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_APPEND_FIXTURE_ORIGINAL);
    UmicomKernelConsoleWriteLine("fat16-append.original-disk-bytes=8388608-verified");
    UmicomFatAppendFill(umicomFatAppendInput, sizeof(umicomFatAppendInput), 0xa7U);
    UmicomU8 *const input = umicomFatAppendInput + UMICOM_FAT16_APPEND_GUARD_BYTES;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_APPEND_FIXTURE_SLACK + 1U; ++i)
        input[i] = UmicomFat16AppendFixturePattern(i);
    UmicomFatAppendRefused("/README.TXT", 1U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatAppendRefused("/DOCS/GUIDE.TXT", 1U, UMICOM_FAT16_UPDATE_READ_ONLY);
    UmicomFatAppendRefused("/FRAG.BIN", UMICOM_FAT16_APPEND_FIXTURE_SLACK + 1U, UMICOM_FAT16_UPDATE_RANGE);
    UmicomFatAppendRefused("/EMPTY.TXT", 1U, UMICOM_FAT16_UPDATE_RANGE);
    UmicomFatAppendDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_APPEND_FIXTURE_ORIGINAL);
    UmicomKernelConsoleWriteLine("fat16-append.preflight-refusals-before-write-and-flush=verified");

    UmicomKernelFat16FileCommitResult result, historical;
    UmicomFatAppendFill(&result, sizeof(result), 0U);
    UmicomFatAppendFill(&historical, sizeof(historical), 0U);
    UmicomFatAppendStatus(UmicomKernelFat16FileCommitAppend(owner, "/frag.bin",
        input, UMICOM_FAT16_APPEND_FIXTURE_BYTES,
        &umicomFatAppendTime, &result),
        UMICOM_FAT16_UPDATE_OK, "dirty guards precede append inside the existing final cluster");
    UmicomFatAppendRequire(owner->commit.state == UMICOM_FAT16_COMMIT_STAGED &&
        result.commit.dataOutcome == UMICOM_FAT16_UPDATE_COMPLETED && result.commit.offset == UMICOM_FAT16_APPEND_FIXTURE_OFFSET &&
        result.commit.requestedBytes == UMICOM_FAT16_APPEND_FIXTURE_BYTES &&
        result.commit.confirmedBytes == UMICOM_FAT16_APPEND_FIXTURE_BYTES &&
        result.commit.submittedBytes == UMICOM_FAT16_APPEND_FIXTURE_BYTES &&
        result.commit.completedDataSectors == 1U && result.commit.submittedDataSectors == 1U &&
        result.commit.completedMetadataSectors == 3U && result.commit.submittedMetadataSectors == 3U &&
        result.commit.completedFlushes == 4U && result.commit.mediaTouched && result.commit.dirtyDurable && result.commit.dirtyVerified &&
        result.commit.dataDurable && result.commit.dataVerified && !result.commit.cleanFinalisationStarted && !result.commit.cleanDurable &&
        !result.commit.cleanVerified && !result.commit.commitAccepted && !result.commit.uncertainSectorValid &&
        !result.commit.needsFlush && !result.commit.writeUncertain, "staged data and dirty flags have distinct complete evidence");
    UmicomFatAppendMetadataCheck(&result);
    UmicomFatAppendInputCheck();
    UmicomFatAppendDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_APPEND_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-append.staged-disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-append.staged-changes=197-appended-bytes-size-archive-time-and-two-clean-bits");
    UmicomKernelConsoleWriteLine("fat16-append.data-sectors=1 metadata-sectors=3 flushes=4");
    UmicomKernelConsoleWriteLine("fat16-append.write-calendar=requested-2044-02-29T23:58:57-stored-2044-02-29T23:58:56");
    UmicomKernelConsoleWriteLine("fat16-append.directory-sector=2145 entry-offset=96 archive=clear-to-set");
    UmicomFatAppendCopy(&historical, &owner->lastResult, sizeof(historical));
    UmicomFatAppendFill(&result, sizeof(result), 0xa5U);
    UmicomKernelFat16FileCommitResult untouched;
    UmicomFatAppendFill(&untouched, sizeof(untouched), 0xa5U);
    UmicomFatAppendStatus(UmicomKernelFat16FileCommitAppend(owner, "/FRAG.BIN", input, 1U,
        &umicomFatAppendTime, &result),
        UMICOM_FAT16_UPDATE_BAD_STATE, "a staged owner cannot append again");
    UmicomFatAppendRequire(UmicomFatAppendEqual(&result, &untouched, sizeof(result)) &&
        UmicomFatAppendEqual(&historical, &owner->lastResult, sizeof(historical)),
        "refused second Append preserves caller output and historical evidence");

    if (complete) {
        UmicomFatAppendStatus(UmicomKernelFat16FileCommitFinish(owner, &result), UMICOM_FAT16_UPDATE_OK,
            "explicit clean finalisation after complete staged re-verification");
        UmicomFatAppendRequire(owner->commit.state == UMICOM_FAT16_COMMIT_COMMITTED &&
            result.commit.phase == UMICOM_FAT16_COMMIT_COMPLETE && result.commit.commitAccepted &&
            result.commit.dirtyDurable && result.commit.dirtyVerified && result.commit.dataDurable && result.commit.dataVerified &&
            result.commit.cleanFinalisationStarted && result.commit.cleanDurable && result.commit.cleanVerified &&
            result.commit.confirmedBytes == UMICOM_FAT16_APPEND_FIXTURE_BYTES &&
            result.commit.completedDataSectors == 1U && result.commit.submittedDataSectors == 1U &&
            result.commit.completedMetadataSectors == 5U && result.commit.submittedMetadataSectors == 5U &&
            result.commit.completedFlushes == 6U && !result.commit.uncertainSectorValid &&
            !result.commit.needsFlush && !result.commit.writeUncertain, "clean commit retains all ordered evidence");
        UmicomFatAppendMetadataCheck(&result);
        UmicomFatAppendDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_APPEND_FIXTURE_COMMITTED);
        UmicomKernelConsoleWriteLine("fat16-append.committed-disk-bytes=8388608-verified");
        UmicomKernelConsoleWriteLine("fat16-append.committed-changes=197-appended-bytes-size-archive-and-time");
        UmicomKernelConsoleWriteLine("fat16-append.data-sectors=1 metadata-sectors=5 flushes=6");
        UmicomFatAppendCopy(&historical, &owner->lastResult, sizeof(historical));
    } else {
        UmicomKernelConsoleWriteLine("fat16-append.finish=deliberately-omitted");
    }
    UmicomFatAppendStatus(UmicomKernelFat16FileCommitClose(owner), UMICOM_FAT16_UPDATE_OK,
        "resource-only close after accepted or deliberately unfinished update");
    UmicomFatAppendRequire(owner->commit.state == UMICOM_FAT16_COMMIT_CLOSED && !owner->commit.updater.handle &&
        UmicomFatAppendEqual(&historical, &owner->lastResult, sizeof(historical)),
        "close releases the lease without editing commit evidence");
    UmicomFatAppendStatus(UmicomKernelFat16FileCommitClose(owner), UMICOM_FAT16_UPDATE_OK,
        "completed close remains idempotent");
    UmicomFatAppendInputCheck();
    UmicomFatAppendRestore(&before, &machineBefore);
    UmicomFatAppendFill(umicomFatAppendInput, sizeof(umicomFatAppendInput), 0U);
    UmicomKernelConsoleWriteLine(complete ? "fat16-append-test=pass" : "fat16-append-interrupted-test=pass");
    UmicomKernelConsoleWriteLine(complete ? "UMICOM_KERNEL_FAT16_APPEND_READY" :
        "UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_READY");
}
#ifdef UMICOM_KERNEL_FAT16_APPEND_TEST
void UmicomKernelFat16AppendValidate(void) { UmicomFatAppendWriter(UMICOM_TRUE); }
#else
void UmicomKernelFat16AppendInterruptedValidate(void) { UmicomFatAppendWriter(UMICOM_FALSE); }
#endif

#elif defined(UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST)
static UmicomKernelDiskMount umicomFatAppendMount;
static UmicomKernelVfsClient umicomFatAppendClient;

static void UmicomFatAppendFile(const char *path, const char *literal, UmicomSize expectedBytes)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomFatAppendFill(&info, sizeof(info), 0U);
    UmicomFatAppendRequire(UmicomKernelVfsOpen(&umicomFatAppendClient, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "fresh file descriptor over unchanged read-only provider");
    UmicomFatAppendRequire(UmicomKernelVfsQuery(&umicomFatAppendClient, descriptor, &info) == UMICOM_VFS_OK &&
        info.kind == UMICOM_VFS_FILE && info.bytes == expectedBytes, "fresh VFS reports the persisted new size or unchanged neighbour size");
    UmicomFatAppendFill(umicomFatAppendReadback, sizeof(umicomFatAppendReadback), 0xa5U);
    UmicomSize count = 0U;
    UmicomFatAppendRequire(UmicomKernelVfsRead(&umicomFatAppendClient, descriptor,
        umicomFatAppendReadback, sizeof(umicomFatAppendReadback), &count) == UMICOM_VFS_OK &&
        count == expectedBytes, "complete file read through VFS after a fresh guest start");
    for (UmicomSize i = 0U; i < sizeof(umicomFatAppendReadback); ++i) {
        const UmicomU8 expected = i < expectedBytes ?
            (literal ? (UmicomU8)literal[i] : UmicomFat16AppendFixtureFileByte(i)) : 0xa5U;
        UmicomFatAppendRequire(umicomFatAppendReadback[i] == expected,
            "file contents and unused caller output are exact");
    }
    count = 99U;
    UmicomFatAppendRequire(UmicomKernelVfsRead(&umicomFatAppendClient, descriptor,
        umicomFatAppendReadback, 1U, &count) == UMICOM_VFS_OK && !count, "fresh EOF follows the complete appended payload");
    UmicomFatAppendRequire(UmicomKernelVfsClose(&umicomFatAppendClient, descriptor) == UMICOM_VFS_OK,
        "file description and provider pin released");
}
void UmicomKernelFat16AppendReadbackValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-append-readback-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatAppendFill(&before, sizeof(before), 0U);
    UmicomFatAppendFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatAppendRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatAppendDiscover(&slot);
    UmicomKernelBlockHandle metadataHandle = 0U;
    UmicomFatAppendBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &metadataHandle), UMICOM_BLOCK_OK,
        "read-only metadata lease is independent of the future VFS mount");
    UmicomFatAppendMetadata(domain, metadataHandle, UMICOM_FALSE);
    UmicomFatAppendDisk(domain, metadataHandle, UMICOM_FAT16_APPEND_FIXTURE_COMMITTED);
    UmicomFatAppendBlock(UmicomKernelBlockClose(domain, metadataHandle), UMICOM_BLOCK_OK,
        "metadata lease releases before fresh VFS acquisition");
    UmicomFatAppendRequire(UmicomKernelDiskMountOpen(&umicomFatAppendMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_OK, "fresh read-only mount after clean finalisation");
    UmicomKernelBlockInfo info;
    UmicomFatAppendFill(&info, sizeof(info), 0U);
    UmicomFatAppendBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh read-only transport geometry");
    UmicomFatAppendRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "fixture capacity and exclusive read-only DMA lease");
    UmicomFatAppendDisk(domain, umicomFatAppendMount.handle, UMICOM_FAT16_APPEND_FIXTURE_COMMITTED);
    UmicomKernelConsoleWriteLine("fat16-append-readback.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-append-readback.changes=197-appended-bytes-size-archive-and-time");
    UmicomFatAppendRequire(UmicomKernelDiskMountClientOpen(&umicomFatAppendMount,
        &umicomFatAppendClient, 42U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK,
        "independent read-only file client");
    UmicomFatAppendFile("/FRAG.BIN", (const char *)0, UMICOM_FAT16_APPEND_FIXTURE_NEW_BYTES);
    UmicomFatAppendFile("/README.TXT", UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    UmicomFatAppendFile("/DOCS/GUIDE.TXT", UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    UmicomKernelFileDescriptor refused = 0U;
    UmicomFatAppendRequire(UmicomKernelVfsOpen(&umicomFatAppendClient, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE, &refused) == UMICOM_VFS_ACCESS_DENIED && !refused,
        "the existing VFS provider still denies write rights");
    UmicomKernelConsoleWriteLine("fat16-append-readback.vfs-size-content-eof-and-read-only-rights=verified");
    UmicomSize closed = 0U;
    UmicomFatAppendRequire(UmicomKernelVfsClientClose(&umicomFatAppendClient, &closed) == UMICOM_VFS_OK &&
        !closed && UmicomKernelDiskMountClose(&umicomFatAppendMount) == UMICOM_VFS_OK &&
        !umicomFatAppendMount.handle, "client and mount retired before transport release");
    UmicomFatAppendRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-append-readback-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_APPEND_READBACK_READY");
}

#else
static UmicomKernelDiskMount umicomFatAppendRefusedMount;

void UmicomKernelFat16AppendRejectedValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-append-rejected-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatAppendFill(&before, sizeof(before), 0U);
    UmicomFatAppendFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatAppendRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh refusal guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatAppendDiscover(&slot);
    UmicomKernelBlockHandle handle = 0U;
    UmicomFatAppendBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &handle), UMICOM_BLOCK_OK,
        "fresh read-only lease can inspect raw interruption evidence");
    UmicomKernelBlockInfo info;
    UmicomFatAppendFill(&info, sizeof(info), 0U);
    UmicomFatAppendBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh refusal transport geometry");
    UmicomFatAppendRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "read-only interruption fixture and owned DMA frames");
    UmicomFatAppendDisk(domain, handle, UMICOM_FAT16_APPEND_FIXTURE_STAGED);
    UmicomFatAppendMetadata(domain, handle, UMICOM_TRUE);
    UmicomFatAppendDisk(domain, handle, UMICOM_FAT16_APPEND_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-append-rejected.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-append-rejected.changes=197-appended-bytes-size-archive-time-and-two-clean-bits");
    UmicomFatAppendBlock(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK,
        "raw verification lease released before filesystem admission");
    UmicomFatAppendRequire(UmicomKernelDiskMountOpen(&umicomFatAppendRefusedMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_CORRUPT_FILESYSTEM &&
        umicomFatAppendRefusedMount.provider.lastDiskStatus == UMICOM_DISK_DIRTY &&
        !umicomFatAppendRefusedMount.admitted && !umicomFatAppendRefusedMount.provider.volume.open,
        "unchanged read-only filesystem rejects persistent dirty flags before publication");
    UmicomFatAppendRequire(UmicomKernelDiskMountClose(&umicomFatAppendRefusedMount) == UMICOM_VFS_OK &&
        !umicomFatAppendRefusedMount.handle, "refused mount releases transport without repairing flags");
    UmicomKernelConsoleWriteLine("fat16-append-rejected.filesystem=dirty-refused");
    UmicomKernelConsoleWriteLine("fat16-append-rejected.repair=not-performed");
    UmicomFatAppendRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-append-rejected-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_APPEND_REJECTED_READY");
}
#endif
