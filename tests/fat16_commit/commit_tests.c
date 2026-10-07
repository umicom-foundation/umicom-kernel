/*-----------------------------------------------------------------------------
 * Umicom Kernel — ordered FAT16 commit qualification.
 *
 * The original register/reset harness supplies the real transport, DMA and
 * allocator boundary. This suite adds an independent chronological media
 * model: writes affect visible bytes, FLUSH acknowledges durable bytes, and a
 * second mode permits each WRITE to persist immediately. Interrupted operation
 * checks therefore never assume that an unflushed WRITE disappears.
 *
 * Complete-image comparisons include both FATs, directory records, neighbours
 * and slack. Simulated loss tests establish protocol ordering under this model;
 * they do not establish physical-sector atomicity or repair a damaged volume.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#define UmicomPlatformBlockDomainGet UmicomCommitOriginalDomainGet
#define main UmicomCommitOriginalBlockQualificationEntry
#include "../virtio_block/virtio_block_tests.c"
#undef main
#undef UmicomPlatformBlockDomainGet
#include "../disk_inspection/fixture_layout.h"
#include "umicom/kernel/fat16_commit.h"
#include "umicom/kernel/fat16_commit_console.h"
#include "umicom/kernel/disk_filesystem.h"

#define COMMIT_MEDIA_BYTES ((UmicomSize)UMICOM_DISK_FIXTURE_SECTORS * 512U)
#define COMMIT_EVENT_LIMIT 8192U
#define COMMIT_PRIMARY ((UmicomU64)UMICOM_DISK_FIXTURE_FIRST + 1U)
#define COMMIT_MIRROR (COMMIT_PRIMARY + UMICOM_DISK_FIXTURE_FAT_SECTORS)

typedef struct CommitEvent {
    UmicomU32 command;
    UmicomU64 sector;
    UmicomU8 data[512];
    UmicomBoolean completed;
} CommitEvent;

static UmicomU8 commitInitial[COMMIT_MEDIA_BYTES];
static UmicomU8 commitVisible[COMMIT_MEDIA_BYTES];
static UmicomU8 commitDurable[COMMIT_MEDIA_BYTES];
static UmicomU8 commitExpected[COMMIT_MEDIA_BYTES];
static UmicomU8 commitInput[4097], commitInputSnapshot[4097];
static CommitEvent commitEvents[COMMIT_EVENT_LIMIT];
static UmicomKernelFat16Committer committer;
static UmicomKernelFat16CommitResult commitResult, commitResultBefore;
static UmicomU32 commitEventCount, commitReads, commitWrites, commitFlushes, commitMutations;
static UmicomU32 commitWriteCompletions, commitFlushCompletions, commitDataWrites;
static UmicomU32 commitFailWrite, commitFailFlush, commitFailRead, commitCorruptRead, commitDropWrite;
static UmicomU32 commitAlterWrite, commitHangMutation, commitCutMutation;
static UmicomU32 commitReadFaultMutation, commitReadFaultOrdinal, commitReadFaultSeen;
static UmicomU32 commitReadFaultOperation, commitOperation;
static UmicomU32 commitClockMutation, commitClockRead, commitClockSkip, commitReentries;
static UmicomU64 commitPreviousClock;
static UmicomBoolean commitPartialWrite, commitPersistWrites, commitReadFaultCorrupt;
static UmicomBoolean commitClockInside, commitClockRollback, commitClockInjected;
static UmicomBoolean commitStoppedClock, commitBackwardClock, commitCallbackReenter, commitPolicyReenter;
static UmicomKernelBlockStatus commitPlatformStatus;
static UmicomBoolean commitConsoleCallbackReenter, commitConsoleOutputReenter;
static void CommitConsoleReenter(void);

UmicomKernelBlockStatus UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **out)
{
    if (commitPlatformStatus != UMICOM_BLOCK_OK) return commitPlatformStatus;
    return UmicomCommitOriginalDomainGet(out);
}

static void CommitEqual(const void *left, const void *right, UmicomSize bytes)
{
    CHECK(memcmp(left, right, (size_t)bytes) == 0);
}
static void CommitPut16(UmicomU8 *p, UmicomU16 value)
{
    p[0] = (UmicomU8)value; p[1] = (UmicomU8)(value >> 8U);
}
static void CommitPut32(UmicomU8 *p, UmicomU32 value)
{
    for (UmicomSize i = 0U; i < 4U; ++i) p[i] = (UmicomU8)(value >> (8U * i));
}
static UmicomU16 CommitFlags(const UmicomU8 *media, UmicomU64 sector)
{
    const UmicomSize offset = (UmicomSize)sector * 512U + 2U;
    return (UmicomU16)((UmicomU16)media[offset] | (UmicomU16)((UmicomU16)media[offset + 1U] << 8U));
}
static UmicomU8 *CommitRoot(UmicomSize entry)
{
    CHECK(entry < 512U);
    return commitVisible + (UmicomSize)UMICOM_DISK_FIXTURE_ROOT * 512U + entry * 32U;
}
static UmicomU8 *CommitCluster(UmicomU16 cluster)
{
    CHECK(cluster >= 2U && cluster < 12000U);
    return commitVisible + ((UmicomSize)UMICOM_DISK_FIXTURE_DATA + cluster - 2U) * 512U;
}
static void CommitFat(UmicomU16 cluster, UmicomU16 next)
{
    CHECK((UmicomSize)cluster * 2U + 2U <= UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U);
    CommitPut16(commitVisible + (UmicomSize)COMMIT_PRIMARY * 512U + (UmicomSize)cluster * 2U, next);
    CommitPut16(commitVisible + (UmicomSize)COMMIT_MIRROR * 512U + (UmicomSize)cluster * 2U, next);
}
static void CommitEntry(UmicomU8 *entry, const char *name, UmicomU8 attributes,
    UmicomU16 cluster, UmicomU32 bytes)
{
    memset(entry, 0, 32U); memcpy(entry, name, 11U); entry[11] = attributes;
    CommitPut16(entry + 26U, cluster); CommitPut32(entry + 28U, bytes);
}
static void CommitRebase(void)
{
    memcpy(commitInitial, commitVisible, sizeof(commitInitial));
    memcpy(commitDurable, commitVisible, sizeof(commitDurable));
    memcpy(commitExpected, commitVisible, sizeof(commitExpected));
}
static UmicomU16 CommitFileCluster(UmicomSize index)
{
    static const UmicomU16 clusters[] = {4U, 9U, 6U, 10U, 11U, 12U, 13U, 14U, 15U};
    CHECK(index < sizeof(clusters) / sizeof(clusters[0]));
    return clusters[index];
}
static UmicomSize CommitFileAddress(UmicomU64 offset)
{
    return ((UmicomSize)UMICOM_DISK_FIXTURE_DATA + CommitFileCluster((UmicomSize)(offset / 512U)) - 2U) *
        512U + (UmicomSize)(offset % 512U);
}
static void CommitPatchExpected(UmicomU64 offset, UmicomSize bytes)
{
    for (UmicomSize i = 0U; i < bytes; ++i)
        commitExpected[CommitFileAddress(offset + i)] = commitInputSnapshot[i];
}
static void CommitExpectedDirty(UmicomBoolean dirty)
{
    const UmicomU64 sectors[] = {COMMIT_PRIMARY, COMMIT_MIRROR};
    for (UmicomSize i = 0U; i < 2U; ++i) {
        const UmicomSize byte = (UmicomSize)sectors[i] * 512U + 3U;
        commitExpected[byte] = dirty ? (UmicomU8)(commitInitial[byte] & 0x7fU) : commitInitial[byte];
    }
}
static void CommitLargeFile(void)
{
    for (UmicomSize i = 0U; i < 9U; ++i) {
        const UmicomU16 cluster = CommitFileCluster(i);
        CommitFat(cluster, i == 8U ? 0xffffU : CommitFileCluster(i + 1U));
        for (UmicomSize j = 0U; j < 512U; ++j)
            CommitCluster(cluster)[j] = (UmicomU8)((i * 512U + j) * 29U + 7U);
    }
    CommitPut32(CommitRoot(3U) + 28U, 9U * 512U); CommitRebase();
}
static UmicomBoolean CommitHeaderSector(UmicomU64 sector)
{
    return sector == COMMIT_PRIMARY || sector == COMMIT_MIRROR ? UMICOM_TRUE : UMICOM_FALSE;
}
static void CommitReenter(void)
{
    UmicomKernelFat16CommitResult ignored, before;
    memset(&ignored, 0x59, sizeof(ignored)); memcpy(&before, &ignored, sizeof(before));
    CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16CommitStage(&committer, "/FRAG.BIN", 0U, commitInput, 1U, &ignored) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16CommitFinish(&committer, &ignored) == UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_BUSY);
    CommitEqual(&ignored, &before, sizeof(ignored)); ++commitReentries;
}
static UmicomBoolean CommitAllowed(void *context)
{
    CHECK(context == &model);
    if (commitPolicyReenter) CommitReenter();
    return model.allowed;
}
static UmicomU64 CommitClock(void *context)
{
    CHECK(context == &model);
    if (commitBackwardClock) { if (model.now) --model.now; return model.now; }
    if (commitStoppedClock) return model.now;
    UmicomU64 now = Clock(context);
    const UmicomBoolean matching = (commitClockMutation && commitMutations == commitClockMutation) ||
        (commitClockRead && commitReads == commitClockRead) ? UMICOM_TRUE : UMICOM_FALSE;
    if (matching && !commitClockInjected && !model.pending && (commitClockInside ? domain.busy : !domain.busy) && commitClockSkip)
        --commitClockSkip;
    else if (matching && !commitClockInjected && !model.pending && (commitClockInside ? domain.busy : !domain.busy)) {
        commitClockInjected = UMICOM_TRUE;
        now = commitClockRollback ? (commitClockInside ? commitPreviousClock - 1U : committer.updater.operationClock - 1U) :
            now + (commitClockInside ? domain.slots[0].timeoutTicks : UMICOM_FAT16_UPDATE_OPERATION_TICKS);
        model.now = now;
    }
    commitPreviousClock = now;
    return now;
}
static void CommitComplete(void)
{
    CHECK(model.desc && model.avail && model.used && commitEventCount);
    CommitEvent *const event = &commitEvents[commitEventCount - 1U];
    const UmicomU16 used = *(const UmicomU16 *)(model.used + 2U);
    CHECK(*(const UmicomU16 *)(model.avail + 2U) == (UmicomU16)(used + 1U));
    CHECK(*(const UmicomU16 *)(model.avail + 4U + 2U * (used % 8U)) == 0U);
    const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
    CHECK(header == model.desc + UMICOM_VIRTIO_HEADER_OFFSET);
    CHECK(*(const UmicomU32 *)header == event->command && *(const UmicomU32 *)(header + 4U) == 0U);
    CHECK(*(const UmicomU32 *)(model.desc + 8U) == 16U);
    CHECK(*(const UmicomU16 *)(model.desc + 12U) == 1U && *(const UmicomU16 *)(model.desc + 14U) == 1U);
    UmicomU32 statusByte = model.result, usedBytes = 1U;
    UmicomAddress status;
    if (event->command == UMICOM_VIRTIO_REQUEST_FLUSH) {
        CHECK(*(const UmicomU64 *)(header + 8U) == 0U);
        status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
        CHECK(*(const UmicomU32 *)(model.desc + 24U) == 1U);
        CHECK(*(const UmicomU16 *)(model.desc + 28U) == 2U && *(const UmicomU16 *)(model.desc + 30U) == 0U);
        if (commitFailFlush == commitFlushes) statusByte = 1U;
        if (!statusByte) {
            memcpy(commitDurable, commitVisible, sizeof(commitDurable)); ++commitFlushCompletions;
        }
    } else {
        const UmicomAddress data = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
        CHECK(event->sector < UMICOM_DISK_FIXTURE_SECTORS);
        CHECK(*(const UmicomU32 *)(model.desc + 24U) == 512U && data == domain.slots[0].dataFrame);
        CHECK(data >= (UmicomAddress)ram && data + 4096U <= (UmicomAddress)ram + sizeof(ram));
        CHECK(*(const UmicomU16 *)(model.desc + 28U) == (event->command == UMICOM_VIRTIO_REQUEST_READ ? 3U : 1U));
        CHECK(*(const UmicomU16 *)(model.desc + 30U) == 2U);
        status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 32U);
        CHECK(*(const UmicomU32 *)(model.desc + 40U) == 1U);
        CHECK(*(const UmicomU16 *)(model.desc + 44U) == 2U && *(const UmicomU16 *)(model.desc + 46U) == 0U);
        const UmicomSize offset = (UmicomSize)event->sector * 512U;
        if (event->command == UMICOM_VIRTIO_REQUEST_READ) {
            usedBytes = 513U;
            if (commitFailRead == commitReads) { statusByte = 1U; memset((void *)data, 0x67, 512U); }
            else {
                memcpy((void *)data, commitVisible + offset, 512U);
                if (commitCorruptRead == commitReads) ((UmicomU8 *)data)[0] ^= 0x53U;
            }
        } else {
            CHECK(event->command == UMICOM_VIRTIO_REQUEST_WRITE);
            CHECK(domain.slots[0].needsFlush && domain.slots[0].writeUncertain);
            CommitEqual(event->data, (const void *)data, 512U);
            if (!CommitHeaderSector(event->sector)) {
                /* This assertion is independent of the production phase enum.
                 * Every actual data submission requires both durable markers. */
                CHECK(!(CommitFlags(commitDurable, COMMIT_PRIMARY) & UMICOM_FAT16_CLEAN_MASK));
                CHECK(!(CommitFlags(commitDurable, COMMIT_MIRROR) & UMICOM_FAT16_CLEAN_MASK));
                CHECK(commitFlushCompletions >= 2U); ++commitDataWrites;
            }
            if (commitFailWrite == commitWrites) {
                statusByte = 1U;
                if (commitPartialWrite) {
                    memcpy(commitVisible + offset, (const void *)data, 256U);
                    if (commitPersistWrites) memcpy(commitDurable + offset, (const void *)data, 256U);
                }
            } else if (commitDropWrite != commitWrites) {
                memcpy(commitVisible + offset, (const void *)data, 512U);
                if (commitAlterWrite == commitWrites) commitVisible[offset] ^= 0x53U;
                if (commitPersistWrites) memcpy(commitDurable + offset, commitVisible + offset, 512U);
            }
            if (!statusByte) ++commitWriteCompletions;
        }
    }
    CHECK(status == model.desc + UMICOM_VIRTIO_RESULT_OFFSET);
    if (!model.omitResult) *(UmicomU8 *)status = (UmicomU8)statusByte;
    const UmicomAddress element = model.used + 4U + 8U * (used % 8U);
    *(UmicomU32 *)element = model.usedId;
    *(UmicomU32 *)(element + 4U) = model.usedLength == 0xffffffffU ? usedBytes : model.usedLength;
    atomic_thread_fence(memory_order_seq_cst);
    *(volatile UmicomU16 *)(model.used + 2U) = (UmicomU16)(used + model.usedDelta);
    model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] |= 1U;
    model.pending = UMICOM_FALSE; event->completed = statusByte ? UMICOM_FALSE : UMICOM_TRUE;
    if (event->command != UMICOM_VIRTIO_REQUEST_READ && commitCutMutation == commitMutations)
        model.allowed = UMICOM_FALSE;
}
static void CommitWriteRegister(void *context, UmicomAddress address, UmicomU32 value)
{
    if (Offset(address) == UMICOM_VIRTIO_QUEUE_NOTIFY) {
        CHECK(commitEventCount < COMMIT_EVENT_LIMIT);
        CommitEvent *const event = &commitEvents[commitEventCount++];
        const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
        event->command = *(const UmicomU32 *)header;
        event->sector = *(const UmicomU64 *)(header + 8U);
        if (event->command == UMICOM_VIRTIO_REQUEST_READ) {
            ++commitReads;
            if (commitReadFaultMutation && commitMutations == commitReadFaultMutation &&
                commitReadFaultOperation == commitOperation && ++commitReadFaultSeen == commitReadFaultOrdinal) {
                if (commitReadFaultCorrupt) commitCorruptRead = commitReads; else commitFailRead = commitReads;
            }
        } else {
            ++commitMutations;
            if (event->command == UMICOM_VIRTIO_REQUEST_WRITE) {
                ++commitWrites;
                memcpy(event->data, (const void *)(UmicomAddress)*(const UmicomU64 *)(model.desc + 16U), 512U);
                if (!CommitHeaderSector(event->sector)) {
                    CHECK(!(CommitFlags(commitDurable, COMMIT_PRIMARY) & UMICOM_FAT16_CLEAN_MASK));
                    CHECK(!(CommitFlags(commitDurable, COMMIT_MIRROR) & UMICOM_FAT16_CLEAN_MASK));
                    CHECK(commitFlushCompletions >= 2U);
                }
            } else { CHECK(event->command == UMICOM_VIRTIO_REQUEST_FLUSH); ++commitFlushes; }
            if (commitHangMutation == commitMutations) model.noCompletion = UMICOM_TRUE;
        }
        if (commitCallbackReenter) CommitReenter();
        if (commitConsoleCallbackReenter) CommitConsoleReenter();
    }
    WriteRegister(context, address, value);
}
static void CommitStart(const char *fixture)
{
    Start();
    FILE *file = fopen(fixture, "rb"); CHECK(file);
    CHECK(fread(commitVisible, 1U, sizeof(commitVisible), file) == sizeof(commitVisible));
    CHECK(fgetc(file) == EOF && fclose(file) == 0); CommitRebase();
    memset(&committer, 0, sizeof(committer)); memset(commitEvents, 0, sizeof(commitEvents));
    memset(&commitResult, 0xa5, sizeof(commitResult)); memcpy(&commitResultBefore, &commitResult, sizeof(commitResult));
    for (UmicomSize i = 0U; i < sizeof(commitInput); ++i) commitInput[i] = (UmicomU8)(i * 73U + 0x5dU);
    memcpy(commitInputSnapshot, commitInput, sizeof(commitInput));
    model.capacity = UMICOM_DISK_FIXTURE_SECTORS;
    model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY; model.featuresLow |= UMICOM_VIRTIO_FEATURE_FLUSH;
    model.expectedDriverLow = UMICOM_VIRTIO_FEATURE_FLUSH; model.completeRequest = CommitComplete;
    domain.operations.write32 = CommitWriteRegister; domain.operations.clock = CommitClock;
    domain.operations.allowed = CommitAllowed;
    commitEventCount = 0U; commitReads = 0U; commitWrites = 0U; commitFlushes = 0U; commitMutations = 0U;
    commitWriteCompletions = 0U; commitFlushCompletions = 0U; commitDataWrites = 0U;
    commitFailWrite = 0U; commitFailFlush = 0U; commitFailRead = 0U; commitCorruptRead = 0U;
    commitDropWrite = 0U; commitAlterWrite = 0U; commitHangMutation = 0U; commitCutMutation = 0U;
    commitReadFaultMutation = 0U; commitReadFaultOrdinal = 0U; commitReadFaultSeen = 0U;
    commitReadFaultOperation = 0U; commitOperation = 0U;
    commitClockMutation = 0U; commitClockRead = 0U; commitClockSkip = 0U; commitReentries = 0U; commitPreviousClock = 0U;
    commitPartialWrite = UMICOM_FALSE; commitPersistWrites = UMICOM_FALSE; commitReadFaultCorrupt = UMICOM_FALSE;
    commitClockInside = UMICOM_FALSE; commitClockRollback = UMICOM_FALSE; commitClockInjected = UMICOM_FALSE;
    commitStoppedClock = UMICOM_FALSE; commitBackwardClock = UMICOM_FALSE;
    commitCallbackReenter = UMICOM_FALSE; commitPolicyReenter = UMICOM_FALSE;
    commitPlatformStatus = UMICOM_BLOCK_OK;
    commitConsoleCallbackReenter = UMICOM_FALSE; commitConsoleOutputReenter = UMICOM_FALSE;
}
static void CommitOpenOwner(void)
{
    CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_OK);
    CHECK(committer.state == UMICOM_FAT16_COMMIT_READY && committer.updater.admitted && committer.updater.handle);
    CHECK(committer.self == &committer && !committer.busy && !committer.updater.busy && !committer.updater.volume.open);
    CHECK(Allocated() == 2U && !commitWrites && !commitFlushes);
}
static void CommitCloseOwner(void)
{
    const UmicomKernelFat16CommitResult saved = committer.lastResult;
    const UmicomU32 mutations = commitMutations;
    CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_OK);
    CHECK(!committer.updater.handle && !committer.busy && !committer.updater.volume.open);
    CHECK(!Allocated() && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    CommitEqual(&saved, &committer.lastResult, sizeof(saved)); CHECK(commitMutations == mutations);
}
static UmicomKernelFat16UpdateStatus CommitStageAt(const char *path, UmicomU64 offset, UmicomSize bytes)
{
    commitOperation = 1U; memset(&commitResult, 0xa5, sizeof(commitResult));
    memcpy(&commitResultBefore, &commitResult, sizeof(commitResult));
    return UmicomKernelFat16CommitStage(&committer, path, offset, commitInput, bytes, &commitResult);
}
static UmicomKernelFat16UpdateStatus CommitFinishOwner(void)
{
    commitOperation = 2U; memset(&commitResult, 0xa5, sizeof(commitResult));
    memcpy(&commitResultBefore, &commitResult, sizeof(commitResult));
    return UmicomKernelFat16CommitFinish(&committer, &commitResult);
}
static void CommitUnchanged(void)
{
    CHECK(!commitWrites && !commitFlushes);
    CommitEqual(commitVisible, commitInitial, sizeof(commitVisible));
    CommitEqual(commitDurable, commitInitial, sizeof(commitDurable));
    CommitEqual(commitInput, commitInputSnapshot, sizeof(commitInput));
}
static void CommitAssertStage(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize sectors = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    CHECK(committer.state == UMICOM_FAT16_COMMIT_STAGED && !committer.busy && !committer.updater.busy);
    CHECK(commitResult.status == UMICOM_FAT16_UPDATE_OK && commitResult.diskStatus == UMICOM_DISK_OK);
    CHECK(commitResult.blockStatus == UMICOM_BLOCK_OK && commitResult.dataOutcome == UMICOM_FAT16_UPDATE_COMPLETED);
    CHECK(commitResult.offset == offset && commitResult.requestedBytes == bytes);
    CHECK(commitResult.confirmedBytes == bytes && commitResult.submittedBytes == bytes);
    CHECK(commitResult.completedDataSectors == sectors && commitResult.submittedDataSectors == sectors);
    CHECK(commitResult.completedMetadataSectors == 2U && commitResult.submittedMetadataSectors == 2U);
    CHECK(commitResult.completedFlushes == 3U && commitWrites == sectors + 2U && commitFlushes == 3U);
    CHECK(commitResult.mediaTouched && commitResult.dirtyDurable && commitResult.dirtyVerified);
    CHECK(commitResult.dataDurable && commitResult.dataVerified && !commitResult.cleanFinalisationStarted);
    CHECK(!commitResult.cleanDurable && !commitResult.cleanVerified && !commitResult.commitAccepted);
    CHECK(!commitResult.needsFlush && !commitResult.writeUncertain && !commitResult.uncertainSectorValid);
    CommitEqual(&committer.lastResult, &commitResult, sizeof(commitResult));
    CommitPatchExpected(offset, bytes); CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    CommitEqual(commitInput, commitInputSnapshot, sizeof(commitInput));
}
static void CommitAssertFinished(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize sectors = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    CHECK(committer.state == UMICOM_FAT16_COMMIT_COMMITTED && commitResult.phase == UMICOM_FAT16_COMMIT_COMPLETE);
    CHECK(commitResult.status == UMICOM_FAT16_UPDATE_OK && commitResult.commitAccepted);
    CHECK(commitResult.cleanFinalisationStarted && commitResult.cleanDurable && commitResult.cleanVerified);
    CHECK(commitResult.dirtyDurable && commitResult.dirtyVerified && commitResult.dataDurable && commitResult.dataVerified);
    CHECK(commitResult.completedMetadataSectors == 4U && commitResult.submittedMetadataSectors == 4U);
    CHECK(commitResult.completedDataSectors == sectors && commitResult.submittedDataSectors == sectors);
    CHECK(commitResult.completedFlushes == 5U && commitWrites == sectors + 4U && commitFlushes == 5U);
    CHECK(!commitResult.needsFlush && !commitResult.writeUncertain && !commitResult.uncertainSectorValid);
    CommitExpectedDirty(UMICOM_FALSE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    CommitEqual(&committer.lastResult, &commitResult, sizeof(commitResult));
    CommitEqual(commitInput, commitInputSnapshot, sizeof(commitInput));
}
static void CommitTrace(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize sectors = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    UmicomSize mutation = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i) {
        const CommitEvent *const event = &commitEvents[i];
        if (event->command == UMICOM_VIRTIO_REQUEST_READ) continue;
        const UmicomSize index = mutation++;
        if (index == 1U || index == 3U || index == sectors + 4U || index == sectors + 6U || index == sectors + 8U) {
            CHECK(event->command == UMICOM_VIRTIO_REQUEST_FLUSH && event->completed); continue;
        }
        CHECK(event->command == UMICOM_VIRTIO_REQUEST_WRITE && event->completed);
        if (index == 0U || index == 2U || index == sectors + 5U || index == sectors + 7U) {
            CHECK(event->sector == (index == 0U || index == sectors + 5U ? COMMIT_MIRROR : COMMIT_PRIMARY));
            UmicomU8 expected[512]; memcpy(expected, commitInitial + (UmicomSize)event->sector * 512U, sizeof(expected));
            if (index < 4U) expected[3] &= 0x7fU;
            CommitEqual(event->data, expected, sizeof(expected));
        } else {
            CHECK(index >= 4U && index < sectors + 4U);
            const UmicomU64 sector = UMICOM_DISK_FIXTURE_DATA + CommitFileCluster((UmicomSize)(offset / 512U) + index - 4U) - 2U;
            CHECK(event->sector == sector); CommitEqual(event->data, commitExpected + (UmicomSize)sector * 512U, 512U);
        }
    }
    CHECK(mutation == sectors + 9U);
}
static void CommitSuccess(const char *name)
{
    UmicomU64 offset = 0U; UmicomSize bytes = 1U;
    if (!strcmp(name, "fragmented")) { offset = 511U; bytes = 700U; }
    else if (!strcmp(name, "whole_file")) bytes = 1300U;
    else if (!strcmp(name, "last_byte")) offset = 1299U;
    else if (!strcmp(name, "aligned_sector")) { offset = 512U; bytes = 512U; }
    else if (!strcmp(name, "nine_sectors")) { CommitLargeFile(); offset = 1U; bytes = 4096U; }
    else if (!strcmp(name, "write_through")) { offset = 511U; bytes = 700U; commitPersistWrites = UMICOM_TRUE; }
    else if (!strcmp(name, "hidden_system")) { CommitRoot(3U)[11] = 0x26U; CommitRebase(); }
    else if (!strcmp(name, "matching_padding")) {
        const UmicomSize padding = UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U - 1U;
        commitVisible[(UmicomSize)COMMIT_PRIMARY * 512U + padding] = 0x73U;
        commitVisible[(UmicomSize)COMMIT_MIRROR * 512U + padding] = 0x73U; CommitRebase();
    } else if (!strcmp(name, "deleted_records")) {
        CommitEntry(CommitRoot(5U), "OLD     BIN", 0x0fU, 4U, 1300U); CommitRoot(5U)[0] = 0xe5U;
        CommitEntry(CommitRoot(7U), "STALE   BIN", 0x20U, 4U, 1300U); CommitRebase();
    } else if (strcmp(name, "single_byte") && strcmp(name, "casefold") && strcmp(name, "idle_intervals")) CHECK(0);
    CommitOpenOwner();
    if (!strcmp(name, "idle_intervals")) model.now += (UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS * 3U;
    CHECK(CommitStageAt(!strcmp(name, "casefold") ? "/frag.bin" : "/FRAG.BIN", offset, bytes) == UMICOM_FAT16_UPDATE_OK);
    CommitAssertStage(offset, bytes);
    if (!strcmp(name, "idle_intervals")) model.now += (UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS * 3U;
    CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK); CommitAssertFinished(offset, bytes);
    CommitTrace(offset, bytes); CommitCloseOwner();
}

static void CommitMakeDirectory(UmicomU16 first, UmicomU16 parent, UmicomSize clusters, UmicomSize files)
{
    for (UmicomSize i = 0U; i < clusters; ++i) {
        const UmicomU16 cluster = (UmicomU16)(first + i);
        CommitFat(cluster, i + 1U == clusters ? 0xffffU : (UmicomU16)(cluster + 1U));
        memset(CommitCluster(cluster), 0, 512U);
    }
    CommitEntry(CommitCluster(first), ".          ", 0x10U, first, 0U);
    CommitEntry(CommitCluster(first) + 32U, "..         ", 0x10U, parent, 0U);
    CHECK(files + 2U <= clusters * 16U);
    for (UmicomSize i = 0U; i < files; ++i) {
        char name[12]; CHECK(snprintf(name, sizeof(name), "F%07uTXT", (unsigned)i) == 11);
        const UmicomSize entry = i + 2U;
        CommitEntry(CommitCluster((UmicomU16)(first + entry / 16U)) + (entry % 16U) * 32U, name, 0x20U, 0U, 0U);
    }
}
static UmicomKernelDiskStatus CommitCorrupt(const char *name)
{
    UmicomKernelDiskStatus expected = UMICOM_DISK_CORRUPT;
    if (!strcmp(name, "file_crosslink")) {
        CommitPut16(CommitRoot(1U) + 26U, 4U); CommitPut32(CommitRoot(1U) + 28U, 1300U);
    } else if (!strcmp(name, "directory_crosslink")) CommitFat(3U, 4U);
    else if (!strcmp(name, "shared_tail")) { CommitFat(2U, 9U); CommitPut32(CommitRoot(1U) + 28U, 1300U); }
    else if (!strcmp(name, "orphan_to_target")) CommitFat(20U, 9U);
    else if (!strcmp(name, "orphan_allocation")) CommitFat(20U, 0xffffU);
    else if (!strcmp(name, "allocated_empty")) { CommitPut16(CommitRoot(4U) + 26U, 20U); CommitFat(20U, 0xffffU); }
    else if (!strcmp(name, "directory_cycle")) CommitEntry(CommitCluster(3U) + 96U, "LOOP       ", 0x10U, 3U, 0U);
    else if (!strcmp(name, "wrong_parent")) CommitPut16(CommitCluster(3U) + 32U + 26U, 2U);
    else if (!strcmp(name, "missing_dot")) CommitCluster(3U)[0] = 0xe5U;
    else if (!strcmp(name, "missing_parent")) CommitCluster(3U)[32U] = 0xe5U;
    else if (!strcmp(name, "root_label_cluster")) CommitPut16(CommitRoot(0U) + 26U, 4U);
    else if (!strcmp(name, "nested_label")) CommitEntry(CommitCluster(3U) + 96U, "LABEL      ", 0x08U, 0U, 0U);
    else if (!strcmp(name, "live_lfn")) {
        memset(CommitRoot(5U), 0x41, 32U); CommitRoot(5U)[11U] = 0x0fU; expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    } else if (!strcmp(name, "bad_cluster")) { CommitFat(20U, 0xfff7U); expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM; }
    else if (!strcmp(name, "free_link")) CommitFat(9U, 0U);
    else if (!strcmp(name, "reserved_link")) CommitFat(9U, 0xfff0U);
    else if (!strcmp(name, "target_cycle")) { CommitFat(6U, 4U); expected = UMICOM_DISK_CHAIN_CYCLE; }
    else if (!strcmp(name, "short_chain")) CommitFat(4U, 0xffffU);
    else if (!strcmp(name, "surplus_chain")) { CommitFat(6U, 10U); CommitFat(10U, 0xffffU); }
    else if (!strcmp(name, "dirty")) { CommitFat(1U, 0x7fffU); expected = UMICOM_DISK_DIRTY; }
    else if (!strcmp(name, "hard_error")) { CommitFat(1U, 0xbfffU); expected = UMICOM_DISK_DIRTY; }
    else if (!strcmp(name, "padding_mismatch")) {
        commitVisible[(UmicomSize)COMMIT_MIRROR * 512U - 1U] ^= 0x17U; expected = UMICOM_DISK_FAT_MISMATCH;
    } else if (!strcmp(name, "remote_fat_mismatch")) {
        commitVisible[((UmicomSize)COMMIT_PRIMARY + 40U) * 512U] ^= 0x53U; expected = UMICOM_DISK_FAT_MISMATCH;
    } else if (!strcmp(name, "unknown_attribute")) CommitRoot(3U)[11U] |= 0x40U;
    else if (!strcmp(name, "archive_clear")) { CommitRoot(3U)[11U] = 0U; expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM; }
    else if (!strcmp(name, "readonly")) { CommitRoot(3U)[11U] |= 0x01U; expected = UMICOM_DISK_READ_ONLY; }
    else if (!strcmp(name, "duplicate_name")) CommitEntry(CommitRoot(5U), "FRAG    BIN", 0x20U, 0U, 0U);
    else if (!strcmp(name, "directory_limit")) {
        for (UmicomSize i = 0U; i < 63U; ++i) {
            char shortName[12]; CHECK(snprintf(shortName, sizeof(shortName), "D%07u   ", (unsigned)i) == 11);
            const UmicomU16 cluster = (UmicomU16)(20U + i);
            CommitEntry(CommitRoot(5U + i), shortName, 0x10U, cluster, 0U); CommitMakeDirectory(cluster, 0U, 1U, 0U);
        }
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "object_limit")) {
        CommitEntry(CommitRoot(5U), "ONE        ", 0x10U, 20U, 0U);
        CommitEntry(CommitRoot(6U), "TWO        ", 0x10U, 30U, 0U);
        CommitMakeDirectory(20U, 0U, 8U, 126U); CommitMakeDirectory(30U, 0U, 8U, 126U); expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "depth_limit")) {
        CommitEntry(CommitRoot(5U), "DEEP       ", 0x10U, 20U, 0U);
        for (UmicomU16 i = 0U; i < 9U; ++i) {
            const UmicomU16 cluster = (UmicomU16)(20U + i);
            CommitMakeDirectory(cluster, i ? (UmicomU16)(cluster - 1U) : 0U, 1U, 0U);
            if (i < 8U) CommitEntry(CommitCluster(cluster) + 64U, "DEEP       ", 0x10U, (UmicomU16)(cluster + 1U), 0U);
        }
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "chain_limit")) {
        CommitFat(4U, 20U); CommitFat(9U, 0U); CommitFat(6U, 0U);
        for (UmicomU16 i = 20U; i < 276U; ++i) CommitFat(i, i == 275U ? 0xffffU : (UmicomU16)(i + 1U));
        CommitPut32(CommitRoot(3U) + 28U, 257U * 512U); expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "io_budget")) {
        for (UmicomSize i = 0U; i < 123U; ++i) {
            char shortName[12]; CHECK(snprintf(shortName, sizeof(shortName), "F%07uBIN", (unsigned)i) == 11);
            CommitEntry(CommitRoot(5U + i), shortName, 0x20U, (UmicomU16)(100U + i), 20U * 512U);
            for (UmicomSize j = 0U; j < 20U; ++j) {
                const UmicomU16 cluster = (UmicomU16)(100U + i + j * 256U);
                CommitFat(cluster, j == 19U ? 0xffffU : (UmicomU16)(cluster + 256U));
            }
        }
        expected = UMICOM_DISK_LIMIT;
    } else CHECK(0);
    CommitRebase(); return expected;
}
static void CommitFormat(const char *name)
{
    const UmicomKernelDiskStatus expected = CommitCorrupt(name);
    UmicomKernelFat16UpdateStatus status = UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        status = CommitStageAt("/FRAG.BIN", 0U, 1U); CHECK(status != UMICOM_FAT16_UPDATE_OK);
        CHECK(committer.state == UMICOM_FAT16_COMMIT_READY && commitResult.status == status);
        CHECK(!commitResult.mediaTouched && !commitResult.submittedMetadataSectors && !commitResult.submittedDataSectors);
        CHECK(!commitResult.confirmedBytes && !commitResult.submittedBytes && !commitResult.completedFlushes);
        CHECK(!commitResult.commitAccepted && !commitResult.writeUncertain && !commitResult.needsFlush);
        if (!strcmp(name, "io_budget")) {
            CHECK(commitResult.diskStatus == UMICOM_DISK_IO_ERROR && commitResult.blockStatus == UMICOM_BLOCK_TIMEOUT);
            CHECK(committer.updater.operationReads == UMICOM_FAT16_IO_LIMIT);
        } else CHECK(commitResult.diskStatus == expected);
    } else CHECK(committer.updater.lastDiskStatus == expected);
    CommitUnchanged(); CommitCloseOwner();
}
static void CommitFailed(void)
{
    CHECK(committer.state == UMICOM_FAT16_COMMIT_FAILED && commitResult.status != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitResult.mediaTouched && !commitResult.commitAccepted);
    CHECK(!committer.busy && !committer.updater.busy && !committer.updater.volume.open);
    CommitEqual(&committer.lastResult, &commitResult, sizeof(commitResult));
    const UmicomKernelFat16CommitResult history = committer.lastResult;
    const UmicomU32 events = commitEventCount;
    CHECK(CommitStageAt("/FRAG.BIN", 0U, 1U) != UMICOM_FAT16_UPDATE_OK);
    CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
    CHECK(CommitFinishOwner() != UMICOM_FAT16_UPDATE_OK);
    CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
    CommitEqual(&history, &committer.lastResult, sizeof(history)); CHECK(commitEventCount == events);
    memcpy(&commitResult, &history, sizeof(commitResult));
}
static UmicomU32 CommitWriteOrdinal(const char *name)
{
    if (!strcmp(name, "dirty_mirror")) return 1U;
    if (!strcmp(name, "dirty_primary")) return 2U;
    if (!strcmp(name, "data_first")) return 3U;
    if (!strcmp(name, "data_middle")) return 4U;
    if (!strcmp(name, "data_last")) return 5U;
    if (!strcmp(name, "clean_mirror")) return 6U;
    if (!strcmp(name, "clean_primary")) return 7U;
    CHECK(0); return 0U;
}
static UmicomKernelFat16CommitPhase CommitWritePhase(UmicomU32 ordinal)
{
    static const UmicomKernelFat16CommitPhase phases[] = {UMICOM_FAT16_COMMIT_NONE,
        UMICOM_FAT16_COMMIT_DIRTY_MIRROR, UMICOM_FAT16_COMMIT_DIRTY_PRIMARY,
        UMICOM_FAT16_COMMIT_DATA_WRITE, UMICOM_FAT16_COMMIT_DATA_WRITE, UMICOM_FAT16_COMMIT_DATA_WRITE,
        UMICOM_FAT16_COMMIT_CLEAN_MIRROR, UMICOM_FAT16_COMMIT_CLEAN_PRIMARY};
    CHECK(ordinal > 0U && ordinal < sizeof(phases) / sizeof(phases[0])); return phases[ordinal];
}
static void CommitWriteFailure(const char *name)
{
    const UmicomU32 failed = CommitWriteOrdinal(name);
    CommitOpenOwner(); commitFailWrite = failed; commitPartialWrite = UMICOM_TRUE;
    if (failed > 5U) { CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U); }
    CHECK((failed > 5U ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitWrites == failed && commitResult.phase == CommitWritePhase(failed));
    CHECK(commitResult.blockStatus == UMICOM_BLOCK_IO_ERROR && commitResult.lastBlockOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    CHECK(commitResult.writeUncertain && commitResult.needsFlush && commitResult.uncertainSectorValid);
    const UmicomSize dataBefore = failed <= 3U ? 0U : (failed <= 5U ? failed - 3U : 3U);
    const UmicomSize dataSubmitted = failed <= 2U ? 0U : (failed <= 5U ? failed - 2U : 3U);
    static const UmicomSize bytes[] = {0U, 1U, 513U, 700U};
    CHECK(commitResult.completedDataSectors == dataBefore && commitResult.submittedDataSectors == dataSubmitted);
    CHECK(commitResult.confirmedBytes == bytes[dataBefore] && commitResult.submittedBytes == bytes[dataSubmitted]);
    const UmicomSize metadataBefore = failed <= 2U ? failed - 1U : (failed == 7U ? 3U : 2U);
    CHECK(commitResult.completedMetadataSectors == metadataBefore);
    CHECK(commitResult.submittedMetadataSectors == metadataBefore + (failed <= 2U || failed >= 6U ? 1U : 0U));
    UmicomU32 written = 0U; UmicomU64 sector = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i)
        if (commitEvents[i].command == UMICOM_VIRTIO_REQUEST_WRITE && ++written == failed) sector = commitEvents[i].sector;
    CHECK(commitResult.uncertainSector == sector);
    CHECK(commitResult.cleanFinalisationStarted == (failed >= 6U ? UMICOM_TRUE : UMICOM_FALSE));
    CommitFailed(); CommitCloseOwner();
}
static void CommitFlushFailure(const char *name)
{
    UmicomU32 failed = 0U; UmicomKernelFat16CommitPhase phase = UMICOM_FAT16_COMMIT_NONE;
    if (!strcmp(name, "dirty_mirror")) { failed = 1U; phase = UMICOM_FAT16_COMMIT_DIRTY_MIRROR_FLUSH; }
    else if (!strcmp(name, "dirty_primary")) { failed = 2U; phase = UMICOM_FAT16_COMMIT_DIRTY_PRIMARY_FLUSH; }
    else if (!strcmp(name, "data")) { failed = 3U; phase = UMICOM_FAT16_COMMIT_DATA_FLUSH; }
    else if (!strcmp(name, "clean_mirror")) { failed = 4U; phase = UMICOM_FAT16_COMMIT_CLEAN_MIRROR_FLUSH; }
    else if (!strcmp(name, "clean_primary")) { failed = 5U; phase = UMICOM_FAT16_COMMIT_CLEAN_PRIMARY_FLUSH; }
    else CHECK(0);
    CommitOpenOwner(); commitFailFlush = failed;
    if (failed > 3U) { CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U); }
    CHECK((failed > 3U ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitFlushes == failed && commitResult.completedFlushes == failed - 1U && commitResult.phase == phase);
    CHECK(commitResult.blockStatus == UMICOM_BLOCK_IO_ERROR && commitResult.lastBlockOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    CHECK(commitResult.needsFlush && !commitResult.uncertainSectorValid);
    CHECK(commitResult.cleanFinalisationStarted == (failed > 3U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(commitResult.submittedDataSectors == (failed < 3U ? 0U : 3U));
    CommitFailed(); CommitCloseOwner();
}
static void CommitReadFailure(const char *name, UmicomBoolean corrupt)
{
    commitReadFaultCorrupt = corrupt; commitReadFaultOperation = 1U;
    UmicomKernelFat16CommitPhase phase = UMICOM_FAT16_COMMIT_NONE;
    if (!strcmp(name, "dirty_primary")) { commitReadFaultMutation = 4U; commitReadFaultOrdinal = 1U; phase = UMICOM_FAT16_COMMIT_DIRTY_VERIFY; }
    else if (!strcmp(name, "dirty_mirror")) { commitReadFaultMutation = 4U; commitReadFaultOrdinal = 2U; phase = UMICOM_FAT16_COMMIT_DIRTY_VERIFY; }
    else if (!strncmp(name, "data_", 5U)) {
        commitReadFaultMutation = 8U; phase = UMICOM_FAT16_COMMIT_DATA_VERIFY;
        commitReadFaultOrdinal = !strcmp(name, "data_first") ? 1U : (!strcmp(name, "data_middle") ? 2U : 3U);
    } else if (!strncmp(name, "finish_", 7U)) {
        commitReadFaultOperation = 2U; commitReadFaultMutation = 8U; phase = UMICOM_FAT16_COMMIT_FINISH_VERIFY;
        commitReadFaultOrdinal = !strcmp(name, "finish_primary") ? 1U : (!strcmp(name, "finish_mirror") ? 2U :
            (!strcmp(name, "finish_first") ? 3U : (!strcmp(name, "finish_middle") ? 4U : 5U)));
    } else if (!strcmp(name, "clean_primary") || !strcmp(name, "clean_mirror")) {
        commitReadFaultOperation = 2U; commitReadFaultMutation = 12U; phase = UMICOM_FAT16_COMMIT_CLEAN_VERIFY;
        commitReadFaultOrdinal = !strcmp(name, "clean_primary") ? 1U : 2U;
    } else CHECK(0);
    CommitOpenOwner();
    if (commitReadFaultOperation == 2U) {
        CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    }
    CHECK((commitReadFaultOperation == 2U ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitReadFaultSeen == commitReadFaultOrdinal && commitResult.phase == phase);
    CHECK(commitResult.diskStatus == (corrupt ? UMICOM_DISK_CORRUPT : UMICOM_DISK_IO_ERROR));
    CHECK(commitResult.blockStatus == (corrupt ? UMICOM_BLOCK_OK : UMICOM_BLOCK_IO_ERROR));
    CHECK(!commitResult.writeUncertain && !commitResult.uncertainSectorValid);
    CHECK(commitResult.cleanFinalisationStarted == (commitReadFaultMutation == 12U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(commitResult.cleanDurable == (commitReadFaultMutation == 12U ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(!commitResult.cleanVerified); CommitFailed(); CommitCloseOwner();
}
static void CommitDroppedWrite(const char *name, UmicomBoolean altered)
{
    const UmicomU32 selected = CommitWriteOrdinal(name);
    CommitOpenOwner(); if (altered) commitAlterWrite = selected; else commitDropWrite = selected;
    if (selected >= 6U) {
        CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    }
    CHECK((selected >= 6U ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(commitResult.diskStatus == UMICOM_DISK_CORRUPT && commitResult.blockStatus == UMICOM_BLOCK_OK);
    CHECK(!commitResult.writeUncertain && !commitResult.uncertainSectorValid);
    if (selected <= 2U) CHECK(!commitDataWrites && !commitResult.dirtyVerified);
    else if (selected <= 5U) CHECK(!commitResult.dataVerified && !commitResult.cleanFinalisationStarted);
    else CHECK(commitResult.cleanDurable && !commitResult.cleanVerified);
    CommitFailed(); CommitCloseOwner();
}
static UmicomBoolean CommitDirectRead(void *context, UmicomU64 sector, UmicomU8 *out)
{
    CHECK(context == commitVisible && sector < UMICOM_DISK_FIXTURE_SECTORS && out);
    memcpy(out, commitVisible + (UmicomSize)sector * 512U, 512U); return UMICOM_TRUE;
}
static void CommitFreshReader(UmicomBoolean clean)
{
    static UmicomKernelFat16 volume;
    memset(&volume, 0, sizeof(volume));
    const UmicomKernelDiskReader reader = {UMICOM_DISK_FIXTURE_SECTORS, CommitDirectRead, commitVisible};
    const UmicomKernelDiskStatus status = UmicomKernelFat16Open(&volume, &reader, 0U);
    if (clean) {
        CHECK(status == UMICOM_DISK_OK);
        UmicomU8 bytes[1300]; UmicomSize read = 0U;
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 0U, bytes, sizeof(bytes), &read) == UMICOM_DISK_OK && read == sizeof(bytes));
        for (UmicomSize i = 0U; i < sizeof(bytes); ++i) CHECK(bytes[i] == commitVisible[CommitFileAddress(i)]);
        CHECK(UmicomKernelFat16Close(&volume) == UMICOM_DISK_OK);
    } else CHECK(status == UMICOM_DISK_DIRTY || status == UMICOM_DISK_FAT_MISMATCH);
}
static void CommitReplay(UmicomBoolean durable)
{
    UmicomSize stop = commitEventCount;
    if (durable && !commitPersistWrites) {
        stop = 0U;
        for (UmicomSize i = 0U; i < commitEventCount; ++i)
            if (commitEvents[i].command == UMICOM_VIRTIO_REQUEST_FLUSH && commitEvents[i].completed) stop = i;
    }
    memcpy(commitExpected, commitInitial, sizeof(commitExpected));
    for (UmicomSize i = 0U; i < stop; ++i)
        if (commitEvents[i].command == UMICOM_VIRTIO_REQUEST_WRITE && commitEvents[i].completed)
            memcpy(commitExpected + (UmicomSize)commitEvents[i].sector * 512U, commitEvents[i].data, 512U);
}
static void CommitCut(const char *name)
{
    const UmicomBoolean eager = !strncmp(name, "eager.", 6U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(eager || !strncmp(name, "cached.", 7U));
    const unsigned long parsed = strtoul(name + (eager ? 6U : 7U), 0, 10);
    CHECK(parsed >= 1U && parsed <= 12U);
    commitPersistWrites = eager; commitCutMutation = (UmicomU32)parsed;
    CommitOpenOwner();
    if (commitCutMutation > 8U) {
        CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    }
    CHECK((commitCutMutation > 8U ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) != UMICOM_FAT16_UPDATE_OK);
    CHECK(commitMutations == commitCutMutation && !model.allowed); CommitFailed();
    CommitReplay(UMICOM_FALSE); CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitReplay(UMICOM_TRUE); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    model.allowed = UMICOM_TRUE; CommitCloseOwner();
    memcpy(commitVisible, commitDurable, sizeof(commitVisible));
    const UmicomBoolean clean = (CommitFlags(commitVisible, COMMIT_PRIMARY) & UMICOM_FAT16_CLEAN_MASK) &&
        (CommitFlags(commitVisible, COMMIT_MIRROR) & UMICOM_FAT16_CLEAN_MASK) ? UMICOM_TRUE : UMICOM_FALSE;
    if (clean) {
        memcpy(commitExpected, commitInitial, sizeof(commitExpected));
        if (commitCutMutation > 4U) CommitPatchExpected(511U, 700U);
        CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    } else CHECK(commitCutMutation >= (eager ? 1U : 2U));
    CommitFreshReader(clean);
}

static void CommitOwnership(const char *name, UmicomBoolean finish)
{
    CommitOpenOwner();
    if (finish) { CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U); }
    const UmicomKernelFat16CommitResult history = committer.lastResult;
    memset(&commitResult, 0xa5, sizeof(commitResult)); memcpy(&commitResultBefore, &commitResult, sizeof(commitResult));
    const UmicomU32 events = commitEventCount;
    UmicomKernelFat16Committer *owner = &committer;
    UmicomKernelFat16CommitResult *output = &commitResult;
    const char *path = "/FRAG.BIN"; const void *input = commitInput; UmicomSize bytes = 1U;
    UmicomAddress extra = 0U;
    static UmicomKernelFat16Committer copied;
    _Alignas(UmicomKernelFat16CommitResult) UmicomU8 unaligned[sizeof(UmicomKernelFat16CommitResult) + 8U];
    memset(unaligned, 0x49, sizeof(unaligned));
    char longPath[UMICOM_FAT16_PATH_BYTES]; memset(longPath, 'A', sizeof(longPath));
    if (!strcmp(name, "result_owner")) output = &committer.lastResult;
    else if (!strcmp(name, "result_outer_header")) output = (UmicomKernelFat16CommitResult *)(void *)committer.cleanHeader;
    else if (!strcmp(name, "result_domain")) output = (UmicomKernelFat16CommitResult *)(void *)&domain;
    else if (!strcmp(name, "result_dma")) output = (UmicomKernelFat16CommitResult *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "result_queue")) output = (UmicomKernelFat16CommitResult *)domain.slots[0].queueFrame;
    else if (!strcmp(name, "result_alignment")) output = (UmicomKernelFat16CommitResult *)(void *)(unaligned + 1U);
    else if (!strcmp(name, "result_overflow"))
        output = (UmicomKernelFat16CommitResult *)(~(UmicomAddress)0U & ~((UmicomAddress)alignof(UmicomKernelFat16CommitResult) - 1U));
    else if (!strcmp(name, "result_null")) output = 0;
    else if (!strcmp(name, "input_owner")) input = &committer.updater;
    else if (!strcmp(name, "input_outer_header")) input = committer.dirtyHeader;
    else if (!strcmp(name, "input_domain")) input = &domain;
    else if (!strcmp(name, "input_dma")) input = (const void *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "input_queue")) input = (const void *)domain.slots[0].queueFrame;
    else if (!strcmp(name, "input_result")) input = &commitResult;
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
    } else if (!strcmp(name, "path_result_nonterminated")) path = (const char *)&commitResult;
    else if (!strcmp(name, "path_outer_header")) path = (const char *)committer.cleanHeader;
    else if (!strcmp(name, "path_domain")) path = (const char *)&domain;
    else if (!strcmp(name, "path_dma")) path = (const char *)domain.slots[0].dataFrame;
    else if (!strcmp(name, "path_unterminated")) path = longPath;
    else if (!strcmp(name, "path_null")) path = 0;
    else if (!strcmp(name, "other_slot_dma")) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&extra) == UMICOM_KERNEL_MEMORY_OK);
        domain.count = 2U; domain.slots[1].dataFrame = extra;
        if (finish) output = (UmicomKernelFat16CommitResult *)extra; else input = (const void *)extra;
    } else if (!strcmp(name, "owner_alignment")) owner = (UmicomKernelFat16Committer *)(void *)((UmicomU8 *)&committer + 1U);
    else if (!strcmp(name, "owner_null")) owner = 0;
    else if (!strcmp(name, "copied_owner")) { memcpy(&copied, &committer, sizeof(copied)); owner = &copied; }
    else if (!strcmp(name, "busy_owner")) committer.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_embedded")) committer.updater.busy = UMICOM_TRUE;
    else if (!strcmp(name, "busy_domain")) domain.busy = UMICOM_TRUE;
    else if (!strcmp(name, "unsafe_context")) model.allowed = UMICOM_FALSE;
    else CHECK(0);
    const UmicomKernelFat16UpdateStatus status = finish ? UmicomKernelFat16CommitFinish(owner, output) :
        UmicomKernelFat16CommitStage(owner, path, 0U, input, bytes, output);
    CHECK(status != UMICOM_FAT16_UPDATE_OK);
    if (!strncmp(name, "busy_", 5U)) CHECK(status == UMICOM_FAT16_UPDATE_BUSY);
    else if (!strcmp(name, "unsafe_context")) CHECK(status == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
    else if (!strcmp(name, "copied_owner")) {
        CHECK(status == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(UmicomKernelFat16CommitClose(&copied) == UMICOM_FAT16_UPDATE_BAD_STATE);
    } else CHECK(status == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    committer.busy = UMICOM_FALSE; committer.updater.busy = UMICOM_FALSE; domain.busy = UMICOM_FALSE; model.allowed = UMICOM_TRUE;
    if (extra) {
        domain.count = 1U; domain.slots[1].dataFrame = 0U;
        CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(extra) == UMICOM_KERNEL_MEMORY_OK);
    }
    CHECK(commitEventCount == events); CommitEqual(&history, &committer.lastResult, sizeof(history));
    CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
    for (UmicomSize i = 0U; i < sizeof(unaligned); ++i) CHECK(unaligned[i] == 0x49U);
    if (!finish) CommitUnchanged();
    else { CommitEqual(commitVisible, commitExpected, sizeof(commitVisible)); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable)); }
    CommitCloseOwner();
}
static void CommitOpenArguments(const char *name)
{
    UmicomKernelFat16Committer *owner = &committer;
    UmicomSize slot = 0U, partition = 0U; UmicomU64 timeout = 32U;
    UmicomAddress frame = 0U;
    const UmicomKernelBlockDomain before = domain;
    if (!strcmp(name, "domain_alias")) owner = (UmicomKernelFat16Committer *)(void *)&domain;
    else if (!strcmp(name, "dma_alias")) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&frame) == UMICOM_KERNEL_MEMORY_OK);
        domain.slots[0].dataFrame = frame; owner = (UmicomKernelFat16Committer *)frame;
    } else if (!strcmp(name, "alignment")) owner = (UmicomKernelFat16Committer *)(void *)((UmicomU8 *)&committer + 1U);
    else if (!strcmp(name, "null")) owner = 0;
    else if (!strcmp(name, "slot")) slot = UMICOM_BLOCK_SLOT_LIMIT;
    else if (!strcmp(name, "partition")) partition = UMICOM_DISK_PRIMARY_PARTITIONS;
    else if (!strcmp(name, "timeout_zero")) timeout = 0U;
    else if (!strcmp(name, "timeout_excess")) timeout = (UmicomU64)UMICOM_BLOCK_MAX_TIMEOUT_TICKS + 1U;
    else if (!strcmp(name, "nonzero_storage")) committer.cleanHeader[0] = 1U;
    else CHECK(0);
    CHECK(UmicomKernelFat16CommitOpen(owner, &domain, slot, partition, timeout) != UMICOM_FAT16_UPDATE_OK);
    if (frame) { domain.slots[0].dataFrame = 0U; CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(frame) == UMICOM_KERNEL_MEMORY_OK); }
    CommitEqual(&before, &domain, sizeof(domain));
    CHECK(!model.reads && !model.writes && !committer.updater.handle && !Allocated()); CommitUnchanged();
}
static void CommitRange(const char *name)
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
    CommitOpenOwner(); CHECK(CommitStageAt(path, offset, bytes) != UMICOM_FAT16_UPDATE_OK);
    CHECK(committer.state == UMICOM_FAT16_COMMIT_READY && !commitResult.mediaTouched);
    CHECK(!commitResult.submittedBytes && !commitResult.submittedDataSectors && !commitResult.submittedMetadataSectors);
    CHECK(!commitResult.completedFlushes && !commitResult.commitAccepted);
    CommitUnchanged();
    CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK); CommitAssertFinished(511U, 700U); CommitCloseOwner();
}
static void CommitLifecycle(const char *name)
{
    if (!strcmp(name, "zero_close")) {
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_OK);
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_OK); CHECK(!model.notifications); return;
    }
    if (!strcmp(name, "readonly_retry")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_READ_ONLY);
        CHECK(!Allocated() && !committer.updater.admitted); model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    } else if (!strcmp(name, "no_flush_retry")) {
        model.featuresLow &= ~UMICOM_VIRTIO_FEATURE_FLUSH;
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated() && !committer.updater.admitted); model.featuresLow |= UMICOM_VIRTIO_FEATURE_FLUSH;
    } else if (!strcmp(name, "first_allocation_retry") || !strcmp(name, "second_allocation_retry")) {
        model.failAllocate = !strcmp(name, "first_allocation_retry") ? 1U : 2U;
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated() && !committer.updater.admitted); model.failAllocate = 0U;
    } else if (!strcmp(name, "unsafe_open_retry")) {
        model.allowed = UMICOM_FALSE;
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(!model.reads && !model.writes && !Allocated()); model.allowed = UMICOM_TRUE;
    } else if (!strcmp(name, "failed_open_retained")) {
        model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(committer.state == UMICOM_FAT16_COMMIT_CLOSING && committer.updater.handle && Allocated() == 2U);
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls);
        model.stuckReset = UMICOM_FALSE; model.refuseDriver = UMICOM_FALSE; model.stickAfterDriver = UMICOM_FALSE;
        CommitCloseOwner(); CHECK(committer.state == UMICOM_FAT16_COMMIT_UNUSED);
    } else if (!strcmp(name, "inspection_retry")) {
        commitFailRead = 1U;
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated() && !committer.updater.admitted); commitFailRead = 0U;
    }
    CommitOpenOwner();
    if (!strcmp(name, "ready_close")) { CommitUnchanged(); CommitCloseOwner(); return; }
    if (!strcmp(name, "finish_before_stage")) {
        const UmicomKernelFat16CommitResult history = committer.lastResult;
        CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
        CommitEqual(&history, &committer.lastResult, sizeof(history)); CommitUnchanged(); CommitCloseOwner(); return;
    }
    if (!strcmp(name, "timeout_retained")) {
        commitHangMutation = 5U; model.stuckReset = UMICOM_TRUE;
        CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(commitResult.writeUncertain && commitResult.uncertainSectorValid && Allocated() == 2U && !model.freeCalls);
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls && committer.state == UMICOM_FAT16_COMMIT_CLOSING);
        model.stuckReset = UMICOM_FALSE; model.noCompletion = UMICOM_FALSE; CommitCloseOwner(); return;
    }
    CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    if (!strcmp(name, "stage_twice")) {
        const UmicomKernelFat16CommitResult history = committer.lastResult; const UmicomU32 events = commitEventCount;
        CHECK(CommitStageAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
        CommitEqual(&history, &committer.lastResult, sizeof(history)); CHECK(commitEventCount == events);
    } else if (!strcmp(name, "reset_retry") || !strcmp(name, "first_release_retry") || !strcmp(name, "second_release_retry")) {
        const UmicomKernelBlockHandle handle = committer.updater.handle;
        if (!strcmp(name, "reset_retry")) model.stuckReset = UMICOM_TRUE;
        else model.failFree = !strcmp(name, "first_release_retry") ? 1U : 2U;
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(committer.updater.handle == handle && committer.state == UMICOM_FAT16_COMMIT_CLOSING);
        CHECK(Allocated() == (!strcmp(name, "second_release_retry") ? 1U : 2U));
        CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
        model.stuckReset = UMICOM_FALSE; model.failFree = 0U;
    } else if (!strcmp(name, "unsafe_close_retry")) {
        model.allowed = UMICOM_FALSE;
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(Allocated() == 2U && committer.updater.handle); model.allowed = UMICOM_TRUE;
    } else if (!strcmp(name, "single_lifetime") || !strcmp(name, "finish_twice")) {
        CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK); CommitAssertFinished(511U, 700U);
        if (!strcmp(name, "finish_twice")) {
            const UmicomKernelFat16CommitResult history = committer.lastResult;
            CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_BAD_STATE);
            CommitEqual(&commitResult, &commitResultBefore, sizeof(commitResult));
            CommitEqual(&history, &committer.lastResult, sizeof(history));
        }
    }
    CommitCloseOwner();
    if (!strcmp(name, "single_lifetime")) {
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(UmicomKernelFat16CommitClose(&committer) == UMICOM_FAT16_UPDATE_OK);
    } else if (!strcmp(name, "staged_close")) {
        memcpy(commitVisible, commitDurable, sizeof(commitVisible)); CommitFreshReader(UMICOM_FALSE);
    }
}

