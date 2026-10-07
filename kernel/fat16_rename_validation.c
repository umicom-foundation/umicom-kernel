/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_rename_validation.c
 *
 * Qualify an ordered same-directory FAT16 short-name rename while
 * preserving size, attributes, calendars, data and every allocation byte.
 * A complete writer stages and finishes the rename on an initially unarchived
 * file; a fresh read-only process checks every disk byte and VFS behaviour.
 * A second writer ends after Stage and Close; another fresh process verifies
 * the new alias, unchanged data and all directory neighbours. The original
 * provider refuses persistent dirty FATs before publishing a mount.
 *
 * These are real file-backed flush/restart observations. Deliberately omitting
 * Finish is not a physical power cut, atomic-write guarantee or repair test.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_rename_commit.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/fat16_metadata.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_rename/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_RENAME_TEST) + \
     defined(UMICOM_KERNEL_FAT16_RENAME_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_RENAME_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_RENAME_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 rename qualification image"
#endif

#define UMICOM_FAT16_RENAME_GUARD_BYTES 32U
static UmicomU8 umicomFatRenameReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatRenameExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatRenameFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatRenameRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-rename.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatRenameBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-rename.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatRenameRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatRenameEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatRenameMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatRenameDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatRenameBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatRenameFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outSlot = i; ++found; }
        else UmicomFatRenameRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatRenameRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatRenameDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomFat16RenameFixtureState state)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatRenameBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatRenameReadback, sizeof(umicomFatRenameReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16RenameFixtureSector(first + sector, umicomFatRenameExpected, state);
            UmicomFatRenameRequire(UmicomFatRenameEqual(umicomFatRenameExpected,
                umicomFatRenameReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "complete disk agrees with exact original, staged or committed fixture");
        }
    }
}
static void UmicomFatRenameRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatRenameFill(&after, sizeof(after), 0U);
    UmicomFatRenameFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatRenameRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatRenameRequire(UmicomFatRenameMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-rename.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-rename.machine-state=unchanged");
    UmicomFatRenameFill(umicomFatRenameReadback, sizeof(umicomFatRenameReadback), 0U);
    UmicomFatRenameFill(umicomFatRenameExpected, sizeof(umicomFatRenameExpected), 0U);
}

#if defined(UMICOM_KERNEL_FAT16_RENAME_READBACK_TEST) || defined(UMICOM_KERNEL_FAT16_RENAME_REJECTED_TEST)
typedef struct UmicomFatRenameReader {
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
} UmicomFatRenameReader;
static UmicomKernelFat16 umicomFatRenameMetadataVolume;
static UmicomBoolean UmicomFatRenameSectorRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomFatRenameReader *const reader = (UmicomFatRenameReader *)context;
    return UmicomKernelBlockRead(reader->domain, reader->handle, sector, 1U,
        output, UMICOM_BLOCK_SECTOR_BYTES) == UMICOM_BLOCK_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
