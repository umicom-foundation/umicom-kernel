/*-----------------------------------------------------------------------------
 * Umicom Kernel — timestamped FAT16 file commit qualification.
 *
 * Reuse the unchanged transport/allocator model and its visible/durable media
 * arrays. New assertions inspect chronological requests and every byte of the
 * synthetic medium, independently of the production phase/state decisions.
 * Complete directory sectors include neighbouring records, unused bytes and
 * preserved creation/access fields. Cached and immediate-persistence modes
 * both allow inspection at every mutation boundary; these are deterministic
 * protocol checks, not a physical power-loss or atomic-sector guarantee.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "../fat16_commit/commit_tests.c"
#include "umicom/kernel/fat16_file_plan.h"
#include "umicom/kernel/fat16_file_commit.h"
#include "umicom/kernel/fat16_file_commit_console.h"

static UmicomKernelFat16FileCommitter fileOwner;
static UmicomKernelFat16FileCommitResult fileResult, fileResultBefore;
static UmicomKernelFat16FileTime fileTime, fileTimeBefore;
static UmicomU64 fileDirectorySector;
static UmicomSize fileEntryOffset;
static UmicomBoolean fileCallbackReenter, filePolicyReenter;
static UmicomU32 fileReentries;
static UmicomSize fileReadCorruptionByte;
static UmicomBoolean fileConsoleCallbackReenter, fileConsoleOutputReenter;
static void FileConsoleReenter(void);

static void FileZero(const void *storage, UmicomSize bytes)
{
    const UmicomU8 *const data = storage;
    for (UmicomSize i = 0U; i < bytes; ++i) CHECK(data[i] == 0U);
}

static void FileReenter(void)
{
    UmicomKernelFat16FileCommitResult ignored, before;
    memset(&ignored, 0x59, sizeof(ignored)); memcpy(&before, &ignored, sizeof(before));
    CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16FileCommitStage(&fileOwner, "/FRAG.BIN", 0U, commitInput, 1U,
        &fileTime, &ignored) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16FileCommitFinish(&fileOwner, &ignored) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_BUSY);
    CommitEqual(&ignored, &before, sizeof(ignored)); ++fileReentries;
}
static UmicomBoolean FileAllowed(void *context)
{
    CHECK(context == &model);
    if (filePolicyReenter) FileReenter();
    return model.allowed;
}
static UmicomU64 FileClock(void *context)
{
    CHECK(context == &model);
    if (commitBackwardClock) { if (model.now) --model.now; return model.now; }
    if (commitStoppedClock) return model.now;
    UmicomU64 now = Clock(context);
    const UmicomBoolean matching = (commitClockMutation && commitMutations == commitClockMutation) ||
        (commitClockRead && commitReads == commitClockRead) ? UMICOM_TRUE : UMICOM_FALSE;
    if (matching && !commitClockInjected && !model.pending &&
        (commitClockInside ? domain.busy : !domain.busy) && commitClockSkip) --commitClockSkip;
    else if (matching && !commitClockInjected && !model.pending &&
        (commitClockInside ? domain.busy : !domain.busy)) {
        commitClockInjected = UMICOM_TRUE;
        now = commitClockRollback ? (commitClockInside ? commitPreviousClock - 1U :
            fileOwner.commit.updater.operationClock - 1U) : now +
            (commitClockInside ? domain.slots[0].timeoutTicks : UMICOM_FAT16_UPDATE_OPERATION_TICKS);
        model.now = now;
    }
    commitPreviousClock = now; return now;
}
static void FileWriteRegister(void *context, UmicomAddress address, UmicomU32 value)
{
    if (Offset(address) == UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU32 command = *(const UmicomU32 *)header;
        const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
        if (command == UMICOM_VIRTIO_REQUEST_WRITE && sector == fileDirectorySector) {
            /* Every previous data WRITE must be durable and have a successful
             * readback after the data FLUSH before metadata may be submitted. */
            CHECK(commitFlushCompletions == 3U && commitFlushes == 3U);
            UmicomSize dataWrites = 0U, lastFlush = 0U;
            for (UmicomSize i = 0U; i < commitEventCount; ++i)
                if (commitEvents[i].command == UMICOM_VIRTIO_REQUEST_FLUSH && commitEvents[i].completed) lastFlush = i;
            for (UmicomSize i = 0U; i < commitEventCount; ++i) {
                const CommitEvent *const event = &commitEvents[i];
                if (event->command != UMICOM_VIRTIO_REQUEST_WRITE || CommitHeaderSector(event->sector)) continue;
                CHECK(event->completed && event->sector != fileDirectorySector); ++dataWrites;
                CommitEqual(event->data, commitDurable + (UmicomSize)event->sector * 512U, 512U);
                UmicomBoolean readback = UMICOM_FALSE;
                for (UmicomSize j = lastFlush + 1U; j < commitEventCount; ++j)
                    if (commitEvents[j].command == UMICOM_VIRTIO_REQUEST_READ && commitEvents[j].completed &&
                        commitEvents[j].sector == event->sector) readback = UMICOM_TRUE;
                CHECK(readback);
            }
            CHECK(dataWrites > 0U);
        }
        if (fileCallbackReenter) FileReenter();
        if (fileConsoleCallbackReenter) FileConsoleReenter();
    }
    CommitWriteRegister(context, address, value);
}
static void FileComplete(void)
{
    CommitComplete();
    const CommitEvent *const event = &commitEvents[commitEventCount - 1U];
    if (fileReadCorruptionByte && event->command == UMICOM_VIRTIO_REQUEST_READ &&
        commitCorruptRead == commitReads && event->completed) {
        CHECK(fileReadCorruptionByte < 512U);
        UmicomU8 *const data = (UmicomU8 *)domain.slots[0].dataFrame;
        data[0] ^= 0x53U; data[fileReadCorruptionByte] ^= 0x53U;
    }
}
static void FileStart(const char *fixture)
{
    CommitStart(fixture); memset(&fileOwner, 0, sizeof(fileOwner));
    memset(&fileResult, 0xa5, sizeof(fileResult)); memcpy(&fileResultBefore, &fileResult, sizeof(fileResult));
    fileTime = (UmicomKernelFat16FileTime){2037U, 11U, 23U, 14U, 35U, 59U}; fileTimeBefore = fileTime;
    fileDirectorySector = UMICOM_DISK_FIXTURE_ROOT; fileEntryOffset = 3U * 32U;
    fileCallbackReenter = UMICOM_FALSE; filePolicyReenter = UMICOM_FALSE; fileReentries = 0U;
    fileReadCorruptionByte = 0U; model.completeRequest = FileComplete;
    fileConsoleCallbackReenter = UMICOM_FALSE; fileConsoleOutputReenter = UMICOM_FALSE;
    domain.operations.write32 = FileWriteRegister; domain.operations.clock = FileClock;
    domain.operations.allowed = FileAllowed;
    CommitRoot(3U)[11] &= (UmicomU8)~0x20U; CommitRebase();
}
static void FileOpen(void)
{
    CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_OK);
    CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY && fileOwner.commit.updater.admitted);
    CHECK(fileOwner.self == &fileOwner && !fileOwner.busy && !fileOwner.commit.busy &&
        !fileOwner.commit.updater.busy && !fileOwner.commit.updater.volume.open);
    CHECK(Allocated() == 2U && !commitWrites && !commitFlushes);
}
static void FileClose(void)
{
    const UmicomKernelFat16FileCommitResult saved = fileOwner.lastResult;
    const UmicomU32 mutations = commitMutations;
    CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_OK);
    CHECK(!fileOwner.commit.updater.handle && !fileOwner.busy && !fileOwner.commit.updater.volume.open);
    CHECK(!Allocated() && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    CommitEqual(&saved, &fileOwner.lastResult, sizeof(saved)); CHECK(commitMutations == mutations);
    FileZero(&fileOwner.commit.updater.plan, sizeof(fileOwner.commit.updater.plan));
    FileZero(&fileOwner.filePlan, sizeof(fileOwner.filePlan));
    FileZero(fileOwner.fileWorkspace.path, sizeof(fileOwner.fileWorkspace.path));
    FileZero(&fileOwner.fileWorkspace.stage, sizeof(fileOwner.fileWorkspace.stage));
    FileZero(fileOwner.commit.cleanHeader, sizeof(fileOwner.commit.cleanHeader));
    FileZero(fileOwner.commit.dirtyHeader, sizeof(fileOwner.commit.dirtyHeader));
    FileZero(fileOwner.commit.readback, sizeof(fileOwner.commit.readback));
}
static UmicomKernelFat16UpdateStatus FileStage(const char *path, UmicomU64 offset, UmicomSize bytes)
{
    commitOperation = 1U; memset(&fileResult, 0xa5, sizeof(fileResult));
    memcpy(&fileResultBefore, &fileResult, sizeof(fileResult));
    return UmicomKernelFat16FileCommitStage(&fileOwner, path, offset, commitInput, bytes, &fileTime, &fileResult);
}
static UmicomKernelFat16UpdateStatus FileFinish(void)
{
    commitOperation = 2U; memset(&fileResult, 0xa5, sizeof(fileResult));
    memcpy(&fileResultBefore, &fileResult, sizeof(fileResult));
    return UmicomKernelFat16FileCommitFinish(&fileOwner, &fileResult);
}
static void FilePatchMetadata(void)
{
    UmicomU8 *const entry = commitExpected + (UmicomSize)fileDirectorySector * 512U + fileEntryOffset;
    entry[11] |= 0x20U;
    /* Use the on-disk formula directly, not the production encoding helper. */
    const UmicomU16 time = (UmicomU16)((UmicomU32)fileTime.hour * 2048U + (UmicomU32)fileTime.minute * 32U +
        fileTime.second / 2U);
    const UmicomU16 date = (UmicomU16)(((UmicomU32)fileTime.year - 1980U) * 512U +
        (UmicomU32)fileTime.month * 32U + fileTime.day);
    CommitPut16(entry + 22U, time); CommitPut16(entry + 24U, date);
}
static void FileAssertStage(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize sectors = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_STAGED && !fileOwner.busy && !fileOwner.commit.busy);
    CHECK(fileResult.commit.status == UMICOM_FAT16_UPDATE_OK && fileResult.commit.diskStatus == UMICOM_DISK_OK);
    CHECK(fileResult.commit.blockStatus == UMICOM_BLOCK_OK && fileResult.commit.dataOutcome == UMICOM_FAT16_UPDATE_COMPLETED);
    CHECK(fileResult.commit.offset == offset && fileResult.commit.requestedBytes == bytes);
    CHECK(fileResult.commit.confirmedBytes == bytes && fileResult.commit.submittedBytes == bytes);
    CHECK(fileResult.commit.completedDataSectors == sectors && fileResult.commit.submittedDataSectors == sectors);
    CHECK(fileResult.commit.completedMetadataSectors == 3U && fileResult.commit.submittedMetadataSectors == 3U);
    CHECK(fileResult.commit.completedFlushes == 4U && commitWrites == sectors + 3U && commitFlushes == 4U);
    CHECK(fileResult.commit.mediaTouched && fileResult.commit.dirtyDurable && fileResult.commit.dirtyVerified);
    CHECK(fileResult.commit.dataDurable && fileResult.commit.dataVerified && !fileResult.commit.cleanFinalisationStarted);
    CHECK(!fileResult.commit.cleanDurable && !fileResult.commit.cleanVerified && !fileResult.commit.commitAccepted);
    CHECK(!fileResult.commit.needsFlush && !fileResult.commit.writeUncertain && !fileResult.commit.uncertainSectorValid);
    CHECK(fileResult.directoryPlanned && fileResult.directorySubmitted && fileResult.directoryCompleted);
    CHECK(fileResult.directoryDurable && fileResult.directoryVerified);
    CHECK(fileResult.directorySector == fileDirectorySector && fileResult.entryOffset == fileEntryOffset);
    CommitEqual(&fileResult.requestedTime, &fileTime, sizeof(fileTime));
    CHECK(fileResult.encodedTime.storedSecond == (fileTime.second / 2U) * 2U);
    CHECK(fileResult.originalAttributes == commitInitial[(UmicomSize)fileDirectorySector * 512U + fileEntryOffset + 11U]);
    CHECK(fileResult.updatedAttributes == (UmicomU8)(fileResult.originalAttributes | 0x20U));
    CommitEqual(&fileOwner.lastResult, &fileResult, sizeof(fileResult));
    CommitPatchExpected(offset, bytes); FilePatchMetadata(); CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    CommitEqual(commitInput, commitInputSnapshot, sizeof(commitInput)); CommitEqual(&fileTime, &fileTimeBefore, sizeof(fileTime));
}
static void FileAssertFinished(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize sectors = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_COMMITTED);
    CHECK(fileResult.commit.status == UMICOM_FAT16_UPDATE_OK && fileResult.commit.commitAccepted);
    CHECK(fileResult.commit.cleanFinalisationStarted && fileResult.commit.cleanDurable && fileResult.commit.cleanVerified);
    CHECK(fileResult.commit.dirtyDurable && fileResult.commit.dirtyVerified && fileResult.commit.dataDurable && fileResult.commit.dataVerified);
    CHECK(fileResult.directoryDurable && fileResult.directoryVerified);
    CHECK(fileResult.commit.completedMetadataSectors == 5U && fileResult.commit.submittedMetadataSectors == 5U);
    CHECK(fileResult.commit.completedDataSectors == sectors && fileResult.commit.submittedDataSectors == sectors);
    CHECK(fileResult.commit.completedFlushes == 6U && commitWrites == sectors + 5U && commitFlushes == 6U);
    CHECK(!fileResult.commit.needsFlush && !fileResult.commit.writeUncertain && !fileResult.commit.uncertainSectorValid);
    CommitExpectedDirty(UMICOM_FALSE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    CommitEqual(&fileOwner.lastResult, &fileResult, sizeof(fileResult));
    CommitEqual(commitInput, commitInputSnapshot, sizeof(commitInput)); CommitEqual(&fileTime, &fileTimeBefore, sizeof(fileTime));
}
static void FileTrace(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize sectors = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    UmicomSize mutation = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i) {
        const CommitEvent *const event = &commitEvents[i];
        if (event->command == UMICOM_VIRTIO_REQUEST_READ) continue;
        const UmicomSize index = mutation++;
        if (index == 1U || index == 3U || index == sectors + 4U || index == sectors + 6U ||
            index == sectors + 8U || index == sectors + 10U) {
            CHECK(event->command == UMICOM_VIRTIO_REQUEST_FLUSH && event->completed); continue;
        }
        CHECK(event->command == UMICOM_VIRTIO_REQUEST_WRITE && event->completed);
        if (index == 0U || index == 2U || index == sectors + 7U || index == sectors + 9U) {
            CHECK(event->sector == (index == 0U || index == sectors + 7U ? COMMIT_MIRROR : COMMIT_PRIMARY));
            UmicomU8 expected[512]; memcpy(expected, commitInitial + (UmicomSize)event->sector * 512U, sizeof(expected));
            if (index < 4U) expected[3] &= 0x7fU;
            CommitEqual(event->data, expected, sizeof(expected));
        } else if (index == sectors + 5U) {
            CHECK(event->sector == fileDirectorySector);
            CommitEqual(event->data, commitExpected + (UmicomSize)fileDirectorySector * 512U, 512U);
        } else {
            CHECK(index >= 4U && index < sectors + 4U);
            const UmicomU64 sector = UMICOM_DISK_FIXTURE_DATA + CommitFileCluster((UmicomSize)(offset / 512U) + index - 4U) - 2U;
            CHECK(event->sector == sector); CommitEqual(event->data, commitExpected + (UmicomSize)sector * 512U, 512U);
        }
    }
    CHECK(mutation == sectors + 11U);
}

