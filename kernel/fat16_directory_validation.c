/* Persistent FAT16 lifecycle qualification through nine fresh-boot directory transitions.
 * Complete independent media checks cover allocation, zeroed new storage,
 * retained truncated/freed bytes, directory metadata and every neighbour.
 * Nine checkpoint forks omit Finish and are refused in fresh read-only guests.
 * Controlled omission is not a physical power cut, atomicity or repair claim.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_directory_validation.h"
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/fat16_metadata.h"
#include "umicom/kernel/fat16_lifecycle_query.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_directories/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_DIRECTORY_TEST) + \
     defined(UMICOM_KERNEL_FAT16_DIRECTORY_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_DIRECTORY_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_DIRECTORY_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 lifecycle qualification image"
#endif

#define UMICOM_FAT16_LIFECYCLE_GUARD_BYTES 32U
static UmicomU8 umicomFatDirectoryReadback[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomFatDirectoryExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomFatDirectoryFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static void UmicomFatDirectoryRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-directory.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    for (;;) {}
}
static void UmicomFatDirectoryBlock(UmicomKernelBlockStatus actual,
    UmicomKernelBlockStatus expected, const char *reason)
{
    if (actual == expected) return;
    UmicomKernelConsoleWrite("fat16-directory.block-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelBlockStatusName(actual));
    UmicomFatDirectoryRequire(UMICOM_FALSE, reason);
}
static UmicomBoolean UmicomFatDirectoryEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomFatDirectoryMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval;
}
static UmicomKernelBlockDomain *UmicomFatDirectoryDiscover(UmicomSize *outSlot)
{
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFatDirectoryBlock(UmicomPlatformBlockDomainGet(&domain), UMICOM_BLOCK_OK,
        "qualified platform and transport catalogue");
    UmicomSize found = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFatDirectoryFill(&info, sizeof(info), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { *outSlot = i; ++found; }
        else UmicomFatDirectoryRequire(status == UMICOM_BLOCK_NO_DEVICE ||
            status == UMICOM_BLOCK_NOT_BLOCK || status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
            "safe transport identity probe");
    }
    UmicomFatDirectoryRequire(found == 1U, "exactly one dedicated FAT16 fixture device");
    return domain;
}
static void UmicomFatDirectoryDisk(UmicomKernelBlockDomain *domain, UmicomKernelBlockHandle handle,
    UmicomSize state, UmicomBoolean dirty)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomFatDirectoryBlock(UmicomKernelBlockRead(domain, handle, first, UMICOM_BLOCK_MAX_SECTORS,
            umicomFatDirectoryReadback, sizeof(umicomFatDirectoryReadback)), UMICOM_BLOCK_OK,
            "whole disposable FAT16 disk read");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16DirectoryFixtureSector(first + sector, umicomFatDirectoryExpected, state, dirty);
            UmicomFatDirectoryRequire(UmicomFatDirectoryEqual(umicomFatDirectoryExpected,
                umicomFatDirectoryReadback + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "complete disk agrees with exact original, staged or committed fixture");
        }
    }
}
static void UmicomFatDirectoryRestore(const UmicomKernelPhysicalMemorySnapshot *before,
    const UmicomRiscvSupervisorMachineState *machineBefore)
{
    UmicomKernelPhysicalMemorySnapshot after;
    UmicomRiscvSupervisorMachineState machineAfter;
    UmicomFatDirectoryFill(&after, sizeof(after), 0U);
    UmicomFatDirectoryFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomFatDirectoryRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before->allocatedFrames == after.allocatedFrames && before->freeFrames == after.freeFrames &&
        before->reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFatDirectoryRequire(UmicomFatDirectoryMachineEqual(machineBefore, &machineAfter),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("fat16-directory.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("fat16-directory.machine-state=unchanged");
    UmicomFatDirectoryFill(umicomFatDirectoryReadback, sizeof(umicomFatDirectoryReadback), 0U);
    UmicomFatDirectoryFill(umicomFatDirectoryExpected, sizeof(umicomFatDirectoryExpected), 0U);
}



/* Probe a finite set of synthetic checkpoints using several distinguishing
 * sectors, then verify the entire disk before admitting any mutation. */
/* The added destination-parent probe distinguishes tree rename checkpoints.
 * The preceding implementation is retained for engineering review. */