static void UmicomFatRenameMetadata(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle, UmicomBoolean dirty)
{
    UmicomFatRenameReader source = {domain, handle};
    const UmicomKernelDiskReader reader = {UMICOM_DISK_FIXTURE_SECTORS, UmicomFatRenameSectorRead, &source};
    struct {
        UmicomU8 before[32];
        UmicomKernelFat16Metadata value;
        UmicomU8 after[32];
    } output;
    UmicomFatRenameFill(&output, sizeof(output), 0xa5U);
    if (dirty) {
        UmicomFatRenameRequire(UmicomKernelFat16Open(&umicomFatRenameMetadataVolume, &reader, 0U) == UMICOM_DISK_DIRTY &&
            !umicomFatRenameMetadataVolume.open, "fresh inspector refuses dirty rename before metadata publication");
        UmicomFatRenameRequire(UmicomKernelFat16MetadataRead(&umicomFatRenameMetadataVolume, "/FRAG.BIN",
            &output.value) == UMICOM_DISK_BAD_STATE, "unopened metadata owner refuses query");
        const UmicomU8 *const bytes = (const UmicomU8 *)&output;
        for (UmicomSize i = 0U; i < sizeof(output); ++i)
            UmicomFatRenameRequire(bytes[i] == 0xa5U, "dirty refusal preserves result, padding and guards");
        UmicomKernelConsoleWriteLine("fat16-rename-rejected.metadata=dirty-refused-output-unchanged");
        return;
    }
    UmicomFatRenameRequire(UmicomKernelFat16Open(&umicomFatRenameMetadataVolume, &reader, 0U) == UMICOM_DISK_OK,
        "fresh public metadata owner opens the completed rename image");
    UmicomFatRenameRequire(UmicomKernelFat16MetadataRead(&umicomFatRenameMetadataVolume, "/FRAG.BIN",
        &output.value) == UMICOM_DISK_NOT_FOUND, "original alias is absent in a fresh inspector");
    const UmicomU8 *const untouched = (const UmicomU8 *)&output;
    for (UmicomSize i = 0U; i < sizeof(output); ++i)
        UmicomFatRenameRequire(untouched[i] == 0xa5U, "old-name refusal preserves output, padding and guards");
    UmicomFatRenameRequire(UmicomKernelFat16MetadataRead(&umicomFatRenameMetadataVolume, "/saved.bin", &output.value) == UMICOM_DISK_OK,
        "fresh public metadata owner reads the renamed alias");
    for (UmicomSize i = 0U; i < sizeof(output.before); ++i)
        UmicomFatRenameRequire(output.before[i] == 0xa5U && output.after[i] == 0xa5U, "metadata caller guards unchanged");
    const UmicomKernelFat16Metadata *const metadata = &output.value;
    UmicomFatRenameRequire(metadata->directoryEntryPresent && !metadata->entry.directory &&
        UmicomFatRenameEqual(metadata->entry.name, "SAVED.BIN", 10U) &&
        metadata->entry.bytes == 1300U && metadata->entry.firstCluster == 4U && metadata->entry.attributes == 0U &&
        metadata->writeTimestamp.state == UMICOM_FAT16_TIMESTAMP_ABSENT &&
        !metadata->writeTimestamp.rawTime && !metadata->writeTimestamp.rawDate &&
        !metadata->writeTimestamp.value.year && !metadata->writeTimestamp.value.month &&
        !metadata->writeTimestamp.value.day && !metadata->writeTimestamp.value.hour &&
        !metadata->writeTimestamp.value.minute && !metadata->writeTimestamp.value.second,
        "only the saved alias changed; size, first cluster, ARCHIVE and absent calendar remain original");
    UmicomFatRenameRequire(UmicomKernelFat16Close(&umicomFatRenameMetadataVolume) == UMICOM_DISK_OK,
        "independent metadata inspector releases its borrowed reader");
    UmicomKernelConsoleWriteLine("fat16-rename-readback.metadata=old-name-absent-new-name-SAVED.BIN-size-1300-archive-clear-time-absent");
}
#endif

#if defined(UMICOM_KERNEL_FAT16_RENAME_TEST) || defined(UMICOM_KERNEL_FAT16_RENAME_INTERRUPTED_TEST)
static UmicomKernelFat16RenameCommitter umicomFatRenameOwner;

