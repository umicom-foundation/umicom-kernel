/* Bounded append qualification: actual VirtIO requests, independent visible
 * and durable media, complete backed-byte comparison, and exhaustive failure
 * positions derived from each successful request trace. No legacy test source
 * is edited. This models persistence ordering, not physical power-loss atomicity.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_file_append.h"
#include "umicom/kernel/fat16_metadata.h"
#define __wrap_main UmicomAppendOriginalFileCommitEntry
#include "../fat16_file_commit/file_commit_tests.c"
#undef __wrap_main
static UmicomU64 appendOffset, appendHighestSector;
static UmicomSize appendBytes, appendClusterSectors;
static const char *appendPath;
static UmicomBoolean appendReentry, appendPolicyReentry;
static UmicomU32 appendReentries;
static UmicomBoolean appendConsoleQueue,appendConsoleOutput;
static UmicomU32 appendConsoleReentries;
static void AppendConsoleReenter(void);
static UmicomSize AppendAddress(UmicomU64 position)
{
    const UmicomU64 clusterBytes=appendClusterSectors*512U;
    return ((UmicomSize)UMICOM_DISK_FIXTURE_DATA+(CommitFileCluster((UmicomSize)(position/clusterBytes))-2U)*
        appendClusterSectors)*512U+(UmicomSize)(position%clusterBytes);
}
static UmicomKernelFat16UpdateStatus AppendStage(void)
{
    commitOperation=1U;memset(&fileResult,0xa5,sizeof(fileResult));memcpy(&fileResultBefore,&fileResult,sizeof(fileResult));
    return UmicomKernelFat16FileCommitAppend(&fileOwner,appendPath,commitInput,appendBytes,&fileTime,&fileResult);
}
static void AppendReenter(void)
{
    UmicomKernelFat16FileCommitResult output,before;memset(&output,0x49,sizeof(output));memcpy(&before,&output,sizeof(before));
    CHECK(UmicomKernelFat16FileCommitAppend(&fileOwner,appendPath,commitInput,1U,&fileTime,&output)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16FileCommitStage(&fileOwner,appendPath,0U,commitInput,1U,&fileTime,&output)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16FileCommitFinish(&fileOwner,&output)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16FileCommitClose(&fileOwner)==UMICOM_FAT16_UPDATE_BUSY);
    CommitEqual(&output,&before,sizeof(output));++appendReentries;
}
static UmicomBoolean AppendAllowed(void *context)
{
    CHECK(context==&model);if(appendPolicyReentry) AppendReenter();return model.allowed;
}
static void AppendWriteRegister(void *context,UmicomAddress address,UmicomU32 value)
{
    if(Offset(address)==UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header=(UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU32 command=*(const UmicomU32 *)header;
        const UmicomU64 sector=*(const UmicomU64 *)(header+8U);
        if(command!=UMICOM_VIRTIO_REQUEST_FLUSH) {
            CHECK(sector<UMICOM_DISK_FIXTURE_SECTORS && sector<model.capacity);
            if(sector>appendHighestSector) appendHighestSector=sector;
        }
        if(appendReentry) AppendReenter();
        if(appendConsoleQueue) AppendConsoleReenter();
    }
    FileWriteRegister(context,address,value);
}
static void AppendStart(const char *fixture)
{
    FileStart(fixture);appendOffset=1300U;appendBytes=197U;appendClusterSectors=1U;appendPath="/FRAG.BIN";
    appendConsoleQueue=UMICOM_FALSE;appendConsoleOutput=UMICOM_FALSE;appendConsoleReentries=0U;
    appendHighestSector=0U;appendReentry=UMICOM_FALSE;appendPolicyReentry=UMICOM_FALSE;appendReentries=0U;
    fileTime=(UmicomKernelFat16FileTime){2044U,2U,29U,23U,58U,57U};fileTimeBefore=fileTime;
    for(UmicomSize i=0U;i<sizeof(commitInput);++i) commitInput[i]=(UmicomU8)((i*37U+0x53U)&0x7fU);
    memcpy(commitInputSnapshot,commitInput,sizeof(commitInput));
    domain.operations.write32=AppendWriteRegister;domain.operations.allowed=AppendAllowed;
}
static void AppendPatchExpected(UmicomU64 offset,UmicomSize bytes)
{
    for(UmicomSize i=0U;i<bytes;++i) commitExpected[AppendAddress(offset+i)]=commitInputSnapshot[i];
}
static void AppendPatchMetadata(void)
{
    FilePatchMetadata();CommitPut32(commitExpected+(UmicomSize)fileDirectorySector*512U+fileEntryOffset+28U,
        (UmicomU32)(appendOffset+appendBytes));
}
/* A zero-defined sparse virtual device: all allocated objects and every I/O
 * live inside the8MiB backed region; the unallocated virtual tail is zero and
 * cannot be written by this model. This permits valid FAT16 geometry with
 *16-sector clusters without shipping a large raw fixture. */