#if 0
static UmicomSize UmicomFatDirectoryState(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle, UmicomBoolean dirty)
{
    const UmicomU64 probes[] = {UMICOM_DISK_FIXTURE_ROOT, UMICOM_DISK_FIXTURE_DATA + 3U,
        UMICOM_DISK_FIXTURE_DATA + 6U, UMICOM_DISK_FIXTURE_DATA + 8U};
    UmicomSize state = UMICOM_FAT16_DIRECTORY_FIXTURE_STATES;
    for (UmicomSize candidate = 0U; candidate < UMICOM_FAT16_DIRECTORY_FIXTURE_STATES; ++candidate) {
        UmicomBoolean matches = UMICOM_TRUE;
        for (UmicomSize i = 0U; i < sizeof(probes) / sizeof(probes[0]); ++i) {
            UmicomFatDirectoryBlock(UmicomKernelBlockRead(domain, handle, probes[i], 1U,
                umicomFatDirectoryReadback, 512U), UMICOM_BLOCK_OK, "checkpoint probe");
            UmicomFat16DirectoryFixtureSector(probes[i], umicomFatDirectoryExpected, candidate, dirty);
            if (!UmicomFatDirectoryEqual(umicomFatDirectoryReadback, umicomFatDirectoryExpected, 512U))
                matches = UMICOM_FALSE;
        }
        if (matches) { state = candidate; break; }
    }
    UmicomFatDirectoryRequire(state < UMICOM_FAT16_DIRECTORY_FIXTURE_STATES, "known directory checkpoint");
    UmicomFatDirectoryDisk(domain, handle, state, dirty);
    return state;
}
#endif

static UmicomSize UmicomFatDirectoryState(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle, UmicomBoolean dirty)
{
    const UmicomU64 probes[] = {UMICOM_DISK_FIXTURE_ROOT, UMICOM_DISK_FIXTURE_DATA + 3U,
        UMICOM_DISK_FIXTURE_DATA + 6U, UMICOM_DISK_FIXTURE_DATA + 8U, UMICOM_DISK_FIXTURE_DATA + 1U};
    UmicomSize state = UMICOM_FAT16_DIRECTORY_FIXTURE_STATES;
    for (UmicomSize candidate = 0U; candidate < UMICOM_FAT16_DIRECTORY_FIXTURE_STATES; ++candidate) {
        UmicomBoolean matches = UMICOM_TRUE;
        for (UmicomSize i = 0U; i < sizeof(probes) / sizeof(probes[0]); ++i) {
            UmicomFatDirectoryBlock(UmicomKernelBlockRead(domain, handle, probes[i], 1U,
                umicomFatDirectoryReadback, 512U), UMICOM_BLOCK_OK, "checkpoint probe");
            UmicomFat16DirectoryFixtureSector(probes[i], umicomFatDirectoryExpected, candidate, dirty);
            if (!UmicomFatDirectoryEqual(umicomFatDirectoryReadback, umicomFatDirectoryExpected, 512U))
                matches = UMICOM_FALSE;
        }
        if (matches) { state = candidate; break; }
    }
    UmicomFatDirectoryRequire(state < UMICOM_FAT16_DIRECTORY_FIXTURE_STATES, "known directory checkpoint");
    UmicomFatDirectoryDisk(domain, handle, state, dirty);
    return state;
}
static void UmicomFatDirectoryPhase(UmicomSize state)
{
    UmicomKernelConsoleWrite("fat16-directory.phase=");
    UmicomKernelConsoleWriteLine(UmicomFat16DirectoryFixturePhase(state));
}
#if defined(UMICOM_KERNEL_FAT16_DIRECTORY_TEST) || defined(UMICOM_KERNEL_FAT16_DIRECTORY_INTERRUPTED_TEST)
static UmicomKernelFat16LifecycleCommitter umicomFatDirectoryOwner;
static UmicomU8 umicomFatDirectoryPayload[900];
static void UmicomFatDirectoryStatus(UmicomKernelFat16UpdateStatus actual, const char *reason)
{
    if (actual == UMICOM_FAT16_UPDATE_OK) return;
    UmicomKernelConsoleWriteLine(UmicomKernelFat16UpdateStatusName(actual));
    UmicomKernelConsoleWriteLine(UmicomKernelDiskStatusName(umicomFatDirectoryOwner.commit.updater.lastDiskStatus));
    UmicomFatDirectoryRequire(UMICOM_FALSE, reason);
}
/* Additional fresh-boot checkpoints qualify metadata-only tree and file moves.
 * The preceding implementation is retained for engineering review. */
