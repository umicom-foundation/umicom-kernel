/* Persistent FAT16 lifecycle qualification through six fresh-boot transitions.
 * Complete independent media checks cover allocation, zeroed new storage,
 * retained truncated/freed bytes, directory metadata and every neighbour.
 * Four checkpoint forks omit Finish and are refused in fresh read-only guests.
 * Controlled omission is not a physical power cut, atomicity or repair claim.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_lifecycle_commit.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/fat16_metadata.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_lifecycle/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_LIFECYCLE_TEST) + \
     defined(UMICOM_KERNEL_FAT16_LIFECYCLE_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_LIFECYCLE_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_LIFECYCLE_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 lifecycle qualification image"
#endif

#define UMICOM_FAT16_LIFECYCLE_GUARD_BYTES 32U
static UmicomU8 umicomFatLifecycleReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatLifecycleExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatLifecycleFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatLifecycleRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-lifecycle.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatLifecycleBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-lifecycle.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatLifecycleRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatLifecycleEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatLifecycleMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatLifecycleDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatLifecycleBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatLifecycleFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outSlot = i; ++found; }
        else UmicomFatLifecycleRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatLifecycleRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatLifecycleDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomSize state, UmicomBoolean dirty)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatLifecycleBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatLifecycleReadback, sizeof(umicomFatLifecycleReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16LifecycleFixtureSector(first + sector, umicomFatLifecycleExpected, state, dirty);
            UmicomFatLifecycleRequire(UmicomFatLifecycleEqual(umicomFatLifecycleExpected,
                umicomFatLifecycleReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "complete disk agrees with exact original, staged or committed fixture");
        }
    }
}
static void UmicomFatLifecycleRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatLifecycleFill(&after, sizeof(after), 0U);
    UmicomFatLifecycleFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatLifecycleRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatLifecycleRequire(UmicomFatLifecycleMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-lifecycle.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-lifecycle.machine-state=unchanged");
    UmicomFatLifecycleFill(umicomFatLifecycleReadback, sizeof(umicomFatLifecycleReadback), 0U);
    UmicomFatLifecycleFill(umicomFatLifecycleExpected, sizeof(umicomFatLifecycleExpected), 0U);
}


/* Classify only a finite synthetic checkpoint set, then verify every media byte
 * before using the classification. Caller-controlled disk fields never choose
 * an unchecked operation, address or expected result. */
static UmicomSize UmicomFatLifecycleState(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle, UmicomBoolean dirty)
{
    UmicomFatLifecycleBlock(UmicomKernelBlockRead(domain, handle, UMICOM_DISK_FIXTURE_ROOT,
        1U, umicomFatLifecycleReadback, 512U), UMICOM_BLOCK_OK, "read fixture checkpoint directory");
    UmicomSize state = 7U;
    for (UmicomSize i = 0U; i < 7U; ++i) {
        UmicomFat16LifecycleFixtureSector(UMICOM_DISK_FIXTURE_ROOT, umicomFatLifecycleExpected, i, dirty);
        if (UmicomFatLifecycleEqual(umicomFatLifecycleReadback, umicomFatLifecycleExpected, 512U)) { state = i; break; }
    }
    UmicomFatLifecycleRequire(state < 7U && (!dirty || (state >= 1U && state <= 4U)), "known complete fixture state");
    UmicomFatLifecycleDisk(domain, handle, state, dirty);
    return state;
}
static void UmicomFatLifecyclePhase(UmicomSize state)
{
    UmicomKernelConsoleWrite("fat16-lifecycle.phase=");
    UmicomKernelConsoleWriteLine(UmicomFat16LifecycleFixturePhase(state));
}