static void FileRelocateRoot(UmicomSize destination)
{
    CHECK(destination >= 5U && destination < 512U);
    UmicomU8 entry[32]; memcpy(entry, CommitRoot(3U), sizeof(entry)); CommitRoot(3U)[0] = 0xe5U;
    for (UmicomSize i = 5U; i < destination; ++i) {
        memset(CommitRoot(i), 0x59, 32U); CommitRoot(i)[0] = 0xe5U;
    }
    memcpy(CommitRoot(destination), entry, sizeof(entry));
    fileDirectorySector = UMICOM_DISK_FIXTURE_ROOT + destination / 16U;
    fileEntryOffset = (destination % 16U) * 32U; CommitRebase();
}
static void FileRelocateNested(UmicomBoolean fragmented, UmicomBoolean last)
{
    UmicomU8 entry[32]; memcpy(entry, CommitRoot(3U), sizeof(entry)); CommitRoot(3U)[0] = 0xe5U;
    UmicomU16 cluster = 3U; UmicomSize slot = 3U;
    if (fragmented) {
        CommitFat(3U, 34U); CommitFat(34U, 0xffffU);
        for (UmicomSize i = 3U; i < 16U; ++i) {
            memset(CommitCluster(3U) + i * 32U, 0x67, 32U); CommitCluster(3U)[i * 32U] = 0xe5U;
        }
        cluster = 34U; slot = last ? 15U : 7U; memset(CommitCluster(cluster), 0, 512U);
        for (UmicomSize i = 0U; i < slot; ++i) {
            memset(CommitCluster(cluster) + i * 32U, 0x79, 32U); CommitCluster(cluster)[i * 32U] = 0xe5U;
        }
    }
    memcpy(CommitCluster(cluster) + slot * 32U, entry, sizeof(entry));
    fileDirectorySector = UMICOM_DISK_FIXTURE_DATA + cluster - 2U; fileEntryOffset = slot * 32U; CommitRebase();
}
static void FileSuccess(const char *name)
{
    UmicomU64 offset = 511U; UmicomSize bytes = 700U; const char *path = "/FRAG.BIN";
    if (!strcmp(name, "archive_set")) { CommitRoot(3U)[11] |= 0x20U; CommitRebase(); }
    else if (!strcmp(name, "hidden_system")) { CommitRoot(3U)[11] = 0x06U; CommitRebase(); }
    else if (!strcmp(name, "single_byte")) { offset = 0U; bytes = 1U; }
    else if (!strcmp(name, "whole_file")) { offset = 0U; bytes = 1300U; }
    else if (!strcmp(name, "last_byte")) { offset = 1299U; bytes = 1U; }
    else if (!strcmp(name, "aligned_sector")) { offset = 512U; bytes = 512U; }
    else if (!strcmp(name, "nine_sectors")) { CommitLargeFile(); offset = 1U; bytes = 4096U; }
    else if (!strcmp(name, "write_through")) commitPersistWrites = UMICOM_TRUE;
    else if (!strcmp(name, "root_later_sector")) FileRelocateRoot(19U);
    else if (!strcmp(name, "root_last_entry")) FileRelocateRoot(511U);
    else if (!strncmp(name, "nested_", 7U)) {
        FileRelocateNested(strcmp(name, "nested_first") ? UMICOM_TRUE : UMICOM_FALSE,
            !strcmp(name, "nested_last_entry") ? UMICOM_TRUE : UMICOM_FALSE);
        path = "/DOCS/FRAG.BIN";
    } else if (!strcmp(name, "preserved_fields")) {
        UmicomU8 *const entry = CommitRoot(3U); entry[12] = 0x18U; entry[13] = 199U;
        CommitPut16(entry + 14U, 0x9137U); CommitPut16(entry + 16U, 0x5133U);
        CommitPut16(entry + 18U, 0x53baU); CommitPut16(entry + 22U, 0x1100U); CommitPut16(entry + 24U, 0x2021U);
        CommitRebase();
    } else if (!strcmp(name, "matching_metadata")) {
        FilePatchMetadata(); memcpy(commitVisible, commitExpected, sizeof(commitVisible)); CommitRebase();
    } else if (!strcmp(name, "casefold")) path = "/frag.bin";
    else CHECK(!strcmp(name, "archive_clear") || !strcmp(name, "fragmented") || !strcmp(name, "idle_intervals"));
    FileOpen();
    if (!strcmp(name, "idle_intervals")) model.now += (UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS * 3U;
    CHECK(FileStage(path, offset, bytes) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(offset, bytes);
    if (!strcmp(name, "idle_intervals")) model.now += (UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS * 3U;
    CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); FileAssertFinished(offset, bytes); FileTrace(offset, bytes); FileClose();
}
static void FileCalendar(const char *name, UmicomBoolean invalid)
{
    if (!strcmp(name, "minimum")) fileTime = (UmicomKernelFat16FileTime){1980U, 1U, 1U, 0U, 0U, 0U};
    else if (!strcmp(name, "maximum")) fileTime = (UmicomKernelFat16FileTime){2107U, 12U, 31U, 23U, 59U, 59U};
    else if (!strncmp(name, "leap_", 5U) && strcmp(name, "leap_february_30"))
        fileTime = (UmicomKernelFat16FileTime){(UmicomU16)strtoul(name + 5U, 0, 10), 2U, 29U, 0U, 0U, 0U};
    else if (!strcmp(name, "common_february")) fileTime = (UmicomKernelFat16FileTime){2100U, 2U, 28U, 0U, 0U, 0U};
    else if (!strcmp(name, "even_second")) fileTime.second = 58U;
    else if (!strcmp(name, "odd_second")) fileTime.second = 1U;
    else if (!strcmp(name, "midnight")) { fileTime.hour = 0U; fileTime.minute = 0U; fileTime.second = 0U; }
    else if (!strcmp(name, "end_of_day")) { fileTime.hour = 23U; fileTime.minute = 59U; fileTime.second = 59U; }
    else if (!strcmp(name, "year_low")) fileTime.year = 1979U;
    else if (!strcmp(name, "year_high")) fileTime.year = 2108U;
    else if (!strcmp(name, "month_zero")) fileTime.month = 0U;
    else if (!strcmp(name, "month_high")) fileTime.month = 13U;
    else if (!strcmp(name, "day_zero")) fileTime.day = 0U;
    else if (!strcmp(name, "day_high")) fileTime.day = 32U;
    else if (!strcmp(name, "april_31")) { fileTime.month = 4U; fileTime.day = 31U; }
    else if (!strcmp(name, "june_31")) { fileTime.month = 6U; fileTime.day = 31U; }
    else if (!strcmp(name, "september_31")) { fileTime.month = 9U; fileTime.day = 31U; }
    else if (!strcmp(name, "november_31")) { fileTime.month = 11U; fileTime.day = 31U; }
    else if (!strcmp(name, "common_february_29")) { fileTime.year = 2037U; fileTime.month = 2U; fileTime.day = 29U; }
    else if (!strcmp(name, "century_2100_february_29")) { fileTime.year = 2100U; fileTime.month = 2U; fileTime.day = 29U; }
    else if (!strcmp(name, "leap_february_30")) { fileTime.year = 2000U; fileTime.month = 2U; fileTime.day = 30U; }
    else if (!strcmp(name, "hour_high")) fileTime.hour = 24U;
    else if (!strcmp(name, "minute_high")) fileTime.minute = 60U;
    else if (!strcmp(name, "second_high")) fileTime.second = 60U;
    else CHECK(0);
    fileTimeBefore = fileTime;
    UmicomKernelFat16FileTimeEncoding encoded, before;
    memset(&encoded, 0xa5, sizeof(encoded)); memcpy(&before, &encoded, sizeof(before));
    const UmicomKernelDiskStatus encodedStatus = UmicomKernelFat16FileTimeEncode(&fileTime, &encoded);
    if (invalid) { CHECK(encodedStatus == UMICOM_DISK_INVALID_ARGUMENT); CommitEqual(&encoded, &before, sizeof(encoded)); }
    else {
        CHECK(encodedStatus == UMICOM_DISK_OK);
        CHECK(encoded.writeTime == (UmicomU16)((UmicomU32)fileTime.hour * 2048U +
            (UmicomU32)fileTime.minute * 32U + fileTime.second / 2U));
        CHECK(encoded.writeDate == (UmicomU16)(((UmicomU32)fileTime.year - 1980U) * 512U +
            (UmicomU32)fileTime.month * 32U + fileTime.day));
        CHECK(encoded.storedSecond == (fileTime.second / 2U) * 2U);
        if (!strcmp(name, "minimum")) CHECK(encoded.writeTime == 0U && encoded.writeDate == 0x0021U);
        if (!strcmp(name, "maximum")) CHECK(encoded.writeTime == 0xbf7dU && encoded.writeDate == 0xff9fU);
        if (!strcmp(name, "leap_2000")) CHECK(encoded.writeDate == 0x285dU);
    }
    CHECK(!commitEventCount && !Allocated()); FileOpen(); const UmicomU32 reads = commitReads;
    if (invalid) {
        const UmicomKernelFat16FileCommitResult history = fileOwner.lastResult;
        CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(commitReads == reads); CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
        CommitEqual(&history, &fileOwner.lastResult, sizeof(history)); CommitUnchanged();
    } else {
        CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); FileAssertFinished(511U, 700U); FileTrace(511U, 700U);
    }
    FileClose(); CommitEqual(&fileTime, &fileTimeBefore, sizeof(fileTime));
}

static void FileFailed(void)
{
    CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_FAILED && fileResult.commit.status != UMICOM_FAT16_UPDATE_OK);
    CHECK(fileResult.commit.mediaTouched && !fileResult.commit.commitAccepted);
    CHECK(!fileOwner.busy && !fileOwner.commit.busy && !fileOwner.commit.updater.busy && !fileOwner.commit.updater.volume.open);
    CommitEqual(&fileOwner.lastResult, &fileResult, sizeof(fileResult));
    const UmicomKernelFat16FileCommitResult history = fileOwner.lastResult; const UmicomU32 events = commitEventCount;
    CHECK(FileStage("/FRAG.BIN", 0U, 1U) != UMICOM_FAT16_UPDATE_OK);
    CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
    CHECK(FileFinish() != UMICOM_FAT16_UPDATE_OK); CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
    CommitEqual(&history, &fileOwner.lastResult, sizeof(history)); CHECK(commitEventCount == events);
    memcpy(&fileResult, &history, sizeof(fileResult));
}
static void FileFormat(const char *name)
{
    const UmicomKernelDiskStatus expected = CommitCorrupt(name);
    UmicomKernelFat16UpdateStatus status = UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        status = FileStage("/FRAG.BIN", 0U, 1U); CHECK(status != UMICOM_FAT16_UPDATE_OK);
        CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY && fileResult.commit.status == status);
        CHECK(!fileResult.commit.mediaTouched && !fileResult.commit.submittedMetadataSectors && !fileResult.commit.submittedDataSectors);
        CHECK(!fileResult.commit.confirmedBytes && !fileResult.commit.submittedBytes && !fileResult.commit.completedFlushes);
        CHECK(!fileResult.commit.commitAccepted && !fileResult.commit.writeUncertain && !fileResult.commit.needsFlush);
        CHECK(!fileResult.directoryDurable && !fileResult.directoryVerified);
        if (!strcmp(name, "io_budget")) {
            CHECK(fileResult.commit.diskStatus == UMICOM_DISK_IO_ERROR && fileResult.commit.blockStatus == UMICOM_BLOCK_TIMEOUT);
            CHECK(fileOwner.commit.updater.operationReads == UMICOM_FAT16_IO_LIMIT);
        } else CHECK(fileResult.commit.diskStatus == expected);
    } else CHECK(fileOwner.commit.updater.lastDiskStatus == expected);
    CommitUnchanged(); FileClose();
}
static UmicomU32 FileWriteOrdinal(const char *name)
{
    if (!strcmp(name, "directory")) return 6U;
    const UmicomU32 old = CommitWriteOrdinal(name); return old >= 6U ? old + 1U : old;
}
static void FileWriteFailure(const char *name)
{
    const UmicomU32 failed = FileWriteOrdinal(name); FileOpen(); commitFailWrite = failed; commitPartialWrite = UMICOM_TRUE;
    if (failed > 6U) { CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U); }
    CHECK((failed > 6U ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitWrites == failed && fileResult.commit.blockStatus == UMICOM_BLOCK_IO_ERROR);
    CHECK(fileResult.commit.lastBlockOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    CHECK(fileResult.commit.writeUncertain && fileResult.commit.needsFlush && fileResult.commit.uncertainSectorValid);
    const UmicomSize dataBefore = failed <= 3U ? 0U : (failed <= 5U ? failed - 3U : 3U);
    const UmicomSize dataSubmitted = failed <= 2U ? 0U : (failed <= 5U ? failed - 2U : 3U);
    static const UmicomSize bytes[] = {0U, 1U, 513U, 700U};
    CHECK(fileResult.commit.completedDataSectors == dataBefore && fileResult.commit.submittedDataSectors == dataSubmitted);
    CHECK(fileResult.commit.confirmedBytes == bytes[dataBefore] && fileResult.commit.submittedBytes == bytes[dataSubmitted]);
    const UmicomSize metadataBefore = failed <= 2U ? failed - 1U : (failed >= 6U ? failed - 4U : 2U);
    CHECK(fileResult.commit.completedMetadataSectors == metadataBefore);
    CHECK(fileResult.commit.submittedMetadataSectors == metadataBefore + (failed <= 2U || failed >= 6U ? 1U : 0U));
    UmicomU32 written = 0U; UmicomU64 sector = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i)
        if (commitEvents[i].command == UMICOM_VIRTIO_REQUEST_WRITE && ++written == failed) sector = commitEvents[i].sector;
    CHECK(fileResult.commit.uncertainSector == sector);
    CHECK(fileResult.commit.cleanFinalisationStarted == (failed >= 7U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.directoryPlanned && fileResult.directorySubmitted == (failed >= 6U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.directoryCompleted == (failed >= 7U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.directoryDurable == (failed >= 7U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.directoryVerified == (failed >= 7U ? UMICOM_TRUE : UMICOM_FALSE));
    FileFailed(); FileClose();
}
static void FileFlushFailure(const char *name)
{
    UmicomU32 failed = 0U;
    if (!strcmp(name, "dirty_mirror")) failed = 1U;
    else if (!strcmp(name, "dirty_primary")) failed = 2U;
    else if (!strcmp(name, "data")) failed = 3U;
    else if (!strcmp(name, "directory")) failed = 4U;
    else if (!strcmp(name, "clean_mirror")) failed = 5U;
    else if (!strcmp(name, "clean_primary")) failed = 6U;
    else CHECK(0);
    FileOpen(); commitFailFlush = failed;
    if (failed > 4U) { CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U); }
    CHECK((failed > 4U ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitFlushes == failed && fileResult.commit.completedFlushes == failed - 1U);
    CHECK(fileResult.commit.blockStatus == UMICOM_BLOCK_IO_ERROR && fileResult.commit.lastBlockOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    CHECK(fileResult.commit.needsFlush && !fileResult.commit.uncertainSectorValid);
    CHECK(fileResult.commit.cleanFinalisationStarted == (failed > 4U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.commit.submittedDataSectors == (failed < 3U ? 0U : 3U));
    CHECK(fileResult.directoryDurable == (failed > 4U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.directoryVerified == (failed > 4U ? UMICOM_TRUE : UMICOM_FALSE));
    FileFailed(); FileClose();
}
static void FileReadFailure(const char *name, UmicomBoolean corrupt)
{
    commitReadFaultCorrupt = corrupt; commitReadFaultOperation = 1U;
    if (!strcmp(name, "dirty_primary")) { commitReadFaultMutation = 4U; commitReadFaultOrdinal = 1U; }
    else if (!strcmp(name, "dirty_mirror")) { commitReadFaultMutation = 4U; commitReadFaultOrdinal = 2U; }
    else if (!strncmp(name, "data_", 5U)) {
        commitReadFaultMutation = 8U;
        commitReadFaultOrdinal = !strcmp(name, "data_first") ? 1U : (!strcmp(name, "data_middle") ? 2U : 3U);
    } else if (!strcmp(name, "directory")) { commitReadFaultMutation = 10U; commitReadFaultOrdinal = 1U; }
    else if (!strncmp(name, "finish_", 7U)) {
        commitReadFaultOperation = 2U; commitReadFaultMutation = 10U;
        commitReadFaultOrdinal = !strcmp(name, "finish_primary") ? 1U : (!strcmp(name, "finish_mirror") ? 2U :
            (!strcmp(name, "finish_first") ? 3U : (!strcmp(name, "finish_middle") ? 4U :
            (!strcmp(name, "finish_last") ? 5U : 6U))));
    } else if (!strcmp(name, "clean_primary") || !strcmp(name, "clean_mirror")) {
        commitReadFaultOperation = 2U; commitReadFaultMutation = 14U;
        commitReadFaultOrdinal = !strcmp(name, "clean_primary") ? 1U : 2U;
    } else CHECK(0);
    FileOpen();
    if (commitReadFaultOperation == 2U) {
        CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
    }
    CHECK((commitReadFaultOperation == 2U ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitReadFaultSeen == commitReadFaultOrdinal);
    CHECK(fileResult.commit.diskStatus == (corrupt ? UMICOM_DISK_CORRUPT : UMICOM_DISK_IO_ERROR));
    CHECK(fileResult.commit.blockStatus == (corrupt ? UMICOM_BLOCK_OK : UMICOM_BLOCK_IO_ERROR));
    CHECK(!fileResult.commit.writeUncertain && !fileResult.commit.uncertainSectorValid);
    CHECK(fileResult.commit.cleanFinalisationStarted == (commitReadFaultMutation == 14U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.commit.cleanDurable == (commitReadFaultMutation == 14U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(!fileResult.commit.cleanVerified);
    CHECK(fileResult.directoryDurable == (commitReadFaultMutation >= 10U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.directoryVerified == (commitReadFaultOperation == 2U ? UMICOM_TRUE : UMICOM_FALSE));
    FileFailed(); FileClose();
}
static void FileDroppedWrite(const char *name, UmicomBoolean altered)
{
    const UmicomU32 selected = FileWriteOrdinal(name); FileOpen();
    if (altered) commitAlterWrite = selected; else commitDropWrite = selected;
    if (selected >= 7U) { CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U); }
    CHECK((selected >= 7U ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(fileResult.commit.diskStatus == UMICOM_DISK_CORRUPT && fileResult.commit.blockStatus == UMICOM_BLOCK_OK);
    CHECK(!fileResult.commit.writeUncertain && !fileResult.commit.uncertainSectorValid);
    if (selected <= 2U) CHECK(!commitDataWrites && !fileResult.commit.dirtyVerified);
    else if (selected <= 5U) CHECK(!fileResult.commit.dataVerified && !fileResult.commit.cleanFinalisationStarted);
    else if (selected == 6U) CHECK(fileResult.directoryDurable && !fileResult.directoryVerified && !fileResult.commit.cleanFinalisationStarted);
    else CHECK(fileResult.commit.cleanDurable && !fileResult.commit.cleanVerified);
    FileFailed(); FileClose();
}
static void FileCut(const char *name)
{
    const UmicomBoolean eager = !strncmp(name, "eager.", 6U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(eager || !strncmp(name, "cached.", 7U));
    const unsigned long parsed = strtoul(name + (eager ? 6U : 7U), 0, 10); CHECK(parsed >= 1U && parsed <= 14U);
    commitPersistWrites = eager; commitCutMutation = (UmicomU32)parsed; FileOpen();
    if (commitCutMutation > 10U) { CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U); }
    CHECK((commitCutMutation > 10U ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitMutations == commitCutMutation && !model.allowed); FileFailed();
    CommitReplay(UMICOM_FALSE); CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitReplay(UMICOM_TRUE); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    model.allowed = UMICOM_TRUE; FileClose(); memcpy(commitVisible, commitDurable, sizeof(commitVisible));
    const UmicomBoolean clean = (CommitFlags(commitVisible, COMMIT_PRIMARY) & UMICOM_FAT16_CLEAN_MASK) &&
        (CommitFlags(commitVisible, COMMIT_MIRROR) & UMICOM_FAT16_CLEAN_MASK) ? UMICOM_TRUE : UMICOM_FALSE;
    if (clean) {
        memcpy(commitExpected, commitInitial, sizeof(commitExpected));
        if (commitCutMutation > 4U) { CommitPatchExpected(511U, 700U); FilePatchMetadata(); }
        CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    } else CHECK(commitCutMutation >= (eager ? 1U : 2U));
    CommitFreshReader(clean);
}

static void FileOwnership(const char *name, UmicomBoolean finish)
{
    FileOpen();
    if (finish) { CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U); }
    const UmicomKernelFat16FileCommitResult history = fileOwner.lastResult;
    memset(&fileResult, 0xa5, sizeof(fileResult)); memcpy(&fileResultBefore, &fileResult, sizeof(fileResult));
    const UmicomU32 events = commitEventCount;
    UmicomKernelFat16FileCommitter *owner = &fileOwner;
    UmicomKernelFat16FileCommitResult *output = &fileResult;
    const char *path = "/FRAG.BIN"; const void *input = commitInput; UmicomSize bytes = 1U;
    const UmicomKernelFat16FileTime *time = &fileTime;
    UmicomAddress extra = 0U;
    static UmicomKernelFat16FileCommitter copied;
    _Alignas(UmicomKernelFat16FileCommitResult) UmicomU8 unaligned[sizeof(UmicomKernelFat16FileCommitResult) + 8U];
    memset(unaligned, 0x49, sizeof(unaligned));
    char longPath[UMICOM_FAT16_PATH_BYTES]; memset(longPath, 'A', sizeof(longPath));
    if (!strcmp(name, "result_owner")) output = &fileOwner.lastResult;
    else if (!strcmp(name, "result_outer_plan")) output = (UmicomKernelFat16FileCommitResult *)(void *)&fileOwner.filePlan;
    else if (!strcmp(name, "result_domain")) output = (UmicomKernelFat16FileCommitResult *)(void *)&domain;
    else if (!strcmp(name, "result_dma")) output = (UmicomKernelFat16FileCommitResult *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "result_queue")) output = (UmicomKernelFat16FileCommitResult *)domain.slots[0].queueFrame;
    else if (!strcmp(name, "result_alignment")) output = (UmicomKernelFat16FileCommitResult *)(void *)(unaligned + 1U);
    else if (!strcmp(name, "result_overflow"))
        output = (UmicomKernelFat16FileCommitResult *)(~(UmicomAddress)0U & ~((UmicomAddress)alignof(UmicomKernelFat16FileCommitResult) - 1U));
    else if (!strcmp(name, "result_null")) output = 0;
    else if (!strcmp(name, "input_owner")) input = &fileOwner.commit.updater;
    else if (!strcmp(name, "input_outer_plan")) input = &fileOwner.filePlan;
    else if (!strcmp(name, "input_domain")) input = &domain;
    else if (!strcmp(name, "input_dma")) input = (const void *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "input_queue")) input = (const void *)domain.slots[0].queueFrame;
    else if (!strcmp(name, "input_result")) input = &fileResult;
    else if (!strcmp(name, "input_time")) input = &fileTime;
    else if (!strcmp(name, "input_overflow")) { input = (const void *)(~(UmicomAddress)0U - 15U); bytes = 32U; }
    else if (!strcmp(name, "input_null")) input = 0;
    else if (!strcmp(name, "input_zero")) bytes = 0U;
    else if (!strcmp(name, "input_oversized")) bytes = sizeof(commitInput);
    else if (!strcmp(name, "path_input")) {
        memcpy(commitInput, "/FRAG.BIN", 10U); memcpy(commitInputSnapshot, commitInput, sizeof(commitInput));
        path = (const char *)commitInput; input = commitInput + 2U;
    } else if (!strcmp(name, "path_input_nonterminated")) {
        memset(commitInput, 0xa5, sizeof(commitInput)); memcpy(commitInputSnapshot, commitInput, sizeof(commitInput));
        path = (const char *)commitInput;
    } else if (!strcmp(name, "path_result_nonterminated")) path = (const char *)&fileResult;
    else if (!strcmp(name, "path_outer_plan")) path = (const char *)&fileOwner.filePlan;
    else if (!strcmp(name, "path_time")) path = (const char *)&fileTime;
    else if (!strcmp(name, "path_domain")) path = (const char *)&domain;
    else if (!strcmp(name, "path_dma")) path = (const char *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "path_unterminated")) path = longPath;
    else if (!strcmp(name, "path_null")) path = 0;
    else if (!strcmp(name, "time_owner")) time = (const UmicomKernelFat16FileTime *)(const void *)&fileOwner;
    else if (!strcmp(name, "time_outer_plan")) time = &fileOwner.filePlan.requestedTime;
    else if (!strcmp(name, "time_domain")) time = (const UmicomKernelFat16FileTime *)(const void *)&domain;
    else if (!strcmp(name, "time_dma")) time = (const UmicomKernelFat16FileTime *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "time_queue")) time = (const UmicomKernelFat16FileTime *)domain.slots[0].queueFrame;
    else if (!strcmp(name, "time_result")) time = &fileResult.requestedTime;
    else if (!strcmp(name, "time_alignment")) time = (const UmicomKernelFat16FileTime *)(const void *)(unaligned + 1U);
    else if (!strcmp(name, "time_overflow"))
        time = (const UmicomKernelFat16FileTime *)(~(UmicomAddress)0U & ~((UmicomAddress)alignof(UmicomKernelFat16FileTime) - 1U));
    else if (!strcmp(name, "time_null")) time = 0;
    else if (!strcmp(name, "other_slot_dma")) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&extra) == UMICOM_KERNEL_MEMORY_OK);
        domain.count = 2U; domain.slots[1].dataFrame = extra;
        if (finish) output = (UmicomKernelFat16FileCommitResult *)extra; else input = (const void *)extra;
    } else if (!strcmp(name, "owner_alignment")) owner = (UmicomKernelFat16FileCommitter *)(void *)((UmicomU8 *)&fileOwner + 1U);
    else if (!strcmp(name, "owner_null")) owner = 0;
    else if (!strcmp(name, "copied_owner")) { memcpy(&copied, &fileOwner, sizeof(copied)); owner = &copied; }
    else if (!strcmp(name, "busy_owner")) fileOwner.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_embedded")) fileOwner.commit.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_updater")) fileOwner.commit.updater.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_file_workspace")) fileOwner.fileWorkspace.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_base_workspace")) fileOwner.commit.updater.workspace.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_volume")) fileOwner.commit.updater.volume.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_domain")) domain.busy = UMICOM_TRUE;
    else if (!strcmp(name, "unsafe_context")) model.allowed = UMICOM_FALSE;
    else CHECK(0);
    const UmicomKernelFat16UpdateStatus status = finish ? UmicomKernelFat16FileCommitFinish(owner, output) :
        UmicomKernelFat16FileCommitStage(owner, path, 0U, input, bytes, time, output);
    CHECK(status != UMICOM_FAT16_UPDATE_OK);
    if (!strncmp(name, "busy_", 5U)) CHECK(status == UMICOM_FAT16_UPDATE_BUSY);
    else if (!strcmp(name, "unsafe_context")) CHECK(status == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
    else if (!strcmp(name, "copied_owner")) {
        CHECK(status == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(UmicomKernelFat16FileCommitClose(&copied) == UMICOM_FAT16_UPDATE_BAD_STATE);
    } else CHECK(status == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    fileOwner.busy = UMICOM_FALSE; fileOwner.commit.busy = UMICOM_FALSE; fileOwner.commit.updater.busy = UMICOM_FALSE;
    fileOwner.fileWorkspace.busy = UMICOM_FALSE; fileOwner.commit.updater.workspace.busy = UMICOM_FALSE;
    fileOwner.commit.updater.volume.busy = UMICOM_FALSE;
    domain.busy = UMICOM_FALSE; model.allowed = UMICOM_TRUE;
    if (extra) {
        domain.count = 1U; domain.slots[1].dataFrame = 0U;
        CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(extra) == UMICOM_KERNEL_MEMORY_OK);
    }
    CHECK(commitEventCount == events); CommitEqual(&history, &fileOwner.lastResult, sizeof(history));
    CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
    for (UmicomSize i = 0U; i < sizeof(unaligned); ++i) CHECK(unaligned[i] == 0x49U);
    if (!finish) CommitUnchanged();
    else { CommitEqual(commitVisible, commitExpected, sizeof(commitVisible)); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable)); }
    FileClose();
}