#if 0
static UmicomKernelFat16LifecycleRequest UmicomFatDirectoryRequest(UmicomSize state)
{
    UmicomKernelFat16LifecycleRequest request;
    UmicomFatDirectoryFill(&request, sizeof(request), 0U);
    request.operation = state <= 2U || state >= 8U ? UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY :
        state == 3U ? UMICOM_FAT16_LIFECYCLE_CREATE : state == 4U ? UMICOM_FAT16_LIFECYCLE_APPEND :
        state == 5U ? UMICOM_FAT16_LIFECYCLE_DELETE : UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY;
    request.path = state == 1U || state == 7U ? "/WORK" : state == 2U || state == 6U ? "/WORK/SUB" :
        state == 8U ? "/AGAIN" : state == 9U ? "/AGAIN/CHILD" : "/WORK/SUB/NOTE.TXT";
    if (state < 5U || state > 7U) {
        request.time.year = 2044U; request.time.month = 2U; request.time.day = 29U;
        request.time.hour = 23U; request.time.minute = 58U; request.time.second = 57U;
    }
    if (state == 3U || state == 4U) {
        request.bytes = state == 3U ? 700U : 900U;
        request.input = umicomFatDirectoryPayload;
        for (UmicomSize i = 0U; i < request.bytes; ++i)
            umicomFatDirectoryPayload[i] = UmicomFat16LifecycleFixturePattern(state == 3U ? 1U : 2U, i);
    }
    return request;
}
#endif

static UmicomKernelFat16LifecycleRequest UmicomFatDirectoryRequest(UmicomSize state)
{
    UmicomKernelFat16LifecycleRequest request;
    UmicomFatDirectoryFill(&request, sizeof(request), 0U);
    request.operation = state <= 2U || state >= 8U ? UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY :
        state == 3U ? UMICOM_FAT16_LIFECYCLE_CREATE : state == 4U ? UMICOM_FAT16_LIFECYCLE_APPEND :
        state == 5U ? UMICOM_FAT16_LIFECYCLE_DELETE : UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY;
    request.path = state == 1U || state == 7U ? "/WORK" : state == 2U || state == 6U ? "/WORK/SUB" :
        state == 8U ? "/AGAIN" : state == 9U ? "/AGAIN/CHILD" : "/WORK/SUB/NOTE.TXT";
    if (state < 5U || state > 7U) {
        request.time.year = 2044U; request.time.month = 2U; request.time.day = 29U;
        request.time.hour = 23U; request.time.minute = 58U; request.time.second = 57U;
    }
    if (state == 3U || state == 4U) {
        request.bytes = state == 3U ? 700U : 900U;
        request.input = umicomFatDirectoryPayload;
        for (UmicomSize i = 0U; i < request.bytes; ++i)
            umicomFatDirectoryPayload[i] = UmicomFat16LifecycleFixturePattern(state == 3U ? 1U : 2U, i);
    }
    if (state >= 10U) {
        /* Rename/move preserves calendars; no fresh timestamp is supplied. */
        UmicomFatDirectoryFill(&request, sizeof(request), 0U);
        request.operation = UMICOM_FAT16_LIFECYCLE_MOVE;
        request.path = state == 10U ? "/AGAIN" : state == 11U ? "/DOCS/LIBRARY/CHILD" :
            state == 12U ? "/DOCS/LIBRARY" : "/DOCS/RENAMED/F0000000.TXT";
        request.destination = state == 10U ? "/DOCS/LIBRARY" : state == 11U ? "/CHILD" :
            state == 12U ? "/DOCS/RENAMED" : "/MOVED.TXT";
    }
    return request;
}
/* Occupy each existing slot through real accepted file operations, not by
 * writing fixture bytes through a privileged shortcut in the guest. */
static void UmicomFatDirectoryFillParent(void)
{
    for (UmicomSize i = 0U; i < 14U; ++i) {
        char path[] = "/AGAIN/F0000000.TXT";
        path[13] = (char)('0' + i / 10U);
        path[14] = (char)('0' + i % 10U);
        UmicomKernelFat16LifecycleRequest request = UmicomFatDirectoryRequest(8U);
        request.operation = UMICOM_FAT16_LIFECYCLE_CREATE;
        request.path = path;
        UmicomKernelFat16LifecycleResult result;
        UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleStage(&umicomFatDirectoryOwner, &request, &result), "fill parent slot");
        UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleFinish(&umicomFatDirectoryOwner, &result), "accept parent slot");
    }
}
/* Exercise committed inspection in the standard RV64 acceptance images,
 * including the same untouched result guarantee while publication is staged. */