#if defined(UMICOM_KERNEL_FAT16_LIFECYCLE_TEST) || defined(UMICOM_KERNEL_FAT16_LIFECYCLE_INTERRUPTED_TEST)
static UmicomKernelFat16LifecycleCommitter umicomFatLifecycleOwner;
static UmicomU8 umicomFatLifecyclePayload[900];
static void UmicomFatLifecycleCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const UmicomU8 *const input = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static void UmicomFatLifecycleStatus(UmicomKernelFat16UpdateStatus actual,
    UmicomKernelFat16UpdateStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-lifecycle.status=");
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWrite("fat16-lifecycle.disk-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatLifecycleOwner.commit.updater.lastDiskStatus));
    UmicomFatLifecycleRequire(UMICOM_FALSE, reason);
}
static UmicomKernelFat16LifecycleRequest UmicomFatLifecycleRequest(UmicomSize state)
{
    UmicomKernelFat16LifecycleRequest request;
    UmicomFatLifecycleFill(&request, sizeof(request), 0U);
    request.operation = state == 1U || state == 5U ? UMICOM_FAT16_LIFECYCLE_CREATE :
        (state == 2U || state == 6U ? UMICOM_FAT16_LIFECYCLE_APPEND :
        (state == 3U ? UMICOM_FAT16_LIFECYCLE_TRUNCATE : UMICOM_FAT16_LIFECYCLE_DELETE));
    request.path = state == 5U ? "/reuse.bin" : (state == 6U ? "/empty.txt" : "/life.bin");
    request.time = UmicomFat16LifecycleFixtureTime(state);
    request.bytes = state == 1U || state == 5U ? 700U : (state == 2U ? 900U : (state == 6U ? 600U : 0U));
    request.size = state == 3U ? 513U : 0U;
    if (request.bytes) {
        request.input = umicomFatLifecyclePayload;
        for (UmicomSize i = 0U; i < request.bytes; ++i)
            umicomFatLifecyclePayload[i] = UmicomFat16LifecycleFixturePattern(state, i);
    }
    return request;
}
static void UmicomFatLifecycleEvidence(const UmicomKernelFat16LifecycleResult *result, UmicomSize state,
    UmicomBoolean complete)
{
    const UmicomBoolean data = state != 3U && state != 4U;
    const UmicomSize sectors = data ? (state == 2U ? 3U : 2U) : 0U;
    const UmicomSize payload = state == 1U || state == 5U ? 700U : (state == 2U ? 900U : (state == 6U ? 600U : 0U));
    const UmicomSize metadata = complete ? 7U : 5U;
    const UmicomSize flushes = (complete ? 7U : 5U) + (data ? 1U : 0U);
    UmicomFatLifecycleRequire(result->planned && result->operation ==
        (state == 1U || state == 5U ? UMICOM_FAT16_LIFECYCLE_CREATE :
        (state == 2U || state == 6U ? UMICOM_FAT16_LIFECYCLE_APPEND :
        (state == 3U ? UMICOM_FAT16_LIFECYCLE_TRUNCATE : UMICOM_FAT16_LIFECYCLE_DELETE))), "planned operation identity");
    UmicomFatLifecycleRequire(result->originalEntryPresent == (state != 1U && state != 5U) &&
        result->updatedEntryPresent == (state != 4U) && result->allocatedClusters == (data ? 2U : 0U) &&
        result->freedClusters == (data ? 0U : 2U) && result->plannedDataSectors == sectors &&
        result->plannedFatSectors == 1U && result->changedFatSectors == 1U && result->plannedDirectorySectors == 1U &&
        result->directorySector == UMICOM_DISK_FIXTURE_ROOT && result->entryOffset == (state == 6U ? 128U : 160U),
        "literal allocation and sector-plan counts");
    if (state != 4U) {
        UmicomFatLifecycleRequire(result->updatedEntry.bytes == (state == 2U ? 1600U :
            (state == 3U ? 513U : (state == 6U ? 600U : 700U))) &&
            result->updatedEntry.firstCluster == (state == 6U ? 10U : 5U) &&
            result->updatedEntry.attributes == 0x20U && !result->updatedEntry.directory &&
            result->encodedTime.writeTime == UmicomFat16LifecycleFixtureClock(state) &&
            result->encodedTime.writeDate == UmicomFat16LifecycleFixtureDate(state), "new EOF, first cluster, archive and explicit calendar");
    }
    UmicomFatLifecycleRequire(result->commit.offset == (state == 2U ? 700U : 0U) &&
        result->commit.requestedBytes == payload && result->commit.confirmedBytes == payload &&
        result->commit.submittedBytes == payload && result->commit.completedDataSectors == sectors &&
        result->commit.submittedDataSectors == sectors && result->commit.dataDurable == data && result->commit.dataVerified == data,
        "payload evidence counts caller bytes and all real initialisation writes");
    if (!data) UmicomFatLifecycleRequire(result->commit.dataOutcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED,
        "truncate and delete never fabricate file-data writes");
    UmicomFatLifecycleRequire(result->submittedFatSectors == 2U && result->completedFatSectors == 2U &&
        result->submittedDirectorySectors == 1U && result->completedDirectorySectors == 1U && result->completedDirectoryFlushes == 1U &&
        result->fatMirrorDurable && result->fatPrimaryDurable && result->fatMirrorVerified && result->fatPrimaryVerified &&
        result->directoryDurable && result->directoryVerified && result->commit.dirtyDurable && result->commit.dirtyVerified &&
        result->commit.mediaTouched && !result->commit.uncertainSectorValid && !result->commit.needsFlush && !result->commit.writeUncertain &&
        result->commit.submittedMetadataSectors == metadata && result->commit.completedMetadataSectors == metadata &&
        result->commit.completedFlushes == flushes && result->commit.commitAccepted == complete &&
        result->commit.cleanFinalisationStarted == complete && result->commit.cleanDurable == complete &&
        result->commit.cleanVerified == complete && result->committedOperations == (complete ? 1U : 0U),
        "ordered FAT, directory, barrier, dirty and accepted clean evidence");
}
static void UmicomFatLifecycleRefusals(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomSize state)
{
    UmicomKernelFat16LifecycleRequest request;
    UmicomKernelFat16LifecycleResult result;
    UmicomFatLifecycleFill(&request, sizeof(request), 0U);
    request.operation = UMICOM_FAT16_LIFECYCLE_DELETE;
    request.path = "/README.TXT";
    UmicomFatLifecycleFill(&result, sizeof(result), 0U);
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleStage(&umicomFatLifecycleOwner, &request, &result),
        UMICOM_FAT16_UPDATE_READ_ONLY, "read-only file refuses unlink before mutation");
    UmicomFatLifecycleRequire(result.commit.diskStatus == UMICOM_DISK_READ_ONLY && !result.commit.mediaTouched &&
        !result.commit.submittedMetadataSectors && !result.commit.submittedDataSectors && !result.commit.completedFlushes,
        "read-only refusal has no WRITE or FLUSH");
    request.operation = UMICOM_FAT16_LIFECYCLE_CREATE;
    request.path = "/FRAG.BIN";
    request.time = UmicomFat16LifecycleFixtureTime(1U);
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleStage(&umicomFatLifecycleOwner, &request, &result),
        UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR, "existing sibling refuses replacement");
    UmicomFatLifecycleRequire(result.commit.diskStatus == UMICOM_DISK_EXISTS && !result.commit.mediaTouched &&
        !result.commit.submittedMetadataSectors && !result.commit.submittedDataSectors && !result.commit.completedFlushes &&
        umicomFatLifecycleOwner.commit.state == UMICOM_FAT16_COMMIT_READY, "collision refuses before media submission");
    UmicomFatLifecycleDisk(domain, handle, state, UMICOM_FALSE);
    UmicomKernelConsoleWriteLine("fat16-lifecycle.preflight-refusals-before-write-and-flush=verified");
}
static void UmicomFatLifecycleWriter(UmicomBoolean complete)
{
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatLifecycleFill(&before, sizeof(before), 0U);
    UmicomFatLifecycleFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatLifecycleRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatLifecycleDiscover(&slot);
    UmicomKernelFat16LifecycleCommitter *const owner = &umicomFatLifecycleOwner;
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleOpen(owner, domain, slot, 0U, 10000000U),
        UMICOM_FAT16_UPDATE_OK, "exclusive lifecycle lease on fresh clean checkpoint");
    UmicomFatLifecycleRequire(owner->commit.state == UMICOM_FAT16_COMMIT_READY && owner->commit.updater.handle &&
        owner->commit.updater.sectors == UMICOM_DISK_FIXTURE_SECTORS && owner->commit.updater.info.clusters == 12159U,
        "clean fixture geometry");
    const UmicomSize previous = UmicomFatLifecycleState(domain, owner->commit.updater.handle, UMICOM_FALSE);
    const UmicomSize state = previous + 1U;
    UmicomFatLifecycleRequire(state <= (complete ? 6U : 4U), "bounded expected transition");
    UmicomFatLifecyclePhase(state);
    UmicomKernelConsoleWriteLine("fat16-lifecycle.before-disk-bytes=8388608-verified");
    UmicomFatLifecycleRefusals(domain, owner->commit.updater.handle, previous);
    UmicomKernelFat16LifecycleRequest request = UmicomFatLifecycleRequest(state);
    UmicomKernelFat16LifecycleResult result, historical, untouched;
    UmicomFatLifecycleFill(&result, sizeof(result), 0U);
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleStage(owner, &request, &result),
        UMICOM_FAT16_UPDATE_OK, "ordered lifecycle Stage");
    UmicomFatLifecycleEvidence(&result, state, UMICOM_FALSE);
    UmicomFatLifecycleRequire(owner->commit.state == UMICOM_FAT16_COMMIT_STAGED && !owner->committedOperations,
        "Stage is not accepted completion");
    UmicomFatLifecycleDisk(domain, owner->commit.updater.handle, state, UMICOM_TRUE);
    UmicomKernelConsoleWriteLine("fat16-lifecycle.staged-disk-bytes=8388608-verified");
    UmicomFatLifecycleCopy(&historical, &owner->lastResult, sizeof(historical));
    UmicomFatLifecycleFill(&result, sizeof(result), 0xa5U);
    UmicomFatLifecycleFill(&untouched, sizeof(untouched), 0xa5U);
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleStage(owner, &request, &result),
        UMICOM_FAT16_UPDATE_BAD_STATE, "second Stage requires previous accepted Finish");
    UmicomFatLifecycleRequire(UmicomFatLifecycleEqual(&result, &untouched, sizeof(result)) &&
        UmicomFatLifecycleEqual(&owner->lastResult, &historical, sizeof(historical)), "Stage refusal preserves complete output/history");
    if (complete) {
        UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleFinish(owner, &result), UMICOM_FAT16_UPDATE_OK,
            "explicit Finish retains changed FAT words while publishing clean");
        UmicomFatLifecycleEvidence(&result, state, UMICOM_TRUE);
        UmicomFatLifecycleRequire(owner->commit.state == UMICOM_FAT16_COMMIT_COMMITTED && owner->committedOperations == 1U,
            "exactly one operation accepted in this fresh writer");
        UmicomFatLifecycleDisk(domain, owner->commit.updater.handle, state, UMICOM_FALSE);
        UmicomKernelConsoleWriteLine("fat16-lifecycle.committed-disk-bytes=8388608-verified");
        UmicomFatLifecycleCopy(&historical, &owner->lastResult, sizeof(historical));
    } else UmicomKernelConsoleWriteLine("fat16-lifecycle.finish=deliberately-omitted-not-a-physical-power-cut");
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleClose(owner), UMICOM_FAT16_UPDATE_OK, "resource-only Close");
    UmicomFatLifecycleRequire(owner->commit.state == UMICOM_FAT16_COMMIT_CLOSED && !owner->commit.updater.handle &&
        owner->committedOperations == (complete ? 1U : 0U) && UmicomFatLifecycleEqual(&historical, &owner->lastResult, sizeof(historical)),
        "Close preserves accepted count and latest evidence");
    UmicomFatLifecycleStatus(UmicomKernelFat16LifecycleClose(owner), UMICOM_FAT16_UPDATE_OK, "idempotent Close");
    UmicomFatLifecycleRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine(complete ? "UMICOM_KERNEL_FAT16_LIFECYCLE_READY" : "UMICOM_KERNEL_FAT16_LIFECYCLE_INTERRUPTED_READY");
}
#ifdef UMICOM_KERNEL_FAT16_LIFECYCLE_TEST
void UmicomKernelFat16LifecycleValidate(void) { UmicomFatLifecycleWriter(UMICOM_TRUE); }
#else
void UmicomKernelFat16LifecycleInterruptedValidate(void) { UmicomFatLifecycleWriter(UMICOM_FALSE); }
#endif

