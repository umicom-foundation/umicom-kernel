/* Metadata-only rename qualification using the real parser, VirtIO transport
 * and allocator. The unchanged model supplies visible/durable media and fault
 * injection. New assertions independently prohibit all file-data writes and
 * compare every byte of the complete8MiB synthetic image.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_rename_commit.h"
#include "umicom/kernel/fat16_metadata.h"
#define __wrap_main UmicomRenameOriginalFileCommitEntry
#include "../fat16_file_commit/file_commit_tests.c"
#undef __wrap_main
static UmicomKernelFat16RenameCommitter renameOwner;
static UmicomKernelFat16RenameResult renameResult,renameResultBefore;
static const char *renamePath,*renameNewName,*renameCanonical;
static char renamePathInput[UMICOM_FAT16_PATH_BYTES],renameNameInput[13];
static UmicomBoolean renameCallbackReenter,renamePolicyReenter,renameConsoleQueue,renameConsoleOutput;
static UmicomU32 renameReentries,renameConsoleReentries;
static void RenameConsoleReenter(void);
static UmicomKernelFat16UpdateStatus RenameStage(void)
{
    /* A compiler may pool the bare alias as a suffix of the source literal.
     * Normal fixtures must satisfy the API's disjoint input contract; explicit
     * alias cases below call the API directly with their selected addresses. */
    const UmicomSize pathBytes=strlen(renamePath)+1U,nameBytes=strlen(renameNewName)+1U;
    CHECK(pathBytes<=sizeof(renamePathInput) && nameBytes<=sizeof(renameNameInput));
    memcpy(renamePathInput,renamePath,pathBytes);memcpy(renameNameInput,renameNewName,nameBytes);
    commitOperation=1U;memset(&renameResult,0xa5,sizeof(renameResult));memcpy(&renameResultBefore,&renameResult,sizeof(renameResult));
    return UmicomKernelFat16RenameStage(&renameOwner,renamePathInput,renameNameInput,&renameResult);
}
static UmicomKernelFat16UpdateStatus RenameFinish(void)
{
    commitOperation=2U;memset(&renameResult,0xa5,sizeof(renameResult));memcpy(&renameResultBefore,&renameResult,sizeof(renameResult));
    return UmicomKernelFat16RenameFinish(&renameOwner,&renameResult);
}
static void RenameNoData(const UmicomKernelFat16RenameResult *result)
{
    CHECK(!result->commit.offset && !result->commit.requestedBytes && !result->commit.confirmedBytes && !result->commit.submittedBytes);
    CHECK(!result->commit.completedDataSectors && !result->commit.submittedDataSectors);
    CHECK(!result->commit.dataDurable && !result->commit.dataVerified && result->commit.dataOutcome==UMICOM_FAT16_UPDATE_NOT_SUBMITTED);
}
static void RenameReenter(void)
{
    UmicomKernelFat16RenameResult output,before;memset(&output,0x59,sizeof(output));before=output;
    CHECK(UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16RenameStage(&renameOwner,renamePath,renameNewName,&output)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16RenameFinish(&renameOwner,&output)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_BUSY);
    CommitEqual(&output,&before,sizeof(output));++renameReentries;
}
static UmicomBoolean RenameAllowed(void *context)
{
    CHECK(context==&model);if(renamePolicyReenter) RenameReenter();return model.allowed;
}
static UmicomU64 RenameClock(void *context)
{
    CHECK(context==&model);
    if(commitBackwardClock) {if(model.now) --model.now;return model.now;}
    if(commitStoppedClock) return model.now;
    UmicomU64 now=Clock(context);
    const UmicomBoolean matching=(commitClockMutation && commitMutations==commitClockMutation)||
        (commitClockRead && commitReads==commitClockRead)?UMICOM_TRUE:UMICOM_FALSE;
    if(matching && !commitClockInjected && !model.pending && (commitClockInside?domain.busy:!domain.busy) && commitClockSkip) --commitClockSkip;
    else if(matching && !commitClockInjected && !model.pending && (commitClockInside?domain.busy:!domain.busy)) {
        commitClockInjected=UMICOM_TRUE;
        now=commitClockRollback?(commitClockInside?commitPreviousClock-1U:renameOwner.commit.updater.operationClock-1U):
            now+(commitClockInside?domain.slots[0].timeoutTicks:UMICOM_FAT16_UPDATE_OPERATION_TICKS);
        model.now=now;
    }
    commitPreviousClock=now;return now;
}
static void RenameWriteRegister(void *context,UmicomAddress address,UmicomU32 value)
{
    if(Offset(address)==UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header=(UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU32 command=*(const UmicomU32 *)header;const UmicomU64 sector=*(const UmicomU64 *)(header+8U);
        if(command==UMICOM_VIRTIO_REQUEST_WRITE) {
            CHECK(CommitHeaderSector(sector)||sector==fileDirectorySector);
            if(sector==fileDirectorySector) {
                CHECK(commitWrites==2U && commitFlushCompletions==2U && commitFlushes==2U);
                UmicomBoolean primary=UMICOM_FALSE,mirror=UMICOM_FALSE;UmicomSize lastFlush=0U;
                for(UmicomSize i=0U;i<commitEventCount;++i)
                    if(commitEvents[i].command==UMICOM_VIRTIO_REQUEST_FLUSH && commitEvents[i].completed) lastFlush=i;
                for(UmicomSize i=lastFlush+1U;i<commitEventCount;++i) {
                    const CommitEvent *const event=&commitEvents[i];
                    if(event->command==UMICOM_VIRTIO_REQUEST_READ && event->completed) {
                        if(event->sector==COMMIT_PRIMARY && i>3U) primary=UMICOM_TRUE;
                        if(event->sector==COMMIT_MIRROR && i>3U) mirror=UMICOM_TRUE;
                    }
                }
                CHECK(primary && mirror);
            }
        }
        if(renameCallbackReenter) { RenameReenter(); }
        if(renameConsoleQueue) { RenameConsoleReenter(); }
    }
    CommitWriteRegister(context,address,value);
}
static void RenameStart(const char *fixture)
{
    FileStart(fixture);memset(&renameOwner,0,sizeof(renameOwner));memset(&renameResult,0xa5,sizeof(renameResult));renameResultBefore=renameResult;
    renamePath="/FRAG.BIN";renameNewName="SAVED.BIN";renameCanonical="SAVED.BIN";
    renameCallbackReenter=UMICOM_FALSE;renamePolicyReenter=UMICOM_FALSE;renameReentries=0U;
    renameConsoleQueue=UMICOM_FALSE;renameConsoleOutput=UMICOM_FALSE;renameConsoleReentries=0U;
    domain.operations.write32=RenameWriteRegister;domain.operations.clock=RenameClock;domain.operations.allowed=RenameAllowed;
}
static void RenameOpen(void)
{
    CHECK(UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_OK);
    CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_READY && renameOwner.commit.updater.admitted && Allocated()==2U);
    CHECK(!commitWrites && !commitFlushes);
}
static void RenameClose(void)
{
    const UmicomKernelFat16RenameResult saved=renameOwner.lastResult;const UmicomU32 mutations=commitMutations;
    CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_OK);
    CHECK(!Allocated() && !renameOwner.commit.updater.handle && !renameOwner.busy);
    CHECK(UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK && commitMutations==mutations);
    CommitEqual(&saved,&renameOwner.lastResult,sizeof(saved));
    FileZero(&renameOwner.renamePlan,sizeof(renameOwner.renamePlan));FileZero(renameOwner.renameWorkspace.path,sizeof(renameOwner.renameWorkspace.path));
    FileZero(renameOwner.renameWorkspace.newName,sizeof(renameOwner.renameWorkspace.newName));FileZero(&renameOwner.renameWorkspace.stage,sizeof(renameOwner.renameWorkspace.stage));
    FileZero(&renameOwner.commit.updater.plan,sizeof(renameOwner.commit.updater.plan));
    FileZero(renameOwner.commit.cleanHeader,sizeof(renameOwner.commit.cleanHeader));FileZero(renameOwner.commit.dirtyHeader,sizeof(renameOwner.commit.dirtyHeader));
    FileZero(renameOwner.commit.readback,sizeof(renameOwner.commit.readback));
}
static void RenamePatchExpected(void)
{
    UmicomU8 *const entry=commitExpected+(UmicomSize)fileDirectorySector*512U+fileEntryOffset;
    memset(entry,' ',11U);UmicomSize field=0U,pos=0U;
    for(UmicomSize i=0U;renameCanonical[i];++i) {
        if(renameCanonical[i]=='.') {field=8U;pos=0U;} else entry[field+pos++]=(UmicomU8)renameCanonical[i];
    }
    entry[12U]&=(UmicomU8)~0x18U;
}
static void RenameAssertStage(void)
{
    RenameNoData(&renameResult);CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_STAGED);
    CHECK(renameResult.commit.status==UMICOM_FAT16_UPDATE_OK && renameResult.commit.dirtyDurable && renameResult.commit.dirtyVerified);
    CHECK(renameResult.directoryPlanned && renameResult.directorySubmitted && renameResult.directoryCompleted && renameResult.directoryDurable && renameResult.directoryVerified);
    CHECK(renameResult.directorySector==fileDirectorySector && renameResult.entryOffset==fileEntryOffset && !strcmp(renameResult.updatedName,renameCanonical));
    CHECK(renameResult.commit.completedMetadataSectors==3U && renameResult.commit.submittedMetadataSectors==3U && renameResult.commit.completedFlushes==3U);
    CHECK(commitWrites==3U && commitFlushes==3U && !renameResult.commit.cleanFinalisationStarted && !renameResult.commit.commitAccepted);
    CHECK(!renameResult.commit.needsFlush && !renameResult.commit.writeUncertain && !renameResult.commit.uncertainSectorValid);
    RenamePatchExpected();CommitExpectedDirty(UMICOM_TRUE);
    CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    CommitEqual(&renameResult,&renameOwner.lastResult,sizeof(renameResult));
}
static void RenameAssertFinished(void)
{
    RenameNoData(&renameResult);CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_COMMITTED);
    CHECK(renameResult.commit.status==UMICOM_FAT16_UPDATE_OK && renameResult.commit.commitAccepted);
    CHECK(renameResult.commit.cleanFinalisationStarted && renameResult.commit.cleanDurable && renameResult.commit.cleanVerified);
    CHECK(renameResult.commit.dirtyDurable && renameResult.commit.dirtyVerified && renameResult.directoryDurable && renameResult.directoryVerified);
    CHECK(renameResult.commit.completedMetadataSectors==5U && renameResult.commit.submittedMetadataSectors==5U && renameResult.commit.completedFlushes==5U);
    CHECK(commitWrites==5U && commitFlushes==5U);CommitExpectedDirty(UMICOM_FALSE);
    CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    CommitEqual(&renameResult,&renameOwner.lastResult,sizeof(renameResult));
    UmicomSize mutation=0U;const UmicomU64 sectors[]={COMMIT_MIRROR,COMMIT_PRIMARY,fileDirectorySector,COMMIT_MIRROR,COMMIT_PRIMARY};
    for(UmicomSize i=0U;i<commitEventCount;++i) {
        const CommitEvent *const event=&commitEvents[i];if(event->command==UMICOM_VIRTIO_REQUEST_READ) continue;
        CHECK(event->completed);
        if(mutation%2U) CHECK(event->command==UMICOM_VIRTIO_REQUEST_FLUSH);
        else {CHECK(event->command==UMICOM_VIRTIO_REQUEST_WRITE && event->sector==sectors[mutation/2U]);}
        ++mutation;
    }
    CHECK(mutation==10U);
}
static void RenameFailed(void)
{
    RenameNoData(&renameResult);CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_FAILED && !renameResult.commit.commitAccepted);
    CHECK(renameResult.commit.mediaTouched && !renameOwner.busy && !renameOwner.commit.busy);
    const UmicomKernelFat16RenameResult saved=renameOwner.lastResult;const UmicomU32 events=commitEventCount;
    CHECK(RenameStage()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&renameResult,&renameResultBefore,sizeof(renameResult));
    CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&renameResult,&renameResultBefore,sizeof(renameResult));
    CHECK(events==commitEventCount);CommitEqual(&saved,&renameOwner.lastResult,sizeof(saved));renameResult=saved;
}
static void RenameFreshRead(void)
{
    UmicomKernelFat16 fresh={0};UmicomKernelFat16Metadata metadata;UmicomKernelFat16Entry old;
    const UmicomKernelDiskReader reader={UMICOM_DISK_FIXTURE_SECTORS,CommitDirectRead,commitVisible};
    CHECK(UmicomKernelFat16Open(&fresh,&reader,0U)==UMICOM_DISK_OK);
    CHECK(UmicomKernelFat16Stat(&fresh,renamePath,&old)==UMICOM_DISK_NOT_FOUND);
    char updated[UMICOM_FAT16_PATH_BYTES];const char *const slash=strrchr(renamePath,'/');CHECK(slash);
    const UmicomSize parent=(UmicomSize)(slash-renamePath)+1U;memcpy(updated,renamePath,parent);
    memcpy(updated+parent,renameCanonical,strlen(renameCanonical)+1U);
    CHECK(UmicomKernelFat16MetadataRead(&fresh,updated,&metadata)==UMICOM_DISK_OK);
    const UmicomU8 *const original=commitInitial+(UmicomSize)fileDirectorySector*512U+fileEntryOffset;
    CHECK(metadata.entry.bytes==renameResult.originalEntry.bytes && metadata.entry.firstCluster==renameResult.originalEntry.firstCluster);
    CHECK(metadata.entry.attributes==original[11U] && !strcmp(metadata.entry.name,renameCanonical));
    CHECK(metadata.writeTimestamp.rawTime==(UmicomU16)((UmicomU16)original[22U]|(UmicomU16)((UmicomU16)original[23U]<<8U)));
    CHECK(metadata.writeTimestamp.rawDate==(UmicomU16)((UmicomU16)original[24U]|(UmicomU16)((UmicomU16)original[25U]<<8U)));
    UmicomU8 bytes[1536];UmicomSize got=0U;
    CHECK(UmicomKernelFat16Read(&fresh,updated,0U,bytes,sizeof(bytes),&got)==UMICOM_DISK_OK);
    CHECK(got==metadata.entry.bytes);
    for(UmicomSize i=0U;i<got;++i) CHECK(bytes[i]==commitInitial[CommitFileAddress(i)]);
    CHECK(UmicomKernelFat16Close(&fresh)==UMICOM_DISK_OK);
}
static void RenameSuccess(const char *name)
{
    if(!strcmp(name,"empty")) {renamePath="/EMPTY.TXT";fileEntryOffset=128U;}
    else if(!strcmp(name,"archive_set")) {CommitRoot(3U)[11U]=0x20U;CommitRebase();}
    else if(!strcmp(name,"hidden_system")) {CommitRoot(3U)[11U]=6U;CommitRebase();}
    else if(!strcmp(name,"nt_case_flags")) {CommitRoot(3U)[12U]=0x18U;CommitRebase();}
    else if(!strcmp(name,"mixed_case")) renameNewName="sAvEd.BiN";
    else if(!strcmp(name,"casefold_source")) renamePath="/frag.bin";
    else if(!strcmp(name,"no_extension")) {renameNewName="NOEXT";renameCanonical="NOEXT";}
    else if(!strcmp(name,"maximum_alias")) {renameNewName="abcdefgh.xyz";renameCanonical="ABCDEFGH.XYZ";}
    else if(!strcmp(name,"different_parent")) {renameNewName="GUIDE.TXT";renameCanonical="GUIDE.TXT";}
    else if(!strcmp(name,"deleted_collision")) {CommitEntry(CommitRoot(5U),"SAVED   BIN",0x20U,0U,0U);CommitRoot(5U)[0]=0xe5U;CommitRebase();}
    else if(!strcmp(name,"root_later_sector")) FileRelocateRoot(19U);
    else if(!strcmp(name,"root_last_entry")) FileRelocateRoot(511U);
    else if(!strncmp(name,"nested_",7U)) {
        FileRelocateNested(strcmp(name,"nested_first")?UMICOM_TRUE:UMICOM_FALSE,!strcmp(name,"nested_last_entry")?UMICOM_TRUE:UMICOM_FALSE);
        renamePath="/DOCS/FRAG.BIN";
    } else if(!strcmp(name,"all_preserved_fields")) {
        UmicomU8 *const record=CommitRoot(3U);record[11U]=0x26U;record[12U]=0x18U;record[13U]=199U;
        CommitPut16(record+14U,0x9137U);CommitPut16(record+16U,0x5133U);CommitPut16(record+18U,0x53baU);
        CommitPut16(record+22U,0x747dU);CommitPut16(record+24U,0x7377U);CommitRebase();
    } else if(!strcmp(name,"malformed_timestamp_preserved")) {
        CommitPut16(CommitRoot(3U)+22U,0xffffU);CommitPut16(CommitRoot(3U)+24U,0xffffU);CommitRebase();
    } else if(!strcmp(name,"write_through")) commitPersistWrites=UMICOM_TRUE;
    else if(!strcmp(name,"callback_reentry")) renameCallbackReenter=UMICOM_TRUE;
    else if(!strcmp(name,"policy_reentry")) renamePolicyReenter=UMICOM_TRUE;
    else CHECK(!strcmp(name,"ordinary")||!strcmp(name,"idle_intervals"));
    RenameOpen();if(!strcmp(name,"idle_intervals")) model.now+=(UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS*3U;
    CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);RenameAssertStage();
    if(!strcmp(name,"idle_intervals")) model.now+=(UmicomU64)UMICOM_FAT16_UPDATE_OPERATION_TICKS*3U;
    CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_OK);RenameAssertFinished();
    if(renameCallbackReenter||renamePolicyReenter) CHECK(renameReentries>0U);
    RenameClose();RenameFreshRead();
}
static void RenameRefusal(const char *name)
{
    UmicomKernelDiskStatus expected=UMICOM_DISK_EXISTS;
    if(!strcmp(name,"same_alias")) renameNewName="FRAG.BIN";
    else if(!strcmp(name,"case_only")) renameNewName="frag.bin";
    else if(!strcmp(name,"file_collision")) renameNewName="README.TXT";
    else if(!strcmp(name,"directory_collision")) renameNewName="DOCS";
    else if(!strcmp(name,"root")) {renamePath="/";expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"directory")) {renamePath="/DOCS";expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"missing")) {renamePath="/MISSING.BIN";expected=UMICOM_DISK_NOT_FOUND;}
    else if(!strcmp(name,"readonly")) {CommitRoot(3U)[11U]|=1U;CommitRebase();expected=UMICOM_DISK_READ_ONLY;}
    else if(!strcmp(name,"invalid_name")) {renameNewName="A?.X";expected=UMICOM_DISK_INVALID_ARGUMENT;}
    else if(!strcmp(name,"move_name")) {renameNewName="DOCS/A.X";expected=UMICOM_DISK_INVALID_ARGUMENT;}
    else if(!strcmp(name,"trailing_dot")) {renameNewName="SAVED.";expected=UMICOM_DISK_INVALID_ARGUMENT;}
    else CHECK(0);
    RenameOpen();CHECK(RenameStage()!=UMICOM_FAT16_UPDATE_OK);RenameNoData(&renameResult);
    CHECK(renameResult.commit.diskStatus==expected && !renameResult.commit.mediaTouched && !renameResult.directorySubmitted);
    CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_READY);
    if(!strcmp(name,"file_collision")) {
        renameNewName="EMPTY.TXT";CHECK(RenameStage()!=UMICOM_FAT16_UPDATE_OK);
        CHECK(renameResult.commit.diskStatus==UMICOM_DISK_EXISTS);RenameNoData(&renameResult);
    }
    CommitUnchanged();RenameClose();
}
static void RenameFormat(const char *name)
{
    const UmicomKernelDiskStatus expected=CommitCorrupt(name);
    const UmicomKernelFat16UpdateStatus opened=UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U);
    if(opened==UMICOM_FAT16_UPDATE_OK) {
        CHECK(RenameStage()!=UMICOM_FAT16_UPDATE_OK);RenameNoData(&renameResult);
        CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_READY && !renameResult.commit.mediaTouched && !renameResult.directorySubmitted);
        if(!strcmp(name,"io_budget")) {
            CHECK(renameResult.commit.diskStatus==UMICOM_DISK_IO_ERROR && renameResult.commit.blockStatus==UMICOM_BLOCK_TIMEOUT);
            CHECK(renameOwner.commit.updater.operationReads==UMICOM_FAT16_IO_LIMIT);
            printf("rename stage-read-budget=%u actual-reads-including-open=%u writes=%u flushes=%u\n",
                (unsigned)renameOwner.commit.updater.operationReads,commitReads,commitWrites,commitFlushes);
        } else CHECK(renameResult.commit.diskStatus==expected);
    } else CHECK(renameOwner.commit.updater.lastDiskStatus==expected);
    CommitUnchanged();RenameClose();
}
static void RenameAllFailures(const char *fixture,const char *name)
{
    RenameOpen();CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 totalReads=commitReads;UmicomU32 verified[16],countVerified=0U,ordinal=0U;
    UmicomBoolean mutation=UMICOM_FALSE;
    for(UmicomU32 i=0U;i<commitEventCount;++i) {
        if(commitEvents[i].command==UMICOM_VIRTIO_REQUEST_READ) {++ordinal;if(mutation) {CHECK(countVerified<16U);verified[countVerified++]=ordinal;}}
        else mutation=UMICOM_TRUE;
    }
    CHECK(commitWrites==5U && commitFlushes==5U);RenameClose();
    const UmicomBoolean readError=!strcmp(name,"read_errors"),mismatch=!strcmp(name,"read_mismatches");
    const UmicomBoolean writeError=!strcmp(name,"write_errors"),flushError=!strcmp(name,"flush_errors"),drop=!strcmp(name,"dropped_writes");
    CHECK(readError||mismatch||writeError||flushError||drop||!strcmp(name,"altered_writes"));
    const UmicomU32 count=readError?totalReads:(mismatch?countVerified:5U);
    for(UmicomU32 at=1U;at<=count;++at) {
        RenameStart(fixture);
        if(readError) commitFailRead=at;else if(mismatch) commitCorruptRead=verified[at-1U];
        else if(writeError) {commitFailWrite=at;commitPartialWrite=UMICOM_TRUE;}
        else if(flushError) commitFailFlush=at;else if(drop) commitDropWrite=at;else commitAlterWrite=at;
        UmicomKernelFat16UpdateStatus status=UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U);
        if(status==UMICOM_FAT16_UPDATE_OK) {status=RenameStage();if(status==UMICOM_FAT16_UPDATE_OK) status=RenameFinish();}
        CHECK(status!=UMICOM_FAT16_UPDATE_OK);
        if(commitMutations) {
            RenameNoData(&renameResult);CHECK(!renameResult.commit.commitAccepted);
            if(writeError) CHECK(renameResult.commit.writeUncertain && renameResult.commit.uncertainSectorValid && renameResult.commit.needsFlush);
            if(flushError) CHECK(renameResult.commit.needsFlush && !renameResult.commit.uncertainSectorValid);
            RenameFailed();
        } else CommitUnchanged();
        if(readError) CHECK(commitReads==at);
        if(mismatch) CHECK(commitReads==verified[at-1U] && renameResult.commit.diskStatus==UMICOM_DISK_CORRUPT);
        if(writeError) CHECK(commitWrites==at);
        if(flushError) CHECK(commitFlushes==at);
        RenameClose();
    }
    printf("rename injected-%s=%u baseline-reads=%u writes=5 flushes=5\n",name,count,totalReads);
}
static void RenameCut(const char *name)
{
    const UmicomBoolean eager=!strncmp(name,"eager.",6U)?UMICOM_TRUE:UMICOM_FALSE;
    const unsigned long at=strtoul(name+(eager?6U:7U),NULL,10);CHECK(at>=1U && at<=10U);
    commitPersistWrites=eager;commitCutMutation=(UmicomU32)at;RenameOpen();
    UmicomKernelFat16UpdateStatus status=RenameStage();if(status==UMICOM_FAT16_UPDATE_OK) status=RenameFinish();
    CHECK(status!=UMICOM_FAT16_UPDATE_OK && commitMutations==at && !model.allowed);RenameFailed();
    CommitReplay(UMICOM_FALSE);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));
    CommitReplay(UMICOM_TRUE);CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    const UmicomBoolean clean=(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK)&&
        (CommitFlags(commitDurable,COMMIT_MIRROR)&UMICOM_FAT16_CLEAN_MASK)?UMICOM_TRUE:UMICOM_FALSE;
    if(clean) {
        memcpy(commitExpected,commitInitial,sizeof(commitExpected));if(at>4U) RenamePatchExpected();
        CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    }
    model.allowed=UMICOM_TRUE;RenameClose();
}
static void RenameTamper(const char *name)
{
    RenameOpen();CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);RenameAssertStage();
    const UmicomU32 mutations=commitMutations;UmicomSize address=0U;
    if(!strcmp(name,"primary_header")) address=(UmicomSize)COMMIT_PRIMARY*512U+123U;
    else if(!strcmp(name,"mirror_header")) address=(UmicomSize)COMMIT_MIRROR*512U+345U;
    else {
        address=(UmicomSize)fileDirectorySector*512U+fileEntryOffset;
        if(!strcmp(name,"name")) address+=3U;else if(!strcmp(name,"attribute")) address+=11U;
        else if(!strcmp(name,"case_bits")) address+=12U;else if(!strcmp(name,"creation")) address+=14U;
        else if(!strcmp(name,"time")) address+=22U;else if(!strcmp(name,"date")) address+=24U;
        else if(!strcmp(name,"cluster")) address+=26U;else if(!strcmp(name,"size")) address+=28U;
        else if(!strcmp(name,"neighbour")) address+=37U;else CHECK(0);
    }
    commitVisible[address]^=0x53U;CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(commitMutations==mutations && !renameResult.commit.cleanFinalisationStarted && !renameResult.commit.cleanDurable);
    CHECK(renameResult.directoryDurable && renameResult.directoryVerified);RenameFailed();RenameClose();
}
static void RenameOwnership(const char *name,UmicomBoolean finish)
{
    RenameOpen();if(finish) {CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);RenameAssertStage();}
    const UmicomKernelFat16RenameResult history=renameOwner.lastResult;const UmicomU32 events=commitEventCount;
    UmicomKernelFat16RenameCommitter *owner=&renameOwner;UmicomKernelFat16RenameResult *out=&renameResult;
    const char *source=renamePath,*newName=renameNewName;UmicomKernelFat16UpdateStatus expected=UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    UmicomAddress extra=0U;static UmicomKernelFat16RenameCommitter copied;
    _Alignas(UmicomKernelFat16RenameResult) UmicomU8 unaligned[sizeof(UmicomKernelFat16RenameResult)+8U];memset(unaligned,0x49,sizeof(unaligned));
    char longPath[UMICOM_FAT16_PATH_BYTES],longName[13];memset(longPath,'A',sizeof(longPath));memset(longName,'A',sizeof(longName));
    memset(&renameResult,0xa5,sizeof(renameResult));renameResultBefore=renameResult;
    if(!strcmp(name,"result_owner")) out=&renameOwner.lastResult;
    else if(!strcmp(name,"result_plan")) out=(UmicomKernelFat16RenameResult *)(void *)&renameOwner.renamePlan;
    else if(!strcmp(name,"result_domain")) out=(UmicomKernelFat16RenameResult *)(void *)&domain;
    else if(!strcmp(name,"result_dma")) out=(UmicomKernelFat16RenameResult *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"result_queue")) out=(UmicomKernelFat16RenameResult *)domain.slots[0].queueFrame;
    else if(!strcmp(name,"result_alignment")) out=(UmicomKernelFat16RenameResult *)(void *)(unaligned+1U);
    else if(!strcmp(name,"result_overflow")) out=(UmicomKernelFat16RenameResult *)(~(UmicomAddress)0U&~((UmicomAddress)alignof(UmicomKernelFat16RenameResult)-1U));
    else if(!strcmp(name,"result_null")) out=NULL;
    else if(!strcmp(name,"owner_null")) owner=NULL;
    else if(!strcmp(name,"owner_alignment")) owner=(UmicomKernelFat16RenameCommitter *)((UmicomU8 *)&renameOwner+1U);
    else if(!strcmp(name,"copied_owner")) {copied=renameOwner;owner=&copied;expected=UMICOM_FAT16_UPDATE_BAD_STATE;}
    else if(!strcmp(name,"other_slot_dma")) {
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&extra)==UMICOM_KERNEL_MEMORY_OK);domain.count=2U;domain.slots[1].dataFrame=extra;
        if(finish) out=(UmicomKernelFat16RenameResult *)extra;else newName=(const char *)extra;
    } else if(!strcmp(name,"path_owner")) source=(const char *)&renameOwner;
    else if(!strcmp(name,"path_plan")) source=(const char *)&renameOwner.renamePlan;
    else if(!strcmp(name,"path_domain")) source=(const char *)&domain;
    else if(!strcmp(name,"path_dma")) source=(const char *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"path_queue")) source=(const char *)domain.slots[0].queueFrame;
    else if(!strcmp(name,"path_result")) source=(const char *)&renameResult;
    else if(!strcmp(name,"path_name")) source=newName;
    else if(!strcmp(name,"path_unterminated")) source=longPath;
    else if(!strcmp(name,"path_null")) source=NULL;
    else if(!strcmp(name,"name_owner")) newName=(const char *)&renameOwner;
    else if(!strcmp(name,"name_plan")) newName=renameOwner.renamePlan.updatedName;
    else if(!strcmp(name,"name_domain")) newName=(const char *)&domain;
    else if(!strcmp(name,"name_dma")) newName=(const char *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"name_queue")) newName=(const char *)domain.slots[0].queueFrame;
    else if(!strcmp(name,"name_result")) newName=renameResult.updatedName;
    else if(!strcmp(name,"name_path")) newName=source+1U;
    else if(!strcmp(name,"name_unterminated")) newName=longName;
    else if(!strcmp(name,"name_null")) newName=NULL;
    else if(!strcmp(name,"busy_owner")) renameOwner.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_embedded")) renameOwner.commit.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_updater")) renameOwner.commit.updater.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_workspace")) renameOwner.renameWorkspace.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_base_workspace")) renameOwner.commit.updater.workspace.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_volume")) renameOwner.commit.updater.volume.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_domain")) domain.busy=UMICOM_TRUE;
    else if(!strcmp(name,"unsafe_context")) {model.allowed=UMICOM_FALSE;expected=UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT;}
    else if(!strcmp(name,"unused_data_plan")) {renameOwner.commit.updater.plan.bytes=1U;expected=UMICOM_FAT16_UPDATE_BAD_STATE;}
    else CHECK(0);
    if(!strncmp(name,"busy_",5U)) expected=UMICOM_FAT16_UPDATE_BUSY;
    CHECK((finish?UmicomKernelFat16RenameFinish(owner,out):UmicomKernelFat16RenameStage(owner,source,newName,out))==expected);
    renameOwner.busy=UMICOM_FALSE;renameOwner.commit.busy=UMICOM_FALSE;renameOwner.commit.updater.busy=UMICOM_FALSE;
    renameOwner.renameWorkspace.busy=UMICOM_FALSE;renameOwner.commit.updater.workspace.busy=UMICOM_FALSE;renameOwner.commit.updater.volume.busy=UMICOM_FALSE;
    renameOwner.commit.updater.plan.bytes=0U;domain.busy=UMICOM_FALSE;model.allowed=UMICOM_TRUE;
    if(extra) {domain.count=1U;domain.slots[1].dataFrame=0U;CHECK(__real_UmicomKernelPhysicalMemoryFreeFrame(extra)==UMICOM_KERNEL_MEMORY_OK);}
    CHECK(commitEventCount==events);CommitEqual(&history,&renameOwner.lastResult,sizeof(history));CommitEqual(&renameResult,&renameResultBefore,sizeof(renameResult));
    for(UmicomSize i=0U;i<sizeof(unaligned);++i) CHECK(unaligned[i]==0x49U);
    if(!finish) CommitUnchanged();else {CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));}
    RenameClose();
}
static void RenameOpenArguments(const char *name)
{
    UmicomKernelFat16RenameCommitter *owner=&renameOwner;UmicomKernelBlockDomain *transport=&domain;
    UmicomSize slot=0U,partition=0U;UmicomU64 ticks=32U;UmicomAddress frame=0U;
    if(!strcmp(name,"owner_null")) owner=NULL;
    else if(!strcmp(name,"owner_alignment")) owner=(UmicomKernelFat16RenameCommitter *)((UmicomU8 *)&renameOwner+1U);
    else if(!strcmp(name,"domain_alias")) owner=(UmicomKernelFat16RenameCommitter *)(void *)&domain;
    else if(!strcmp(name,"dma_alias")) {domain.count=2U;domain.slots[1].dataFrame=(UmicomAddress)&renameOwner;frame=domain.slots[1].dataFrame;}
    else if(!strcmp(name,"null_domain")) transport=NULL;
    else if(!strcmp(name,"slot")) slot=UMICOM_BLOCK_SLOT_LIMIT;
    else if(!strcmp(name,"partition")) partition=UMICOM_DISK_PRIMARY_PARTITIONS;
    else if(!strcmp(name,"timeout_zero")) ticks=0U;
    else if(!strcmp(name,"timeout_excess")) ticks=UMICOM_BLOCK_MAX_TIMEOUT_TICKS+1U;
    else if(!strcmp(name,"nonzero_storage")) renameOwner.renameWorkspace.newName[0]='x';
    else CHECK(0);
    const UmicomKernelFat16RenameCommitter before=renameOwner;
    const UmicomKernelFat16UpdateStatus status=UmicomKernelFat16RenameOpen(owner,transport,slot,partition,ticks);
    CHECK(status==(!strcmp(name,"nonzero_storage")||!strcmp(name,"null_domain")?UMICOM_FAT16_UPDATE_BAD_STATE:UMICOM_FAT16_UPDATE_INVALID_ARGUMENT));
    CHECK(!commitEventCount && !Allocated() && !model.notifications);
    if(!strcmp(name,"slot")||!strcmp(name,"partition")||!strcmp(name,"timeout_zero")||!strcmp(name,"timeout_excess")) {
        CHECK(renameOwner.self==&renameOwner && !renameOwner.commit.updater.admitted);
        RenameOpen();RenameClose();CommitUnchanged();
    } else CommitEqual(&before,&renameOwner,sizeof(before));
    if(frame) {domain.count=1U;domain.slots[1].dataFrame=0U;}
}
static void RenameLifecycle(const char *name)
{
    if(!strcmp(name,"zero_close")) {CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_OK);CHECK(!model.notifications);return;}
    if(!strcmp(name,"readonly_retry")) {
        model.featuresLow|=UMICOM_VIRTIO_READ_ONLY;CHECK(UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_READ_ONLY);
        CHECK(!Allocated());model.featuresLow&=~UMICOM_VIRTIO_READ_ONLY;
    } else if(!strcmp(name,"no_flush_retry")) {
        model.featuresLow&=~UMICOM_VIRTIO_FEATURE_FLUSH;CHECK(UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated());model.featuresLow|=UMICOM_VIRTIO_FEATURE_FLUSH;
    } else if(!strcmp(name,"failed_open_retained")) {
        model.refuseDriver=UMICOM_TRUE;model.stickAfterDriver=UMICOM_TRUE;
        CHECK(UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated()==2U && !model.freeCalls);CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        model.stuckReset=UMICOM_FALSE;model.refuseDriver=UMICOM_FALSE;model.stickAfterDriver=UMICOM_FALSE;RenameClose();return;
    }
    RenameOpen();
    if(!strcmp(name,"ready_close")) {RenameClose();CommitUnchanged();return;}
    if(!strcmp(name,"finish_before_stage")) {CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&renameResult,&renameResultBefore,sizeof(renameResult));CommitUnchanged();RenameClose();return;}
    if(!strcmp(name,"timeout_retained")) {
        commitHangMutation=5U;model.stuckReset=UMICOM_TRUE;CHECK(RenameStage()==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        RenameNoData(&renameResult);CHECK(renameResult.commit.writeUncertain && renameResult.commit.uncertainSectorValid && Allocated()==2U && !model.freeCalls);
        CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);CHECK(Allocated()==2U && !model.freeCalls);
        model.stuckReset=UMICOM_FALSE;model.noCompletion=UMICOM_FALSE;RenameClose();return;
    }
    CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);RenameAssertStage();const UmicomU32 mutations=commitMutations;
    if(!strcmp(name,"stage_twice")) {
        const UmicomKernelFat16RenameResult history=renameOwner.lastResult;const UmicomU32 events=commitEventCount;
        CHECK(RenameStage()==UMICOM_FAT16_UPDATE_BAD_STATE);CHECK(commitEventCount==events);
        CommitEqual(&renameResult,&renameResultBefore,sizeof(renameResult));CommitEqual(&history,&renameOwner.lastResult,sizeof(history));
    } else if(!strcmp(name,"reset_retry")||!strcmp(name,"first_release_retry")||!strcmp(name,"second_release_retry")) {
        const UmicomKernelBlockHandle handle=renameOwner.commit.updater.handle;
        if(!strcmp(name,"reset_retry")) model.stuckReset=UMICOM_TRUE;else model.failFree=!strcmp(name,"first_release_retry")?1U:2U;
        CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(renameOwner.commit.updater.handle==handle && renameOwner.commit.state==UMICOM_FAT16_COMMIT_CLOSING);
        CHECK(Allocated()==(!strcmp(name,"second_release_retry")?1U:2U));model.stuckReset=UMICOM_FALSE;model.failFree=0U;
    } else if(!strcmp(name,"unsafe_close_retry")) {
        model.allowed=UMICOM_FALSE;CHECK(UmicomKernelFat16RenameClose(&renameOwner)==UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(Allocated()==2U);model.allowed=UMICOM_TRUE;
    } else if(!strcmp(name,"finish_twice")||!strcmp(name,"single_lifetime")) {
        CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_OK);RenameAssertFinished();
        if(!strcmp(name,"finish_twice")) {CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&renameResult,&renameResultBefore,sizeof(renameResult));}
    }
    RenameClose();
    if(!strcmp(name,"single_lifetime")) CHECK(UmicomKernelFat16RenameOpen(&renameOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_BAD_STATE);
    if(strcmp(name,"finish_twice") && strcmp(name,"single_lifetime")) {
        CHECK(commitMutations==mutations);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
        CHECK(!(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK));
    }
}
static void RenameMutationClocks(const char *fixture,const char *name)
{
    for(UmicomU32 at=1U;at<=10U;++at) {
        RenameStart(fixture);commitClockInside=!strncmp(name,"inner_",6U)?UMICOM_TRUE:UMICOM_FALSE;
        commitClockRollback=strstr(name,"rollback")?UMICOM_TRUE:UMICOM_FALSE;commitClockMutation=at;RenameOpen();
        UmicomKernelFat16UpdateStatus status=RenameStage();if(status==UMICOM_FAT16_UPDATE_OK) status=RenameFinish();
        CHECK(status==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR && commitClockInjected && commitMutations==at);
        CHECK(renameResult.commit.blockStatus==(commitClockRollback?UMICOM_BLOCK_CLOCK_ERROR:UMICOM_BLOCK_TIMEOUT));
        CHECK(renameResult.commit.lastBlockOutcome==(commitClockInside?UMICOM_BLOCK_SUBMITTED_UNCONFIRMED:UMICOM_BLOCK_COMPLETED));
        const UmicomBoolean write=at%2U?UMICOM_TRUE:UMICOM_FALSE;
        CHECK(renameResult.commit.uncertainSectorValid==(commitClockInside && write?UMICOM_TRUE:UMICOM_FALSE));
        CHECK(renameResult.commit.writeUncertain==(commitClockInside && write?UMICOM_TRUE:UMICOM_FALSE));
        CHECK(renameResult.commit.completedMetadataSectors==commitWriteCompletions-(commitClockInside && write?1U:0U));
        CHECK(renameResult.commit.completedFlushes==commitFlushCompletions-(commitClockInside && !write?1U:0U));
        if(!commitClockInside && at==4U) CHECK(renameResult.commit.dirtyDurable);
        if(!commitClockInside && at==6U) CHECK(renameResult.directoryDurable);
        if(!commitClockInside && at==10U) CHECK(renameResult.commit.cleanDurable && !renameResult.commit.cleanVerified);
        RenameFailed();RenameClose();
    }
    printf("rename mutation-clock positions=10 kind=%s\n",name);
}
static void RenameAcceptanceClock(const char *fixture,const char *name)
{
    const UmicomBoolean finish=!strncmp(name,"finish_",7U)?UMICOM_TRUE:UMICOM_FALSE;
    RenameOpen();CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);const UmicomU32 stageRead=commitReads;
    CHECK(RenameFinish()==UMICOM_FAT16_UPDATE_OK);const UmicomU32 finishRead=commitReads;
    RenameClose();RenameStart(fixture);RenameOpen();commitClockRead=finish?finishRead:stageRead;commitClockSkip=1U;
    commitClockRollback=strstr(name,"rollback")?UMICOM_TRUE:UMICOM_FALSE;
    if(finish) {CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);RenameAssertStage();}
    CHECK((finish?RenameFinish():RenameStage())==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
    CHECK(commitClockInjected && !commitClockSkip && !renameResult.commit.commitAccepted);
    CHECK(renameResult.commit.dirtyDurable && renameResult.commit.dirtyVerified && renameResult.directoryDurable && renameResult.directoryVerified);
    CHECK(renameResult.commit.cleanVerified==finish && renameResult.commit.cleanDurable==finish);
    CHECK(!renameResult.commit.needsFlush && !renameResult.commit.writeUncertain && !renameResult.commit.uncertainSectorValid);
    CHECK(renameResult.commit.blockStatus==(commitClockRollback?UMICOM_BLOCK_CLOCK_ERROR:UMICOM_BLOCK_TIMEOUT));
    RenameFailed();RenameClose();
}
static void RenamePreflight(const char *fixture,const char *name)
{
    RenameOpen();CHECK(RenameStage()==UMICOM_FAT16_UPDATE_OK);UmicomU32 lastRead=0U,directoryRead=0U;
    for(UmicomSize i=0U;i<commitEventCount;++i) {
        if(commitEvents[i].command!=UMICOM_VIRTIO_REQUEST_READ) break;
        ++lastRead;if(commitEvents[i].sector==fileDirectorySector) directoryRead=lastRead;
    }
    CHECK(lastRead>=directoryRead && directoryRead>4U);RenameClose();RenameStart(fixture);RenameOpen();
    const UmicomBoolean clock=strstr(name,"clock_")?UMICOM_TRUE:UMICOM_FALSE;
    if(clock) {commitClockRead=lastRead;commitClockRollback=strstr(name,"rollback")?UMICOM_TRUE:UMICOM_FALSE;}
    else {commitCorruptRead=directoryRead;fileReadCorruptionByte=fileEntryOffset+26U;}
    CHECK(RenameStage()!=UMICOM_FAT16_UPDATE_OK);
    CHECK(renameOwner.commit.state==UMICOM_FAT16_COMMIT_READY && !renameResult.commit.mediaTouched && !renameResult.directorySubmitted);
    if(clock) {CHECK(commitClockInjected && renameResult.commit.diskStatus==UMICOM_DISK_IO_ERROR);CHECK(renameResult.commit.blockStatus==(commitClockRollback?UMICOM_BLOCK_CLOCK_ERROR:UMICOM_BLOCK_TIMEOUT));}
    else CHECK(renameResult.commit.diskStatus==UMICOM_DISK_CORRUPT);
    RenameNoData(&renameResult);CommitUnchanged();RenameClose();
}
static UmicomBoolean renameInitialGetReenter;
UmicomKernelBlockStatus __real_UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **out);
UmicomKernelBlockStatus __wrap_UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **out)
{
    if(renameInitialGetReenter) RenameConsoleReenter();
    return __real_UmicomPlatformBlockDomainGet(out);
}
static void RenameConsoleReenter(void)
{
    const UmicomSize before=transcriptBytes;
    CHECK(FileConsoleCommand(&commitShell,"fatrenamestage /FRAG.BIN SAVED.BIN")==UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell,"fatrenamecommit")==UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell,"fatrenameinfo")==UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell,"fatfileinfo")==UMICOM_SHELL_BUSY);
    CHECK(FileConsoleCommand(&commitShell,"fatfileappend /FRAG.BIN x")==UMICOM_SHELL_BUSY);
    CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(transcriptBytes==before);++renameConsoleReentries;
}
static void RenameConsoleOutput(void *context,const char *text,UmicomSize bytes)
{
    Output(context,text,bytes);if(renameConsoleOutput) RenameConsoleReenter();
}
static void RenameConsoleOpen(void)
{
    CHECK(FileConsoleCommand(&commitShell,"fatrenameopen 0 0")==UMICOM_SHELL_OK);
    CHECK(strstr(transcript,"fat.rename.open=ok") && strstr(transcript,"fat.rename-state=ready") && Allocated()==2U);
    CHECK(!commitWrites && !commitFlushes);CommitClearTranscript();
}
static void RenameConsoleClose(void)
{
    const UmicomU32 mutations=commitMutations;
    CHECK(FileConsoleCommand(&commitShell,"fatrenameclose")==UMICOM_SHELL_OK);CHECK(!Allocated() && mutations==commitMutations);
}
static void RenameConsole(const char *name)
{
    memset(&commitShell,0,sizeof(commitShell));memset(&commitForeignShell,0,sizeof(commitForeignShell));
    commitShell.output=RenameConsoleOutput;commitForeignShell.output=RenameConsoleOutput;
    if(!strcmp(name,"before_open")) {
        CHECK(FileConsoleCommand(&commitShell,"fatrenamestage /FRAG.BIN SAVED.BIN")==UMICOM_SHELL_IO_ERROR);
        CHECK(!commitEventCount && !Allocated());return;
    }
    if(!strcmp(name,"invalid_arguments")) {
        const char *const bad[]={"fatrenameopen","fatrenameopen 0","fatrenameopen -1 0","fatrenameopen 0 4",
            "fatrenamestage","fatrenamestage /FRAG.BIN","fatrenamestage /FRAG.BIN SAVED.BIN extra","fatrenamecommit x","fatrenameinfo x","fatrenameclose x"};
        for(UmicomSize i=0U;i<sizeof(bad)/sizeof(bad[0]);++i) CHECK(FileConsoleCommand(&commitShell,bad[i])==UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!commitEventCount && !Allocated());return;
    }
    if(!strcmp(name,"foreign_rename_after_failed_file")||!strcmp(name,"foreign_file_after_failed_rename")) {
        const UmicomBoolean fileFirst=!strcmp(name,"foreign_rename_after_failed_file")?UMICOM_TRUE:UMICOM_FALSE;
        model.featuresLow|=UMICOM_VIRTIO_READ_ONLY;
        CHECK(FileConsoleCommand(&commitShell,fileFirst?"fatfileopen 0 0":"fatrenameopen 0 0")==UMICOM_SHELL_IO_ERROR);
        CHECK(!Allocated());model.featuresLow&=~UMICOM_VIRTIO_READ_ONLY;
        const UmicomU32 events=commitEventCount;const UmicomSize output=transcriptBytes;
        CHECK(FileConsoleCommand(&commitForeignShell,fileFirst?"fatrenameopen 0 0":"fatfileopen 0 0")==UMICOM_SHELL_BAD_STATE);
        CHECK(events==commitEventCount && output==transcriptBytes);
        CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);CHECK(!Allocated());return;
    }
    if(!strcmp(name,"initial_get_file")) {
        renameInitialGetReenter=UMICOM_TRUE;FileConsoleOpen();renameInitialGetReenter=UMICOM_FALSE;
        CHECK(renameConsoleReentries>0U);CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);CHECK(!Allocated());return;
    }
    if(!strcmp(name,"foreign_rename_after_file")) {
        FileConsoleOpen();const UmicomU32 events=commitEventCount;
        CHECK(FileConsoleCommand(&commitForeignShell,"fatrenameopen 0 0")==UMICOM_SHELL_BAD_STATE);
        CHECK(commitEventCount==events);CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);CHECK(!Allocated());return;
    }
    if(!strcmp(name,"initial_get_rename")) renameInitialGetReenter=UMICOM_TRUE;
    RenameConsoleOpen();renameInitialGetReenter=UMICOM_FALSE;
    if(!strcmp(name,"initial_get_rename")) {CHECK(renameConsoleReentries>0U);RenameConsoleClose();return;}
    if(!strcmp(name,"foreign_file_after_rename")||!strcmp(name,"foreign_append_after_rename")||!strcmp(name,"wrong_shell")) {
        const UmicomU32 events=commitEventCount;
        CHECK(FileConsoleCommand(&commitForeignShell,!strcmp(name,"wrong_shell")?"fatrenamestage /FRAG.BIN SAVED.BIN":(!strcmp(name,"foreign_append_after_rename")?"fatfileappend /FRAG.BIN x":"fatfileopen 0 0"))==UMICOM_SHELL_BAD_STATE);
        CHECK(commitEventCount==events);CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);CHECK(!Allocated());return;
    }
    if(!strcmp(name,"collision_retry")||!strcmp(name,"invalid_name_retry")) {
        CHECK(FileConsoleCommand(&commitShell,!strcmp(name,"collision_retry")?"fatrenamestage /FRAG.BIN frag.bin":"fatrenamestage /FRAG.BIN A?.X")==UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript,!strcmp(name,"collision_retry")?"disk=exists":"disk=invalid-argument"));CommitUnchanged();CommitClearTranscript();
    }
    renameConsoleQueue=!strcmp(name,"queue_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    renameConsoleOutput=!strcmp(name,"output_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    if(!strcmp(name,"directory_failure")) commitFailWrite=3U;
    const UmicomKernelShellStatus staged=FileConsoleCommand(&commitShell,"fatrenamestage /FRAG.BIN sAvEd.BiN");
    CHECK(strstr(transcript,"requested=0 confirmed=0 submitted=0") && strstr(transcript,"data-durable=0 data-verified=0"));
    CHECK(strstr(transcript,"fat.rename.original-name=FRAG.BIN") && strstr(transcript,"updated-name=SAVED.BIN"));
    CHECK(strstr(transcript,"commit-accepted=0"));
    if(!strcmp(name,"directory_failure")) {
        CHECK(staged==UMICOM_SHELL_IO_ERROR && commitWrites==3U && commitFlushes==2U);RenameConsoleClose();return;
    }
    CHECK(staged==UMICOM_SHELL_OK && commitWrites==3U && commitFlushes==3U && strstr(transcript,"fat.rename-state=staged"));
    RenamePatchExpected();CommitExpectedDirty(UMICOM_TRUE);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    if(!strcmp(name,"staged_close")||!strcmp(name,"shell_shutdown")) {
        if(!strcmp(name,"shell_shutdown")) CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);else RenameConsoleClose();
        CHECK(!Allocated() && commitWrites==3U && commitFlushes==3U);return;
    }
    if(!strcmp(name,"stage_twice")) {
        const UmicomU32 events=commitEventCount;CommitClearTranscript();
        CHECK(FileConsoleCommand(&commitShell,"fatrenamestage /SAVED.BIN AGAIN.BIN")==UMICOM_SHELL_IO_ERROR);
        CHECK(commitEventCount==events && strstr(transcript,"fat.rename.previous-result"));
    }
    CommitClearTranscript();if(!strcmp(name,"late_clean_failure")) commitFailFlush=5U;
    const UmicomKernelShellStatus finished=FileConsoleCommand(&commitShell,"fatrenamecommit");
    if(!strcmp(name,"late_clean_failure")) {CHECK(finished==UMICOM_SHELL_IO_ERROR && strstr(transcript,"commit-accepted=0"));RenameConsoleClose();return;}
    CHECK(finished==UMICOM_SHELL_OK && strstr(transcript,"commit-accepted=1") && commitWrites==5U && commitFlushes==5U);
    CommitExpectedDirty(UMICOM_FALSE);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
    RenameConsoleClose();if(renameConsoleQueue||renameConsoleOutput) CHECK(renameConsoleReentries>0U);
}
int __wrap_main(int argc,char **argv)
{
    CHECK(argc==3);RenameStart(argv[2]);const char *const name=argv[1];
    if(!strncmp(name,"success.",8U)) RenameSuccess(name+8U);
    else if(!strncmp(name,"refusal.",8U)) RenameRefusal(name+8U);
    else if(!strncmp(name,"format.",7U)) RenameFormat(name+7U);
    else if(!strncmp(name,"ownership.",10U)) RenameOwnership(name+10U,UMICOM_FALSE);
    else if(!strncmp(name,"finish_ownership.",17U)) RenameOwnership(name+17U,UMICOM_TRUE);
    else if(!strncmp(name,"open_arguments.",15U)) RenameOpenArguments(name+15U);
    else if(!strncmp(name,"failures.",9U)) RenameAllFailures(argv[2],name+9U);
    else if(!strncmp(name,"cut.",4U)) RenameCut(name+4U);
    else if(!strncmp(name,"tamper.",7U)) RenameTamper(name+7U);
    else if(!strncmp(name,"clock.",6U)) RenameMutationClocks(argv[2],name+6U);
    else if(!strncmp(name,"acceptance_clock.",17U)) RenameAcceptanceClock(argv[2],name+17U);
    else if(!strncmp(name,"preflight.",10U)) RenamePreflight(argv[2],name+10U);
    else if(!strncmp(name,"lifetime.",9U)) RenameLifecycle(name+9U);
    else if(!strncmp(name,"console.",8U)) RenameConsole(name+8U);
    else CHECK(0);
    CHECK(!Allocated());printf("fat16-rename.%s: ok\n",name);return 0;
}