static UmicomKernelFat16QueryResult umicomFatDirectoryQueryResult;
/* Committed queries now verify that moved subtrees keep their children.
 * The preceding implementation is retained for engineering review. */
#if 0
static void UmicomFatDirectoryInspect(UmicomSize state, UmicomBoolean staged,
    const UmicomKernelFat16LifecycleResult *evidence)
{
    UmicomKernelFat16Query query;
    UmicomFatDirectoryFill(&query, sizeof(query), 0U);
    query.kind = UMICOM_FAT16_QUERY_LIST;
    query.path = state >= 8U ? "/AGAIN" : state >= 3U && state <= 5U ? "/WORK/SUB" :
        state == 7U ? "/" : "/WORK";
    UmicomFatDirectoryFill(&umicomFatDirectoryQueryResult, sizeof(umicomFatDirectoryQueryResult), 0xa5U);
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16LifecycleQuery(
        &umicomFatDirectoryOwner, &query, &umicomFatDirectoryQueryResult);
    if (staged) {
        UmicomFatDirectoryRequire(status == UMICOM_FAT16_UPDATE_BAD_STATE, "staged inspection refused");
        const UmicomU8 *const bytes = (const UmicomU8 *)&umicomFatDirectoryQueryResult;
        for (UmicomSize i = 0U; i < sizeof(umicomFatDirectoryQueryResult); ++i)
            UmicomFatDirectoryRequire(bytes[i] == 0xa5U, "refused inspection leaves complete output unchanged");
    } else {
        UmicomFatDirectoryStatus(status, "committed listing under the exclusive lease");
        if (state != 7U)
            UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.directory.count ==
                (state == 9U ? 15U : state == 2U || state == 3U || state == 4U ? 1U : 0U),
                "committed directory count");
        UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.committedOperations == evidence->committedOperations,
            "query carries the accepted operation count");
        if (state == 3U || state == 4U) {
            query.kind = UMICOM_FAT16_QUERY_READ;
            query.path = "/WORK/SUB/NOTE.TXT";
            query.capacity = UMICOM_FAT16_READ_BYTES;
            UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleQuery(&umicomFatDirectoryOwner,
                &query, &umicomFatDirectoryQueryResult), "committed file read under the exclusive lease");
            UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.bytes == (state == 3U ? 700U : 1600U),
                "query returns the persisted file size");
            for (UmicomSize i = 0U; i < umicomFatDirectoryQueryResult.bytes; ++i)
                UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.data[i] ==
                    UmicomFat16LifecycleFixturePattern(i < 700U ? 1U : 2U, i < 700U ? i : i - 700U),
                    "query preserves original and appended content");
        }
        UmicomKernelConsoleWriteLine("fat16-directory.committed-inspection=verified");
    }
    UmicomFatDirectoryRequire(UmicomFatDirectoryEqual(evidence, &umicomFatDirectoryOwner.lastResult,
        sizeof(*evidence)), "query preserves the last mutation evidence");
    UmicomFatDirectoryFill(&umicomFatDirectoryQueryResult, sizeof(umicomFatDirectoryQueryResult), 0U);
}
#endif