static void UmicomFatRenameCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static void UmicomFatRenameStatus(UmicomKernelFat16UpdateStatus actual,
    UmicomKernelFat16UpdateStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-rename.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWrite("fat16-rename.disk-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatRenameOwner.commit.updater.lastDiskStatus));
    UmicomFatRenameRequire(UMICOM_FALSE, reason);
}
static void UmicomFatRenameNoData(const UmicomKernelFat16RenameResult *result)
{
    UmicomFatRenameRequire(result->commit.dataOutcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED &&
        !result->commit.offset && !result->commit.requestedBytes && !result->commit.confirmedBytes &&
        !result->commit.submittedBytes && !result->commit.submittedDataSectors &&
        !result->commit.completedDataSectors && !result->commit.dataDurable && !result->commit.dataVerified,
        "metadata-only rename never fabricates a file-data write, durability or byte result");
}
static void UmicomFatRenameMetadataCheck(const UmicomKernelFat16RenameResult *result)
{
    UmicomFatRenameNoData(result);
    UmicomFatRenameRequire(result->directoryPlanned && result->directorySubmitted &&
        result->directoryCompleted && result->directoryDurable && result->directoryVerified &&
        result->directorySector == UMICOM_DISK_FIXTURE_ROOT && result->entryOffset == 96U &&
        UmicomFatRenameEqual(result->originalEntry.name, "FRAG.BIN", 9U) &&
        result->originalEntry.bytes == 1300U && result->originalEntry.firstCluster == 4U &&
        !result->originalEntry.directory && !result->originalEntry.attributes &&
        UmicomFatRenameEqual(result->updatedName, "SAVED.BIN", 10U),
        "rename evidence retains the original entry and canonical replacement alias");
}
static void UmicomFatRenameRefused(const char *path, const char *replacement,
    UmicomKernelFat16UpdateStatus expected, UmicomKernelDiskStatus diskStatus)
{
    /* Separate storage prevents compiler suffix-merging of source and target
     * literals from accidentally making this an argument-overlap test. */
    char newName[13];
    UmicomFatRenameFill(newName, sizeof(newName), 0U);
    for (UmicomSize i = 0U; replacement[i] && i < sizeof(newName) - 1U; ++i) newName[i] = replacement[i];
    UmicomKernelFat16RenameResult result;
    UmicomFatRenameFill(&result, sizeof(result), 0U);
    UmicomFatRenameStatus(UmicomKernelFat16RenameStage(&umicomFatRenameOwner,
        path, newName, &result), expected, "rename refusal before any metadata mutation");
    UmicomFatRenameNoData(&result);
    UmicomFatRenameRequire(umicomFatRenameOwner.commit.state == UMICOM_FAT16_COMMIT_READY &&
        result.commit.diskStatus == diskStatus && !result.commit.submittedMetadataSectors &&
        !result.commit.completedMetadataSectors && !result.commit.completedFlushes &&
        !result.commit.mediaTouched && !result.commit.uncertainSectorValid &&
        !result.commit.needsFlush && !result.commit.writeUncertain && !result.directorySubmitted &&
        !result.directoryCompleted && !result.directoryDurable && !result.directoryVerified,
        "collision, readonly and directory refusals retain READY with no WRITE or FLUSH");
}
static void UmicomFatRenameWriter(UmicomBoolean complete)
{
    UmicomKernelConsoleWriteLine(complete ? "fat16-rename-test=begin" : "fat16-rename-interrupted-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatRenameFill(&before, sizeof(before), 0U);
    UmicomFatRenameFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatRenameRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatRenameDiscover(&slot);
    UmicomKernelFat16RenameCommitter *const owner = &umicomFatRenameOwner;
    UmicomFatRenameStatus(UmicomKernelFat16RenameOpen(owner, domain, slot, 0U, 10000000U),
        UMICOM_FAT16_UPDATE_OK, "exclusive writable rename lease admitted");
    UmicomFatRenameRequire(owner->commit.state == UMICOM_FAT16_COMMIT_READY && owner->commit.updater.handle &&
        owner->commit.updater.sectors == UMICOM_DISK_FIXTURE_SECTORS && owner->commit.updater.info.clusters == 12159U,
        "clean admission and exact fixture geometry");
    UmicomFatRenameDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_RENAME_FIXTURE_ORIGINAL);
    UmicomKernelConsoleWriteLine("fat16-rename.original-disk-bytes=8388608-verified");
    UmicomFatRenameRefused("/README.TXT", "SAVED.BIN", UMICOM_FAT16_UPDATE_READ_ONLY, UMICOM_DISK_READ_ONLY);
    UmicomFatRenameRefused("/FRAG.BIN", "FRAG.BIN", UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR, UMICOM_DISK_EXISTS);
    UmicomFatRenameRefused("/FRAG.BIN", "README.TXT", UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR, UMICOM_DISK_EXISTS);
    UmicomFatRenameRefused("/DOCS", "SAVED", UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR, UMICOM_DISK_IS_DIRECTORY);
    UmicomFatRenameDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_RENAME_FIXTURE_ORIGINAL);
    UmicomKernelConsoleWriteLine("fat16-rename.preflight-refusals-before-write-and-flush=verified");
    UmicomKernelFat16RenameResult result, historical, untouched;
    UmicomFatRenameFill(&result, sizeof(result), 0U);
    UmicomFatRenameStatus(UmicomKernelFat16RenameStage(owner, "/frag.bin", "saved.bin", &result),
        UMICOM_FAT16_UPDATE_OK, "dirty guards precede the sole directory-sector rename write");
    UmicomFatRenameMetadataCheck(&result);
    UmicomFatRenameRequire(owner->commit.state == UMICOM_FAT16_COMMIT_STAGED &&
        result.commit.submittedMetadataSectors == 3U && result.commit.completedMetadataSectors == 3U &&
        result.commit.completedFlushes == 3U && result.commit.mediaTouched && result.commit.dirtyDurable &&
        result.commit.dirtyVerified && !result.commit.cleanFinalisationStarted && !result.commit.cleanDurable &&
        !result.commit.cleanVerified && !result.commit.commitAccepted && !result.commit.uncertainSectorValid &&
        !result.commit.needsFlush && !result.commit.writeUncertain, "three metadata writes and barriers leave durable dirty evidence");
    UmicomFatRenameDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_RENAME_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-rename.staged-disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-rename.staged-changes=five-short-alias-bytes-and-two-clean-bits");
    UmicomKernelConsoleWriteLine("fat16-rename.data-sectors=0 metadata-sectors=3 flushes=3");
    UmicomFatRenameCopy(&historical, &owner->lastResult, sizeof(historical));
    UmicomFatRenameFill(&result, sizeof(result), 0xa5U);
    UmicomFatRenameFill(&untouched, sizeof(untouched), 0xa5U);
    UmicomFatRenameStatus(UmicomKernelFat16RenameStage(owner, "/SAVED.BIN", "OTHER.BIN", &result),
        UMICOM_FAT16_UPDATE_BAD_STATE, "staged rename owner cannot begin another operation");
    UmicomFatRenameRequire(UmicomFatRenameEqual(&result, &untouched, sizeof(result)) &&
        UmicomFatRenameEqual(&historical, &owner->lastResult, sizeof(historical)),
        "second Stage refusal preserves every output and historical evidence byte");
    if (complete) {
        UmicomFatRenameStatus(UmicomKernelFat16RenameFinish(owner, &result), UMICOM_FAT16_UPDATE_OK,
            "explicit Finish publishes clean only after directory and dirty-header reverification");
        UmicomFatRenameMetadataCheck(&result);
        UmicomFatRenameRequire(owner->commit.state == UMICOM_FAT16_COMMIT_COMMITTED &&
            result.commit.phase == UMICOM_FAT16_COMMIT_COMPLETE && result.commit.commitAccepted &&
            result.commit.dirtyDurable && result.commit.dirtyVerified && result.commit.cleanFinalisationStarted &&
            result.commit.cleanDurable && result.commit.cleanVerified &&
            result.commit.completedMetadataSectors == 5U && result.commit.submittedMetadataSectors == 5U &&
            result.commit.completedFlushes == 5U && !result.commit.uncertainSectorValid &&
            !result.commit.needsFlush && !result.commit.writeUncertain, "five metadata writes and barriers complete without any data operation");
        UmicomFatRenameDisk(domain, owner->commit.updater.handle, UMICOM_FAT16_RENAME_FIXTURE_COMMITTED);
        UmicomKernelConsoleWriteLine("fat16-rename.committed-disk-bytes=8388608-verified");
        UmicomKernelConsoleWriteLine("fat16-rename.committed-changes=only-five-short-alias-bytes");
        UmicomKernelConsoleWriteLine("fat16-rename.data-sectors=0 metadata-sectors=5 flushes=5");
        UmicomFatRenameCopy(&historical, &owner->lastResult, sizeof(historical));
    } else {
        UmicomKernelConsoleWriteLine("fat16-rename.finish=deliberately-omitted-not-a-physical-power-cut");
    }
    UmicomFatRenameStatus(UmicomKernelFat16RenameClose(owner), UMICOM_FAT16_UPDATE_OK,
        "resource-only Close neither commits nor repairs dirty media");
    UmicomFatRenameRequire(owner->commit.state == UMICOM_FAT16_COMMIT_CLOSED && !owner->commit.updater.handle &&
        UmicomFatRenameEqual(&historical, &owner->lastResult, sizeof(historical)), "Close releases the lease and retains evidence");
    UmicomFatRenameStatus(UmicomKernelFat16RenameClose(owner), UMICOM_FAT16_UPDATE_OK, "successful Close is idempotent");
    UmicomFatRenameRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine(complete ? "fat16-rename-test=pass" : "fat16-rename-interrupted-test=pass");
    UmicomKernelConsoleWriteLine(complete ? "UMICOM_KERNEL_FAT16_RENAME_READY" : "UMICOM_KERNEL_FAT16_RENAME_INTERRUPTED_READY");
}
#ifdef UMICOM_KERNEL_FAT16_RENAME_TEST
void UmicomKernelFat16RenameValidate(void) { UmicomFatRenameWriter(UMICOM_TRUE); }
#else
void UmicomKernelFat16RenameInterruptedValidate(void) { UmicomFatRenameWriter(UMICOM_FALSE); }
#endif