static void CommitMutationClock(const char *name)
{
    commitClockInside = !strncmp(name, "inner_", 6U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(commitClockInside || !strncmp(name, "outer_", 6U));
    commitClockRollback = strstr(name, "rollback.") ? UMICOM_TRUE : UMICOM_FALSE;
    const char *const number = strrchr(name, '.'); CHECK(number);
    const unsigned long parsed = strtoul(number + 1U, 0, 10); CHECK(parsed >= 1U && parsed <= 12U);
    commitClockMutation = (UmicomU32)parsed;
    CommitOpenOwner();
    if (commitClockMutation > 8U) {
        CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    }
    CHECK((commitClockMutation > 8U ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(commitClockInjected && commitMutations == commitClockMutation);
    CHECK(commitResult.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    CHECK(commitResult.lastBlockOutcome == (commitClockInside ? UMICOM_BLOCK_SUBMITTED_UNCONFIRMED : UMICOM_BLOCK_COMPLETED));
    const CommitEvent *last = 0;
    for (UmicomSize i = 0U; i < commitEventCount; ++i)
        if (commitEvents[i].command != UMICOM_VIRTIO_REQUEST_READ) last = &commitEvents[i];
    CHECK(last && last->completed);
    const UmicomBoolean write = last->command == UMICOM_VIRTIO_REQUEST_WRITE ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(commitResult.uncertainSectorValid == (commitClockInside && write ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(commitResult.writeUncertain == (commitClockInside && write ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(commitResult.completedMetadataSectors + commitResult.completedDataSectors ==
        commitWriteCompletions - (commitClockInside && write ? 1U : 0U));
    CHECK(commitResult.completedFlushes == commitFlushCompletions - (commitClockInside && !write ? 1U : 0U));
    if (!commitClockInside && commitClockMutation == 4U) CHECK(commitResult.dirtyDurable);
    if (!commitClockInside && commitClockMutation == 8U) CHECK(commitResult.dataDurable);
    if (!commitClockInside && commitClockMutation == 12U) CHECK(commitResult.cleanDurable && !commitResult.cleanVerified);
    CommitFailed(); CommitCloseOwner();
}
static void CommitPreflightFailure(const char *fixture, const char *name)
{
    CommitOpenOwner(); CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
    UmicomU32 lastRead = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i) {
        if (commitEvents[i].command != UMICOM_VIRTIO_REQUEST_READ) break;
        ++lastRead;
    }
    CHECK(lastRead > 4U); CommitCloseOwner(); CommitStart(fixture); CommitOpenOwner();
    const UmicomBoolean clock = strstr(name, "clock_") ? UMICOM_TRUE : UMICOM_FALSE;
    if (clock) { commitClockRead = lastRead; commitClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE; }
    else if (!strcmp(name, "header_mismatch")) commitCorruptRead = lastRead;
    else commitFailRead = !strcmp(name, "first_read") ? commitReads + 1U : lastRead;
    CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) != UMICOM_FAT16_UPDATE_OK);
    CHECK(committer.state == UMICOM_FAT16_COMMIT_READY && commitResult.phase == UMICOM_FAT16_COMMIT_PREFLIGHT);
    CHECK(!commitResult.mediaTouched && !commitResult.completedFlushes && !commitResult.submittedMetadataSectors);
    CHECK(!commitResult.submittedDataSectors && !commitResult.writeUncertain && !commitResult.needsFlush);
    if (clock) {
        CHECK(commitClockInjected && commitResult.diskStatus == UMICOM_DISK_IO_ERROR);
        CHECK(commitResult.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    } else if (!strcmp(name, "header_mismatch")) CHECK(commitResult.diskStatus == UMICOM_DISK_CORRUPT);
    else CHECK(commitResult.diskStatus == UMICOM_DISK_IO_ERROR && commitResult.blockStatus == UMICOM_BLOCK_IO_ERROR);
    CommitUnchanged(); CommitCloseOwner();
}
static void CommitAcceptanceClock(const char *fixture, const char *name)
{
    const UmicomBoolean finish = !strncmp(name, "finish_", 7U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(finish || !strncmp(name, "stage_", 6U));
    CommitOpenOwner(); CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 stageRead = commitReads;
    CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK); const UmicomU32 finishRead = commitReads;
    CommitCloseOwner(); CommitStart(fixture); CommitOpenOwner();
    commitClockRead = finish ? finishRead : stageRead; commitClockSkip = 1U;
    commitClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    if (finish) { CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U); }
    CHECK((finish ? CommitFinishOwner() : CommitStageAt("/FRAG.BIN", 511U, 700U)) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(commitClockInjected && !commitClockSkip && !commitResult.commitAccepted);
    CHECK(commitResult.dataDurable && commitResult.dataVerified && commitResult.dirtyDurable && commitResult.dirtyVerified);
    CHECK(commitResult.cleanVerified == finish && commitResult.cleanDurable == finish);
    CHECK(!commitResult.needsFlush && !commitResult.writeUncertain && !commitResult.uncertainSectorValid);
    CHECK(commitResult.lastBlockOutcome == UMICOM_BLOCK_COMPLETED);
    CHECK(commitResult.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    CommitFailed(); CommitCloseOwner();
}
static void CommitChangedBeforeFinish(const char *name)
{
    CommitOpenOwner(); CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
    if (!strcmp(name, "primary_header")) commitVisible[(UmicomSize)COMMIT_PRIMARY * 512U + 123U] ^= 0x53U;
    else if (!strcmp(name, "mirror_header")) commitVisible[(UmicomSize)COMMIT_MIRROR * 512U + 345U] ^= 0x53U;
    else if (!strcmp(name, "first_neighbour")) commitVisible[CommitFileAddress(0U)] ^= 0x53U;
    else if (!strcmp(name, "middle_data")) commitVisible[CommitFileAddress(600U)] ^= 0x53U;
    else if (!strcmp(name, "last_slack")) commitVisible[CommitFileAddress(1500U)] ^= 0x53U;
    else CHECK(0);
    CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(commitResult.phase == UMICOM_FAT16_COMMIT_FINISH_VERIFY && commitResult.diskStatus == UMICOM_DISK_CORRUPT);
    CHECK(commitMutations == 8U && !commitResult.cleanFinalisationStarted && !commitResult.cleanDurable);
    /* Existing positive fields remain historical observations from Stage. */
    CHECK(commitResult.dataVerified && commitResult.dirtyVerified); CommitFailed(); CommitCloseOwner();
}
static void CommitMisc(const char *name)
{
    if (!strcmp(name, "status_names")) {
        for (unsigned i = 0U; i <= (unsigned)UMICOM_FAT16_COMMIT_CLOSED; ++i)
            CHECK(strcmp(UmicomKernelFat16CommitStateName((UmicomKernelFat16CommitState)i), "unknown-fat16-commit-state"));
        for (unsigned i = 0U; i <= (unsigned)UMICOM_FAT16_COMMIT_COMPLETE; ++i)
            CHECK(strcmp(UmicomKernelFat16CommitPhaseName((UmicomKernelFat16CommitPhase)i), "unknown-fat16-commit-phase"));
        CHECK(!strcmp(UmicomKernelFat16CommitStateName((UmicomKernelFat16CommitState)999), "unknown-fat16-commit-state"));
        CHECK(!strcmp(UmicomKernelFat16CommitPhaseName((UmicomKernelFat16CommitPhase)999), "unknown-fat16-commit-phase")); return;
    }
    if (!strcmp(name, "readonly_vfs_boundary")) {
        static UmicomKernelDiskMount mount; static UmicomKernelVfsClient client;
        memset(&mount, 0, sizeof(mount)); memset(&client, 0, sizeof(client));
        CHECK(UmicomKernelDiskMountOpen(&mount, &domain, 0U, 0U, 32U) == UMICOM_VFS_READ_ONLY);
        CHECK(!mount.handle && !Allocated()); model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        model.expectedDriverLow = UMICOM_VIRTIO_READ_ONLY;
        CHECK(UmicomKernelDiskMountOpen(&mount, &domain, 0U, 0U, 32U) == UMICOM_VFS_OK);
        CHECK(UmicomKernelDiskMountClientOpen(&mount, &client, 7U, UMICOM_VFS_RIGHT_WRITE) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(UmicomKernelFat16CommitOpen(&committer, &domain, 0U, 0U, 32U) != UMICOM_FAT16_UPDATE_OK);
        CHECK(mount.handle && Allocated() == 2U && !committer.updater.handle);
        CHECK(UmicomKernelDiskMountClose(&mount) == UMICOM_VFS_OK); CommitUnchanged(); return;
    }
    if (!strcmp(name, "callback_reentry") || !strcmp(name, "policy_reentry")) {
        commitCallbackReenter = !strcmp(name, "callback_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        commitPolicyReenter = !strcmp(name, "policy_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        CommitOpenOwner(); CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
        CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK); CommitAssertFinished(511U, 700U);
        CHECK(commitReentries > 0U); CommitCloseOwner(); return;
    }
    CommitOpenOwner();
    if (!strcmp(name, "stopped_clock") || !strcmp(name, "backward_clock")) {
        if (!strcmp(name, "stopped_clock")) { commitStoppedClock = UMICOM_TRUE; model.noCompletion = UMICOM_TRUE; }
        else commitBackwardClock = UMICOM_TRUE;
        CHECK(CommitStageAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(commitResult.blockStatus == (!strcmp(name, "stopped_clock") ? UMICOM_BLOCK_TIMEOUT : UMICOM_BLOCK_CLOCK_ERROR));
        CHECK(committer.state == UMICOM_FAT16_COMMIT_READY); CommitUnchanged();
        commitStoppedClock = UMICOM_FALSE; commitBackwardClock = UMICOM_FALSE; model.noCompletion = UMICOM_FALSE;
    } else if (!strcmp(name, "revalidate_before_stage")) {
        CommitFat(20U, 9U); CommitRebase();
        CHECK(CommitStageAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
        CHECK(committer.state == UMICOM_FAT16_COMMIT_READY); CommitUnchanged();
        CommitFat(20U, 0U); CommitRebase();
        CHECK(CommitStageAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK); CommitAssertStage(511U, 700U);
        CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK); CommitAssertFinished(511U, 700U);
    } else CHECK(0);
    CommitCloseOwner();
}

static UmicomKernelConsoleShell commitShell, commitForeignShell;
static void CommitClearTranscript(void)
{
    memset(transcript, 0, sizeof(transcript)); transcriptBytes = 0U;
}
static UmicomKernelShellStatus CommitConsoleCommand(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_FALSE;
    const UmicomKernelShellStatus parsed = UmicomKernelShellParse(text, strlen(text), &command);
    if (parsed != UMICOM_SHELL_OK) return parsed;
    const UmicomKernelShellStatus status = UmicomKernelFat16CommitCommand(shell, &command, &handled);
    CHECK(handled); return status;
}
static void CommitConsoleReenter(void)
{
    CHECK(CommitConsoleCommand(&commitShell, "fatcommitinfo") == UMICOM_SHELL_BUSY);
    CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 0 text") == UMICOM_SHELL_BUSY);
    CHECK(CommitConsoleCommand(&commitShell, "fatcommit") == UMICOM_SHELL_BUSY);
    CHECK(UmicomKernelFat16CommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_BUSY);
    ++commitReentries;
}
static void CommitConsoleOutput(void *context, const char *text, UmicomSize bytes)
{
    Output(context, text, bytes); if (commitConsoleOutputReenter) CommitConsoleReenter();
}
static void CommitConsoleOpen(void)
{
    CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.commit.open=ok") && strstr(transcript, "fat.commit-state=ready") && Allocated() == 2U);
    CHECK(!commitWrites && !commitFlushes); CommitClearTranscript();
}
static void CommitConsoleClose(void)
{
    const UmicomU32 mutations = commitMutations;
    CHECK(CommitConsoleCommand(&commitShell, "fatcommitclose") == UMICOM_SHELL_OK);
    CHECK(!Allocated() && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    CHECK(commitMutations == mutations);
}
static void CommitConsoleStage(void)
{
    CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 511 \"Umicom file update\"") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.commit-state=staged") && strstr(transcript, "phase=data-verify"));
    CHECK(strstr(transcript, "requested=18 confirmed=18 submitted=18"));
    CHECK(strstr(transcript, "dirty-durable=1 dirty-verified=1 data-durable=1 data-verified=1"));
    CHECK(strstr(transcript, "clean-started=0 clean-durable=0 clean-verified=0 commit-accepted=0"));
    CHECK(commitWrites == 4U && commitFlushes == 3U);
    const char text[] = "Umicom file update";
    for (UmicomSize i = 0U; i < sizeof(text) - 1U; ++i) commitExpected[CommitFileAddress(511U + i)] = (UmicomU8)text[i];
    CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable)); CommitClearTranscript();
}
static void CommitConsoleCase(const char *name)
{
    memset(&commitShell, 0, sizeof(commitShell)); memset(&commitForeignShell, 0, sizeof(commitForeignShell));
    commitShell.output = CommitConsoleOutput; commitForeignShell.output = CommitConsoleOutput;
    if (!strcmp(name, "before_open")) {
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "fat.commit-state=unused")); CommitClearTranscript();
        CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 0 text") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.result=not-admitted")); CommitClearTranscript();
        CHECK(CommitConsoleCommand(&commitShell, "fatcommit") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.result=not-admitted"));
        CHECK(UmicomKernelFat16CommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_OK); CommitUnchanged(); return;
    }
    if (!strcmp(name, "invalid_arguments")) {
        const char *invalid[] = {"fatcommitopen", "fatcommitopen 0", "fatcommitopen 8 0", "fatcommitopen 0 4",
            "fatcommitopen -1 0", "fatcommitopen 18446744073709551616 0", "fatstage /FRAG.BIN x text",
            "fatstage /FRAG.BIN 0", "fatcommit extra", "fatcommitinfo extra", "fatcommitclose extra"};
        for (UmicomSize i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
            CHECK(CommitConsoleCommand(&commitShell, invalid[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_TRUE;
        CHECK(UmicomKernelFat16CommitCommand(0, &command, &handled) == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16CommitCommand(&commitShell, 0, &handled) == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16CommitCommand(&commitShell, &command, 0) == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelShellParse("unrelated", 9U, &command) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelFat16CommitCommand(&commitShell, &command, &handled) == UMICOM_SHELL_OK && !handled);
        CHECK(UmicomKernelFat16CommitConsoleClose(0) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(!model.reads && !model.writes); CommitUnchanged(); return;
    }
    if (!strcmp(name, "no_device_retry")) {
        model.device = 0U; CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(!Allocated() && !commitWrites); model.device = 2U;
    } else if (!strcmp(name, "readonly_retry")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.open=read-only") && !Allocated()); model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    } else if (!strcmp(name, "dirty_retry") || !strcmp(name, "fat_mismatch_retry")) {
        const UmicomBoolean dirty = !strcmp(name, "dirty_retry") ? UMICOM_TRUE : UMICOM_FALSE;
        if (dirty) CommitFat(1U, 0x7fffU);
        else commitVisible[(UmicomSize)COMMIT_MIRROR * 512U + 4U] ^= 1U;
        CommitRebase();
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.open=filesystem-error") && !Allocated());
        CHECK(strstr(transcript, dirty ? "last-disk=unclean-volume" : "last-disk=fat-copies-differ"));
        CHECK(strstr(transcript, "last-block=ok")); CommitUnchanged();
        if (dirty) CommitFat(1U, 0xffffU);
        else commitVisible[(UmicomSize)COMMIT_MIRROR * 512U + 4U] ^= 1U;
        CommitRebase(); CommitClearTranscript();
    } else if (!strcmp(name, "platform_retry")) {
        commitPlatformStatus = UMICOM_BLOCK_NO_CATALOGUE;
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.transport=no-catalogue") && !Allocated()); commitPlatformStatus = UMICOM_BLOCK_OK;
    } else if (!strcmp(name, "failed_open_retained")) {
        model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit-state=closing") && Allocated() == 2U && !model.freeCalls);
        CHECK(UmicomKernelFat16CommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls);
        model.stuckReset = UMICOM_FALSE; model.refuseDriver = UMICOM_FALSE; model.stickAfterDriver = UMICOM_FALSE;
        CHECK(UmicomKernelFat16CommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_OK && !Allocated());
    } else if (!strcmp(name, "legacy_exclusion")) {
        CHECK(UmicomKernelFat16UpdateOpen(&committer.updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_OK);
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(Allocated() == 2U && !commitWrites);
        CHECK(UmicomKernelFat16UpdateClose(&committer.updater) == UMICOM_FAT16_UPDATE_OK && !Allocated());
    }
    commitConsoleCallbackReenter = !strcmp(name, "callback_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
    commitConsoleOutputReenter = !strcmp(name, "output_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
    CommitConsoleOpen();
    if (!strcmp(name, "wrong_shell")) {
        const UmicomU32 events = commitEventCount, resets = model.resets;
        CHECK(CommitConsoleCommand(&commitForeignShell, "fatcommitinfo") == UMICOM_SHELL_BAD_STATE);
        CHECK(CommitConsoleCommand(&commitForeignShell, "fatstage /FRAG.BIN 0 text") == UMICOM_SHELL_BAD_STATE);
        CHECK(CommitConsoleCommand(&commitForeignShell, "fatcommit") == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16CommitConsoleClose(&commitForeignShell) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(commitEventCount == events && model.resets == resets);
    } else if (!strcmp(name, "empty_text")) {
        CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 0 \"\"") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.stage=invalid-argument") && strstr(transcript, "fat.commit.result=not-admitted"));
        CHECK(!commitWrites && !commitFlushes);
    } else if (!strcmp(name, "historical_failure")) {
        commitFailWrite = 4U; commitPartialWrite = UMICOM_TRUE;
        CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 511 \"Umicom file update\"") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit-state=failed") && strstr(transcript, "phase=data-write"));
        CHECK(strstr(transcript, "requested=18 confirmed=1 submitted=18") && strstr(transcript, "sector-bytes-at-risk=512"));
        const UmicomU32 events = commitEventCount; CommitClearTranscript();
        CHECK(CommitConsoleCommand(&commitShell, "fatcommit") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.result=not-admitted") && strstr(transcript, "fat.commit.previous-result="));
        CHECK(strstr(transcript, "requested=18 confirmed=1 submitted=18") && strstr(transcript, "write-uncertain-at-return=1"));
        CommitClearTranscript();
        CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 0 retry") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.previous-result=") && strstr(transcript, "phase=data-write"));
        CHECK(commitEventCount == events);
    } else if (!strcmp(name, "reset_retry") || !strcmp(name, "release_retry")) {
        CommitConsoleStage();
        if (!strcmp(name, "reset_retry")) model.stuckReset = UMICOM_TRUE; else model.failFree = 2U;
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitclose") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit-state=closing"));
        CHECK(Allocated() == (!strcmp(name, "reset_retry") ? 2U : 1U));
        model.stuckReset = UMICOM_FALSE; model.failFree = 0U;
    } else if (!strcmp(name, "command_sequence") || !strcmp(name, "staged_close") ||
        !strcmp(name, "late_clean_failure") || !strcmp(name, "callback_reentry") || !strcmp(name, "output_reentry")) {
        CHECK(CommitConsoleCommand(&commitShell, "fatstage /README.TXT 0 x") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.stage=read-only") && !commitWrites); CommitClearTranscript();
        CommitConsoleStage();
        CHECK(CommitConsoleCommand(&commitShell, "fatcommitinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "fat.commit.last-result=ok") && strstr(transcript, "phase=data-verify")); CommitClearTranscript();
        CHECK(CommitConsoleCommand(&commitShell, "fatstage /FRAG.BIN 0 retry") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.previous-result=ok") && strstr(transcript, "requested=18 confirmed=18 submitted=18"));
        CommitClearTranscript();
        if (!strcmp(name, "late_clean_failure")) {
            commitFailFlush = 5U;
            CHECK(CommitConsoleCommand(&commitShell, "fatcommit") == UMICOM_SHELL_IO_ERROR);
            CHECK(strstr(transcript, "phase=clean-primary-flush") && strstr(transcript, "clean-started=1"));
            CHECK(strstr(transcript, "clean-durable=0 clean-verified=0 commit-accepted=0"));
            CHECK(strstr(transcript, "A fresh reader may see a complete clean volume despite this error"));
        } else if (strcmp(name, "staged_close")) {
            CHECK(CommitConsoleCommand(&commitShell, "fatcommit") == UMICOM_SHELL_OK);
            CHECK(strstr(transcript, "fat.commit-state=committed") && strstr(transcript, "phase=complete"));
            CHECK(strstr(transcript, "clean-durable=1 clean-verified=1 commit-accepted=1"));
            CHECK(commitWrites == 6U && commitFlushes == 5U); CommitExpectedDirty(UMICOM_FALSE);
            CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
            CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
        }
        if (commitConsoleCallbackReenter || commitConsoleOutputReenter) CHECK(commitReentries > 0U);
    }
    CommitConsoleClose();
    if (!strcmp(name, "staged_close")) {
        CHECK(strstr(transcript, "resources-released; update not accepted as committed; no flush or flag repair submitted"));
        memcpy(commitVisible, commitDurable, sizeof(commitVisible)); CommitFreshReader(UMICOM_FALSE);
    } else if (!strcmp(name, "command_sequence")) {
        CommitClearTranscript(); CHECK(CommitConsoleCommand(&commitShell, "fatcommitopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.commit.open=bad-state") && !Allocated());
    }
    CHECK(UmicomKernelFat16CommitConsoleClose(&commitShell) == UMICOM_FAT16_UPDATE_OK);
}

static void CommitReadBudget(const char *name)
{
    const UmicomBoolean exact = !strcmp(name, "exact_boundary") ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(exact || !strcmp(name, "one_over_reserve"));
    const UmicomSize files = exact ? 90U : 99U;
    /* Every cluster belongs to one reachable regular file. Striding by a FAT
     * sector forces real cache misses during the whole-volume ownership proof;
     * no callback edits the production owner's counters or decision state.
     * These layouts exercise the actual 4096-read bound including the reads
     * reserved for both dirty headers and three staged data sectors. */
    for (UmicomSize i = 0U; i < files; ++i) {
        const UmicomSize clusters = exact ? (i < 9U ? 40U : (i == 9U ? 22U : 20U)) : (i ? 20U : 21U);
        char shortName[12]; CHECK(snprintf(shortName, sizeof(shortName), "F%07uBIN", (unsigned)i) == 11);
        CommitEntry(CommitRoot(5U + i), shortName, 0x20U, (UmicomU16)(100U + i), (UmicomU32)(clusters * 512U));
        for (UmicomSize j = 0U; j < clusters; ++j) {
            const UmicomU16 cluster = (UmicomU16)(100U + i + j * 256U);
            CommitFat(cluster, j + 1U == clusters ? 0xffffU : (UmicomU16)(cluster + 256U));
        }
    }
    CommitRebase(); CommitOpenOwner();
    const UmicomKernelFat16UpdateStatus status = CommitStageAt("/FRAG.BIN", 511U, 700U);
    if (exact) {
        CHECK(status == UMICOM_FAT16_UPDATE_OK && committer.updater.operationReads == UMICOM_FAT16_IO_LIMIT);
        CommitAssertStage(511U, 700U);
        CHECK(CommitFinishOwner() == UMICOM_FAT16_UPDATE_OK && committer.updater.operationReads == 7U);
        CommitAssertFinished(511U, 700U);
    } else {
        CHECK(status == UMICOM_FAT16_UPDATE_INSPECTION_LIMIT && commitResult.diskStatus == UMICOM_DISK_LIMIT);
        CHECK(committer.state == UMICOM_FAT16_COMMIT_READY && commitResult.phase == UMICOM_FAT16_COMMIT_PREFLIGHT);
        CHECK(committer.updater.operationReads == 4092U && !committer.updater.plan.count);
        CHECK(!commitResult.mediaTouched && !commitResult.submittedDataSectors && !commitResult.submittedMetadataSectors);
        CommitUnchanged();
    }
    CommitCloseOwner();
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: umicom-fat16-commit-tests CASE READONLY-FIXTURE\n"); return 2; }
    CommitStart(argv[2]); const char *const name = argv[1];
    if (!strncmp(name, "success.", 8U)) CommitSuccess(name + 8U);
    else if (!strncmp(name, "format.", 7U)) CommitFormat(name + 7U);
    else if (!strncmp(name, "write_error.", 12U)) CommitWriteFailure(name + 12U);
    else if (!strncmp(name, "flush_error.", 12U)) CommitFlushFailure(name + 12U);
    else if (!strncmp(name, "read_error.", 11U)) CommitReadFailure(name + 11U, UMICOM_FALSE);
    else if (!strncmp(name, "read_mismatch.", 14U)) CommitReadFailure(name + 14U, UMICOM_TRUE);
    else if (!strncmp(name, "dropped_write.", 14U)) CommitDroppedWrite(name + 14U, UMICOM_FALSE);
    else if (!strncmp(name, "altered_write.", 14U)) CommitDroppedWrite(name + 14U, UMICOM_TRUE);
    else if (!strncmp(name, "cut.", 4U)) CommitCut(name + 4U);
    else if (!strncmp(name, "ownership.", 10U)) CommitOwnership(name + 10U, UMICOM_FALSE);
    else if (!strncmp(name, "finish_ownership.", 17U)) CommitOwnership(name + 17U, UMICOM_TRUE);
    else if (!strncmp(name, "open_arguments.", 15U)) CommitOpenArguments(name + 15U);
    else if (!strncmp(name, "range.", 6U)) CommitRange(name + 6U);
    else if (!strncmp(name, "lifetime.", 9U)) CommitLifecycle(name + 9U);
    else if (!strncmp(name, "clock.", 6U)) CommitMutationClock(name + 6U);
    else if (!strncmp(name, "preflight.", 10U)) CommitPreflightFailure(argv[2], name + 10U);
    else if (!strncmp(name, "acceptance_clock.", 17U)) CommitAcceptanceClock(argv[2], name + 17U);
    else if (!strncmp(name, "changed_before_finish.", 22U)) CommitChangedBeforeFinish(name + 22U);
    else if (!strncmp(name, "console.", 8U)) CommitConsoleCase(name + 8U);
    else if (!strncmp(name, "read_budget.", 12U)) CommitReadBudget(name + 12U);
    else CommitMisc(name);
    CHECK(!Allocated()); printf("fat16-commit.%s: ok\n", name); return 0;
}