static void UmicomFatDirectoryInspect(UmicomSize state, UmicomBoolean staged,
    const UmicomKernelFat16LifecycleResult *evidence)
{
    UmicomKernelFat16Query query;
    UmicomFatDirectoryFill(&query, sizeof(query), 0U);
    query.kind = UMICOM_FAT16_QUERY_LIST;
    query.path = state >= 12U ? "/DOCS/RENAMED" : state >= 10U ? "/DOCS/LIBRARY" : state >= 8U ? "/AGAIN" : state >= 3U && state <= 5U ? "/WORK/SUB" :
        state == 7U ? "/" : "/WORK";
    UmicomFatDirectoryFill(&umicomFatDirectoryQueryResult, sizeof(umicomFatDirectoryQueryResult), 0xa5U);
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16LifecycleQuery(
        &umicomFatDirectoryOwner, &query, &umicomFatDirectoryQueryResult);
    if (staged) {
        UmicomFatDirectoryRequire(status == UMICOM_FAT16_UPDATE_BAD_STATE, "staged inspection refused");
        const UmicomU8 *const bytes = (const UmicomU8 *)&umicomFatDirectoryQueryResult;
        for (UmicomSize i = 0U; i < sizeof(umicomFatDirectoryQueryResult); ++i)
            UmicomFatDirectoryRequire(bytes[i] == 0xa5U, "refused inspection leaves complete output unchanged");
    } else {
        UmicomFatDirectoryStatus(status, "committed listing under the exclusive lease");
        if (state != 7U)
            UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.directory.count ==
                (state == 13U ? 13U : state == 11U || state == 12U ? 14U : state == 9U || state == 10U ? 15U : state == 2U || state == 3U || state == 4U ? 1U : 0U),
                "committed directory count");
        UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.committedOperations == evidence->committedOperations,
            "query carries the accepted operation count");
        if (state == 3U || state == 4U) {
            query.kind = UMICOM_FAT16_QUERY_READ;
            query.path = "/WORK/SUB/NOTE.TXT";
            query.capacity = UMICOM_FAT16_READ_BYTES;
            UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleQuery(&umicomFatDirectoryOwner,
                &query, &umicomFatDirectoryQueryResult), "committed file read under the exclusive lease");
            UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.bytes == (state == 3U ? 700U : 1600U),
                "query returns the persisted file size");
            for (UmicomSize i = 0U; i < umicomFatDirectoryQueryResult.bytes; ++i)
                UmicomFatDirectoryRequire(umicomFatDirectoryQueryResult.data[i] ==
                    UmicomFat16LifecycleFixturePattern(i < 700U ? 1U : 2U, i < 700U ? i : i - 700U),
                    "query preserves original and appended content");
        }
        UmicomKernelConsoleWriteLine("fat16-directory.committed-inspection=verified");
    }
    UmicomFatDirectoryRequire(UmicomFatDirectoryEqual(evidence, &umicomFatDirectoryOwner.lastResult,
        sizeof(*evidence)), "query preserves the last mutation evidence");
    UmicomFatDirectoryFill(&umicomFatDirectoryQueryResult, sizeof(umicomFatDirectoryQueryResult), 0U);
}

static void UmicomFatDirectoryWriter(UmicomBoolean complete)
{
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatDirectoryFill(&before, sizeof(before), 0U);
    UmicomFatDirectoryFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatDirectoryRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatDirectoryDiscover(&slot);
    UmicomKernelFat16LifecycleCommitter *const owner = &umicomFatDirectoryOwner;
    UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleOpen(owner, domain, slot, 0U, 10000000U), "exclusive directory lease");
    const UmicomSize state = UmicomFatDirectoryState(domain, owner->commit.updater.handle, UMICOM_FALSE) + 1U;
    UmicomFatDirectoryRequire(state < UMICOM_FAT16_DIRECTORY_FIXTURE_STATES, "bounded directory transition");
    UmicomFatDirectoryPhase(state);
    if (state == 9U) UmicomFatDirectoryFillParent();
    UmicomKernelFat16LifecycleRequest request = UmicomFatDirectoryRequest(state);
    UmicomKernelFat16LifecycleResult result;
    UmicomFatDirectoryFill(&result, sizeof(result), 0U);
    UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleStage(owner, &request, &result), "directory Stage");
    UmicomFatDirectoryRequire(!result.commit.commitAccepted && result.directoryVerified &&
        result.directoryDurable && result.commit.dirtyDurable, "staged evidence without acceptance");
    if (state == 9U) UmicomFatDirectoryRequire(owner->plan.parentGrown && owner->plan.parentAddedCluster == 10U &&
        result.allocatedClusters == 2U, "distinct child and parent extension");
    UmicomFatDirectoryInspect(state, UMICOM_TRUE, &result);
    UmicomFatDirectoryDisk(domain, owner->commit.updater.handle, state, UMICOM_TRUE);
    if (complete) {
        UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleFinish(owner, &result), "directory Finish");
        UmicomFatDirectoryRequire(result.commit.commitAccepted && owner->committedOperations == (state == 9U ? 15U : 1U),
            "accepted operations count");
        UmicomFatDirectoryInspect(state, UMICOM_FALSE, &result);
        UmicomFatDirectoryDisk(domain, owner->commit.updater.handle, state, UMICOM_FALSE);
    } else UmicomKernelConsoleWriteLine("fat16-directory.finish=deliberately-omitted-not-a-physical-power-cut");
    UmicomFatDirectoryStatus(UmicomKernelFat16LifecycleClose(owner), "release without repair");
    UmicomFatDirectoryRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine(complete ? "UMICOM_KERNEL_FAT16_DIRECTORY_READY" : "UMICOM_KERNEL_FAT16_DIRECTORY_INTERRUPTED_READY");
}
#ifdef UMICOM_KERNEL_FAT16_DIRECTORY_TEST
void UmicomKernelFat16DirectoryValidate(void) { UmicomFatDirectoryWriter(UMICOM_TRUE); }
#else
void UmicomKernelFat16DirectoryInterruptedValidate(void) { UmicomFatDirectoryWriter(UMICOM_FALSE); }
#endif
#else
typedef struct UmicomFatDirectoryReader {
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
} UmicomFatDirectoryReader;
static UmicomKernelFat16 umicomFatDirectoryVolume;
static UmicomBoolean UmicomFatDirectoryRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomFatDirectoryReader *const source = (UmicomFatDirectoryReader *)context;
    return UmicomKernelBlockRead(source->domain, source->handle, sector, 1U, output, 512U) == UMICOM_BLOCK_OK;
}
/* Fresh read-only boots verify old-name absence and preserved moved metadata.
 * The preceding implementation is retained for engineering review. */