static void FileOpenArguments(const char *name)
{
    UmicomKernelFat16FileCommitter *owner = &fileOwner;
    UmicomSize slot = 0U, partition = 0U; UmicomU64 timeout = 32U;
    UmicomAddress frame = 0U;
    const UmicomKernelBlockDomain before = domain;
    if (!strcmp(name, "domain_alias")) owner = (UmicomKernelFat16FileCommitter *)(void *)&domain;
    else if (!strcmp(name, "dma_alias")) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&frame) == UMICOM_KERNEL_MEMORY_OK);
        domain.slots[0].dataFrame = frame; owner = (UmicomKernelFat16FileCommitter *)frame;
    } else if (!strcmp(name, "alignment")) owner = (UmicomKernelFat16FileCommitter *)(void *)((UmicomU8 *)&fileOwner + 1U);
    else if (!strcmp(name, "null")) owner = 0;
    else if (!strcmp(name, "slot")) slot = UMICOM_BLOCK_SLOT_LIMIT;
    else if (!strcmp(name, "partition")) partition = UMICOM_DISK_PRIMARY_PARTITIONS;
    else if (!strcmp(name, "timeout_zero")) timeout = 0U;
    else if (!strcmp(name, "timeout_excess")) timeout = (UmicomU64)UMICOM_BLOCK_MAX_TIMEOUT_TICKS + 1U;
    else if (!strcmp(name, "nonzero_storage")) fileOwner.commit.cleanHeader[0] = 1U;
    else CHECK(0);
    CHECK(UmicomKernelFat16FileCommitOpen(owner, &domain, slot, partition, timeout) != UMICOM_FAT16_UPDATE_OK);
    if (frame) { domain.slots[0].dataFrame = 0U; CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(frame) == UMICOM_KERNEL_MEMORY_OK); }
    CommitEqual(&before, &domain, sizeof(domain));
    CHECK(!model.reads && !model.writes && !fileOwner.commit.updater.handle && !Allocated()); CommitUnchanged();
}