static void AppendLargeCluster(void)
{
    static const UmicomU16 clusters[]={2U,3U,4U,9U,6U,7U};UmicomU8 saved[6][512];
    for(UmicomSize i=0U;i<6U;++i) memcpy(saved[i],CommitCluster(clusters[i]),512U);
    memset(commitVisible+(UmicomSize)UMICOM_DISK_FIXTURE_DATA*512U,0,
        sizeof(commitVisible)-(UmicomSize)UMICOM_DISK_FIXTURE_DATA*512U);
    appendClusterSectors=16U;
    for(UmicomSize i=0U;i<6U;++i) {
        UmicomU8 *const target=commitVisible+((UmicomSize)UMICOM_DISK_FIXTURE_DATA+(clusters[i]-2U)*16U)*512U;
        if(clusters[i]!=3U) for(UmicomSize j=0U;j<8192U;++j) target[j]=(UmicomU8)(i*41U+j*13U+7U);
        memcpy(target,saved[i],512U);
    }
    UmicomU8 *const boot=commitVisible+(UmicomSize)UMICOM_DISK_FIXTURE_FIRST*512U;
    boot[13U]=16U;CommitPut16(boot+19U,0U);CommitPut32(boot+32U,65536U);
    CommitPut32(commitVisible+446U+12U,65536U);model.capacity=UMICOM_DISK_FIXTURE_FIRST+65536U;
    appendOffset=20479U;appendBytes=4096U;CommitPut32(CommitRoot(3U)+28U,(UmicomU32)appendOffset);CommitRebase();
}
static void AppendFailed(void)
{
    CHECK(fileOwner.commit.state==UMICOM_FAT16_COMMIT_FAILED && !fileResult.commit.commitAccepted);
    CHECK(fileResult.commit.mediaTouched && !fileOwner.busy && !fileOwner.commit.busy);
    const UmicomKernelFat16FileCommitResult history=fileOwner.lastResult;const UmicomU32 events=commitEventCount;
    CHECK(AppendStage()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&fileResult,&fileResultBefore,sizeof(fileResult));
    CHECK(FileFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&fileResult,&fileResultBefore,sizeof(fileResult));
    CHECK(commitEventCount==events);CommitEqual(&history,&fileOwner.lastResult,sizeof(history));fileResult=history;
}
static void AppendAssertStage(UmicomU64 offset, UmicomSize bytes)
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
    AppendPatchExpected(offset, bytes); AppendPatchMetadata(); CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
    CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
    CommitEqual(commitInput, commitInputSnapshot, sizeof(commitInput)); CommitEqual(&fileTime, &fileTimeBefore, sizeof(fileTime));
}
static void AppendTrace(UmicomU64 offset, UmicomSize bytes)
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
            const UmicomU64 sector = AppendAddress((offset / 512U + index - 4U) * 512U) / 512U;
            CHECK(event->sector == sector); CommitEqual(event->data, commitExpected + (UmicomSize)sector * 512U, 512U);
        }
    }
    CHECK(mutation == sectors + 11U);
}