#if 0
static void UmicomFatDirectoryReadPaths(UmicomSize state)
{
    const char *const paths[] = {"/WORK", "/WORK/SUB", "/WORK/SUB/NOTE.TXT", "/AGAIN", "/AGAIN/CHILD"};
    const UmicomBoolean present[] = {state < 7U, state >= 2U && state < 6U, state == 3U || state == 4U,
        state >= 8U, state == 9U};
    for (UmicomSize i = 0U; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        UmicomKernelFat16Metadata result;
        UmicomFatDirectoryFill(&result, sizeof(result), 0xa5U);
        const UmicomKernelDiskStatus status = UmicomKernelFat16MetadataRead(&umicomFatDirectoryVolume, paths[i], &result);
        UmicomFatDirectoryRequire(status == (present[i] ? UMICOM_DISK_OK : UMICOM_DISK_NOT_FOUND), "fresh path lookup");
        if (!present[i]) continue;
        UmicomFatDirectoryRequire(result.entry.directory == (i != 2U) &&
            result.writeTimestamp.rawDate == 0x805dU && result.writeTimestamp.rawTime == 0xbf5cU,
            "fresh type and supplied calendar");
    }
    if (state == 3U || state == 4U) {
        UmicomSize got = 0U;
        UmicomFatDirectoryRequire(UmicomKernelFat16Read(&umicomFatDirectoryVolume, "/WORK/SUB/NOTE.TXT", 0U,
            umicomFatDirectoryReadback, 2048U, &got) == UMICOM_DISK_OK && got == (state == 3U ? 700U : 1600U),
            "newly booted file read");
        for (UmicomSize i = 0U; i < got; ++i)
            UmicomFatDirectoryRequire(umicomFatDirectoryReadback[i] ==
                UmicomFat16LifecycleFixturePattern(i < 700U ? 1U : 2U, i < 700U ? i : i - 700U),
                "saved prefix and appended bytes");
    }
}
#endif