#else
/* These readers start in separate QEMU processes with readonly=on. */
typedef struct UmicomFatLifecycleReader {
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
} UmicomFatLifecycleReader;
static UmicomKernelFat16 umicomFatLifecycleVolume;
static UmicomKernelDiskMount umicomFatLifecycleMount;
static UmicomBoolean UmicomFatLifecycleSectorRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomFatLifecycleReader *const reader = (UmicomFatLifecycleReader *)context;
    return UmicomKernelBlockRead(reader->domain, reader->handle, sector, 1U, output, 512U) == UMICOM_BLOCK_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
static void UmicomFatLifecycleAbsent(const char *path)
{
    UmicomKernelFat16Metadata metadata;
    UmicomFatLifecycleFill(&metadata, sizeof(metadata), 0xa5U);
    UmicomFatLifecycleRequire(UmicomKernelFat16MetadataRead(&umicomFatLifecycleVolume, path, &metadata) == UMICOM_DISK_NOT_FOUND,
        "absent lifecycle alias remains absent");
    const UmicomU8 *const bytes = (const UmicomU8 *)&metadata;
    for (UmicomSize i = 0U; i < sizeof(metadata); ++i)
        UmicomFatLifecycleRequire(bytes[i] == 0xa5U, "not-found metadata output and padding untouched");
}
static void UmicomFatLifecycleMetadata(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomSize state, UmicomBoolean dirty)
{
    UmicomFatLifecycleReader source = {domain, handle};
    const UmicomKernelDiskReader reader = {UMICOM_DISK_FIXTURE_SECTORS, UmicomFatLifecycleSectorRead, &source};
    struct { UmicomU8 before[32]; UmicomKernelFat16Metadata value; UmicomU8 after[32]; } result;
    UmicomFatLifecycleFill(&result, sizeof(result), 0xa5U);
    const UmicomKernelDiskStatus status = UmicomKernelFat16Open(&umicomFatLifecycleVolume, &reader, 0U);
    if (dirty) {
        UmicomFatLifecycleRequire(status == UMICOM_DISK_DIRTY && !umicomFatLifecycleVolume.open &&
            UmicomKernelFat16MetadataRead(&umicomFatLifecycleVolume, "/LIFE.BIN", &result.value) == UMICOM_DISK_BAD_STATE,
            "persistent dirty flags block fresh inspector publication");
        const UmicomU8 *const bytes = (const UmicomU8 *)&result;
        for (UmicomSize i = 0U; i < sizeof(result); ++i)
            UmicomFatLifecycleRequire(bytes[i] == 0xa5U, "dirty refusal preserves caller object and guards");
        UmicomKernelConsoleWriteLine("fat16-lifecycle.metadata=dirty-refused-output-unchanged");
        return;
    }
    UmicomFatLifecycleRequire(status == UMICOM_DISK_OK, "fresh inspector opens clean lifecycle result");
    for (UmicomSize item = 0U; item < 3U; ++item) {
        const char *const path = item == 0U ? "/LIFE.BIN" : (item == 1U ? "/REUSE.BIN" : "/EMPTY.TXT");
        if ((item == 0U && state >= 4U) || (item == 1U && state < 5U)) { UmicomFatLifecycleAbsent(path); continue; }
        const UmicomSize phase = item == 2U ? (state == 6U ? 6U : 0U) : (state >= 5U ? 5U : state);
        const UmicomU32 size = item == 2U ? (state == 6U ? 600U : 0U) : (state == 2U ? 1600U : (state == 3U ? 513U : 700U));
        const UmicomU16 cluster = item == 2U ? (state == 6U ? 10U : 0U) : 5U;
        UmicomFatLifecycleRequire(UmicomKernelFat16MetadataRead(&umicomFatLifecycleVolume, path, &result.value) == UMICOM_DISK_OK &&
            result.value.directoryEntryPresent && !result.value.entry.directory && result.value.entry.bytes == size &&
            result.value.entry.firstCluster == cluster && result.value.entry.attributes == 0x20U &&
            result.value.writeTimestamp.rawTime == UmicomFat16LifecycleFixtureClock(phase) &&
            result.value.writeTimestamp.rawDate == UmicomFat16LifecycleFixtureDate(phase) &&
            result.value.writeTimestamp.state == (phase ? UMICOM_FAT16_TIMESTAMP_VALID : UMICOM_FAT16_TIMESTAMP_ABSENT),
            "fresh public metadata exposes exact saved size, allocation, archive and calendar");
        if (phase) {
            UmicomKernelFat16FileTime expected = UmicomFat16LifecycleFixtureTime(phase);
            expected.second = (UmicomU16)((expected.second / 2U) * 2U);
            UmicomFatLifecycleRequire(UmicomFatLifecycleEqual(&result.value.writeTimestamp.value, &expected, sizeof(expected)),
                "fresh decoded calendar floors odd seconds");
        }
        for (UmicomSize i = 0U; i < sizeof(result.before); ++i)
            UmicomFatLifecycleRequire(result.before[i] == 0xa5U && result.after[i] == 0xa5U, "metadata output guards");
    }
    UmicomFatLifecycleRequire(UmicomKernelFat16Close(&umicomFatLifecycleVolume) == UMICOM_DISK_OK, "metadata inspector closed");
    UmicomKernelConsoleWriteLine("fat16-lifecycle.metadata=names-size-first-cluster-archive-calendar-verified");
}
#ifdef UMICOM_KERNEL_FAT16_LIFECYCLE_READBACK_TEST
static UmicomKernelVfsClient umicomFatLifecycleClient;
static void UmicomFatLifecycleFile(const char *path, const char *literal, UmicomSize expectedBytes,
    UmicomSize pattern)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomFatLifecycleFill(&info, sizeof(info), 0U);
    UmicomFatLifecycleRequire(UmicomKernelVfsOpen(&umicomFatLifecycleClient, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "fresh read-only file descriptor");
    UmicomFatLifecycleRequire(UmicomKernelVfsQuery(&umicomFatLifecycleClient, descriptor, &info) == UMICOM_VFS_OK &&
        info.kind == UMICOM_VFS_FILE && info.bytes == expectedBytes, "fresh VFS size");
    UmicomFatLifecycleFill(umicomFatLifecycleReadback, sizeof(umicomFatLifecycleReadback), 0xa5U);
    UmicomSize count = 0U;
    UmicomFatLifecycleRequire(UmicomKernelVfsRead(&umicomFatLifecycleClient, descriptor,
        umicomFatLifecycleReadback, sizeof(umicomFatLifecycleReadback), &count) == UMICOM_VFS_OK && count == expectedBytes,
        "complete file read after reboot");
    for (UmicomSize i = 0U; i < sizeof(umicomFatLifecycleReadback); ++i) {
        const UmicomU8 expected = i >= expectedBytes ? 0xa5U : (literal ? (UmicomU8)literal[i] :
            (pattern == 0U ? UmicomDiskFixturePattern(i) :
            (pattern == 6U ? UmicomFat16LifecycleFixturePattern(6U, i) : UmicomFat16LifecycleFixtureFileByte(pattern, i))));
        UmicomFatLifecycleRequire(umicomFatLifecycleReadback[i] == expected, "exact contents and untouched caller suffix");
    }
    count = 99U;
    UmicomFatLifecycleRequire(UmicomKernelVfsRead(&umicomFatLifecycleClient, descriptor,
        umicomFatLifecycleReadback, 1U, &count) == UMICOM_VFS_OK && !count, "new EOF verified");
    UmicomFatLifecycleRequire(UmicomKernelVfsClose(&umicomFatLifecycleClient, descriptor) == UMICOM_VFS_OK, "file pin released");
}
static void UmicomFatLifecycleVfs(UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize state)
{
    UmicomFatLifecycleRequire(UmicomKernelDiskMountOpen(&umicomFatLifecycleMount, domain, slot, 0U, 10000000U) == UMICOM_VFS_OK,
        "independent fresh read-only VFS mount");
    UmicomFatLifecycleRequire(UmicomKernelDiskMountClientOpen(&umicomFatLifecycleMount, &umicomFatLifecycleClient,
        42U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK, "read-only client admitted");
    if (state < 4U) UmicomFatLifecycleFile("/LIFE.BIN", (const char *)0,
        state == 1U ? 700U : (state == 2U ? 1600U : 513U), state);
    else {
        UmicomKernelFileDescriptor missing = 0U;
        UmicomFatLifecycleRequire(UmicomKernelVfsOpen(&umicomFatLifecycleClient, "/LIFE.BIN", UMICOM_VFS_RIGHT_READ,
            UMICOM_FALSE, &missing) == UMICOM_VFS_NOT_FOUND && !missing, "unlinked alias remains absent through VFS");
    }
    if (state >= 5U) UmicomFatLifecycleFile("/REUSE.BIN", (const char *)0, 700U, 5U);
    UmicomFatLifecycleFile("/EMPTY.TXT", (const char *)0, state == 6U ? 600U : 0U, 6U);
    UmicomFatLifecycleFile("/FRAG.BIN", (const char *)0, 1300U, 0U);
    UmicomFatLifecycleFile("/README.TXT", UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README)-1U, 0U);
    UmicomFatLifecycleFile("/DOCS/GUIDE.TXT", UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE)-1U, 0U);
    UmicomKernelFileDescriptor refused = 0U;
    UmicomFatLifecycleRequire(UmicomKernelVfsOpen(&umicomFatLifecycleClient, "/EMPTY.TXT", UMICOM_VFS_RIGHT_WRITE,
        UMICOM_FALSE, &refused) == UMICOM_VFS_ACCESS_DENIED && !refused, "existing VFS remains read-only");
    UmicomSize closed = 0U;
    UmicomFatLifecycleRequire(UmicomKernelVfsClientClose(&umicomFatLifecycleClient, &closed) == UMICOM_VFS_OK && !closed &&
        UmicomKernelDiskMountClose(&umicomFatLifecycleMount) == UMICOM_VFS_OK && !umicomFatLifecycleMount.handle,
        "all descriptors and mount retired");
    UmicomKernelConsoleWriteLine("fat16-lifecycle.vfs=contents-new-eof-neighbours-readonly-rights-verified");
}
#endif
static void UmicomFatLifecycleReaderBoot(UmicomBoolean dirty)
{
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatLifecycleFill(&before, sizeof(before), 0U);
    UmicomFatLifecycleFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatLifecycleRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "fresh allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatLifecycleDiscover(&slot);
    UmicomKernelBlockHandle handle = 0U;
    UmicomFatLifecycleBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &handle), UMICOM_BLOCK_OK, "fresh readonly raw lease");
    UmicomKernelBlockInfo info;
    UmicomFatLifecycleFill(&info, sizeof(info), 0U);
    UmicomFatLifecycleBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK, "fresh readonly geometry");
    UmicomFatLifecycleRequire(info.sectors == UMICOM_DISK_FIXTURE_SECTORS && !info.writable && info.heldFrames == 2U,
        "read-only QEMU transport and two owned DMA frames");
    const UmicomSize state = UmicomFatLifecycleState(domain, handle, dirty);
    UmicomFatLifecycleRequire(state > 0U, "reader receives a produced checkpoint");
    UmicomFatLifecyclePhase(state);
    UmicomFatLifecycleMetadata(domain, handle, state, dirty);
    UmicomFatLifecycleDisk(domain, handle, state, dirty);
    UmicomKernelConsoleWriteLine("fat16-lifecycle.readback-disk-bytes=8388608-verified");
    UmicomFatLifecycleBlock(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK, "raw lease closes before filesystem acquisition");