static void AppendOwnership(const char *name, UmicomBoolean finish)
{
    FileOpen();
    if (finish) { CHECK(AppendStage() == UMICOM_FAT16_UPDATE_OK); AppendAssertStage(appendOffset, appendBytes); }
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
        UmicomKernelFat16FileCommitAppend(owner, path, input, bytes, time, output);
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

static void AppendFreshRead(void)
{
    UmicomKernelFat16 fresh={0};UmicomKernelFat16Metadata info;UmicomU8 bytes[4096];UmicomSize got=0U;
    const UmicomKernelDiskReader reader={model.capacity,CommitDirectRead,commitVisible};
    CHECK(UmicomKernelFat16Open(&fresh,&reader,0U)==UMICOM_DISK_OK);
    CHECK(UmicomKernelFat16MetadataRead(&fresh,appendPath,&info)==UMICOM_DISK_OK);
    CHECK(info.entry.bytes==appendOffset+appendBytes && info.entry.attributes==(UmicomU8)(fileResult.originalAttributes|0x20U));
    CHECK(info.writeTimestamp.state==UMICOM_FAT16_TIMESTAMP_VALID);
    CHECK(info.writeTimestamp.rawTime==0xbf5cU && info.writeTimestamp.rawDate==0x805dU);
    CHECK(UmicomKernelFat16Read(&fresh,appendPath,appendOffset,bytes,sizeof(bytes),&got)==UMICOM_DISK_OK);
    CHECK(got==appendBytes);CommitEqual(bytes,commitInputSnapshot,got);
    CHECK(UmicomKernelFat16Read(&fresh,appendPath,appendOffset+appendBytes,bytes,sizeof(bytes),&got)==UMICOM_DISK_OK && !got);
    CHECK(UmicomKernelFat16Close(&fresh)==UMICOM_DISK_OK);
}
static void AppendSuccess(const char *name)
{
    if(!strcmp(name,"archive_set")) {CommitRoot(3U)[11U]=0x20U;CommitRebase();}
    else if(!strcmp(name,"hidden_system")) {CommitRoot(3U)[11U]=6U;CommitRebase();}
    else if(!strcmp(name,"single_byte")) appendBytes=1U;
    else if(!strcmp(name,"exact_capacity")) appendBytes=236U;
    else if(!strcmp(name,"last_byte")) {appendOffset=1535U;appendBytes=1U;CommitPut32(CommitRoot(3U)+28U,1535U);CommitRebase();}
    else if(!strcmp(name,"write_through")) commitPersistWrites=UMICOM_TRUE;
    else if(!strcmp(name,"root_later_sector")) FileRelocateRoot(19U);
    else if(!strcmp(name,"root_last_entry")) FileRelocateRoot(511U);
    else if(!strncmp(name,"nested_",7U)) {
        FileRelocateNested(strcmp(name,"nested_first")?UMICOM_TRUE:UMICOM_FALSE,
            !strcmp(name,"nested_last_entry")?UMICOM_TRUE:UMICOM_FALSE);appendPath="/DOCS/FRAG.BIN";
    } else if(!strcmp(name,"preserved_fields")) {
        UmicomU8 *const record=CommitRoot(3U);record[12U]=0x18U;record[13U]=199U;
        CommitPut16(record+14U,0x9137U);CommitPut16(record+16U,0x5133U);CommitPut16(record+18U,0x53baU);CommitRebase();
    } else if(!strcmp(name,"casefold")) appendPath="/frag.bin";
    else if(!strcmp(name,"large_maximum")||!strcmp(name,"large_exact_capacity")||!strcmp(name,"large_aligned")) {
        AppendLargeCluster();
        if(!strcmp(name,"large_exact_capacity")) {appendOffset=20480U;CommitPut32(CommitRoot(3U)+28U,20480U);CommitRebase();}
        if(!strcmp(name,"large_aligned")) {appendOffset=16896U;appendBytes=1024U;CommitPut32(CommitRoot(3U)+28U,16896U);CommitRebase();}
    } else if(!strcmp(name,"callback_reentry")) appendReentry=UMICOM_TRUE;
    else if(!strcmp(name,"policy_reentry")) appendPolicyReentry=UMICOM_TRUE;
    else CHECK(!strcmp(name,"ordinary")||!strcmp(name,"idle_intervals"));
    FileOpen();if(!strcmp(name,"idle_intervals")) model.now+=(UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS*3U;
    CHECK(AppendStage()==UMICOM_FAT16_UPDATE_OK);AppendAssertStage(appendOffset,appendBytes);
    if(!strcmp(name,"idle_intervals")) model.now+=(UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS*3U;
    CHECK(FileFinish()==UMICOM_FAT16_UPDATE_OK);FileAssertFinished(appendOffset,appendBytes);AppendTrace(appendOffset,appendBytes);
    if(appendReentry||appendPolicyReentry) CHECK(appendReentries>0U);
    FileClose();AppendFreshRead();
    if(appendClusterSectors>1U) printf("sparse device-sectors=%llu partition-sectors=65536 backed-bytes=%llu highest-request-lba=%llu zero-unallocated-tail=1\n",
        (unsigned long long)model.capacity,(unsigned long long)sizeof(commitVisible),(unsigned long long)appendHighestSector);
}
static void AppendRange(const char *name)
{
    UmicomKernelDiskStatus expected=UMICOM_DISK_RANGE;
    if(!strcmp(name,"one_over_capacity")) appendBytes=237U;
    else if(!strcmp(name,"full_cluster")) CommitPut32(CommitRoot(3U)+28U,1536U);
    else if(!strcmp(name,"empty_file")) appendPath="/EMPTY.TXT";
    else if(!strcmp(name,"size_overflow")) CommitPut32(CommitRoot(3U)+28U,0xffffffffU);
    else if(!strcmp(name,"directory")) {appendPath="/DOCS";expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"root")) {appendPath="/";expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"missing")) {appendPath="/MISSING.BIN";expected=UMICOM_DISK_NOT_FOUND;}
    else if(!strcmp(name,"readonly")) {CommitRoot(3U)[11U]=1U;expected=UMICOM_DISK_READ_ONLY;}
    else if(!strcmp(name,"large_one_over_capacity")) {AppendLargeCluster();appendOffset=20481U;CommitPut32(CommitRoot(3U)+28U,20481U);}
    else CHECK(0);
    CommitRebase();FileOpen();CHECK(AppendStage()!=UMICOM_FAT16_UPDATE_OK);
    CHECK(fileResult.commit.diskStatus==expected && !fileResult.commit.mediaTouched && !fileResult.directorySubmitted);
    CHECK(fileOwner.commit.state==UMICOM_FAT16_COMMIT_READY);CommitUnchanged();FileClose();
}
static void AppendFormat(const char *name)
{
    const UmicomKernelDiskStatus expected=CommitCorrupt(name);
    if(!strcmp(name,"chain_limit")) {
        /* Leave append room so the intended oversized-chain refusal is
         * reached instead of the independent full-final-cluster range guard. */
        CommitPut32(CommitRoot(3U)+28U,257U*512U-1U);appendBytes=1U;CommitRebase();
    }
    const UmicomKernelFat16UpdateStatus opened=UmicomKernelFat16FileCommitOpen(&fileOwner,&domain,0U,0U,32U);
    if(opened==UMICOM_FAT16_UPDATE_OK) {
        CHECK(AppendStage()!=UMICOM_FAT16_UPDATE_OK);CHECK(fileOwner.commit.state==UMICOM_FAT16_COMMIT_READY);
        CHECK(!fileResult.commit.mediaTouched && !fileResult.commit.submittedDataSectors && !fileResult.directorySubmitted);
        if(!strcmp(name,"io_budget")) {
            CHECK(fileResult.commit.diskStatus==UMICOM_DISK_IO_ERROR && fileResult.commit.blockStatus==UMICOM_BLOCK_TIMEOUT);
            CHECK(fileOwner.commit.updater.operationReads==UMICOM_FAT16_IO_LIMIT);
            printf("append stage-read-budget=%u actual-reads-including-open=%u writes=%u flushes=%u\n",
                (unsigned)fileOwner.commit.updater.operationReads,commitReads,commitWrites,commitFlushes);
        }
        else CHECK(fileResult.commit.diskStatus==expected);
    } else CHECK(fileOwner.commit.updater.lastDiskStatus==expected);
    CommitUnchanged();FileClose();
}
static void AppendAllFailures(const char *fixture,const char *name)
{
    const UmicomBoolean large=strstr(name,"large")?UMICOM_TRUE:UMICOM_FALSE;
    if(large) { AppendLargeCluster(); }
    FileOpen();CHECK(AppendStage()==UMICOM_FAT16_UPDATE_OK);
    CHECK(FileFinish()==UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 totalReads=commitReads,totalWrites=commitWrites,totalFlushes=commitFlushes;
    UmicomU32 verifiedReads[64],countVerified=0U,ordinal=0U;UmicomBoolean mutation=UMICOM_FALSE;
    for(UmicomU32 i=0U;i<commitEventCount;++i) {
        if(commitEvents[i].command==UMICOM_VIRTIO_REQUEST_READ) {++ordinal;if(mutation) {CHECK(countVerified<64U);verifiedReads[countVerified++]=ordinal;}}
        else mutation=UMICOM_TRUE;
    }
    FileClose();
    const UmicomBoolean readsFault=!strncmp(name,"read_errors",11U), mismatches=!strncmp(name,"read_mismatches",15U);
    const UmicomBoolean writesFault=!strncmp(name,"write_errors",12U),flushFault=!strncmp(name,"flush_errors",12U);
    const UmicomBoolean dropped=!strncmp(name,"dropped_writes",14U),altered=!strncmp(name,"altered_writes",14U);
    CHECK(readsFault||mismatches||writesFault||flushFault||dropped||altered);
    const UmicomU32 count=readsFault?totalReads:(mismatches?countVerified:(flushFault?totalFlushes:totalWrites));
    for(UmicomU32 at=1U;at<=count;++at) {
        AppendStart(fixture);if(large) AppendLargeCluster();
        if(readsFault) commitFailRead=at;
        else if(mismatches) commitCorruptRead=verifiedReads[at-1U];
        else if(writesFault) {commitFailWrite=at;commitPartialWrite=UMICOM_TRUE;}
        else if(flushFault) commitFailFlush=at;
        else if(dropped) commitDropWrite=at;
        else commitAlterWrite=at;
        UmicomKernelFat16UpdateStatus status=UmicomKernelFat16FileCommitOpen(&fileOwner,&domain,0U,0U,32U);
        if(status==UMICOM_FAT16_UPDATE_OK) {status=AppendStage();if(status==UMICOM_FAT16_UPDATE_OK) status=FileFinish();}
        CHECK(status!=UMICOM_FAT16_UPDATE_OK);
        if(commitMutations) {
            CHECK(!fileResult.commit.commitAccepted);
            if(writesFault) CHECK(fileResult.commit.writeUncertain && fileResult.commit.uncertainSectorValid && fileResult.commit.needsFlush);
            if(flushFault) CHECK(fileResult.commit.needsFlush && !fileResult.commit.uncertainSectorValid);
            AppendFailed();
        } else CommitUnchanged();
        if(readsFault) CHECK(commitReads==at);
        if(mismatches) CHECK(commitReads==verifiedReads[at-1U] && fileResult.commit.diskStatus==UMICOM_DISK_CORRUPT);
        if(writesFault) CHECK(commitWrites==at);
        if(flushFault) CHECK(commitFlushes==at);
        FileClose();
    }
    printf("append injected-%s=%u baseline-reads=%u writes=%u flushes=%u\n",name,count,totalReads,totalWrites,totalFlushes);
}
static void AppendCut(const char *name)
{
    const UmicomBoolean eager=!strncmp(name,"eager.",6U)?UMICOM_TRUE:UMICOM_FALSE;
    const unsigned long at=strtoul(name+(eager?6U:7U),NULL,10);CHECK(at>=1U && at<=20U);
    AppendLargeCluster();commitPersistWrites=eager;commitCutMutation=(UmicomU32)at;FileOpen();
    UmicomKernelFat16UpdateStatus status=AppendStage();if(status==UMICOM_FAT16_UPDATE_OK) status=FileFinish();
    CHECK(status!=UMICOM_FAT16_UPDATE_OK && commitMutations==at && !model.allowed);AppendFailed();
    CommitReplay(UMICOM_FALSE);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));
    CommitReplay(UMICOM_TRUE);CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    const UmicomBoolean clean=(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK)&&
        (CommitFlags(commitDurable,COMMIT_MIRROR)&UMICOM_FAT16_CLEAN_MASK)?UMICOM_TRUE:UMICOM_FALSE;
    if(clean) {
        memcpy(commitExpected,commitInitial,sizeof(commitExpected));
        if(at>4U) {AppendPatchExpected(appendOffset,appendBytes);AppendPatchMetadata();}
        CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    }
    model.allowed=UMICOM_TRUE;FileClose();
}
static void AppendTamper(const char *name)
{
    FileOpen();CHECK(AppendStage()==UMICOM_FAT16_UPDATE_OK);AppendAssertStage(appendOffset,appendBytes);
    const UmicomU32 mutations=commitMutations;UmicomSize address=0U;
    if(!strcmp(name,"primary_header")) address=(UmicomSize)COMMIT_PRIMARY*512U+123U;
    else if(!strcmp(name,"mirror_header")) address=(UmicomSize)COMMIT_MIRROR*512U+345U;
    else if(!strcmp(name,"old_prefix")) address=AppendAddress(1024U);
    else if(!strcmp(name,"appended_data")) address=AppendAddress(1300U);
    else if(!strcmp(name,"remaining_slack")) address=AppendAddress(1535U);
    else {
        address=(UmicomSize)fileDirectorySector*512U+fileEntryOffset;
        if(!strcmp(name,"directory_name")) address+=3U;
        else if(!strcmp(name,"directory_attribute")) address+=11U;
        else if(!strcmp(name,"directory_creation")) address+=14U;
        else if(!strcmp(name,"directory_time")) address+=22U;
        else if(!strcmp(name,"directory_date")) address+=24U;
        else if(!strcmp(name,"directory_cluster")) address+=26U;
        else if(!strcmp(name,"directory_size")) address+=28U;
        else if(!strcmp(name,"directory_neighbour")) address+=37U;
        else CHECK(0);
    }
    commitVisible[address]^=0x53U;CHECK(FileFinish()==UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(commitMutations==mutations && !fileResult.commit.cleanFinalisationStarted && !fileResult.commit.cleanDurable);
    CHECK(fileResult.commit.dataVerified && fileResult.directoryDurable && fileResult.directoryVerified);AppendFailed();FileClose();
}
static void AppendLifecycle(const char *name)
{
    FileOpen();
    if(!strcmp(name,"ready_close")) {FileClose();CommitUnchanged();return;}
    if(!strcmp(name,"finish_before_append")) {CHECK(FileFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitUnchanged();FileClose();return;}
    CHECK(AppendStage()==UMICOM_FAT16_UPDATE_OK);AppendAssertStage(appendOffset,appendBytes);
    const UmicomU32 mutations=commitMutations;
    if(!strcmp(name,"append_twice")||!strcmp(name,"overwrite_after_append")) {
        const UmicomKernelFat16FileCommitResult history=fileOwner.lastResult;const UmicomU32 events=commitEventCount;
        if(!strcmp(name,"append_twice")) CHECK(AppendStage()==UMICOM_FAT16_UPDATE_BAD_STATE);
        else CHECK(FileStage(appendPath,0U,1U)==UMICOM_FAT16_UPDATE_BAD_STATE);
        CommitEqual(&fileResult,&fileResultBefore,sizeof(fileResult));CommitEqual(&history,&fileOwner.lastResult,sizeof(history));CHECK(events==commitEventCount);
    } else if(!strcmp(name,"reset_retry")||!strcmp(name,"first_release_retry")||!strcmp(name,"second_release_retry")) {
        const UmicomKernelBlockHandle handle=fileOwner.commit.updater.handle;
        if(!strcmp(name,"reset_retry")) model.stuckReset=UMICOM_TRUE;else model.failFree=!strcmp(name,"first_release_retry")?1U:2U;
        CHECK(UmicomKernelFat16FileCommitClose(&fileOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(fileOwner.commit.updater.handle==handle && fileOwner.commit.state==UMICOM_FAT16_COMMIT_CLOSING);
        CHECK(Allocated()==(!strcmp(name,"second_release_retry")?1U:2U));
        model.stuckReset=UMICOM_FALSE;model.failFree=0U;
    } else CHECK(!strcmp(name,"staged_close"));
    FileClose();CHECK(commitMutations==mutations);
    CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    CHECK(!(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK));
}
static void AppendMutationClockOne(const char *name)
{
    commitClockInside = !strncmp(name, "inner_", 6U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(commitClockInside || !strncmp(name, "outer_", 6U));
    commitClockRollback = strstr(name, "rollback.") ? UMICOM_TRUE : UMICOM_FALSE;
    const char *const number = strrchr(name, '.'); CHECK(number);
    const unsigned long parsed = strtoul(number + 1U, 0, 10); CHECK(parsed >= 1U && parsed <= 20U);
    commitClockMutation = (UmicomU32)parsed;
    AppendLargeCluster(); FileOpen();
    if (commitClockMutation > 16U) {
        CHECK(AppendStage() == UMICOM_FAT16_UPDATE_OK); AppendAssertStage(appendOffset, appendBytes);
    }
    CHECK((commitClockMutation > 16U ? FileFinish() : AppendStage()) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
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
    if (!commitClockInside && commitClockMutation == 14U) CHECK(fileResult.commit.dataDurable);
    if (!commitClockInside && commitClockMutation == 16U) CHECK(fileResult.directoryDurable);
    if (!commitClockInside && commitClockMutation == 20U) CHECK(fileResult.commit.cleanDurable && !fileResult.commit.cleanVerified);
    AppendFailed(); FileClose();
}

static void AppendAcceptanceClock(const char *fixture, const char *name)
{
    const UmicomBoolean finish = !strncmp(name, "finish_", 7U) ? UMICOM_TRUE : UMICOM_FALSE;
    CHECK(finish || !strncmp(name, "stage_", 6U));
    FileOpen(); CHECK(AppendStage() == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 stageRead = commitReads;
    CHECK(FileFinish() == UMICOM_FAT16_UPDATE_OK); const UmicomU32 finishRead = commitReads;
    FileClose(); AppendStart(fixture); FileOpen();
    commitClockRead = finish ? finishRead : stageRead; commitClockSkip = 1U;
    commitClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE;
    if (finish) { CHECK(AppendStage() == UMICOM_FAT16_UPDATE_OK); AppendAssertStage(appendOffset, appendBytes); }
    CHECK((finish ? FileFinish() : AppendStage()) == UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(commitClockInjected && !commitClockSkip && !fileResult.commit.commitAccepted);
    CHECK(fileResult.commit.dataDurable && fileResult.commit.dataVerified && fileResult.commit.dirtyDurable && fileResult.commit.dirtyVerified);
    CHECK(fileResult.directoryDurable && fileResult.directoryVerified);
    CHECK(fileResult.commit.cleanVerified == finish && fileResult.commit.cleanDurable == finish);
    CHECK(!fileResult.commit.needsFlush && !fileResult.commit.writeUncertain && !fileResult.commit.uncertainSectorValid);
    CHECK(fileResult.commit.lastBlockOutcome == UMICOM_BLOCK_COMPLETED);
    CHECK(fileResult.commit.blockStatus == (commitClockRollback ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT));
    AppendFailed(); FileClose();
}

static void AppendPreflightFailure(const char *fixture, const char *name)
{
    FileOpen(); CHECK(AppendStage() == UMICOM_FAT16_UPDATE_OK);
    UmicomU32 lastRead = 0U, directoryRead = 0U;
    for (UmicomSize i = 0U; i < commitEventCount; ++i) {
        if (commitEvents[i].command != UMICOM_VIRTIO_REQUEST_READ) break;
        ++lastRead;
        if (commitEvents[i].sector == fileDirectorySector) directoryRead = lastRead;
    }
    CHECK(lastRead > directoryRead && directoryRead > 4U);
    FileClose(); AppendStart(fixture); FileOpen();
    const UmicomBoolean clock = strstr(name, "clock_") ? UMICOM_TRUE : UMICOM_FALSE;
    if (clock) { commitClockRead = lastRead; commitClockRollback = strstr(name, "rollback") ? UMICOM_TRUE : UMICOM_FALSE; }
    else if (!strcmp(name, "directory_mismatch")) {
        commitCorruptRead = directoryRead; fileReadCorruptionByte = fileEntryOffset + 26U;
    } else commitFailRead = !strcmp(name, "first_read") ? commitReads + 1U : lastRead;
    CHECK(AppendStage() != UMICOM_FAT16_UPDATE_OK);
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
static void AppendMutationClocks(const char *fixture,const char *name)
{
    for(unsigned i=1U;i<=20U;++i) {
        AppendStart(fixture);char selected[64];
        CHECK(snprintf(selected,sizeof(selected),"%s.%u",name,i)>0);AppendMutationClockOne(selected);
    }
    printf("append mutation-clock positions=20 kind=%s\n",name);
}
static void AppendConsoleReenter(void)
{
    const UmicomSize before=transcriptBytes;
    CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell,"fatfilecommit")==UMICOM_SHELL_BUSY);
    CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(transcriptBytes==before);++appendConsoleReentries;
}
static void AppendConsoleOutput(void *context,const char *text,UmicomSize bytes)
{
    Output(context,text,bytes);if(appendConsoleOutput) AppendConsoleReenter();
}
static void AppendConsole(const char *name)
{
    memset(&commitShell,0,sizeof(commitShell));memset(&commitForeignShell,0,sizeof(commitForeignShell));
    commitShell.output=AppendConsoleOutput;commitForeignShell.output=AppendConsoleOutput;
    if(!strcmp(name,"before_open")) {
        CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_IO_ERROR);
        CHECK(!commitEventCount && !Allocated());return;
    }
    if(!strcmp(name,"invalid_arguments")) {
        const char *const bad[]={"fatfileappend","fatfileappend /FRAG.BIN","fatfileappend /FRAG.BIN 0 text"};
        for(UmicomSize i=0U;i<3U;++i) CHECK(FileConsoleCommand(&commitShell,bad[i])==UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!commitEventCount && !Allocated());return;
    }
    if(!strcmp(name,"range_refusal")) {CommitPut32(CommitRoot(3U)+28U,1536U);CommitRebase();}
    FileConsoleOpen();
    if(!strcmp(name,"no_time")) {
        const UmicomU32 events=commitEventCount;
        CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_IO_ERROR);
        CHECK(commitEventCount==events);CommitUnchanged();FileConsoleClose();return;
    }
    fileTime=(UmicomKernelFat16FileTime){2037U,11U,23U,14U,35U,59U};fileTimeBefore=fileTime;FileConsoleTime();
    if(!strcmp(name,"empty_text")||!strcmp(name,"wrong_shell")||!strcmp(name,"range_refusal")) {
        const UmicomU32 events=commitEventCount;
        if(!strcmp(name,"empty_text")) {
            CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN \"\"")==UMICOM_SHELL_IO_ERROR);
            CHECK(strstr(transcript,"fat.file.result=not-admitted; previous evidence retained"));
        } else if(!strcmp(name,"wrong_shell")) {
            CHECK(FileConsoleCommand(&commitForeignShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_BAD_STATE);
        } else {
            CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_IO_ERROR);
            CHECK(strstr(transcript,"fat.file.append=range"));
        }
        if(strcmp(name,"range_refusal")) CHECK(commitEventCount==events);
        CommitUnchanged();FileConsoleClose();return;
    }
    if(!strcmp(name,"append_after_overwrite")) {
        FileConsoleStage();const UmicomU32 events=commitEventCount;
        CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript,"fat.file.previous-result") && commitEventCount==events);FileConsoleClose();return;
    }
    appendConsoleQueue=!strcmp(name,"queue_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    appendConsoleOutput=!strcmp(name,"output_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    if(!strcmp(name,"directory_failure")) commitFailWrite=4U;
    const UmicomKernelShellStatus staged=FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN \"APPEND!\"");
    CHECK(strstr(transcript,"fat.file.append.original-bytes=1300 planned-bytes=1307"));
    CHECK(strstr(transcript,"commit-accepted=0"));
    if(!strcmp(name,"directory_failure")) {
        CHECK(staged==UMICOM_SHELL_IO_ERROR && commitWrites==4U && commitFlushes==3U);
        CHECK(strstr(transcript,"fat.file.directory-observed submitted=1 completed=0 durable=0 verified=0"));
        FileConsoleClose();return;
    }
    CHECK(staged==UMICOM_SHELL_OK && commitWrites==4U && commitFlushes==4U);
    CHECK(strstr(transcript,"fat.file-state=staged") && strstr(transcript,"dirty-durable=1 dirty-verified=1 data-durable=1 data-verified=1"));
    const char payload[]="APPEND!";for(UmicomSize i=0U;i<7U;++i) commitExpected[AppendAddress(1300U+i)]=(UmicomU8)payload[i];
    appendBytes=7U;AppendPatchMetadata();CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    if(!strcmp(name,"staged_close")) {FileConsoleClose();CHECK(commitWrites==4U && commitFlushes==4U);return;}
    CommitClearTranscript();if(!strcmp(name,"late_clean_failure")) commitFailFlush=6U;
    const UmicomKernelShellStatus finished=FileConsoleCommand(&commitShell,"fatfilecommit");
    if(!strcmp(name,"late_clean_failure")) {
        CHECK(finished==UMICOM_SHELL_IO_ERROR && strstr(transcript,"commit-accepted=0"));FileConsoleClose();return;
    }
    CHECK(finished==UMICOM_SHELL_OK && strstr(transcript,"commit-accepted=1"));
    CommitExpectedDirty(UMICOM_FALSE);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));
    CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));FileConsoleClose();
    if(appendConsoleQueue||appendConsoleOutput) CHECK(appendConsoleReentries>0U);
    else CHECK(!strcmp(name,"sequence"));
}

