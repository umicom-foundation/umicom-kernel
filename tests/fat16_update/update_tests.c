/*-----------------------------------------------------------------------------
 * Umicom Kernel — bounded FAT16 file-update qualification.
 *
 * The production inspector, updater, block driver and physical allocator run
 * against the original register/reset model. This suite supplies independent
 * sector storage with separate visible and flushed images. Every test starts
 * from a private copy of the packaged synthetic fixture; the source is opened
 * for reading only. Complete-image comparisons include metadata and slack.
 *
 * A simulated power loss discards the model's unflushed visible array. This
 * establishes the API distinction between completion and flush; it does not
 * establish physical-sector atomicity or hardware crash recovery.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#define UmicomPlatformBlockDomainGet UmicomUpdateOriginalDomainGet
#define main UmicomOriginalBlockQualificationEntry
#include "../virtio_block/virtio_block_tests.c"
#undef main
#undef UmicomPlatformBlockDomainGet
#include "../disk_inspection/fixture_layout.h"
#include "umicom/kernel/fat16_update.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/fat16_update_console.h"

#define UPDATE_MEDIA_BYTES ((UmicomSize)UMICOM_DISK_FIXTURE_SECTORS * 512U)
#define UPDATE_REQUEST_LIMIT 8192U
static UmicomU8 updateInitial[UPDATE_MEDIA_BYTES];
static UmicomU8 updateVisible[UPDATE_MEDIA_BYTES];
static UmicomU8 updateDurable[UPDATE_MEDIA_BYTES];
static UmicomU8 updateExpected[UPDATE_MEDIA_BYTES];
static UmicomU8 updateInput[4097], updateInputSnapshot[4097];
static UmicomKernelFat16Updater updater;
static UmicomKernelFat16UpdateResult updateResult;
static UmicomKernelFat16UpdateResult updateResultSnapshot;
static UmicomKernelBlockMutationOutcome updateFlushOutcome;
static UmicomU32 updateReadRequests, updateWriteRequests, updateFlushRequests;
static UmicomU32 updateReadCompletions, updateWriteCompletions;
static UmicomU32 updateFailRead, updateFailWrite, updateFailFlush;
static UmicomU32 updateHangWrite, updateNoPermissionAfterWrites;
static UmicomBoolean updatePartialWrite, updateReenter;
static UmicomBoolean updateFinalClockFailure, updateFinalClockRollback, updateFinalClockInjected;
static UmicomBoolean updateOuterClockFailure, updateOuterClockRollback;
static UmicomBoolean updateFlushClockFailure, updateFlushClockRollback, updateFlushClockOuter;
static UmicomBoolean updateReadClockRollback, updatePolicyReenter, updateConsoleReenter;
static UmicomU32 updateReadClockAt;
static UmicomBoolean updateStoppedClock, updateClockRollback;
static UmicomU64 updateLastClock;
static UmicomU64 updateReadSectors[UPDATE_REQUEST_LIMIT];
static UmicomU64 updateWrittenSectors[UPDATE_REQUEST_LIMIT];
static UmicomU32 updateReentries;
static UmicomKernelBlockStatus updatePlatformStatus = UMICOM_BLOCK_OK;
static void UpdateConsoleReentry(void);

UmicomKernelBlockStatus UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **out)
{
    if (updatePlatformStatus != UMICOM_BLOCK_OK) return updatePlatformStatus;
    return UmicomUpdateOriginalDomainGet(out);
}

static void UpdateEqual(const void *first, const void *second, UmicomSize bytes)
{
    CHECK(memcmp(first, second, (size_t)bytes) == 0);
}
static void UpdatePut16(UmicomU8 *p, UmicomU16 value)
{
    p[0] = (UmicomU8)value; p[1] = (UmicomU8)(value >> 8U);
}
static void UpdatePut32(UmicomU8 *p, UmicomU32 value)
{
    for (UmicomSize i = 0U; i < 4U; ++i) p[i] = (UmicomU8)(value >> (8U * i));
}
static UmicomU8 *UpdateRoot(UmicomSize entry)
{
    CHECK(entry < 512U);
    return updateVisible + (UmicomSize)UMICOM_DISK_FIXTURE_ROOT * 512U + entry * 32U;
}
static UmicomU8 *UpdateCluster(UmicomU16 cluster)
{
    CHECK(cluster >= 2U && cluster < 12000U);
    return updateVisible + ((UmicomSize)UMICOM_DISK_FIXTURE_DATA + cluster - 2U) * 512U;
}
static void UpdateFat(UmicomU16 cluster, UmicomU16 next)
{
    const UmicomSize first = ((UmicomSize)UMICOM_DISK_FIXTURE_FIRST + 1U) * 512U;
    CHECK((UmicomSize)cluster * 2U + 2U <= UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U);
    UpdatePut16(updateVisible + first + (UmicomSize)cluster * 2U, next);
    UpdatePut16(updateVisible + first + UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U + (UmicomSize)cluster * 2U, next);
}
static void UpdateEntry(UmicomU8 *p, const char *name, UmicomU8 attributes,
    UmicomU16 cluster, UmicomU32 bytes)
{
    memset(p, 0, 32U); memcpy(p, name, 11U); p[11] = attributes;
    UpdatePut16(p + 26U, cluster); UpdatePut32(p + 28U, bytes);
}
static void UpdateRebase(void)
{
    memcpy(updateInitial, updateVisible, sizeof(updateInitial));
    memcpy(updateDurable, updateVisible, sizeof(updateDurable));
    memcpy(updateExpected, updateVisible, sizeof(updateExpected));
}
static UmicomU16 UpdateFileCluster(UmicomSize index)
{
    static const UmicomU16 clusters[] = {4U, 9U, 6U, 10U, 11U, 12U, 13U, 14U, 15U};
    CHECK(index < sizeof(clusters) / sizeof(clusters[0]));
    return clusters[index];
}
static UmicomSize UpdateFileAddress(UmicomU64 offset)
{
    const UmicomU16 cluster = UpdateFileCluster((UmicomSize)(offset / 512U));
    return ((UmicomSize)UMICOM_DISK_FIXTURE_DATA + cluster - 2U) * 512U + (UmicomSize)(offset % 512U);
}
static void UpdatePatchExpected(UmicomU64 offset, UmicomSize bytes)
{
    for (UmicomSize i = 0U; i < bytes; ++i)
        updateExpected[UpdateFileAddress(offset + i)] = updateInputSnapshot[i];
}
static void UpdateLargeFile(void)
{
    for (UmicomSize i = 0U; i < 9U; ++i) {
        const UmicomU16 cluster = UpdateFileCluster(i);
        UpdateFat(cluster, i == 8U ? 0xffffU : UpdateFileCluster(i + 1U));
        for (UmicomSize j = 0U; j < 512U; ++j)
            UpdateCluster(cluster)[j] = (UmicomU8)((i * 512U + j) * 29U + 7U);
    }
    UpdatePut32(UpdateRoot(3U) + 28U, 9U * 512U);
    UpdateRebase();
}
static UmicomU32 UpdateCommand(void)
{
    CHECK(model.desc);
    return *(const UmicomU32 *)(UmicomAddress)*(const UmicomU64 *)model.desc;
}
static void UpdateComplete(void)
{
    CHECK(model.desc && model.avail && model.used);
    const UmicomU16 used = *(const UmicomU16 *)(model.used + 2U);
    CHECK(*(const UmicomU16 *)(model.avail + 2U) == (UmicomU16)(used + 1U));
    CHECK(*(const UmicomU16 *)(model.avail + 4U + 2U * (used % 8U)) == 0U);
    const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
    const UmicomU32 command = *(const UmicomU32 *)header;
    CHECK(header == model.desc + UMICOM_VIRTIO_HEADER_OFFSET);
    CHECK(*(const UmicomU32 *)(header + 4U) == 0U);
    CHECK(*(const UmicomU32 *)(model.desc + 8U) == 16U);
    CHECK(*(const UmicomU16 *)(model.desc + 12U) == 1U);
    CHECK(*(const UmicomU16 *)(model.desc + 14U) == 1U);
    UmicomU32 statusByte = model.result;
    UmicomU32 usedBytes = 1U;
    UmicomAddress status;
    if (command == UMICOM_VIRTIO_REQUEST_FLUSH) {
        CHECK(*(const UmicomU64 *)(header + 8U) == 0U);
        status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
        CHECK(*(const UmicomU32 *)(model.desc + 24U) == 1U);
        CHECK(*(const UmicomU16 *)(model.desc + 28U) == 2U);
        CHECK(*(const UmicomU16 *)(model.desc + 30U) == 0U);
        if (updateFailFlush == updateFlushRequests) statusByte = 1U;
        if (statusByte == 0U) memcpy(updateDurable, updateVisible, sizeof(updateDurable));
    } else {
        CHECK(command == UMICOM_VIRTIO_REQUEST_READ || command == UMICOM_VIRTIO_REQUEST_WRITE);
        const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
        const UmicomAddress data = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
        const UmicomU32 bytes = *(const UmicomU32 *)(model.desc + 24U);
        CHECK(bytes == 512U && sector < UMICOM_DISK_FIXTURE_SECTORS);
        CHECK(data == domain.slots[0].dataFrame && data != model.desc);
        CHECK(data >= (UmicomAddress)ram && data + 4096U <= (UmicomAddress)ram + sizeof(ram));
        CHECK(*(const UmicomU16 *)(model.desc + 28U) == (command == 0U ? 3U : 1U));
        CHECK(*(const UmicomU16 *)(model.desc + 30U) == 2U);
        status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 32U);
        CHECK(*(const UmicomU32 *)(model.desc + 40U) == 1U);
        CHECK(*(const UmicomU16 *)(model.desc + 44U) == 2U);
        CHECK(*(const UmicomU16 *)(model.desc + 46U) == 0U);
        if (command == UMICOM_VIRTIO_REQUEST_READ) {
            ++updateReadCompletions; usedBytes = 513U;
            if (updateFailRead == updateReadRequests) {
                statusByte = 1U;
                memset((void *)data, 0x67, 512U);
            } else memcpy((void *)data, updateVisible + (UmicomSize)sector * 512U, 512U);
        } else {
            ++updateWriteCompletions;
            CHECK(domain.slots[0].needsFlush && domain.slots[0].writeUncertain);
            if (updateFailWrite == updateWriteRequests) {
                statusByte = 1U;
                if (updatePartialWrite)
                    memcpy(updateVisible + (UmicomSize)sector * 512U, (const void *)data, 256U);
            } else memcpy(updateVisible + (UmicomSize)sector * 512U, (const void *)data, 512U);
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
    model.pending = UMICOM_FALSE;
}
static void UpdateWriteRegister(void *context, UmicomAddress address, UmicomU32 value)
{
    if (Offset(address) == UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomU32 command = UpdateCommand();
        const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
        if (command == UMICOM_VIRTIO_REQUEST_READ) {
            CHECK(updateReadRequests < UPDATE_REQUEST_LIMIT);
            updateReadSectors[updateReadRequests++] = sector;
        } else if (command == UMICOM_VIRTIO_REQUEST_WRITE) {
            CHECK(updateWriteRequests < UPDATE_REQUEST_LIMIT);
            updateWrittenSectors[updateWriteRequests++] = sector;
            if (updateHangWrite == updateWriteRequests) model.noCompletion = UMICOM_TRUE;
        } else { CHECK(command == UMICOM_VIRTIO_REQUEST_FLUSH); ++updateFlushRequests; }
        if (updateReenter) {
            UmicomKernelFat16UpdateResult ignored;
            memset(&ignored, 0x5c, sizeof(ignored));
            CHECK(UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U,
                updateInput, 1U, &ignored) == UMICOM_FAT16_UPDATE_BUSY);
            CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_BUSY);
            CHECK(UmicomKernelFat16UpdateFlush(&updater, &updateFlushOutcome) == UMICOM_FAT16_UPDATE_BUSY);
            ++updateReentries;
        }
        if (updateConsoleReenter) UpdateConsoleReentry();
    }
    WriteRegister(context, address, value);
}
static UmicomBoolean UpdateAllowed(void *context)
{
    CHECK(context == &model);
    if (updatePolicyReenter) {
        CHECK(updater.busy);
        UmicomKernelFat16UpdateResult ignored;
        memset(&ignored, 0x38, sizeof(ignored));
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_BUSY);
        CHECK(UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U, &ignored) == UMICOM_FAT16_UPDATE_BUSY);
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_BUSY);
        CHECK(UmicomKernelFat16UpdateFlush(&updater, &updateFlushOutcome) == UMICOM_FAT16_UPDATE_BUSY);
        ++updateReentries;
    }
    if (updateNoPermissionAfterWrites && updateWriteCompletions >= updateNoPermissionAfterWrites)
        return UMICOM_FALSE;
    return model.allowed;
}
static UmicomU64 UpdateClock(void *context)
{
    CHECK(context == &model);
    if (updateClockRollback) { if (model.now) --model.now; return model.now; }
    if (updateStoppedClock) return model.now;
    UmicomU64 now = Clock(context);
    if (updateReadClockAt && !updateFinalClockInjected && updateReadRequests == updateReadClockAt && !domain.busy) {
        updateFinalClockInjected = UMICOM_TRUE;
        now = updateReadClockRollback ? updater.operationClock - 1U : now + UMICOM_FAT16_UPDATE_OPERATION_TICKS;
        model.now = now;
    }
    if (updateFlushClockFailure && !updateFinalClockInjected && updateFlushRequests > 0U &&
        (updateFlushClockOuter ? !domain.busy : domain.busy)) {
        updateFinalClockInjected = UMICOM_TRUE;
        now = updateFlushClockRollback ? (updateFlushClockOuter ? updater.operationClock - 1U : updateLastClock - 1U) :
            now + (updateFlushClockOuter ? UMICOM_FAT16_UPDATE_OPERATION_TICKS : domain.slots[0].timeoutTicks);
        model.now = now;
    }
    if (updateOuterClockFailure && !updateFinalClockInjected && updateWriteCompletions > 0U && !domain.busy) {
        updateFinalClockInjected = UMICOM_TRUE;
        now = updateOuterClockRollback ? updater.operationClock - 1U :
            now + UMICOM_FAT16_UPDATE_OPERATION_TICKS;
        model.now = now;
    }
    if (updateFinalClockFailure && !updateFinalClockInjected && updateWriteCompletions > 0U && !model.pending) {
        updateFinalClockInjected = UMICOM_TRUE;
        now = updateFinalClockRollback ? (updateLastClock ? updateLastClock - 1U : 0U) : now + domain.slots[0].timeoutTicks;
        model.now = now;
    }
    updateLastClock = now;
    return now;
}
static void UpdateStart(const char *fixture)
{
    Start();
    FILE *file = fopen(fixture, "rb"); CHECK(file);
    CHECK(fread(updateVisible, 1U, sizeof(updateVisible), file) == sizeof(updateVisible));
    CHECK(fgetc(file) == EOF && fclose(file) == 0);
    UpdateRebase();
    memset(&updater, 0, sizeof(updater));
    memset(&updateResult, 0xa5, sizeof(updateResult));
    memcpy(&updateResultSnapshot, &updateResult, sizeof(updateResult));
    for (UmicomSize i = 0U; i < sizeof(updateInput); ++i) updateInput[i] = (UmicomU8)(i * 17U + 0x83U);
    memcpy(updateInputSnapshot, updateInput, sizeof(updateInput));
    model.capacity = UMICOM_DISK_FIXTURE_SECTORS;
    model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    model.featuresLow |= UMICOM_VIRTIO_FEATURE_FLUSH;
    model.expectedDriverLow = UMICOM_VIRTIO_FEATURE_FLUSH;
    model.completeRequest = UpdateComplete;
    domain.operations.write32 = UpdateWriteRegister;
    domain.operations.allowed = UpdateAllowed;
    domain.operations.clock = UpdateClock;
    updateReadRequests = 0U; updateWriteRequests = 0U; updateFlushRequests = 0U;
    updateReadCompletions = 0U; updateWriteCompletions = 0U;
    updateFailRead = 0U; updateFailWrite = 0U; updateFailFlush = 0U;
    updateHangWrite = 0U; updateNoPermissionAfterWrites = 0U;
    updatePartialWrite = UMICOM_FALSE; updateReenter = UMICOM_FALSE; updateReentries = 0U;
    updateFinalClockFailure = UMICOM_FALSE; updateFinalClockRollback = UMICOM_FALSE;
    updateFinalClockInjected = UMICOM_FALSE; updateStoppedClock = UMICOM_FALSE;
    updateClockRollback = UMICOM_FALSE; updateLastClock = 0U;
    updateOuterClockFailure = UMICOM_FALSE; updateOuterClockRollback = UMICOM_FALSE;
    updateFlushClockFailure = UMICOM_FALSE; updateFlushClockRollback = UMICOM_FALSE; updateFlushClockOuter = UMICOM_FALSE;
    updateReadClockAt = 0U; updateReadClockRollback = UMICOM_FALSE;
    updatePolicyReenter = UMICOM_FALSE; updateConsoleReenter = UMICOM_FALSE;
    updatePlatformStatus = UMICOM_BLOCK_OK;
    updateFlushOutcome = UMICOM_BLOCK_NOT_SUBMITTED;
    memset(updateReadSectors, 0, sizeof(updateReadSectors));
    memset(updateWrittenSectors, 0, sizeof(updateWrittenSectors));
}
static void UpdateOpenOwner(void)
{
    CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_OK);
    CHECK(updater.state == UMICOM_FAT16_UPDATER_OPEN && updater.admitted && updater.handle);
    CHECK(updater.domain == &domain && !updater.volume.open && !updater.busy);
    CHECK(Allocated() == 2U && domain.slots[0].writable);
    CHECK(updateWriteRequests == 0U && updateFlushRequests == 0U);
}
static void UpdateCloseOwner(void)
{
    CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_OK);
    CHECK(updater.handle == 0U && !updater.busy && !updater.volume.open);
    CHECK(Allocated() == 0U && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
}
static UmicomKernelFat16UpdateStatus UpdateWriteAt(const char *path, UmicomU64 offset, UmicomSize bytes)
{
    memset(&updateResult, 0xa5, sizeof(updateResult));
    memcpy(&updateResultSnapshot, &updateResult, sizeof(updateResult));
    return UmicomKernelFat16UpdateWrite(&updater, path, offset, updateInput, bytes, &updateResult);
}
static void UpdateUnchangedMedia(void)
{
    CHECK(updateWriteRequests == 0U && updateFlushRequests == 0U);
    UpdateEqual(updateVisible, updateInitial, sizeof(updateVisible));
    UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
    UpdateEqual(updateInput, updateInputSnapshot, sizeof(updateInput));
}
static void UpdateKnownSuccess(UmicomU64 offset, UmicomSize bytes)
{
    const UmicomSize touched = ((UmicomSize)(offset % 512U) + bytes + 511U) / 512U;
    CHECK(updateResult.status == UMICOM_FAT16_UPDATE_OK);
    CHECK(updateResult.diskStatus == UMICOM_DISK_OK && updateResult.blockStatus == UMICOM_BLOCK_OK);
    CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_COMPLETED);
    CHECK(updateResult.lastBlockOutcome == UMICOM_BLOCK_COMPLETED);
    CHECK(updateResult.offset == offset && updateResult.requestedBytes == bytes);
    CHECK(updateResult.confirmedBytes == bytes && updateResult.submittedBytes == bytes);
    CHECK(updateResult.completedSectors == touched && updateResult.submittedSectors == touched);
    CHECK(updateResult.uncertainBytes == 0U && !updateResult.writeUncertain);
    CHECK(updateResult.needsFlush && updater.needsFlush && !updater.writeUncertain);
    CHECK(updateWriteRequests == touched && updateFlushRequests == 0U);
    CHECK(!updater.volume.open && !updater.busy);
    for (UmicomSize i = 0U; i < touched; ++i)
        CHECK(updateWrittenSectors[i] == UMICOM_DISK_FIXTURE_DATA +
            UpdateFileCluster((UmicomSize)(offset / 512U) + i) - 2U);
    UpdatePatchExpected(offset, bytes);
    UpdateEqual(updateVisible, updateExpected, sizeof(updateVisible));
    UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
    UpdateEqual(updateInput, updateInputSnapshot, sizeof(updateInput));
    UpdateEqual(&updater.lastWrite, &updateResult, sizeof(updateResult));
}
static void UpdateFlushSuccess(void)
{
    const UmicomKernelFat16UpdateResult saved = updater.lastWrite;
    CHECK(UmicomKernelFat16UpdateFlush(&updater, &updateFlushOutcome) == UMICOM_FAT16_UPDATE_OK);
    CHECK(updateFlushOutcome == UMICOM_BLOCK_COMPLETED && !updater.needsFlush);
    UpdateEqual(&saved, &updater.lastWrite, sizeof(saved));
    UpdateEqual(updateDurable, updateVisible, sizeof(updateDurable));
}
static void UpdateSuccessfulCase(const char *name)
{
    UmicomU64 offset = 0U;
    UmicomSize bytes = 1U;
    if (!strcmp(name, "unaligned_fragmented")) { offset = 511U; bytes = 700U; }
    else if (!strcmp(name, "whole_file")) bytes = 1300U;
    else if (!strcmp(name, "last_byte")) offset = 1299U;
    else if (!strcmp(name, "aligned_sector")) { offset = 512U; bytes = 512U; }
    else if (!strcmp(name, "nine_sectors")) { UpdateLargeFile(); offset = 1U; bytes = 4096U; }
    else if (!strcmp(name, "hidden_system_target")) { UpdateRoot(3U)[11] = 0x26U; UpdateRebase(); }
    else if (!strcmp(name, "deleted_lfn")) {
        memset(UpdateRoot(5U), 0xff, 32U); UpdateRoot(5U)[0] = 0xe5U; UpdateRoot(5U)[11] = 0x0fU;
        UpdateRebase();
    } else if (!strcmp(name, "deleted_stale_cluster")) {
        UpdateEntry(UpdateRoot(5U), "OLD     BIN", 0x20U, 4U, 1300U); UpdateRoot(5U)[0] = 0xe5U;
        UpdateRebase();
    } else if (!strcmp(name, "end_marker_stale_entry")) {
        UpdateEntry(UpdateRoot(6U), "OLD     BIN", 0x20U, 4U, 1300U); UpdateRebase();
    } else if (!strcmp(name, "matching_fat_padding")) {
        const UmicomSize first = ((UmicomSize)UMICOM_DISK_FIXTURE_FIRST + 1U) * 512U;
        const UmicomSize fatBytes = UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U;
        updateVisible[first + fatBytes - 1U] = 0x73U;
        updateVisible[first + 2U * fatBytes - 1U] = 0x73U;
        UpdateRebase();
    } else if (strcmp(name, "single_byte") && strcmp(name, "casefold_path") &&
        strcmp(name, "idle_deadline_restart")) CHECK(0);
    UpdateOpenOwner();
    if (!strcmp(name, "idle_deadline_restart")) model.now += (UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS * 3U;
    CHECK(UpdateWriteAt(!strcmp(name, "casefold_path") ? "/frag.bin" : "/FRAG.BIN", offset, bytes) == UMICOM_FAT16_UPDATE_OK);
    UpdateKnownSuccess(offset, bytes);
    UpdateFlushSuccess();
    UpdateCloseOwner();
}
static void UpdateMakeDirectory(UmicomU16 first, UmicomU16 parent, UmicomSize clusters, UmicomSize emptyFiles)
{
    for (UmicomSize i = 0U; i < clusters; ++i) {
        const UmicomU16 cluster = (UmicomU16)(first + i);
        UpdateFat(cluster, i + 1U == clusters ? 0xffffU : (UmicomU16)(cluster + 1U));
        memset(UpdateCluster(cluster), 0, 512U);
    }
    UpdateEntry(UpdateCluster(first), ".          ", 0x10U, first, 0U);
    UpdateEntry(UpdateCluster(first) + 32U, "..         ", 0x10U, parent, 0U);
    CHECK(emptyFiles + 2U <= clusters * 16U);
    for (UmicomSize i = 0U; i < emptyFiles; ++i) {
        char name[12];
        CHECK(snprintf(name, sizeof(name), "F%07uTXT", (unsigned)i) == 11);
        const UmicomSize entry = i + 2U;
        UpdateEntry(UpdateCluster((UmicomU16)(first + entry / 16U)) + (entry % 16U) * 32U,
            name, 0x20U, 0U, 0U);
    }
}
static UmicomKernelDiskStatus UpdateCorrupt(const char *name)
{
    UmicomKernelDiskStatus expected = UMICOM_DISK_CORRUPT;
    if (!strcmp(name, "file_file_crosslink")) {
        UpdatePut16(UpdateRoot(1U) + 26U, 4U); UpdatePut32(UpdateRoot(1U) + 28U, 1300U);
    } else if (!strcmp(name, "file_directory_crosslink")) {
        UpdateFat(3U, 4U);
    } else if (!strcmp(name, "shared_tail")) {
        UpdateFat(2U, 9U); UpdatePut32(UpdateRoot(1U) + 28U, 1300U);
    } else if (!strcmp(name, "orphan_to_target")) UpdateFat(20U, 9U);
    else if (!strcmp(name, "orphan_allocation")) UpdateFat(20U, 0xffffU);
    else if (!strcmp(name, "allocated_empty_file")) { UpdatePut16(UpdateRoot(4U) + 26U, 20U); UpdateFat(20U, 0xffffU); }
    else if (!strcmp(name, "directory_cycle")) UpdateEntry(UpdateCluster(3U) + 96U, "LOOP       ", 0x10U, 3U, 0U);
    else if (!strcmp(name, "directory_wrong_parent")) UpdatePut16(UpdateCluster(3U) + 32U + 26U, 2U);
    else if (!strcmp(name, "directory_missing_dot")) UpdateCluster(3U)[0] = 0xe5U;
    else if (!strcmp(name, "directory_missing_parent")) UpdateCluster(3U)[32U] = 0xe5U;
    else if (!strcmp(name, "directory_duplicate_dot")) memcpy(UpdateCluster(3U) + 96U, UpdateCluster(3U), 32U);
    else if (!strcmp(name, "directory_dot_file")) UpdateCluster(3U)[11U] = 0x20U;
    else if (!strcmp(name, "directory_dot_high_cluster")) UpdatePut16(UpdateCluster(3U) + 20U, 1U);
    else if (!strcmp(name, "root_dot")) UpdateEntry(UpdateRoot(5U), ".          ", 0x10U, 3U, 0U);
    else if (!strcmp(name, "root_label_cluster")) UpdatePut16(UpdateRoot(0U) + 26U, 4U);
    else if (!strcmp(name, "root_label_size")) UpdatePut32(UpdateRoot(0U) + 28U, 1U);
    else if (!strcmp(name, "root_label_directory")) UpdateRoot(0U)[11U] = 0x18U;
    else if (!strcmp(name, "nested_volume_label")) UpdateEntry(UpdateCluster(3U) + 96U, "LABEL      ", 0x08U, 0U, 0U);
    else if (!strcmp(name, "live_lfn")) {
        memset(UpdateRoot(5U), 0x41, 32U); UpdateRoot(5U)[11U] = 0x0fU;
        expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM;
    } else if (!strcmp(name, "bad_cluster")) { UpdateFat(20U, 0xfff7U); expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM; }
    else if (!strcmp(name, "free_chain_link")) UpdateFat(9U, 0U);
    else if (!strcmp(name, "reserved_chain_link")) UpdateFat(9U, 0xfff0U);
    else if (!strcmp(name, "target_chain_cycle")) { UpdateFat(6U, 4U); expected = UMICOM_DISK_CHAIN_CYCLE; }
    else if (!strcmp(name, "short_chain")) UpdateFat(4U, 0xffffU);
    else if (!strcmp(name, "surplus_chain")) { UpdateFat(6U, 10U); UpdateFat(10U, 0xffffU); }
    else if (!strcmp(name, "dirty_volume")) { UpdateFat(1U, 0x7fffU); expected = UMICOM_DISK_DIRTY; }
    else if (!strcmp(name, "hard_error_volume")) { UpdateFat(1U, 0xbfffU); expected = UMICOM_DISK_DIRTY; }
    else if (!strcmp(name, "fat_padding_mismatch")) {
        updateVisible[((UmicomSize)UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS) * 512U - 1U] ^= 0x17U;
        expected = UMICOM_DISK_FAT_MISMATCH;
    } else if (!strcmp(name, "remote_fat_mismatch")) {
        updateVisible[((UmicomSize)UMICOM_DISK_FIXTURE_FIRST + 41U) * 512U] = 0x53U;
        expected = UMICOM_DISK_FAT_MISMATCH;
    } else if (!strcmp(name, "unknown_attribute")) UpdateRoot(3U)[11U] |= 0x40U;
    else if (!strcmp(name, "archive_clear")) { UpdateRoot(3U)[11U] = 0U; expected = UMICOM_DISK_UNSUPPORTED_FILESYSTEM; }
    else if (!strcmp(name, "target_readonly")) { UpdateRoot(3U)[11U] |= 0x01U; expected = UMICOM_DISK_READ_ONLY; }
    else if (!strcmp(name, "duplicate_name")) UpdateEntry(UpdateRoot(5U), "FRAG    BIN", 0x20U, 0U, 0U);
    else if (!strcmp(name, "directory_limit")) {
        for (UmicomSize i = 0U; i < 63U; ++i) {
            char shortName[12]; CHECK(snprintf(shortName, sizeof(shortName), "D%07u   ", (unsigned)i) == 11);
            const UmicomU16 cluster = (UmicomU16)(20U + i);
            UpdateEntry(UpdateRoot(5U + i), shortName, 0x10U, cluster, 0U);
            UpdateMakeDirectory(cluster, 0U, 1U, 0U);
        }
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "object_limit")) {
        UpdateEntry(UpdateRoot(5U), "ONE        ", 0x10U, 20U, 0U);
        UpdateEntry(UpdateRoot(6U), "TWO        ", 0x10U, 30U, 0U);
        UpdateMakeDirectory(20U, 0U, 8U, 126U);
        UpdateMakeDirectory(30U, 0U, 8U, 126U);
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "depth_limit")) {
        UpdateEntry(UpdateRoot(5U), "DEEP       ", 0x10U, 20U, 0U);
        for (UmicomU16 i = 0U; i < 9U; ++i) {
            const UmicomU16 cluster = (UmicomU16)(20U + i);
            UpdateMakeDirectory(cluster, i ? (UmicomU16)(cluster - 1U) : 0U, 1U, 0U);
            if (i < 8U) UpdateEntry(UpdateCluster(cluster) + 64U, "DEEP       ", 0x10U, (UmicomU16)(cluster + 1U), 0U);
        }
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "chain_limit")) {
        UpdateFat(4U, 20U); UpdateFat(9U, 0U); UpdateFat(6U, 0U);
        for (UmicomU16 i = 20U; i < 276U; ++i) UpdateFat(i, i == 275U ? 0xffffU : (UmicomU16)(i + 1U));
        UpdatePut32(UpdateRoot(3U) + 28U, 257U * 512U); expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "io_budget")) {
        for (UmicomSize i = 0U; i < 123U; ++i) {
            char shortName[12]; CHECK(snprintf(shortName, sizeof(shortName), "F%07uBIN", (unsigned)i) == 11);
            UpdateEntry(UpdateRoot(5U + i), shortName, 0x20U, (UmicomU16)(100U + i), 20U * 512U);
            for (UmicomSize j = 0U; j < 20U; ++j) {
                const UmicomU16 cluster = (UmicomU16)(100U + i + j * 256U);
                UpdateFat(cluster, j == 19U ? 0xffffU : (UmicomU16)(cluster + 256U));
            }
        }
        expected = UMICOM_DISK_LIMIT;
    } else CHECK(0);
    UpdateRebase();
    return expected;
}
static void UpdateFormatCase(const char *name)
{
    const UmicomKernelDiskStatus expected = UpdateCorrupt(name);
    UmicomKernelFat16UpdateStatus status = UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        status = UpdateWriteAt("/FRAG.BIN", 0U, 1U);
        CHECK(status != UMICOM_FAT16_UPDATE_OK);
        CHECK(updateResult.status == status);
        CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED);
        CHECK(updateResult.confirmedBytes == 0U && updateResult.submittedBytes == 0U);
        CHECK(updateResult.completedSectors == 0U && updateResult.submittedSectors == 0U);
        CHECK(updateResult.uncertainBytes == 0U && !updateResult.writeUncertain && !updateResult.needsFlush);
        if (!strcmp(name, "io_budget")) {
            CHECK(updateResult.diskStatus == UMICOM_DISK_IO_ERROR && updateResult.blockStatus == UMICOM_BLOCK_TIMEOUT);
            CHECK(updater.operationReads == UMICOM_FAT16_IO_LIMIT);
        } else CHECK(updateResult.diskStatus == expected);
    } else CHECK(updater.lastDiskStatus == expected);
    UpdateUnchangedMedia();
    UpdateCloseOwner();
}
static void UpdatePartialCase(const char *name)
{
    UpdateOpenOwner();
    const UmicomU32 failed = !strcmp(name, "first_write_failure") ? 1U :
        (!strcmp(name, "last_write_failure") ? 3U : 2U);
    updateFailWrite = failed; updatePartialWrite = UMICOM_TRUE;
    CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    const UmicomSize confirmed = failed == 1U ? 0U : (failed == 2U ? 1U : 513U);
    const UmicomSize uncertain = failed == 1U ? 1U : (failed == 2U ? 512U : 187U);
    CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED);
    CHECK(updateResult.lastBlockOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    CHECK(updateResult.blockStatus == UMICOM_BLOCK_IO_ERROR && updateResult.diskStatus == UMICOM_DISK_OK);
    CHECK(updateResult.confirmedBytes == confirmed && updateResult.submittedBytes == confirmed + uncertain);
    CHECK(updateResult.completedSectors == failed - 1U && updateResult.submittedSectors == failed);
    CHECK(updateResult.uncertainOffset == 511U + confirmed && updateResult.uncertainBytes == uncertain);
    CHECK(updateResult.uncertainSector == UMICOM_DISK_FIXTURE_DATA + UpdateFileCluster(failed - 1U) - 2U);
    CHECK(updateResult.needsFlush && updateResult.writeUncertain && updater.needsFlush && updater.writeUncertain);
    CHECK(updateWriteRequests == failed && updateFlushRequests == 0U);
    for (UmicomSize i = 0U; i < confirmed + uncertain; ++i) {
        const UmicomU64 position = 511U + i;
        if (i < confirmed || position % 512U < 256U)
            updateExpected[UpdateFileAddress(position)] = updateInputSnapshot[i];
    }
    UpdateEqual(updateVisible, updateExpected, sizeof(updateVisible));
    UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
    const UmicomKernelFat16UpdateResult evidence = updater.lastWrite;
    CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN);
    UpdateEqual(&updateResult, &updateResultSnapshot, sizeof(updateResult));
    UpdateEqual(&evidence, &updater.lastWrite, sizeof(evidence));
    CHECK(updateWriteRequests == failed);
    if (!strcmp(name, "uncertain_flush_and_retry")) {
        updateFailFlush = 1U;
        CHECK(UmicomKernelFat16UpdateFlush(&updater, &updateFlushOutcome) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(updateFlushOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED && updater.needsFlush && updater.writeUncertain);
        UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
        UpdateEqual(&evidence, &updater.lastWrite, sizeof(evidence));
        updateFailFlush = 0U;
        UpdateFlushSuccess();
        CHECK(updater.writeUncertain && !updater.needsFlush);
        CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN);
        UpdateEqual(&updateResult, &updateResultSnapshot, sizeof(updateResult));
        UpdateEqual(&evidence, &updater.lastWrite, sizeof(evidence));
        CHECK(updateWriteRequests == failed && updateFlushRequests == 2U);
    }
    const UmicomU32 flushed = updateFlushRequests;
    UpdateCloseOwner();
    CHECK(updateFlushRequests == flushed && updater.writeUncertain);
    UpdateEqual(&evidence, &updater.lastWrite, sizeof(evidence));
}
static void UpdateKnownPrefix(void)
{
    UpdateOpenOwner(); updateNoPermissionAfterWrites = 1U;
    CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
    CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED);
    CHECK(updateResult.confirmedBytes == 1U && updateResult.submittedBytes == 1U);
    CHECK(updateResult.completedSectors == 1U && updateResult.submittedSectors == 1U);
    CHECK(updateResult.lastBlockOutcome == UMICOM_BLOCK_NOT_SUBMITTED);
    CHECK(!updateResult.uncertainBytes && !updater.writeUncertain && updater.needsFlush);
    UpdatePatchExpected(511U, 1U);
    UpdateEqual(updateVisible, updateExpected, sizeof(updateVisible));
    UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
    updateNoPermissionAfterWrites = 0U;
    UpdateFlushSuccess(); UpdateCloseOwner();
}
static void UpdateClockCase(const char *name)
{
    UpdateOpenOwner();
    const UmicomBoolean outer = strstr(name, "outer_") ? UMICOM_TRUE : UMICOM_FALSE;
    const UmicomBoolean rollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    if (outer) { updateOuterClockFailure = UMICOM_TRUE; updateOuterClockRollback = rollback; }
    else { updateFinalClockFailure = UMICOM_TRUE; updateFinalClockRollback = rollback; }
    CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 512U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(updateFinalClockInjected && updateWriteRequests == 1U);
    CHECK(updateResult.blockStatus == (rollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    CHECK(updateResult.outcome == (outer ? UMICOM_FAT16_UPDATE_COMPLETED : UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED));
    CHECK(updateResult.confirmedBytes == (outer ? 512U : 0U) && updateResult.submittedBytes == 512U);
    CHECK(updateResult.completedSectors == (outer ? 1U : 0U) && updateResult.submittedSectors == 1U);
    CHECK(updateResult.writeUncertain == (outer ? UMICOM_FALSE : UMICOM_TRUE));
    UpdatePatchExpected(0U, 512U);
    UpdateEqual(updateVisible, updateExpected, sizeof(updateVisible));
    UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
    UpdateCloseOwner();
}
static void UpdateReadFailure(const char *fixture, const char *name)
{
    UpdateOpenOwner();
    CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 finalRead = updateReadRequests;
    CHECK(finalRead > 3U);
    CHECK(updateReadSectors[finalRead - 1U] == UMICOM_DISK_FIXTURE_DATA + 4U);
    UpdateCloseOwner();
    UpdateStart(fixture); UpdateOpenOwner();
    const UmicomBoolean clockFailure = strstr(name, "clock_") ? UMICOM_TRUE : UMICOM_FALSE;
    if (clockFailure) {
        updateReadClockAt = finalRead;
        updateReadClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    } else updateFailRead = !strcmp(name, "first_preflight_read_failure") ? updateReadRequests + 1U : finalRead;
    CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && updateResult.confirmedBytes == 0U);
    CHECK(updateResult.submittedBytes == 0U && updateResult.submittedSectors == 0U);
    CHECK(updateResult.diskStatus == UMICOM_DISK_IO_ERROR);
    CHECK(updateResult.blockStatus == (clockFailure ?
        (updateReadClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT) : UMICOM_BLOCK_IO_ERROR));
    if (clockFailure) CHECK(updateFinalClockInjected && updateReadRequests == finalRead);
    CHECK(!updater.volume.open && !updater.needsFlush && !updater.writeUncertain);
    UpdateUnchangedMedia();
    updateFailRead = 0U; updateReadClockAt = 0U;
    CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
    UpdateKnownSuccess(511U, 700U);
    UpdateCloseOwner();
}
static void UpdateLifecycleCase(const char *name)
{
    if (!strcmp(name, "zero_close")) {
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_OK);
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_OK);
        CHECK(model.notifications == 0U && !Allocated()); return;
    }
    if (!strcmp(name, "readonly_admission_retry")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_READ_ONLY);
        CHECK(!updater.handle && !updater.admitted && !Allocated());
        model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    } else if (!strcmp(name, "no_flush_admission_retry")) {
        model.featuresLow &= ~UMICOM_VIRTIO_FEATURE_FLUSH;
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!updater.handle && !updater.admitted && !Allocated());
        model.featuresLow |= UMICOM_VIRTIO_FEATURE_FLUSH;
    } else if (!strcmp(name, "failed_open_retained")) {
        model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(updater.handle && updater.state == UMICOM_FAT16_UPDATER_CLOSING && !updater.admitted);
        CHECK(Allocated() == 2U && model.freeCalls == 0U);
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && model.freeCalls == 0U);
        model.stuckReset = UMICOM_FALSE; model.refuseDriver = UMICOM_FALSE; model.stickAfterDriver = UMICOM_FALSE;
        UpdateCloseOwner(); CHECK(updater.state == UMICOM_FAT16_UPDATER_UNUSED);
    } else if (!strcmp(name, "allocation_failure_retry")) {
        model.failAllocate = 2U;
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!updater.handle && !updater.admitted && !Allocated()); model.failAllocate = 0U;
    } else if (!strcmp(name, "unsafe_admission_retry")) {
        model.allowed = UMICOM_FALSE;
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(!model.reads && !model.writes && !updater.handle); model.allowed = UMICOM_TRUE;
    }
    UpdateOpenOwner();
    if (!strcmp(name, "single_lifetime")) {
        UpdateCloseOwner();
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_OK); return;
    }
    if (!strcmp(name, "reset_retry")) {
        model.stuckReset = UMICOM_TRUE;
        const UmicomKernelBlockHandle handle = updater.handle;
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(updater.handle == handle && updater.state == UMICOM_FAT16_UPDATER_CLOSING);
        CHECK(Allocated() == 2U && model.freeCalls == 0U);
        CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_BAD_STATE);
        UpdateEqual(&updateResult, &updateResultSnapshot, sizeof(updateResult));
        model.stuckReset = UMICOM_FALSE;
    } else if (!strcmp(name, "release_retry") || !strcmp(name, "second_release_retry")) {
        const UmicomBoolean second = !strcmp(name, "second_release_retry") ? UMICOM_TRUE : UMICOM_FALSE;
        model.failFree = second ? 2U : 1U;
        const UmicomKernelBlockHandle handle = updater.handle;
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(updater.handle == handle && updater.state == UMICOM_FAT16_UPDATER_CLOSING);
        CHECK(Allocated() == (second ? 1U : 2U)); model.failFree = 0U;
    } else if (!strcmp(name, "timeout_retained")) {
        updateHangWrite = 1U; model.stuckReset = UMICOM_TRUE;
        CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED);
        CHECK(updater.writeUncertain && updater.needsFlush && Allocated() == 2U && !model.freeCalls);
        CHECK(UmicomKernelFat16UpdateClose(&updater) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls); model.stuckReset = UMICOM_FALSE;
    } else if (!strcmp(name, "close_without_flush") || !strcmp(name, "power_loss_unflushed") ||
        !strcmp(name, "power_loss_flushed")) {
        CHECK(UpdateWriteAt("/FRAG.BIN", 511U, 700U) == UMICOM_FAT16_UPDATE_OK);
        UpdateKnownSuccess(511U, 700U);
        if (!strcmp(name, "power_loss_flushed")) UpdateFlushSuccess();
        const UmicomU32 previousFlushes = updateFlushRequests;
        const UmicomKernelFat16UpdateResult evidence = updater.lastWrite;
        UpdateCloseOwner(); CHECK(updateFlushRequests == previousFlushes);
        UpdateEqual(&evidence, &updater.lastWrite, sizeof(evidence));
        if (strcmp(name, "close_without_flush")) {
            memcpy(updateVisible, updateDurable, sizeof(updateVisible));
            UpdateEqual(updateVisible, !strcmp(name, "power_loss_flushed") ? updateExpected : updateInitial,
                sizeof(updateVisible));
        }
        return;
    } else if (!strcmp(name, "empty_flush")) { UpdateFlushSuccess(); CHECK(updateWriteRequests == 0U); }
    UpdateCloseOwner();
}
static void UpdateOwnershipCase(const char *name)
{
    UpdateOpenOwner();
    const UmicomKernelFat16UpdateResult saved = updater.lastWrite;
    const UmicomU32 reads = updateReadRequests;
    UmicomKernelFat16UpdateStatus status = UMICOM_FAT16_UPDATE_OK;
    if (!strcmp(name, "input_owner_alias")) status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, &updater, 1U, &updateResult);
    else if (!strcmp(name, "input_domain_alias")) status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, &domain, 1U, &updateResult);
    else if (!strcmp(name, "input_dma_alias")) {
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, (const void *)domain.slots[0].dataFrame, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, (const void *)domain.slots[0].queueFrame, 1U, &updateResult);
    } else if (!strcmp(name, "other_slot_dma_alias")) {
        UmicomAddress extra = 0U;
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&extra) == UMICOM_KERNEL_MEMORY_OK);
        domain.count = 2U; domain.slots[1].dataFrame = extra;
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, (const void *)extra, 1U, &updateResult);
        domain.count = 1U; domain.slots[1].dataFrame = 0U;
        CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(extra) == UMICOM_KERNEL_MEMORY_OK);
    } else if (!strcmp(name, "input_span_overflow"))
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, (const void *)(~(UmicomAddress)0U - 15U), 32U, &updateResult);
    else if (!strcmp(name, "result_span_overflow"))
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U,
            (UmicomKernelFat16UpdateResult *)(~(UmicomAddress)0U & ~((UmicomAddress)alignof(UmicomKernelFat16UpdateResult) - 1U)));
    else if (!strcmp(name, "result_owner_alias")) {
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U, &updater.lastWrite);
    } else if (!strcmp(name, "result_domain_alias")) {
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U,
            (UmicomKernelFat16UpdateResult *)(void *)&domain);
    } else if (!strcmp(name, "result_dma_alias")) {
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U,
            (UmicomKernelFat16UpdateResult *)domain.slots[0].queueFrame);
    } else if (!strcmp(name, "result_alignment")) {
        _Alignas(UmicomKernelFat16UpdateResult) UmicomU8 storage[sizeof(UmicomKernelFat16UpdateResult) + 8U];
        memset(storage, 0x4d, sizeof(storage));
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U,
            (UmicomKernelFat16UpdateResult *)(void *)(storage + 1U));
        for (UmicomSize i = 0U; i < sizeof(storage); ++i) CHECK(storage[i] == 0x4dU);
    } else if (!strcmp(name, "input_result_alias"))
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, &updateResult, 1U, &updateResult);
    else if (!strcmp(name, "path_input_alias")) {
        memcpy(updateInput, "/FRAG.BIN", 10U); memcpy(updateInputSnapshot, updateInput, sizeof(updateInput));
        status = UmicomKernelFat16UpdateWrite(&updater, (const char *)updateInput, 0U, updateInput + 2U, 1U, &updateResult);
    } else if (!strcmp(name, "path_input_nonterminated")) {
        memset(updateInput, 0xa5, sizeof(updateInput)); memcpy(updateInputSnapshot, updateInput, sizeof(updateInput));
        status = UmicomKernelFat16UpdateWrite(&updater, (const char *)updateInput, 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    } else if (!strcmp(name, "path_result_nonterminated")) {
        /* Ownership must be refused before searching the result record for a
         * terminator. The record deliberately contains none: reading past it
         * would be a sanitizer failure rather than a useful path validation. */
        status = UmicomKernelFat16UpdateWrite(&updater, (const char *)&updateResult, 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    } else if (!strcmp(name, "path_owner_alias"))
        status = UmicomKernelFat16UpdateWrite(&updater, (const char *)&updater.plan, 0U, updateInput, 1U, &updateResult);
    else if (!strcmp(name, "path_domain_alias"))
        status = UmicomKernelFat16UpdateWrite(&updater, (const char *)&domain, 0U, updateInput, 1U, &updateResult);
    else if (!strcmp(name, "path_dma_alias"))
        status = UmicomKernelFat16UpdateWrite(&updater, (const char *)domain.slots[0].dataFrame, 0U, updateInput, 1U, &updateResult);
    else if (!strcmp(name, "owner_alignment"))
        status = UmicomKernelFat16UpdateWrite((UmicomKernelFat16Updater *)(void *)((UmicomU8 *)&updater + 1U),
            "/FRAG.BIN", 0U, updateInput, 1U, &updateResult);
    else if (!strcmp(name, "unterminated_path")) {
        char path[UMICOM_FAT16_PATH_BYTES]; memset(path, 'A', sizeof(path)); path[0] = '/';
        status = UmicomKernelFat16UpdateWrite(&updater, path, 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_INSPECTION_LIMIT);
    } else if (!strcmp(name, "copied_owner")) {
        static UmicomKernelFat16Updater copied; memcpy(&copied, &updater, sizeof(copied));
        status = UmicomKernelFat16UpdateWrite(&copied, "/FRAG.BIN", 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(UmicomKernelFat16UpdateClose(&copied) == UMICOM_FAT16_UPDATE_BAD_STATE);
    } else if (!strcmp(name, "busy_owner")) {
        updater.busy = UMICOM_TRUE;
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_BUSY); updater.busy = UMICOM_FALSE;
    } else if (!strcmp(name, "busy_domain")) {
        domain.busy = UMICOM_TRUE;
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_BUSY); domain.busy = UMICOM_FALSE;
    } else if (!strcmp(name, "unsafe_write")) {
        model.allowed = UMICOM_FALSE;
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U, &updateResult);
        CHECK(status == UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT); model.allowed = UMICOM_TRUE;
    } else if (!strcmp(name, "null_and_oversized")) {
        CHECK(UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 0U, &updateResult) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 4097U, &updateResult) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateWrite(&updater, 0, 0U, updateInput, 1U, &updateResult) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, 0, 1U, &updateResult) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        status = UmicomKernelFat16UpdateWrite(&updater, "/FRAG.BIN", 0U, updateInput, 1U, 0);
    } else CHECK(0);
    CHECK(status != UMICOM_FAT16_UPDATE_OK);
    UpdateEqual(&updateResult, &updateResultSnapshot, sizeof(updateResult));
    UpdateEqual(&saved, &updater.lastWrite, sizeof(saved));
    CHECK(updateReadRequests == reads);
    UpdateUnchangedMedia(); UpdateCloseOwner();
}
static void UpdateOpenArguments(const char *name)
{
    UmicomKernelFat16UpdateStatus status = UMICOM_FAT16_UPDATE_OK;
    const UmicomKernelBlockDomain saved = domain;
    if (!strcmp(name, "domain_alias"))
        status = UmicomKernelFat16UpdateOpen((UmicomKernelFat16Updater *)(void *)&domain, &domain, 0U, 0U, 32U);
    else if (!strcmp(name, "dma_alias")) {
        UmicomAddress frame = 0U;
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&frame) == UMICOM_KERNEL_MEMORY_OK);
        domain.slots[0].dataFrame = frame;
        status = UmicomKernelFat16UpdateOpen((UmicomKernelFat16Updater *)frame, &domain, 0U, 0U, 32U);
        domain.slots[0].dataFrame = 0U;
        CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(frame) == UMICOM_KERNEL_MEMORY_OK);
    } else if (!strcmp(name, "alignment"))
        status = UmicomKernelFat16UpdateOpen((UmicomKernelFat16Updater *)(void *)((UmicomU8 *)&updater + 1U), &domain, 0U, 0U, 32U);
    else if (!strcmp(name, "numeric_limits")) {
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 0U) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U,
            (UmicomU64)UMICOM_BLOCK_MAX_TIMEOUT_TICKS + 1U) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, UMICOM_BLOCK_SLOT_LIMIT, 0U, 32U) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        status = UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, UMICOM_DISK_PRIMARY_PARTITIONS, 32U);
    } else CHECK(0);
    CHECK(status == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    UpdateEqual(&saved, &domain, sizeof(domain));
    CHECK(!model.reads && !model.writes && !updater.handle && !Allocated());
    UpdateUnchangedMedia();
}
static void UpdateRangeCase(const char *name)
{
    UpdateOpenOwner();
    const char *path = "/FRAG.BIN"; UmicomU64 offset = 1300U; UmicomSize bytes = 1U;
    if (!strcmp(name, "past_eof")) offset = 1301U;
    else if (!strcmp(name, "extends_file")) { offset = 1299U; bytes = 2U; }
    else if (!strcmp(name, "offset_overflow")) offset = ~(UmicomU64)0U;
    else if (!strcmp(name, "empty_file")) { path = "/EMPTY.TXT"; offset = 0U; }
    else if (!strcmp(name, "directory_target")) { path = "/DOCS"; offset = 0U; }
    else if (!strcmp(name, "missing_file")) { path = "/ABSENT.TXT"; offset = 0U; }
    else if (!strcmp(name, "readonly_readme")) { path = "/README.TXT"; offset = 0U; }
    else if (!strcmp(name, "readonly_nested")) { path = "/DOCS/GUIDE.TXT"; offset = 0U; }
    else if (!strcmp(name, "relative_path")) { path = "FRAG.BIN"; offset = 0U; }
    else if (!strcmp(name, "dotdot_path")) { path = "/DOCS/../FRAG.BIN"; offset = 0U; }
    else if (!strcmp(name, "trailing_slash")) { path = "/FRAG.BIN/"; offset = 0U; }
    else if (!strcmp(name, "root_target")) { path = "/"; offset = 0U; }
    const UmicomKernelFat16UpdateStatus status = UpdateWriteAt(path, offset, bytes);
    CHECK(status != UMICOM_FAT16_UPDATE_OK && updateResult.status == status);
    CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && updateResult.requestedBytes == bytes);
    CHECK(!updateResult.confirmedBytes && !updateResult.submittedBytes && !updateResult.uncertainBytes);
    UpdateUnchangedMedia(); UpdateCloseOwner();
}
static UmicomKernelFat16 planVolume;
static UmicomKernelFat16UpdateWorkspace planWorkspace;
static UmicomKernelFat16UpdatePlan directPlan, directPlanBefore;
static UmicomU32 directReads, directFailRead, directReentries;
static UmicomBoolean directReenter, directChangeInput;
static UmicomBoolean UpdateDirectRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    CHECK(context == &planVolume && sector < UMICOM_DISK_FIXTURE_SECTORS);
    ++directReads;
    if (directChangeInput) { memset(updateInput, 0x55, sizeof(updateInput)); directChangeInput = UMICOM_FALSE; }
    if (directReenter) {
        static UmicomKernelFat16UpdatePlan ignored;
        CHECK(UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U,
            updateInput, 1U, &planWorkspace, &ignored) == UMICOM_DISK_BUSY);
        CHECK(UmicomKernelFat16Close(&planVolume) == UMICOM_DISK_BUSY); ++directReentries;
    }
    if (directFailRead == directReads) { memset(output, 0xcd, 512U); return UMICOM_FALSE; }
    memcpy(output, updateVisible + (UmicomSize)sector * 512U, 512U); return UMICOM_TRUE;
}
static void UpdatePlanCase(const char *name)
{
    memset(&planVolume, 0, sizeof(planVolume)); memset(&planWorkspace, 0, sizeof(planWorkspace));
    memset(&directPlan, 0xa4, sizeof(directPlan)); memcpy(&directPlanBefore, &directPlan, sizeof(directPlan));
    directReads = 0U; directFailRead = 0U; directReentries = 0U;
    directReenter = UMICOM_FALSE; directChangeInput = UMICOM_FALSE;
    if (!strcmp(name, "io_budget")) CHECK(UpdateCorrupt("io_budget") == UMICOM_DISK_LIMIT);
    const UmicomKernelDiskReader reader = {UMICOM_DISK_FIXTURE_SECTORS, UpdateDirectRead, &planVolume};
    CHECK(UmicomKernelFat16Open(&planVolume, &reader, 0U) == UMICOM_DISK_OK);
    const UmicomU32 firstReads = directReads;
    UmicomKernelDiskStatus status;
    if (!strcmp(name, "input_alias"))
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, &planWorkspace,
            1U, &planWorkspace, &directPlan);
    else if (!strcmp(name, "plan_alias"))
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, &directPlan,
            1U, &planWorkspace, &directPlan);
    else if (!strcmp(name, "workspace_alias"))
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, updateInput,
            1U, (UmicomKernelFat16UpdateWorkspace *)(void *)&planVolume, &directPlan);
    else if (!strcmp(name, "path_alias"))
        status = UmicomKernelFat16PlanUpdate(&planVolume, (const char *)&planWorkspace, 0U, updateInput,
            1U, &planWorkspace, &directPlan);
    else if (!strcmp(name, "alignment")) {
        _Alignas(UmicomKernelFat16UpdatePlan) UmicomU8 buffer[sizeof(UmicomKernelFat16UpdatePlan) + 8U];
        memset(buffer, 0x45, sizeof(buffer));
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, updateInput,
            1U, &planWorkspace, (UmicomKernelFat16UpdatePlan *)(void *)(buffer + 1U));
        for (UmicomSize i = 0U; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0x45U);
    } else if (!strcmp(name, "span_overflow"))
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U,
            (const void *)(~(UmicomAddress)0U - 15U), 32U, &planWorkspace, &directPlan);
    else if (!strcmp(name, "dirty_workspace")) {
        planWorkspace.owned[2U] = 1U;
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, updateInput, 1U, &planWorkspace, &directPlan);
        CHECK(status == UMICOM_DISK_BAD_STATE);
    } else if (!strcmp(name, "copied_workspace")) {
        static UmicomKernelFat16UpdateWorkspace foreign;
        planWorkspace.self = &foreign;
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, updateInput, 1U, &planWorkspace, &directPlan);
        CHECK(status == UMICOM_DISK_BAD_STATE);
    } else if (!strcmp(name, "geometry_divisor")) {
        planVolume.info.sectorsPerCluster = 0U;
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, updateInput, 1U, &planWorkspace, &directPlan);
        CHECK(status == UMICOM_DISK_BAD_STATE);
    } else if (!strcmp(name, "fat_sector_limit")) {
        planVolume.info.sectorsPerFat = 257U;
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 0U, updateInput, 1U, &planWorkspace, &directPlan);
        CHECK(status == UMICOM_DISK_LIMIT);
    } else {
        if (!strcmp(name, "reentry")) directReenter = UMICOM_TRUE;
        else if (!strcmp(name, "input_snapshot")) directChangeInput = UMICOM_TRUE;
        else if (!strcmp(name, "read_failure_atomic")) directFailRead = directReads + 2U;
        else CHECK(!strcmp(name, "complete_plan") || !strcmp(name, "io_budget"));
        status = UmicomKernelFat16PlanUpdate(&planVolume, "/FRAG.BIN", 511U, updateInput,
            700U, &planWorkspace, &directPlan);
        if (!strcmp(name, "read_failure_atomic")) CHECK(status == UMICOM_DISK_IO_ERROR);
        else if (!strcmp(name, "io_budget")) {
            CHECK(status == UMICOM_DISK_LIMIT);
            CHECK(directReads - firstReads == UMICOM_FAT16_IO_LIMIT);
        }
        else {
            CHECK(status == UMICOM_DISK_OK);
            CHECK(directPlan.offset == 511U && directPlan.bytes == 700U && directPlan.count == 3U);
            CHECK(directPlan.entry.bytes == 1300U && directPlan.entry.firstCluster == 4U);
            UmicomSize consumed = 0U;
            for (UmicomSize i = 0U; i < directPlan.count; ++i) {
                const UmicomKernelFat16UpdateSector *sector = &directPlan.sectors[i];
                const UmicomSize expectedOffset = i ? 0U : 511U;
                const UmicomSize expectedBytes = i == 0U ? 1U : (i == 1U ? 512U : 187U);
                CHECK(sector->sector == UMICOM_DISK_FIXTURE_DATA + UpdateFileCluster(i) - 2U);
                CHECK(sector->offset == expectedOffset && sector->bytes == expectedBytes && sector->inputOffset == consumed);
                for (UmicomSize j = 0U; j < 512U; ++j) {
                    const UmicomU8 expected = j >= expectedOffset && j < expectedOffset + expectedBytes ?
                        updateInputSnapshot[consumed + j - expectedOffset] :
                        updateInitial[(UmicomSize)sector->sector * 512U + j];
                    CHECK(sector->data[j] == expected);
                }
                consumed += expectedBytes;
            }
            CHECK(consumed == 700U);
            if (directReenter) CHECK(directReentries > 0U);
            CHECK(planWorkspace.self == &planWorkspace && !planWorkspace.busy);
            const UmicomU8 *scratch = (const UmicomU8 *)&planWorkspace;
            for (UmicomSize i = sizeof(planWorkspace.self); i < sizeof(planWorkspace); ++i) CHECK(scratch[i] == 0U);
        }
        directReenter = UMICOM_FALSE;
        CHECK(UmicomKernelFat16Close(&planVolume) == UMICOM_DISK_OK);
        if (status != UMICOM_DISK_OK) UpdateEqual(&directPlan, &directPlanBefore, sizeof(directPlan));
        CHECK(updateWriteRequests == 0U && updateFlushRequests == 0U);
        UpdateEqual(updateVisible, updateInitial, sizeof(updateVisible));
        return;
    }
    CHECK(status != UMICOM_DISK_OK && directReads == firstReads);
    UpdateEqual(&directPlan, &directPlanBefore, sizeof(directPlan));
    CHECK(UmicomKernelFat16Close(&planVolume) == UMICOM_DISK_OK);
}
static void UpdateReadOnlyBoundary(void)
{
    static UmicomKernelDiskMount mount;
    static UmicomKernelVfsClient client;
    memset(&mount, 0, sizeof(mount)); memset(&client, 0, sizeof(client));
    CHECK(UmicomKernelDiskMountOpen(&mount, &domain, 0U, 0U, 32U) == UMICOM_VFS_READ_ONLY);
    CHECK(!mount.handle && !Allocated());
    model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
    model.expectedDriverLow = UMICOM_VIRTIO_READ_ONLY;
    CHECK(UmicomKernelDiskMountOpen(&mount, &domain, 0U, 0U, 32U) == UMICOM_VFS_OK);
    CHECK(UmicomKernelDiskMountClientOpen(&mount, &client, 7U, UMICOM_VFS_RIGHT_WRITE) == UMICOM_VFS_ACCESS_DENIED);
    CHECK(UmicomKernelFat16UpdateOpen(&updater, &domain, 0U, 0U, 32U) != UMICOM_FAT16_UPDATE_OK);
    CHECK(mount.handle && Allocated() == 2U && !updater.handle);
    CHECK(UmicomKernelDiskMountClose(&mount) == UMICOM_VFS_OK);
    UpdateUnchangedMedia();
}
static void UpdateMiscCase(const char *name)
{
    if (!strcmp(name, "status_names")) {
        for (unsigned i = 0U; i <= (unsigned)UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN; ++i)
            CHECK(strcmp(UmicomKernelFat16UpdateStatusName((UmicomKernelFat16UpdateStatus)i), "unknown-fat16-update-status") != 0);
        CHECK(!strcmp(UmicomKernelFat16UpdateStatusName((UmicomKernelFat16UpdateStatus)999), "unknown-fat16-update-status"));
        return;
    }
    if (!strcmp(name, "readonly_vfs_boundary")) { UpdateReadOnlyBoundary(); return; }
    if (!strcmp(name, "known_prefix_no_later_submission")) { UpdateKnownPrefix(); return; }
    UpdateOpenOwner();
    if (!strcmp(name, "callback_reentry") || !strcmp(name, "policy_reentry")) {
        updatePolicyReenter = !strcmp(name, "policy_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        updateReenter = UMICOM_TRUE;
        CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_OK);
        CHECK(updateReentries > 0U); updateReenter = UMICOM_FALSE; updatePolicyReenter = UMICOM_FALSE;
    } else if (!strcmp(name, "flush_output_ownership")) {
        const UmicomKernelBlockMutationOutcome old = updater.lastFlush;
        CHECK(UmicomKernelFat16UpdateFlush(&updater, &updater.lastFlush) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(updater.lastFlush == old && !updateFlushRequests);
        CHECK(UmicomKernelFat16UpdateFlush(&updater, (UmicomKernelBlockMutationOutcome *)(void *)&domain) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateFlush(&updater, (UmicomKernelBlockMutationOutcome *)domain.slots[0].dataFrame) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        _Alignas(UmicomKernelBlockMutationOutcome) UmicomU8 bytes[8] = {0};
        CHECK(UmicomKernelFat16UpdateFlush(&updater, (UmicomKernelBlockMutationOutcome *)(void *)(bytes + 1U)) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16UpdateFlush(&updater, 0) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        CHECK(!updateFlushRequests);
    } else if (!strcmp(name, "revalidate_between_writes")) {
        CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_OK);
        const UmicomU32 writes = updateWriteRequests;
        UpdateFat(20U, 9U);
        CHECK(UpdateWriteAt("/FRAG.BIN", 1U, 1U) == UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
        CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && updater.needsFlush);
        CHECK(updateWriteRequests == writes && !updater.writeUncertain);
    } else if (!strcmp(name, "stopped_preflight_clock") || !strcmp(name, "backward_preflight_clock")) {
        if (!strcmp(name, "stopped_preflight_clock")) { model.step = 0U; model.noCompletion = UMICOM_TRUE; }
        else updateClockRollback = UMICOM_TRUE;
        CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED && !updateResult.submittedBytes);
        CHECK(updateResult.blockStatus == (!strcmp(name, "stopped_preflight_clock") ?
            UMICOM_BLOCK_TIMEOUT : UMICOM_BLOCK_CLOCK_ERROR) && updateResult.diskStatus == UMICOM_DISK_IO_ERROR);
        UpdateUnchangedMedia(); updateClockRollback = UMICOM_FALSE; model.step = 1U; model.noCompletion = UMICOM_FALSE;
    } else CHECK(0);
    UpdateCloseOwner();
}
static void UpdateRepeatedSuccess(void)
{
    UpdateLargeFile(); UpdateOpenOwner();
    for (UmicomSize attempt = 0U; attempt < 4U; ++attempt) {
        for (UmicomSize i = 0U; i < 4096U; ++i)
            updateInput[i] = (UmicomU8)(i * 17U + attempt * 43U + 0x83U);
        memcpy(updateInputSnapshot, updateInput, sizeof(updateInput));
        CHECK(UpdateWriteAt("/FRAG.BIN", 1U, 4096U) == UMICOM_FAT16_UPDATE_OK);
        CHECK(updateResult.outcome == UMICOM_FAT16_UPDATE_COMPLETED && updateResult.confirmedBytes == 4096U);
        CHECK(updateResult.submittedBytes == 4096U && updateResult.completedSectors == 9U && updateResult.submittedSectors == 9U);
        CHECK(updater.needsFlush && !updater.writeUncertain && !updater.volume.open && !updater.workspace.busy);
        CHECK(updater.workspace.self == &updater.workspace);
        UpdatePatchExpected(1U, 4096U);
        UpdateEqual(updateVisible, updateExpected, sizeof(updateVisible));
        UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
        CHECK(updateWriteRequests == (attempt + 1U) * 9U && !updateFlushRequests);
    }
    UpdateFlushSuccess(); UpdateCloseOwner();
}
static void UpdateFlushClockCase(const char *name)
{
    UpdateOpenOwner();
    CHECK(UpdateWriteAt("/FRAG.BIN", 0U, 1U) == UMICOM_FAT16_UPDATE_OK);
    UpdateKnownSuccess(0U, 1U);
    const UmicomKernelFat16UpdateResult evidence = updater.lastWrite;
    updateFlushClockFailure = UMICOM_TRUE;
    updateFlushClockOuter = strstr(name, "outer_") ? UMICOM_TRUE : UMICOM_FALSE;
    updateFlushClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(UmicomKernelFat16UpdateFlush(&updater, &updateFlushOutcome) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(updateFinalClockInjected && updateFlushRequests == 1U);
    CHECK(updateFlushOutcome == (updateFlushClockOuter ? UMICOM_BLOCK_COMPLETED : UMICOM_BLOCK_SUBMITTED_UNCONFIRMED));
    CHECK(updater.lastBlockStatus == (updateFlushClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    CHECK(updater.needsFlush == (updateFlushClockOuter ? UMICOM_FALSE : UMICOM_TRUE));
    CHECK(!updater.writeUncertain);
    UpdateEqual(&evidence, &updater.lastWrite, sizeof(evidence));
    /* The device performed the copy before the driver's or owner's final
     * time observation. Its caller must still respect the rejected result. */
    UpdateEqual(updateDurable, updateExpected, sizeof(updateDurable));
    UpdateCloseOwner();
}
static UmicomKernelConsoleShell updateShell, updateForeignShell;
static UmicomBoolean updateOutputReenter;
static void UpdateClearTranscript(void)
{
    memset(transcript, 0, sizeof(transcript)); transcriptBytes = 0U;
}
static UmicomKernelShellStatus UpdateConsoleCommand(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_FALSE;
    const UmicomKernelShellStatus parsed = UmicomKernelShellParse(text, strlen(text), &command);
    if (parsed != UMICOM_SHELL_OK) return parsed;
    const UmicomKernelShellStatus status = UmicomKernelFat16UpdateCommand(shell, &command, &handled);
    CHECK(handled);
    return status;
}
static void UpdateConsoleReentry(void)
{
    CHECK(UpdateConsoleCommand(&updateShell, "fatwriteinfo") == UMICOM_SHELL_BUSY);
    CHECK(UmicomKernelFat16UpdateConsoleClose(&updateShell) == UMICOM_FAT16_UPDATE_BUSY);
    ++updateReentries;
}
static void UpdateConsoleOutput(void *context, const char *text, UmicomSize bytes)
{
    Output(context, text, bytes);
    if (updateOutputReenter) UpdateConsoleReentry();
}
static void UpdateConsoleOpen(void)
{
    CHECK(UpdateConsoleCommand(&updateShell, "fatwriteopen 0 0") == UMICOM_SHELL_OK);
    CHECK(strstr(transcript, "fat.open=ok") && Allocated() == 2U);
    UpdateClearTranscript();
}
static void UpdateConsoleClose(void)
{
    CHECK(UpdateConsoleCommand(&updateShell, "fatwriteclose") == UMICOM_SHELL_OK);
    CHECK(Allocated() == 0U && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
}
static void UpdateConsoleCase(const char *name)
{
    memset(&updateShell, 0, sizeof(updateShell)); memset(&updateForeignShell, 0, sizeof(updateForeignShell));
    updateShell.output = UpdateConsoleOutput; updateForeignShell.output = UpdateConsoleOutput;
    updateOutputReenter = UMICOM_FALSE;
    if (!strcmp(name, "before_open")) {
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "fat.update-state=unused")); UpdateClearTranscript();
        CHECK(UpdateConsoleCommand(&updateShell, "fatwrite /FRAG.BIN 0 hello") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.write-result=not-admitted")); UpdateClearTranscript();
        CHECK(UpdateConsoleCommand(&updateShell, "fatflush") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.flush-outcome=unchanged"));
        CHECK(UmicomKernelFat16UpdateConsoleClose(&updateShell) == UMICOM_FAT16_UPDATE_OK);
        UpdateUnchangedMedia(); return;
    }
    if (!strcmp(name, "invalid_arguments")) {
        const char *invalid[] = {"fatwriteopen", "fatwriteopen 0", "fatwriteopen 8 0", "fatwriteopen 0 4",
            "fatwriteopen -1 0", "fatwriteopen 18446744073709551616 0", "fatwrite /FRAG.BIN x text",
            "fatwrite /FRAG.BIN 0", "fatflush extra", "fatwriteinfo extra", "fatwriteclose extra"};
        for (UmicomSize i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
            CHECK(UpdateConsoleCommand(&updateShell, invalid[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!model.reads && !model.writes);
        UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_TRUE;
        CHECK(UmicomKernelFat16UpdateCommand(0, &command, &handled) == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16UpdateCommand(&updateShell, 0, &handled) == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16UpdateCommand(&updateShell, &command, 0) == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelShellParse("unrelated", 9U, &command) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelFat16UpdateCommand(&updateShell, &command, &handled) == UMICOM_SHELL_OK && !handled);
        CHECK(UmicomKernelFat16UpdateConsoleClose(0) == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
        UpdateUnchangedMedia(); return;
    }
    if (!strcmp(name, "no_device_retry")) {
        model.device = 0U;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(!Allocated() && !updateWriteRequests); model.device = 2U;
    } else if (!strcmp(name, "readonly_retry")) {
        model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.open=read-only") && !Allocated()); model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    } else if (!strcmp(name, "platform_retry")) {
        updatePlatformStatus = UMICOM_BLOCK_NO_CATALOGUE;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.transport=no-catalogue") && !Allocated()); updatePlatformStatus = UMICOM_BLOCK_OK;
    } else if (!strcmp(name, "failed_open_retained")) {
        model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.update-state=closing") && Allocated() == 2U && !model.freeCalls);
        CHECK(UmicomKernelFat16UpdateConsoleClose(&updateShell) == UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated() == 2U && !model.freeCalls);
        model.stuckReset = UMICOM_FALSE; model.refuseDriver = UMICOM_FALSE; model.stickAfterDriver = UMICOM_FALSE;
        CHECK(UmicomKernelFat16UpdateConsoleClose(&updateShell) == UMICOM_FAT16_UPDATE_OK);
        CHECK(!Allocated());
    }
    UpdateConsoleOpen();
    if (!strcmp(name, "wrong_shell")) {
        const UmicomU32 notifications = model.notifications, resets = model.resets;
        CHECK(UpdateConsoleCommand(&updateForeignShell, "fatwriteinfo") == UMICOM_SHELL_BAD_STATE);
        CHECK(UpdateConsoleCommand(&updateForeignShell, "fatwrite /FRAG.BIN 0 text") == UMICOM_SHELL_BAD_STATE);
        CHECK(UmicomKernelFat16UpdateConsoleClose(&updateForeignShell) == UMICOM_FAT16_UPDATE_BAD_STATE);
        CHECK(model.notifications == notifications && model.resets == resets);
    } else if (!strcmp(name, "empty_text")) {
        CHECK(UpdateConsoleCommand(&updateShell, "fatwrite /FRAG.BIN 0 \"\"") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.write=invalid-argument") && strstr(transcript, "fat.write-result=not-admitted"));
        CHECK(!updateWriteRequests);
    } else if (!strcmp(name, "reset_retry") || !strcmp(name, "release_retry")) {
        if (!strcmp(name, "reset_retry")) model.stuckReset = UMICOM_TRUE;
        else model.failFree = 2U;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteclose") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.update-state=closing"));
        CHECK(Allocated() == (!strcmp(name, "reset_retry") ? 2U : 1U));
        model.stuckReset = UMICOM_FALSE; model.failFree = 0U;
    } else if (!strcmp(name, "historical_uncertainty")) {
        updateFailWrite = 2U; updatePartialWrite = UMICOM_TRUE;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwrite /FRAG.BIN 511 \"persistent text\"") == UMICOM_SHELL_IO_ERROR);
        CHECK(updateWriteRequests == 2U && strstr(transcript, "fat.write-result=submitted-unconfirmed"));
        CHECK(strstr(transcript, "confirmed=1 submitted=15") && strstr(transcript, "sector-bytes-at-risk=512"));
        UpdateClearTranscript(); updateFailFlush = 1U;
        CHECK(UpdateConsoleCommand(&updateShell, "fatflush") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.flush-outcome=submitted-unconfirmed"));
        UpdateClearTranscript(); updateFailFlush = 0U;
        CHECK(UpdateConsoleCommand(&updateShell, "fatflush") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "needs-flush=0 write-uncertain=1") && strstr(transcript, "fat.flush-outcome=completed"));
        UpdateClearTranscript();
        CHECK(UpdateConsoleCommand(&updateShell, "fatwrite /FRAG.BIN 0 replacement") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.write=write-uncertain") && strstr(transcript, "fat.write-result=not-admitted"));
        CHECK(strstr(transcript, "fat.previous-write=submitted-unconfirmed") && strstr(transcript, "offset=511 requested=15 confirmed=1 submitted=15"));
        CHECK(strstr(transcript, "needs-flush-at-return=1 write-uncertain-at-return=1"));
        CHECK(updateWriteRequests == 2U && updateFlushRequests == 2U);
        UpdateClearTranscript(); CHECK(UpdateConsoleCommand(&updateShell, "fatwriteinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "fat.last-write=submitted-unconfirmed"));
    } else if (!strcmp(name, "command_sequence") || !strcmp(name, "close_without_flush") ||
        !strcmp(name, "callback_reentry") || !strcmp(name, "output_reentry")) {
        updateConsoleReenter = !strcmp(name, "callback_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        updateOutputReenter = !strcmp(name, "output_reentry") ? UMICOM_TRUE : UMICOM_FALSE;
        CHECK(UpdateConsoleCommand(&updateShell, "fatwrite /FRAG.BIN 511 \"persistent text\"") == UMICOM_SHELL_OK);
        CHECK(updateWriteRequests == 2U && !updateFlushRequests && strstr(transcript, "fat.write-result=completed"));
        if (updateConsoleReenter || updateOutputReenter) CHECK(updateReentries > 0U);
        updateConsoleReenter = UMICOM_FALSE; updateOutputReenter = UMICOM_FALSE;
        const char text[] = "persistent text";
        for (UmicomSize i = 0U; i < sizeof(text) - 1U; ++i)
            updateExpected[UpdateFileAddress(511U + i)] = (UmicomU8)text[i];
        UpdateEqual(updateVisible, updateExpected, sizeof(updateVisible));
        UpdateEqual(updateDurable, updateInitial, sizeof(updateDurable));
        if (strcmp(name, "close_without_flush")) {
            CHECK(UpdateConsoleCommand(&updateShell, "fatflush") == UMICOM_SHELL_OK);
            CHECK(updateFlushRequests == 1U); UpdateEqual(updateDurable, updateExpected, sizeof(updateDurable));
        }
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "fat.last-write=completed"));
    }
    const UmicomU32 flushed = updateFlushRequests;
    UpdateConsoleClose(); CHECK(updateFlushRequests == flushed);
    if (!strcmp(name, "command_sequence")) {
        CHECK(UpdateConsoleCommand(&updateShell, "fatwriteopen 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "fat.open=bad-state") && !Allocated());
        CHECK(UmicomKernelFat16UpdateConsoleClose(&updateShell) == UMICOM_FAT16_UPDATE_OK);
    }
}
int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: umicom-fat16-update-tests CASE READONLY-FIXTURE\n"); return 2; }
    UpdateStart(argv[2]);
    const char *name = argv[1];
    if (!strncmp(name, "success.", 8U)) UpdateSuccessfulCase(name + 8U);
    else if (!strncmp(name, "format.", 7U)) UpdateFormatCase(name + 7U);
    else if (!strncmp(name, "partial.", 8U)) UpdatePartialCase(name + 8U);
    else if (!strncmp(name, "clock.", 6U)) UpdateClockCase(name + 6U);
    else if (!strncmp(name, "read.", 5U)) UpdateReadFailure(argv[2], name + 5U);
    else if (!strncmp(name, "lifetime.", 9U)) UpdateLifecycleCase(name + 9U);
    else if (!strncmp(name, "ownership.", 10U)) UpdateOwnershipCase(name + 10U);
    else if (!strncmp(name, "open_arguments.", 15U)) UpdateOpenArguments(name + 15U);
    else if (!strncmp(name, "range.", 6U)) UpdateRangeCase(name + 6U);
    else if (!strncmp(name, "plan.", 5U)) UpdatePlanCase(name + 5U);
    else if (!strncmp(name, "console.", 8U)) UpdateConsoleCase(name + 8U);
    else if (!strncmp(name, "flush_clock.", 12U)) UpdateFlushClockCase(name + 12U);
    else if (!strcmp(name, "repeated_successful_writes")) UpdateRepeatedSuccess();
    else UpdateMiscCase(name);
    CHECK(Allocated() == 0U);
    printf("fat16-update.%s: ok\n", name);
    return 0;
}