#elif defined(UMICOM_KERNEL_FAT16_RENAME_READBACK_TEST)
static UmicomKernelDiskMount umicomFatRenameMount;
static UmicomKernelVfsClient umicomFatRenameClient;

static void UmicomFatRenameFile(const char *path, const char *literal, UmicomSize expectedBytes)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomFatRenameFill(&info, sizeof(info), 0U);
    UmicomFatRenameRequire(UmicomKernelVfsOpen(&umicomFatRenameClient, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "fresh file descriptor over unchanged read-only provider");
    UmicomFatRenameRequire(UmicomKernelVfsQuery(&umicomFatRenameClient, descriptor, &info) == UMICOM_VFS_OK &&
        info.kind == UMICOM_VFS_FILE && info.bytes == expectedBytes, "fresh VFS reports the preserved original file size");
    UmicomFatRenameFill(umicomFatRenameReadback, sizeof(umicomFatRenameReadback), 0xa5U);
    UmicomSize count = 0U;
    UmicomFatRenameRequire(UmicomKernelVfsRead(&umicomFatRenameClient, descriptor,
        umicomFatRenameReadback, sizeof(umicomFatRenameReadback), &count) == UMICOM_VFS_OK &&
        count == expectedBytes, "complete file read through VFS after a fresh guest start");
    for (UmicomSize i = 0U; i < sizeof(umicomFatRenameReadback); ++i) {
        const UmicomU8 expected = i < expectedBytes ?
            (literal ? (UmicomU8)literal[i] : UmicomDiskFixturePattern(i)) : 0xa5U;
        UmicomFatRenameRequire(umicomFatRenameReadback[i] == expected,
            "file contents and unused caller output are exact");
    }
    count = 99U;
    UmicomFatRenameRequire(UmicomKernelVfsRead(&umicomFatRenameClient, descriptor,
        umicomFatRenameReadback, 1U, &count) == UMICOM_VFS_OK && !count, "fresh EOF remains at the original byte count");
    UmicomFatRenameRequire(UmicomKernelVfsClose(&umicomFatRenameClient, descriptor) == UMICOM_VFS_OK,
        "file description and provider pin released");
}
void UmicomKernelFat16RenameReadbackValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-rename-readback-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatRenameFill(&before, sizeof(before), 0U);
    UmicomFatRenameFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatRenameRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatRenameDiscover(&slot);
    UmicomKernelBlockHandle metadataHandle = 0U;
    UmicomFatRenameBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &metadataHandle), UMICOM_BLOCK_OK,
        "read-only metadata lease is independent of the future VFS mount");
    UmicomFatRenameMetadata(domain, metadataHandle, UMICOM_FALSE);
    UmicomFatRenameDisk(domain, metadataHandle, UMICOM_FAT16_RENAME_FIXTURE_COMMITTED);
    UmicomFatRenameBlock(UmicomKernelBlockClose(domain, metadataHandle), UMICOM_BLOCK_OK,
        "metadata lease releases before fresh VFS acquisition");
    UmicomFatRenameRequire(UmicomKernelDiskMountOpen(&umicomFatRenameMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_OK, "fresh read-only mount after clean finalisation");
    UmicomKernelBlockInfo info;
    UmicomFatRenameFill(&info, sizeof(info), 0U);
    UmicomFatRenameBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh read-only transport geometry");
    UmicomFatRenameRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "fixture capacity and exclusive read-only DMA lease");
    UmicomFatRenameDisk(domain, umicomFatRenameMount.handle, UMICOM_FAT16_RENAME_FIXTURE_COMMITTED);
    UmicomKernelConsoleWriteLine("fat16-rename-readback.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-rename-readback.changes=only-five-short-alias-bytes");
    UmicomFatRenameRequire(UmicomKernelDiskMountClientOpen(&umicomFatRenameMount,
        &umicomFatRenameClient, 42U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK,
        "independent read-only file client");
    UmicomKernelFileDescriptor missing = 0U;
    UmicomFatRenameRequire(UmicomKernelVfsOpen(&umicomFatRenameClient, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &missing) == UMICOM_VFS_NOT_FOUND && !missing,
        "fresh VFS cannot open the original short alias");
    UmicomFatRenameFile("/SAVED.BIN", (const char *)0, UMICOM_DISK_FIXTURE_FRAGMENT_BYTES);
    UmicomFatRenameFile("/README.TXT", UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    UmicomFatRenameFile("/DOCS/GUIDE.TXT", UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    UmicomKernelFileDescriptor refused = 0U;
    UmicomFatRenameRequire(UmicomKernelVfsOpen(&umicomFatRenameClient, "/SAVED.BIN",
        UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE, &refused) == UMICOM_VFS_ACCESS_DENIED && !refused,
        "the existing VFS provider still denies write rights");
    UmicomKernelConsoleWriteLine("fat16-rename-readback.vfs-size-content-eof-and-read-only-rights=verified");
    UmicomSize closed = 0U;
    UmicomFatRenameRequire(UmicomKernelVfsClientClose(&umicomFatRenameClient, &closed) == UMICOM_VFS_OK &&
        !closed && UmicomKernelDiskMountClose(&umicomFatRenameMount) == UMICOM_VFS_OK &&
        !umicomFatRenameMount.handle, "client and mount retired before transport release");
    UmicomFatRenameRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-rename-readback-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_RENAME_READBACK_READY");
}

#else
static UmicomKernelDiskMount umicomFatRenameRefusedMount;

void UmicomKernelFat16RenameRejectedValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-rename-rejected-test=begin");
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatRenameFill(&before, sizeof(before), 0U);
    UmicomFatRenameFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatRenameRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator available in fresh refusal guest");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatRenameDiscover(&slot);
    UmicomKernelBlockHandle handle = 0U;
    UmicomFatRenameBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &handle), UMICOM_BLOCK_OK,
        "fresh read-only lease can inspect raw interruption evidence");
    UmicomKernelBlockInfo info;
    UmicomFatRenameFill(&info, sizeof(info), 0U);
    UmicomFatRenameBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK,
        "fresh refusal transport geometry");
    UmicomFatRenameRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS &&
        !info.writable && info.heldFrames == 2U, "read-only interruption fixture and owned DMA frames");
    UmicomFatRenameDisk(domain, handle, UMICOM_FAT16_RENAME_FIXTURE_STAGED);
    UmicomFatRenameMetadata(domain, handle, UMICOM_TRUE);
    UmicomFatRenameDisk(domain, handle, UMICOM_FAT16_RENAME_FIXTURE_STAGED);
    UmicomKernelConsoleWriteLine("fat16-rename-rejected.disk-bytes=8388608-verified");
    UmicomKernelConsoleWriteLine("fat16-rename-rejected.changes=five-short-alias-bytes-and-two-clean-bits");
    UmicomFatRenameBlock(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK,
        "raw verification lease released before filesystem admission");
    UmicomFatRenameRequire(UmicomKernelDiskMountOpen(&umicomFatRenameRefusedMount, domain,
        slot, 0U, 10000000U) == UMICOM_VFS_CORRUPT_FILESYSTEM &&
        umicomFatRenameRefusedMount.provider.lastDiskStatus == UMICOM_DISK_DIRTY &&
        !umicomFatRenameRefusedMount.admitted && !umicomFatRenameRefusedMount.provider.volume.open,
        "unchanged read-only filesystem rejects persistent dirty flags before publication");
    UmicomFatRenameRequire(UmicomKernelDiskMountClose(&umicomFatRenameRefusedMount) == UMICOM_VFS_OK &&
        !umicomFatRenameRefusedMount.handle, "refused mount releases transport without repairing flags");
    UmicomKernelConsoleWriteLine("fat16-rename-rejected.filesystem=dirty-refused");
    UmicomKernelConsoleWriteLine("fat16-rename-rejected.repair=not-performed");
    UmicomFatRenameRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine("fat16-rename-rejected-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_RENAME_REJECTED_READY");
}
#endif