int __wrap_main(int argc,char **argv)
{
    CHECK(argc==3);AppendStart(argv[2]);const char *const name=argv[1];
    if(!strncmp(name,"success.",8U)) AppendSuccess(name+8U);
    else if(!strncmp(name,"range.",6U)) AppendRange(name+6U);
    else if(!strncmp(name,"format.",7U)) AppendFormat(name+7U);
    else if(!strncmp(name,"ownership.",10U)) AppendOwnership(name+10U,UMICOM_FALSE);
    else if(!strncmp(name,"finish_ownership.",17U)) AppendOwnership(name+17U,UMICOM_TRUE);
    else if(!strncmp(name,"failures.",9U)) AppendAllFailures(argv[2],name+9U);
    else if(!strncmp(name,"cut.",4U)) AppendCut(name+4U);
    else if(!strncmp(name,"tamper.",7U)) AppendTamper(name+7U);
    else if(!strncmp(name,"lifetime.",9U)) AppendLifecycle(name+9U);
    else if(!strncmp(name,"console.",8U)) AppendConsole(name+8U);
    else if(!strncmp(name,"clock.",6U)) AppendMutationClocks(argv[2],name+6U);
    else if(!strncmp(name,"acceptance_clock.",17U)) AppendAcceptanceClock(argv[2],name+17U);
    else if(!strncmp(name,"preflight.",10U)) AppendPreflightFailure(argv[2],name+10U);
    else CHECK(0);
    CHECK(!Allocated());printf("fat16-append.%s: ok\n",name);return 0;
}