static void FileRange(const char *name)
{
    const char *path = "/FRAG.BIN"; UmicomU64 offset = 0U; UmicomSize bytes = 1U;
    if (!strcmp(name, "eof")) offset = 1300U;
    else if (!strcmp(name, "past_eof")) offset = 1301U;
    else if (!strcmp(name, "extends_file")) { offset = 1299U; bytes = 2U; }
    else if (!strcmp(name, "offset_overflow")) offset = ~(UmicomU64)0U;
    else if (!strcmp(name, "empty_file")) path = "/EMPTY.TXT";
    else if (!strcmp(name, "directory")) path = "/DOCS";
    else if (!strcmp(name, "missing")) path = "/MISSING.BIN";
    else if (!strcmp(name, "readonly")) path = "/README.TXT";
    else if (!strcmp(name, "nested_readonly")) path = "/DOCS/GUIDE.TXT";
    else if (!strcmp(name, "relative")) path = "FRAG.BIN";
    else if (!strcmp(name, "parent_component")) path = "/DOCS/../FRAG.BIN";
    else if (!strcmp(name, "trailing_slash")) path = "/FRAG.BIN/";
    else if (!strcmp(name, "root")) path = "/";
    else CHECK(0);
    FileOpen(); CHECK(FileStage(path, offset, bytes) != UMICOM_FAT16_UPDATE_OK);
    CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY && !fileResult.commit.mediaTouched);
    CHECK(!fileResult.commit.submittedBytes && !fileResult.commit.submittedDataSectors && !fileResult.commit.submittedMetadataSectors);
    CHECK(!fileResult.commit.completedFlushes && !fileResult.commit.commitAccepted);
    CommitUnchanged();
    CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
    CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); FileAssertFinished(511U, 700U); FileClose();
}