#ifdef UMICOM_KERNEL_FAT16_LIFECYCLE_READBACK_TEST
    UmicomFatLifecycleVfs(domain, slot, state);
#else
    UmicomFatLifecycleRequire(UmicomKernelDiskMountOpen(&umicomFatLifecycleMount, domain, slot, 0U, 10000000U) == UMICOM_VFS_CORRUPT_FILESYSTEM &&
        umicomFatLifecycleMount.provider.lastDiskStatus == UMICOM_DISK_DIRTY && !umicomFatLifecycleMount.admitted &&
        !umicomFatLifecycleMount.provider.volume.open, "fresh VFS refuses dirty allocation/directory publication");
    UmicomFatLifecycleRequire(UmicomKernelDiskMountClose(&umicomFatLifecycleMount) == UMICOM_VFS_OK &&
        !umicomFatLifecycleMount.handle, "failed admission closes without repair");
    UmicomKernelConsoleWriteLine("fat16-lifecycle.filesystem=dirty-refused-repair-not-performed");
#endif
    UmicomFatLifecycleRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine(dirty ? "UMICOM_KERNEL_FAT16_LIFECYCLE_REJECTED_READY" : "UMICOM_KERNEL_FAT16_LIFECYCLE_READBACK_READY");
}
#ifdef UMICOM_KERNEL_FAT16_LIFECYCLE_READBACK_TEST
void UmicomKernelFat16LifecycleReadbackValidate(void) { UmicomFatLifecycleReaderBoot(UMICOM_FALSE); }
#else
void UmicomKernelFat16LifecycleRejectedValidate(void) { UmicomFatLifecycleReaderBoot(UMICOM_TRUE); }
#endif
#endif