static void UmicomFatDirectoryReadPaths(UmicomSize state)
{
    /* Constant path tables live in read-only storage without a hosted copy. */
    static const char *const paths[] = {"/WORK", "/WORK/SUB", "/WORK/SUB/NOTE.TXT", "/AGAIN", "/AGAIN/CHILD",
        "/DOCS/LIBRARY", "/DOCS/LIBRARY/CHILD", "/CHILD", "/DOCS/RENAMED", "/MOVED.TXT"};
    const UmicomBoolean present[] = {state < 7U, state >= 2U && state < 6U, state == 3U || state == 4U,
        state >= 8U && state < 10U, state == 9U, state == 10U || state == 11U,
        state == 10U, state >= 11U, state >= 12U, state == 13U};
    for (UmicomSize i = 0U; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        UmicomKernelFat16Metadata result;
        UmicomFatDirectoryFill(&result, sizeof(result), 0xa5U);
        const UmicomKernelDiskStatus status = UmicomKernelFat16MetadataRead(&umicomFatDirectoryVolume, paths[i], &result);
        UmicomFatDirectoryRequire(status == (present[i] ? UMICOM_DISK_OK : UMICOM_DISK_NOT_FOUND), "fresh path lookup");
        if (!present[i]) continue;
        UmicomFatDirectoryRequire(result.entry.directory == (i != 2U && i != 9U) &&
            result.writeTimestamp.rawDate == 0x805dU && result.writeTimestamp.rawTime == 0xbf5cU,
            "fresh type and supplied calendar");
    }
    if (state == 3U || state == 4U) {
        UmicomSize got = 0U;
        UmicomFatDirectoryRequire(UmicomKernelFat16Read(&umicomFatDirectoryVolume, "/WORK/SUB/NOTE.TXT", 0U,
            umicomFatDirectoryReadback, 2048U, &got) == UMICOM_DISK_OK && got == (state == 3U ? 700U : 1600U),
            "newly booted file read");
        for (UmicomSize i = 0U; i < got; ++i)
            UmicomFatDirectoryRequire(umicomFatDirectoryReadback[i] ==
                UmicomFat16LifecycleFixturePattern(i < 700U ? 1U : 2U, i < 700U ? i : i - 700U),
                "saved prefix and appended bytes");
    }
}
static void UmicomFatDirectoryReaderBoot(UmicomBoolean dirty)
{
    UmicomKernelPhysicalMemorySnapshot before;
    UmicomRiscvSupervisorMachineState machineBefore;
    UmicomFatDirectoryFill(&before, sizeof(before), 0U);
    UmicomFatDirectoryFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFatDirectoryRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "reader allocator ready");
    UmicomSize slot = 0U;
    UmicomKernelBlockDomain *const domain = UmicomFatDirectoryDiscover(&slot);
    UmicomKernelBlockHandle handle = 0U;
    UmicomFatDirectoryBlock(UmicomKernelBlockOpen(domain, slot, 10000000U, &handle), UMICOM_BLOCK_OK, "read-only device lease");
    UmicomKernelBlockInfo info;
    UmicomFatDirectoryFill(&info, sizeof(info), 0U);
    UmicomFatDirectoryBlock(UmicomKernelBlockProbe(domain, slot, &info), UMICOM_BLOCK_OK, "reader geometry");
    UmicomFatDirectoryRequire(!info.writable && info.sectors == UMICOM_DISK_FIXTURE_SECTORS, "read-only disposable image");
    const UmicomSize state = UmicomFatDirectoryState(domain, handle, dirty);
    UmicomFatDirectoryRequire(state > 0U, "produced checkpoint");
    UmicomFatDirectoryPhase(state);
    UmicomFatDirectoryReader source = {domain, handle};
    const UmicomKernelDiskReader reader = {info.sectors, UmicomFatDirectoryRead, &source};
    const UmicomKernelDiskStatus status = UmicomKernelFat16Open(&umicomFatDirectoryVolume, &reader, 0U);
    UmicomFatDirectoryRequire(status == (dirty ? UMICOM_DISK_DIRTY : UMICOM_DISK_OK), "clean admission or dirty refusal");
    if (!dirty) {
        UmicomFatDirectoryReadPaths(state);
        UmicomFatDirectoryRequire(UmicomKernelFat16Close(&umicomFatDirectoryVolume) == UMICOM_DISK_OK, "close inspector");
    } else UmicomFatDirectoryRequire(!umicomFatDirectoryVolume.open, "dirty image not admitted or repaired");
    UmicomFatDirectoryDisk(domain, handle, state, dirty);
    UmicomFatDirectoryBlock(UmicomKernelBlockClose(domain, handle), UMICOM_BLOCK_OK, "release reader lease");
    UmicomFatDirectoryRestore(&before, &machineBefore);
    UmicomKernelConsoleWriteLine(dirty ? "UMICOM_KERNEL_FAT16_DIRECTORY_REJECTED_READY" : "UMICOM_KERNEL_FAT16_DIRECTORY_READBACK_READY");
}
#ifdef UMICOM_KERNEL_FAT16_DIRECTORY_READBACK_TEST
void UmicomKernelFat16DirectoryReadbackValidate(void) { UmicomFatDirectoryReaderBoot(UMICOM_FALSE); }
#else
void UmicomKernelFat16DirectoryRejectedValidate(void) { UmicomFatDirectoryReaderBoot(UMICOM_TRUE); }
#endif
#endif