static void FileLifecycle(const char *name)
{
    if (!strcmp(name, "zero_close")) {
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_OK);
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_OK); CHECK(!model.notifications); return;
    }
    if (!strcmp(name, "readonly_retry")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_READ_ONLY);
        CHECK(!Allocated() && !fileOwner.commit.updater.admitted); model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    } else if (!strcmp(name, "no_flush_retry")) {
        model.featuresLow &= ~UMICOM_VIRTIO_FEATURE_FLUSH;
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated() && !fileOwner.commit.updater.admitted); model.featuresLow |= UMICOM_VIRTIO_FEATURE_FLUSH;
    } else if (!strcmp(name, "first_allocation_retry") || !strcmp(name, "second_allocation_retry")) {
        model.failAllocate = !strcmp(name, "first_allocation_retry") ? 1U : 2U;
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated() && !fileOwner.commit.updater.admitted); model.failAllocate = 0U;
    } else if (!strcmp(name, "unsafe_open_retry")) {
        model.allowed = UMICOM_FALSE;
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(!model.reads && !model.writes && !Allocated()); model.allowed = UMICOM_TRUE;
    } else if (!strcmp(name, "failed_open_retained")) {
        model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_CLOSING && fileOwner.commit.updater.handle && Allocated() == 2U);
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls);
        model.stuckReset = UMICOM_FALSE; model.refuseDriver = UMICOM_FALSE; model.stickAfterDriver = UMICOM_FALSE;
        FileClose(); CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_UNUSED);
    } else if (!strcmp(name, "inspection_retry")) {
        commitFailRead = 1U;
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated() && !fileOwner.commit.updater.admitted); commitFailRead = 0U;
    }
    FileOpen();
    if (!strcmp(name, "ready_close")) { CommitUnchanged(); FileClose(); return; }
    if (!strcmp(name, "finish_before_stage")) {
        const UmicomKernelFat16FileCommitResult history = fileOwner.lastResult;
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
        CommitEqual(&history, &fileOwner.lastResult, sizeof(history)); CommitUnchanged(); FileClose(); return;
    }
    if (!strcmp(name, "timeout_retained")) {
        commitHangMutation = 9U; model.stuckReset = UMICOM_TRUE;
        CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(fileResult.commit.writeUncertain && fileResult.commit.uncertainSectorValid && Allocated() == 2U && !model.freeCalls);
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls && fileOwner.commit.state == UMICOM_FAT16_COMMIT_CLOSING);
        model.stuckReset = UMICOM_FALSE; model.noCompletion = UMICOM_FALSE; FileClose(); return;
    }
    CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
    if (!strcmp(name, "stage_twice")) {
        const UmicomKernelFat16FileCommitResult history = fileOwner.lastResult; const UmicomU32 events = commitEventCount;
        CHECK(FileStage("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
        CommitEqual(&history, &fileOwner.lastResult, sizeof(history)); CHECK(commitEventCount == events);
    } else if (!strcmp(name, "reset_retry") || !strcmp(name, "first_release_retry") || !strcmp(name, "second_release_retry")) {
        const UmicomKernelBlockHandle handle = fileOwner.commit.updater.handle;
        if (!strcmp(name, "reset_retry")) model.stuckReset = UMICOM_TRUE;
        else model.failFree = !strcmp(name, "first_release_retry") ? 1U : 2U;
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(fileOwner.commit.updater.handle == handle && fileOwner.commit.state == UMICOM_FAT16_COMMIT_CLOSING);
        CHECK(Allocated() == (!strcmp(name, "second_release_retry") ? 1U : 2U));
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
        model.stuckReset = UMICOM_FALSE; model.failFree = 0U;
    } else if (!strcmp(name, "unsafe_close_retry")) {
        model.allowed = UMICOM_FALSE;
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(Allocated() == 2U && fileOwner.commit.updater.handle); model.allowed = UMICOM_TRUE;
    } else if (!strcmp(name, "single_lifetime") || !strcmp(name, "finish_twice")) {
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); FileAssertFinished(511U, 700U);
        if (!strcmp(name, "finish_twice")) {
            const UmicomKernelFat16FileCommitResult history = fileOwner.lastResult;
            CHECK(FileFinish() == UMICOM_FAT16_UPDATE_BAD_STATE);
            CommitEqual(&fileResult, &fileResultBefore, sizeof(fileResult));
            CommitEqual(&history, &fileOwner.lastResult, sizeof(history));
        }
    }
    FileClose();
    if (!strcmp(name, "single_lifetime")) {
        CHECK(UmicomKernelFat16FileCommitOpen(&fileOwner, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner) == UMICOM_FAT16_UPDATE_OK);
    } else if (!strcmp(name, "staged_close")) {
        memcpy(commitVisible, commitDurable, sizeof(commitVisible)); CommitFreshReader(UMICOM_FALSE);
    }
}


static void FileMutationClock(const char *name)
{
    commitClockInside = !strncmp(name, "inner_", 6U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(commitClockInside || !strncmp(name, "outer_", 6U));
    commitClockRollback = strstr(name, "rollback.") ? UMICOM_TRUE : UMICOM_FALSE;
    const char *const number = strrchr(name, '.'); CHECK(number);
    const unsigned long parsed = strtoul(number + 1U, 0, 10); CHECK(parsed >= 1U && parsed <= 14U);
    commitClockMutation = (UmicomU32)parsed;
    FileOpen();
    if (commitClockMutation > 10U) {
        CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
    }
    CHECK((commitClockMutation > 10U ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(commitClockInjected && commitMutations == commitClockMutation);
    CHECK(fileResult.commit.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    CHECK(fileResult.commit.lastBlockOutcome == (commitClockInside ? UMICOM_BLOCK_SUBMITTED_UNCONFIRMED : UMICOM_BLOCK_COMPLETED));
    const CommitEvent *last = 0;
    for (UmicomSize i = 0U; i < commitEventCount; ++i)
        if (commitEvents[i].command != UMICOM_VIRTIO_REQUEST_READ) last = &commitEvents[i];
    CHECK(last && last->completed);
    const UmicomBoolean write = last->command == UMICOM_VIRTIO_REQUEST_WRITE ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(fileResult.commit.uncertainSectorValid == (commitClockInside && write ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.commit.writeUncertain == (commitClockInside && write ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(fileResult.commit.completedMetadataSectors + fileResult.commit.completedDataSectors ==
        commitWriteCompletions - (commitClockInside && write ? 1U : 0U));
    CHECK(fileResult.commit.completedFlushes == commitFlushCompletions - (commitClockInside && !write ? 1U : 0U));
    if (!commitClockInside && commitClockMutation == 4U) CHECK(fileResult.commit.dirtyDurable);
    if (!commitClockInside && commitClockMutation == 8U) CHECK(fileResult.commit.dataDurable);
    if (!commitClockInside && commitClockMutation == 10U) CHECK(fileResult.directoryDurable);
    if (!commitClockInside && commitClockMutation == 14U) CHECK(fileResult.commit.cleanDurable && !fileResult.commit.cleanVerified);
    FileFailed(); FileClose();
}

static void FileAcceptanceClock(const char *fixture, const char *name)
{
    const UmicomBoolean finish = !strncmp(name, "finish_", 7U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(finish || !strncmp(name, "stage_", 6U));
    FileOpen(); CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 stageRead = commitReads;
    CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); const UmicomU32 finishRead = commitReads;
    FileClose(); FileStart(fixture); FileOpen();
    commitClockRead = finish ? finishRead : stageRead; commitClockSkip = 1U;
    commitClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    if (finish) { CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U); }
    CHECK((finish ? FileFinish() : FileStage("/FRAG.BIN", 511U, 700U)) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(commitClockInjected && !commitClockSkip && !fileResult.commit.commitAccepted);
    CHECK(fileResult.commit.dataDurable && fileResult.commit.dataVerified && fileResult.commit.dirtyDurable && fileResult.commit.dirtyVerified);
    CHECK(fileResult.directoryDurable && fileResult.directoryVerified);
    CHECK(fileResult.commit.cleanVerified == finish && fileResult.commit.cleanDurable == finish);
    CHECK(!fileResult.commit.needsFlush && !fileResult.commit.writeUncertain && !fileResult.commit.uncertainSectorValid);
    CHECK(fileResult.commit.lastBlockOutcome == UMICOM_BLOCK_COMPLETED);
    CHECK(fileResult.commit.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    FileFailed(); FileClose();
}

static void FilePreflightFailure(const char *fixture, const char *name)
{
    FileOpen(); CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
    UmicomU32 lastRead = 0U, directoryRead = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i) {
        if (commitEvents[i].command != UMICOM_VIRTIO_REQUEST_READ) break;
        ++lastRead;
        if (commitEvents[i].sector == fileDirectorySector) directoryRead = lastRead;
    }
    CHECK(lastRead > directoryRead && directoryRead > 4U);
    FileClose(); FileStart(fixture); FileOpen();
    const UmicomBoolean clock = strstr(name, "clock_") ? UMICOM_TRUE : UMICOM_FALSE;
    if (clock) { commitClockRead = lastRead; commitClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE; }
    else if (!strcmp(name, "directory_mismatch")) {
        commitCorruptRead = directoryRead; fileReadCorruptionByte = fileEntryOffset + 26U;
    } else commitFailRead = !strcmp(name, "first_read") ? commitReads + 1U : lastRead;
    CHECK(FileStage("/FRAG.BIN", 511U, 700U) != UMICOM_FAT16_UPDATE_OK);
    CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY && fileResult.commit.phase == UMICOM_FAT16_COMMIT_PREFLIGHT);
    CHECK(!fileResult.commit.mediaTouched && !fileResult.commit.completedFlushes && !fileResult.commit.submittedMetadataSectors);
    CHECK(!fileResult.commit.submittedDataSectors && !fileResult.commit.writeUncertain && !fileResult.commit.needsFlush);
    CHECK(!fileResult.directorySubmitted && !fileResult.directoryCompleted && !fileResult.directoryDurable && !fileResult.directoryVerified);
    if (clock) {
        CHECK(commitClockInjected && fileResult.commit.diskStatus == UMICOM_DISK_IO_ERROR);
        CHECK(fileResult.commit.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    } else if (!strcmp(name, "directory_mismatch")) CHECK(fileResult.commit.diskStatus == UMICOM_DISK_CORRUPT);
    else CHECK(fileResult.commit.diskStatus == UMICOM_DISK_IO_ERROR && fileResult.commit.blockStatus == UMICOM_BLOCK_IO_ERROR);
    CommitUnchanged(); FileClose();
}
static void FileChangedBeforeFinish(const char *name)
{
    FileOpen(); CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
    UmicomSize address = 0U;
    if (!strcmp(name, "primary_header")) address = (UmicomSize)COMMIT_PRIMARY * 512U + 123U;
    else if (!strcmp(name, "mirror_header")) address = (UmicomSize)COMMIT_MIRROR * 512U + 345U;
    else if (!strcmp(name, "first_neighbour")) address = CommitFileAddress(0U);
    else if (!strcmp(name, "middle_data")) address = CommitFileAddress(600U);
    else if (!strcmp(name, "last_slack")) address = CommitFileAddress(1500U);
    else {
        address = (UmicomSize)fileDirectorySector * 512U + fileEntryOffset;
        if (!strcmp(name, "directory_name")) address += 3U;
        else if (!strcmp(name, "directory_attribute")) address += 11U;
        else if (!strcmp(name, "directory_creation")) address += 14U;
        else if (!strcmp(name, "directory_time")) address += 22U;
        else if (!strcmp(name, "directory_date")) address += 24U;
        else if (!strcmp(name, "directory_cluster")) address += 26U;
        else if (!strcmp(name, "directory_size")) address += 28U;
        else if (!strcmp(name, "directory_neighbour")) address += 32U + 5U;
        else CHECK(0);
    }
    commitVisible[address] ^= 0x53U;
    CHECK(FileFinish() == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(fileResult.commit.phase == UMICOM_FAT16_COMMIT_FINISH_VERIFY && fileResult.commit.diskStatus == UMICOM_DISK_CORRUPT);
    CHECK(commitMutations == 10U && !fileResult.commit.cleanFinalisationStarted && !fileResult.commit.cleanDurable);
    CHECK(fileResult.commit.dataVerified && fileResult.commit.dirtyVerified && fileResult.directoryDurable && fileResult.directoryVerified);
    FileFailed(); FileClose();
}
static void FileMisc(const char *name)
{
    if (!strcmp(name, "callback_reentry") || !strcmp(name, "policy_reentry")) {
        fileCallbackReenter = !strcmp(name, "callback_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        filePolicyReenter = !strcmp(name, "policy_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        FileOpen(); CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); FileAssertFinished(511U, 700U);
        CHECK(fileReentries > 0U); FileClose(); return;
    }
    FileOpen();
    if (!strcmp(name, "stopped_clock") || !strcmp(name, "backward_clock")) {
        if (!strcmp(name, "stopped_clock")) { commitStoppedClock = UMICOM_TRUE; model.noCompletion = UMICOM_TRUE; }
        else commitBackwardClock = UMICOM_TRUE;
        CHECK(FileStage("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(fileResult.commit.blockStatus == (!strcmp(name, "stopped_clock") ? UMICOM_BLOCK_TIMEOUT : UMICOM_BLOCK_CLOCK_ERROR));
        CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY); CommitUnchanged();
        commitStoppedClock = UMICOM_FALSE; commitBackwardClock = UMICOM_FALSE; model.noCompletion = UMICOM_FALSE;
    } else if (!strcmp(name, "revalidate_before_stage")) {
        CommitFat(20U, 9U); CommitRebase();
        CHECK(FileStage("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
        CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY); CommitUnchanged();
        CommitFat(20U, 0U); CommitRebase();
        CHECK(FileStage("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); FileAssertStage(511U, 700U);
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); FileAssertFinished(511U, 700U);
    } else CHECK(0);
    FileClose();
}

static void FileReadBudget(const char *name)
{
    const UmicomBoolean exact = !strcmp(name, "exact_boundary") ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(exact || !strcmp(name, "one_over_reserve"));
    const UmicomSize files = exact ? 90U : 99U;
    /* Reachable files occupy distinct chains separated by one FAT cache line.
     * Every read comes from the real allocation proof. No callback changes a
     * production budget counter. The additional directory capture and verify
     * both consume the same 4096-read Stage budget as the data/FAT work. */
    for (UmicomSize i = 0U; i < files; ++i) {
        const UmicomSize clusters = exact ? (i < 9U ? 40U : (i == 9U ? 21U : 20U)) : 20U;
        char shortName[12]; CHECK(snprintf(shortName, sizeof(shortName), "F%07uBIN", (unsigned)i) == 11);
        CommitEntry(CommitRoot(5U + i), shortName, 0x20U, (UmicomU16)(100U + i), (UmicomU32)(clusters * 512U));
        for (UmicomSize j = 0U; j < clusters; ++j) {
            const UmicomU16 cluster = (UmicomU16)(100U + i + j * 256U);
            CommitFat(cluster, j + 1U == clusters ? 0xffffU : (UmicomU16)(cluster + 256U));
        }
    }
    CommitRebase(); FileOpen(); const UmicomKernelFat16UpdateStatus status = FileStage("/FRAG.BIN", 511U, 700U);
    if (exact) {
        CHECK(status == UMICOM_FAT16_UPDATE_OK && fileOwner.commit.updater.operationReads == UMICOM_FAT16_IO_LIMIT);
        FileAssertStage(511U, 700U);
        CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK && fileOwner.commit.updater.operationReads == 8U);
        FileAssertFinished(511U, 700U);
    } else {
        CHECK(status == UMICOM_FAT16_UPDATE_INSPECTION_LIMIT && fileResult.commit.diskStatus == UMICOM_DISK_LIMIT);
        CHECK(fileOwner.commit.state == UMICOM_FAT16_COMMIT_READY && fileResult.commit.phase == UMICOM_FAT16_COMMIT_PREFLIGHT);
        CHECK(fileOwner.commit.updater.operationReads == 4091U && !fileOwner.commit.updater.plan.count);
        CHECK(!fileResult.commit.mediaTouched && !fileResult.commit.submittedDataSectors && !fileResult.commit.submittedMetadataSectors);
        CHECK(!fileResult.directorySubmitted && !fileResult.directoryDurable && !fileResult.directoryVerified);
        CommitUnchanged();
    }
    FileClose();
}

static UmicomKernelShellStatus FileConsoleCommand(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_FALSE;
    const UmicomKernelShellStatus parsed = UmicomKernelShellParse(text, strlen(text), &command);
    if (parsed != UMICOM_SHELL_OK) return parsed;
    const UmicomKernelShellStatus status = UmicomKernelFat16FileCommitCommand(shell, &command, &handled);
    CHECK(handled); return status;
}
static void FileConsoleReenter(void)
{
    CHECK(FileConsoleCommand(&commitShell, "fatfileinfo") == UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell, "fatfiletime 2037-11-23T14:35:59") == UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 0 text") == UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell, "fatfilecommit") == UMICOM_SHELL_BUSY);
    CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_BUSY);
    ++fileReentries;
}
static void FileConsoleOutput(void *context, const char *text, UmicomSize bytes)
{
    Output(context, text, bytes); if (fileConsoleOutputReenter) FileConsoleReenter();
}
static void FileConsoleOpen(void)
{
    CHECK(FileConsoleCommand(&commitShell, "fatfileopen 0 0") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.file.open=ok") && strstr(transcript, "fat.file-state=ready") && Allocated() == 2U);
    CHECK(!commitWrites && !commitFlushes); CommitClearTranscript();
}
static void FileConsoleTime(void)
{
    const UmicomU32 events = commitEventCount;
    CHECK(FileConsoleCommand(&commitShell, "fatfiletime 2037-11-23T14:35:59") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.file.time=ok") && strstr(transcript, "selected-time=2037-11-23T14:35:59"));
    CHECK(strstr(transcript, "stored-time=2037-11-23T14:35:58")); CHECK(commitEventCount == events);
    CommitClearTranscript();
}
static void FileConsoleStage(void)
{
    CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 511 \"Umicom ordered update\"") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.file-state=staged") && strstr(transcript, "phase=directory-verify"));
    CHECK(strstr(transcript, "requested=21 confirmed=21 submitted=21"));
    CHECK(strstr(transcript, "dirty-durable=1 dirty-verified=1 data-durable=1 data-verified=1"));
    CHECK(strstr(transcript, "clean-started=0 clean-durable=0 clean-verified=0 commit-accepted=0"));
    CHECK(strstr(transcript, "attributes-before=0 attributes-after=32"));
    CHECK(strstr(transcript, "requested-time=2037-11-23T14:35:59 stored-time=2037-11-23T14:35:58"));
    CHECK(strstr(transcript, "fat.file.directory-observed submitted=1 completed=1 durable=1 verified=1"));
    CHECK(commitWrites == 5U && commitFlushes == 4U);
    const char text[] = "Umicom ordered update";
    for (UmicomSize i = 0U; i < sizeof(text) - 1U; ++i) commitExpected[CommitFileAddress(511U + i)] = (UmicomU8)text[i];
    FilePatchMetadata(); CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable)); CommitClearTranscript();
}
static void FileConsoleClose(void)
{
    const UmicomU32 mutations = commitMutations;
    CHECK(FileConsoleCommand(&commitShell, "fatfileclose") == UMICOM_SHELL_OK);
    CHECK(!Allocated() && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    CHECK(commitMutations == mutations);
}
static void FileConsoleCase(const char *name)
{
    memset(&commitShell, 0, sizeof(commitShell)); memset(&commitForeignShell, 0, sizeof(commitForeignShell));
    commitShell.output = FileConsoleOutput; commitForeignShell.output = FileConsoleOutput;
    if (!strcmp(name, "before_open")) {
        CHECK(FileConsoleCommand(&commitShell, "fatfileinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "fat.file-state=unused")); CommitClearTranscript();
        CHECK(FileConsoleCommand(&commitShell, "fatfiletime 2037-11-23T14:35:59") == UMICOM_SHELL_IO_ERROR);
        CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 0 text") == UMICOM_SHELL_IO_ERROR);
        CHECK(FileConsoleCommand(&commitShell, "fatfilecommit") == UMICOM_SHELL_IO_ERROR);
        CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_OK);
        CHECK(!commitEventCount && !Allocated()); return;
    }
    if (!strcmp(name, "invalid_arguments")) {
        static const char *const bad[] = {"fatfileopen", "fatfileopen 0", "fatfileopen -1 0", "fatfileopen 0 4",
            "fatfileopen 99 0", "fatfileopen 0 0 extra", "fatfiletime", "fatfiletime 2037-11-23T14:35:59 extra",
            "fatfilestage", "fatfilestage /FRAG.BIN", "fatfilestage /FRAG.BIN -1 text", "fatfilecommit extra",
            "fatfileinfo extra", "fatfileclose extra"};
        for (UmicomSize i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i)
            CHECK(FileConsoleCommand(&commitShell, bad[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!commitEventCount && !Allocated()); return;
    }
    if (!strcmp(name, "readonly_retry")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CHECK(FileConsoleCommand(&commitShell, "fatfileopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.file.open=read-only") && !Allocated()); model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    } else if (!strcmp(name, "dirty_retry")) {
        CommitFat(1U, 0x7fffU); CommitRebase();
        CHECK(FileConsoleCommand(&commitShell, "fatfileopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "last-disk=unclean-volume") && !Allocated());
        CommitFat(1U, 0xffffU); CommitRebase();
    } else if (!strcmp(name, "fat_mismatch_retry")) {
        commitVisible[(UmicomSize)COMMIT_MIRROR * 512U + 24U] ^= 0x53U; CommitRebase();
        CHECK(FileConsoleCommand(&commitShell, "fatfileopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "last-disk=fat-copies-differ") && !Allocated());
        commitVisible[(UmicomSize)COMMIT_MIRROR * 512U + 24U] ^= 0x53U; CommitRebase();
    } else if (!strcmp(name, "platform_retry")) {
        commitPlatformStatus = UMICOM_BLOCK_UNSAFE_CONTEXT;
        CHECK(FileConsoleCommand(&commitShell, "fatfileopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.file.transport=unsafe-context") && !Allocated()); commitPlatformStatus = UMICOM_BLOCK_OK;
    } else if (!strcmp(name, "callback_reentry")) fileConsoleCallbackReenter = UMICOM_TRUE;
    else if (!strcmp(name, "output_reentry")) fileConsoleOutputReenter = UMICOM_TRUE;
    CommitClearTranscript(); FileConsoleOpen();
    if (!strcmp(name, "no_time")) {
        const UmicomU32 events = commitEventCount;
        CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 0 text") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "select an explicit calendar") && commitEventCount == events); CommitClearTranscript();
    }
    if (!strcmp(name, "bad_calendar")) {
        static const char *const bad[] = {"2037-2-23T14:35:59", "2037-11-23 14:35:59", "2037-11-23t14:35:59",
            "2037-11-23T14:35:59Z", "2037-11-23T14:35:59+00:00", "2037-11-23T14:35:59.1",
            "1979-12-31T23:59:59", "2108-01-01T00:00:00", "2100-02-29T00:00:00", "2000-02-30T00:00:00",
            "2037-11-31T14:35:59", "2037-11-23T24:35:59", "2037-11-23T14:60:59", "2037-11-23T14:35:60"};
        const UmicomU32 events = commitEventCount;
        for (UmicomSize i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            char command[128]; CHECK(snprintf(command, sizeof(command), "fatfiletime \"%s\"", bad[i]) > 0);
            CHECK(FileConsoleCommand(&commitShell, command) == UMICOM_SHELL_INVALID_ARGUMENT); CommitClearTranscript();
        }
        CHECK(commitEventCount == events);
        CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 0 text") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "select an explicit calendar")); CommitClearTranscript();
    }
    FileConsoleTime();
    if (!strcmp(name, "calendar_retry")) {
        CHECK(FileConsoleCommand(&commitShell, "fatfiletime 2100-02-29T00:00:00") == UMICOM_SHELL_INVALID_ARGUMENT);
        CommitClearTranscript();
    } else if (!strcmp(name, "wrong_shell")) {
        const UmicomU32 events = commitEventCount;
        CHECK(FileConsoleCommand(&commitForeignShell, "fatfileinfo") == UMICOM_SHELL_BAD_STATE);
        CHECK(FileConsoleCommand(&commitForeignShell, "fatfiletime 2000-01-01T00:00:00") == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitForeignShell) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(commitEventCount == events && Allocated() == 2U);
    } else if (!strcmp(name, "legacy_exclusion")) {
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(Allocated() == 2U && !commitWrites && !commitFlushes);
        CHECK(UmicomKernelFat16CommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_OK); CommitClearTranscript();
    } else if (!strcmp(name, "empty_text")) {
        const UmicomU32 events = commitEventCount;
        CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 0 \"\"") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.file.result=not-admitted") && commitEventCount == events); CommitClearTranscript();
    } else if (!strcmp(name, "readonly_file")) {
        CHECK(FileConsoleCommand(&commitShell, "fatfilestage /README.TXT 0 x") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.file.stage=read-only") && !commitWrites && !commitFlushes); CommitClearTranscript();
    } else if (!strcmp(name, "directory_failure")) {
        commitFailWrite = 5U; commitPartialWrite = UMICOM_TRUE;
        CHECK(FileConsoleCommand(&commitShell, "fatfilestage /FRAG.BIN 511 \"Umicom ordered update\"") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "phase=directory-write") && strstr(transcript, "write-uncertain=1"));
        CHECK(strstr(transcript, "fat.file.directory-observed submitted=1 completed=0 durable=0 verified=0"));
        const UmicomU32 events = commitEventCount; CommitClearTranscript();
        CHECK(FileConsoleCommand(&commitShell, "fatfilecommit") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "previous-result") && strstr(transcript, "phase=directory-write") && commitEventCount == events);
        FileConsoleClose(); return;
    }
    FileConsoleStage();
    if (!strcmp(name, "time_after_stage")) {
        const UmicomU32 events = commitEventCount;
        CHECK(FileConsoleCommand(&commitShell, "fatfiletime 2000-01-01T00:00:00") == UMICOM_SHELL_IO_ERROR);
        CHECK(commitEventCount == events); CommitClearTranscript();
        CHECK(FileConsoleCommand(&commitShell, "fatfileinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "requested-time=2037-11-23T14:35:59") && !strstr(transcript, "2000-01-01")); CommitClearTranscript();
    } else if (!strcmp(name, "staged_close")) {
        const UmicomU32 mutations = commitMutations;
        CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_OK);
        CHECK(!Allocated() && commitMutations == mutations);
        CHECK(strstr(transcript, "file commit not accepted; no flush or flag repair submitted"));
        CommitEqual(commitVisible, commitExpected, sizeof(commitVisible)); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
        CommitFreshReader(UMICOM_FALSE); return;
    } else if (!strcmp(name, "reset_retry") || !strcmp(name, "release_retry")) {
        if (!strcmp(name, "reset_retry")) model.stuckReset = UMICOM_TRUE; else model.failFree = 1U;
        const UmicomU32 mutations = commitMutations;
        CHECK(FileConsoleCommand(&commitShell, "fatfileclose") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.file-state=closing") && Allocated() == 2U && commitMutations == mutations);
        model.stuckReset = UMICOM_FALSE; model.failFree = 0U; FileConsoleClose(); return;
    } else if (!strcmp(name, "late_clean_failure")) {
        commitFailFlush = 6U; commitPersistWrites = UMICOM_TRUE;
        CHECK(FileConsoleCommand(&commitShell, "fatfilecommit") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "phase=clean-primary-flush") && strstr(transcript, "commit-accepted=0"));
        CHECK(strstr(transcript, "directory-observed submitted=1 completed=1 durable=1 verified=1"));
        CommitExpectedDirty(UMICOM_FALSE); CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
        CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
        FileConsoleClose(); CommitFreshReader(UMICOM_TRUE); return;
    }
    CHECK(FileConsoleCommand(&commitShell, "fatfilecommit") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.file-state=committed") && strstr(transcript, "commit-accepted=1"));
    CHECK(commitWrites == 7U && commitFlushes == 6U); CommitExpectedDirty(UMICOM_FALSE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible)); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    CommitClearTranscript(); CHECK(FileConsoleCommand(&commitShell, "fatfileinfo") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.file.last-result") && strstr(transcript, "phase=complete"));
    CHECK(strstr(transcript, "stored-time=2037-11-23T14:35:58")); FileConsoleClose();
    if (fileConsoleCallbackReenter || fileConsoleOutputReenter) CHECK(fileReentries > 0U);
}

/* The ELF linker routes the C runtime's entry to this suite using --wrap=main.
 * Keeping the included suite's own entry intact avoids modifying its nested
 * harness inclusion or relying on fragile nested preprocessor renaming. */
int __wrap_main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: umicom-fat16-file-commit-tests CASE READONLY-FIXTURE\n"); return 2; }
    FileStart(argv[2]); const char *const name = argv[1];
    if (!strncmp(name, "success.", 8U)) FileSuccess(name + 8U);
    else if (!strncmp(name, "calendar.", 9U)) FileCalendar(name + 9U, UMICOM_FALSE);
    else if (!strncmp(name, "invalid_calendar.", 17U)) FileCalendar(name + 17U, UMICOM_TRUE);
    else if (!strncmp(name, "format.", 7U)) FileFormat(name + 7U);
    else if (!strncmp(name, "write_error.", 12U)) FileWriteFailure(name + 12U);
    else if (!strncmp(name, "flush_error.", 12U)) FileFlushFailure(name + 12U);
    else if (!strncmp(name, "read_error.", 11U)) FileReadFailure(name + 11U, UMICOM_FALSE);
    else if (!strncmp(name, "read_mismatch.", 14U)) FileReadFailure(name + 14U, UMICOM_TRUE);
    else if (!strncmp(name, "dropped_write.", 14U)) FileDroppedWrite(name + 14U, UMICOM_FALSE);
    else if (!strncmp(name, "altered_write.", 14U)) FileDroppedWrite(name + 14U, UMICOM_TRUE);
    else if (!strncmp(name, "cut.", 4U)) FileCut(name + 4U);
    else if (!strncmp(name, "ownership.", 10U)) FileOwnership(name + 10U, UMICOM_FALSE);
    else if (!strncmp(name, "finish_ownership.", 17U)) FileOwnership(name + 17U, UMICOM_TRUE);
    else if (!strncmp(name, "open_arguments.", 15U)) FileOpenArguments(name + 15U);
    else if (!strncmp(name, "range.", 6U)) FileRange(name + 6U);
    else if (!strncmp(name, "lifetime.", 9U)) FileLifecycle(name + 9U);
    else if (!strncmp(name, "clock.", 6U)) FileMutationClock(name + 6U);
    else if (!strncmp(name, "preflight.", 10U)) FilePreflightFailure(argv[2], name + 10U);
    else if (!strncmp(name, "acceptance_clock.", 17U)) FileAcceptanceClock(argv[2], name + 17U);
    else if (!strncmp(name, "changed_before_finish.", 22U)) FileChangedBeforeFinish(name + 22U);
    else if (!strncmp(name, "console.", 8U)) FileConsoleCase(name + 8U);
    else if (!strncmp(name, "read_budget.", 12U)) FileReadBudget(name + 12U);
    else FileMisc(name);
    CHECK(!Allocated()); printf("fat16-file-commit.%s: ok\n", name); return 0;
}
