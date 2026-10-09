/* Real VirtIO/allocator lifecycle qualification with independent complete-image
 * allocation oracle and cached/eager device persistence models.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_lifecycle_commit.h"
#include "umicom/kernel/fat16_metadata.h"
#define __wrap_main UmicomLifecycleOriginalFileCommitEntry
#include "../fat16_file_commit/file_commit_tests.c"
#undef __wrap_main
static UmicomKernelFat16LifecycleCommitter lcOwner;
static UmicomKernelFat16LifecycleResult lcResult,lcResultBefore;
static UmicomKernelFat16LifecycleRequest lcRequest;
static UmicomU8 lcInput[4097],lcExpected[sizeof(commitVisible)];
static char lcPath[UMICOM_FAT16_PATH_BYTES];
static UmicomU64 lcDirectory,lcNextDirectory;
static UmicomSize lcEntry,lcNextEntry,lcExpectedAllocated,lcExpectedFreed;
static UmicomBoolean lcReentry,lcPolicyReentry,lcSnapshot,lcStrict;
static UmicomBoolean lcConsoleQueue,lcConsoleOutput,lcInitialGet;
static UmicomU32 lcConsoleReentries;
static UmicomU32 lcReentries,lcBaseReads,lcBaseWrites,lcBaseFlushes,lcBaseEvents;
static void LCConsoleReenter(void);
UmicomKernelBlockStatus __real_UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **outDomain);
UmicomKernelBlockStatus __wrap_UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **outDomain)
{ if(lcInitialGet) LCConsoleReenter();return __real_UmicomPlatformBlockDomainGet(outDomain); }
static UmicomU8 *LCSector(UmicomU64 sector) {CHECK(sector<UMICOM_DISK_FIXTURE_SECTORS);return commitVisible+(UmicomSize)sector*512U;}
static UmicomU8 *LCCluster(UmicomU16 cluster) {return CommitCluster(cluster);}
static UmicomU16 LCWord(const UmicomU8 *p)
{ return (UmicomU16)((UmicomU16)p[0] | (UmicomU16)((UmicomU16)p[1]<<8U)); }
static UmicomU32 LCLong(const UmicomU8 *p)
{ return (UmicomU32)p[0]|((UmicomU32)p[1]<<8U)|((UmicomU32)p[2]<<16U)|((UmicomU32)p[3]<<24U); }
static void LCLongPut(UmicomU8 *p,UmicomU32 value)
{ for(UmicomSize i=0U;i<4U;++i) p[i]=(UmicomU8)(value>>(i*8U)); }
static UmicomU16 LCFat(const UmicomU8 *image,UmicomU16 cluster)
{ return LCWord(image+(UMICOM_DISK_FIXTURE_FIRST+1U)*512U+(UmicomSize)cluster*2U); }
static void LCFatPut(UmicomU8 *image,UmicomU16 cluster,UmicomU16 next)
{
    for(UmicomSize copy=0U;copy<2U;++copy)
        CommitPut16(image+(UMICOM_DISK_FIXTURE_FIRST+1U+copy*UMICOM_DISK_FIXTURE_FAT_SECTORS)*512U+(UmicomSize)cluster*2U,next);
}
static void LCEntryMake(UmicomU8 *p,const char *alias,UmicomU16 first,UmicomU32 size)
{ memset(p,0,32U);memcpy(p,alias,11U);p[11U]=0x20U;CommitPut16(p+26U,first);LCLongPut(p+28U,size); }

static void LCOracle(void)
{
    memcpy(lcExpected,commitVisible,sizeof(commitVisible));lcExpectedAllocated=0U;lcExpectedFreed=0U;
    const UmicomSize sectorsPerCluster=commitVisible[(UmicomSize)UMICOM_DISK_FIXTURE_FIRST*512U+13U];
    const UmicomSize clusterBytes=sectorsPerCluster*512U;
    UmicomU8 *const record=lcExpected+(UmicomSize)lcDirectory*512U+lcEntry;
    const UmicomBoolean create=lcRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomU32 oldSize=create?0U:LCLong(record+28U);
    UmicomU16 chain[256];UmicomSize count=0U;
    if(!create) for(UmicomU16 cluster=LCWord(record+26U);cluster>=2U && cluster<0xfff8U;cluster=LCFat(commitVisible,cluster)) {
        CHECK(count<256U);chain[count++]=cluster;
    }
    UmicomU32 newSize=0U;
    if(create) newSize=(UmicomU32)lcRequest.bytes;
    else if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_APPEND) newSize=oldSize+(UmicomU32)lcRequest.bytes;
    else if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_WRITE) {
        const UmicomU64 end=lcRequest.offset+lcRequest.bytes;
        newSize=end>oldSize?(UmicomU32)end:oldSize;
    }
    else if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) newSize=lcRequest.size;
    const UmicomSize required=((UmicomSize)newSize+clusterBytes-1U)/clusterBytes;
    const UmicomSize originalCount=count;
    for(UmicomU16 candidate=2U;count<required;++candidate) {
        CHECK(candidate<12000U);if(LCFat(commitVisible,candidate)) continue;
        chain[count++]=candidate;++lcExpectedAllocated;
        CHECK(((UmicomSize)UMICOM_DISK_FIXTURE_DATA+(candidate-2U)*sectorsPerCluster)*512U+clusterBytes<=sizeof(lcExpected));
        memset(lcExpected+((UmicomSize)UMICOM_DISK_FIXTURE_DATA+(candidate-2U)*sectorsPerCluster)*512U,0,clusterBytes);
    }
    for(UmicomSize i=0U;i<required;++i) LCFatPut(lcExpected,chain[i],i+1U<required?chain[i+1U]:0xffffU);
    for(UmicomSize i=required;i<originalCount;++i) {LCFatPut(lcExpected,chain[i],0U);++lcExpectedFreed;}
    if(lcRequest.input) {
        /* Preserve the former EOF-only overlay beside its positional form. */
#if 0
        const UmicomSize offset=create?0U:oldSize;
#endif
        const UmicomSize offset=create?0U:(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_WRITE?
            (UmicomSize)lcRequest.offset:oldSize);
        for(UmicomSize i=0U;i<lcRequest.bytes;++i)
            lcExpected[((UmicomSize)UMICOM_DISK_FIXTURE_DATA+(chain[(offset+i)/clusterBytes]-2U)*sectorsPerCluster)*512U+(offset+i)%clusterBytes]=lcInput[i];
    }
    if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_DELETE) {record[0]=0xe5U;return;}
    if(create) {
        const UmicomBoolean ended=record[0]==0U?UMICOM_TRUE:UMICOM_FALSE;
        memset(record,0,32U);memset(record,' ',11U);const char *alias=strrchr(lcPath,'/');CHECK(alias);++alias;
        UmicomSize field=0U,at=0U;
        for(UmicomSize i=0U;alias[i];++i) {
            if(alias[i]=='.') {field=8U;at=0U;continue;}
            const unsigned char byte=(unsigned char)alias[i];record[field+at++]=(UmicomU8)(byte>='a'&&byte<='z'?byte-'a'+'A':byte);
        }
        if(ended && lcNextDirectory) lcExpected[(UmicomSize)lcNextDirectory*512U+lcNextEntry]=0U;
        CommitPut16(record+14U,0xbf5cU);CommitPut16(record+16U,0x805dU);CommitPut16(record+18U,0x805dU);
    }
    record[11U]|=0x20U;CommitPut16(record+22U,0xbf5cU);CommitPut16(record+24U,0x805dU);
    CommitPut16(record+26U,required?chain[0]:0U);LCLongPut(record+28U,newSize);
}

static void LCReserveBelow(UmicomU16 stop)
{
    UmicomSize slot=5U,count=0U;UmicomU16 first=0U,previous=0U;
    for(UmicomU16 cluster=2U;cluster<stop;++cluster) {
        if(LCFat(commitVisible,cluster)) continue;
        if(!count) first=cluster;else CommitFat(previous,cluster);
        previous=cluster;CommitFat(cluster,0xffffU);++count;
        if(count==256U) {
            char name[12];CHECK(snprintf(name,sizeof(name),"F%07uBIN",(unsigned)slot)==11);
            LCEntryMake(LCSector(UMICOM_DISK_FIXTURE_ROOT)+slot++*32U,name,first,(UmicomU32)(count*512U));count=0U;
        }
    }
    if(count) {
        char name[12];CHECK(snprintf(name,sizeof(name),"F%07uBIN",(unsigned)slot)==11);
        LCEntryMake(LCSector(UMICOM_DISK_FIXTURE_ROOT)+slot++*32U,name,first,(UmicomU32)(count*512U));
    }
    if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE) {
        lcDirectory=UMICOM_DISK_FIXTURE_ROOT+slot/16U;lcEntry=(slot%16U)*32U;
        lcNextDirectory=UMICOM_DISK_FIXTURE_ROOT+(slot+1U)/16U;lcNextEntry=((slot+1U)%16U)*32U;
    }
}
static void LCFixture(const char *name)
{
    UmicomU8 *const root=LCSector(UMICOM_DISK_FIXTURE_ROOT);
    if(!strcmp(name,"write_overwrite")) {lcRequest.offset=17U;lcRequest.bytes=97U;}
    else if(!strcmp(name,"empty")) {
        if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE) {lcRequest.bytes=0U;lcRequest.input=NULL;}
        else {memcpy(lcPath,"/EMPTY.TXT",11U);lcEntry=128U;if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) lcRequest.size=0U;}
    } else if(!strcmp(name,"one")) lcRequest.bytes=1U;
    else if(!strcmp(name,"maximum")) lcRequest.bytes=4096U;
    else if(!strcmp(name,"sector")) lcRequest.bytes=512U;
    else if(!strcmp(name,"partial")) lcRequest.bytes=197U;
    else if(!strcmp(name,"zero")) lcRequest.size=0U;
    else if(!strcmp(name,"equal")) lcRequest.size=1300U;
    else if(!strcmp(name,"cluster_boundary")) lcRequest.size=512U;
    else if(!strcmp(name,"mixed_case")) memcpy(lcPath,"/lIfE.bIn",10U);
    else if(!strcmp(name,"preserved_fields")) {root[109U]=197U;CommitPut16(root+110U,0x1122U);CommitPut16(root+112U,0x5021U);CommitPut16(root+114U,0x5123U);root[107U]=6U;root[108U]=0x18U;}
    else if(!strcmp(name,"deleted_slot")) {memset(root+160U,0x91,32U);root[160U]=0xe5U;root[192U]=0U;}
    else if(!strcmp(name,"nested")) {
        if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE) {memcpy(lcPath,"/DOCS/LIFE.BIN",15U);lcEntry=96U;}
        else {memcpy(LCCluster(3U)+96U,root+96U,32U);root[96U]=0xe5U;memcpy(lcPath,"/DOCS/FRAG.BIN",15U);lcEntry=96U;}
        lcDirectory=UMICOM_DISK_FIXTURE_DATA+1U;lcNextDirectory=lcDirectory;lcNextEntry=lcEntry+32U;
    } else if(!strcmp(name,"end_boundary")||!strcmp(name,"fragmented_boundary")) {
        const UmicomBoolean nested=!strcmp(name,"fragmented_boundary")?UMICOM_TRUE:UMICOM_FALSE;
        UmicomU8 *first=root;UmicomSize begin=5U;
        if(nested) {
            CommitFat(3U,30U);CommitFat(30U,0xffffU);first=LCCluster(3U);begin=3U;
            memcpy(lcPath,"/DOCS/LIFE.BIN",15U);lcDirectory=UMICOM_DISK_FIXTURE_DATA+1U;lcNextDirectory=UMICOM_DISK_FIXTURE_DATA+28U;
        } else lcNextDirectory=UMICOM_DISK_FIXTURE_ROOT+1U;
        for(UmicomSize i=begin;i<15U;++i) {char alias[12];CHECK(snprintf(alias,sizeof(alias),"E%07uTXT",(unsigned)i)==11);LCEntryMake(first+i*32U,alias,0U,0U);}
        first[480U]=0U;memset(LCSector(lcNextDirectory),0x79,512U);lcEntry=480U;lcNextEntry=0U;
    } else if(!strcmp(name,"fat_crossing")) LCReserveBelow(255U);
    else if(!strcmp(name,"fat_later_crossing")) LCReserveBelow(511U);
    else if(!strcmp(name,"spread_chain")||!strcmp(name,"fat_limit")) {
        CommitFat(9U,0U);CommitFat(6U,0U);const UmicomSize count=!strcmp(name,"fat_limit")?32U:3U;
        CommitFat(4U,256U);
        for(UmicomSize i=1U;i<count;++i) CommitFat((UmicomU16)(i*256U),i+1U<count?(UmicomU16)((i+1U)*256U):0xffffU);
        LCLongPut(root+124U,(UmicomU32)(count*512U));
        if(lcRequest.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) lcRequest.size=513U;
    } else CHECK(!strcmp(name,"ordinary")||!strcmp(name,"snapshot")||!strcmp(name,"reentry"));
}
static UmicomKernelFat16UpdateStatus LCStage(void)
{
    commitOperation=1U;memset(&lcResult,0xa5,sizeof(lcResult));memcpy(&lcResultBefore,&lcResult,sizeof(lcResult));
    return UmicomKernelFat16LifecycleStage(&lcOwner,&lcRequest,&lcResult);
}
static UmicomKernelFat16UpdateStatus LCFinish(void)
{
    commitOperation=2U;memset(&lcResult,0xa5,sizeof(lcResult));memcpy(&lcResultBefore,&lcResult,sizeof(lcResult));
    return UmicomKernelFat16LifecycleFinish(&lcOwner,&lcResult);
}
static void LCReenter(void)
{
    UmicomKernelFat16LifecycleResult out,before;memset(&out,0x65,sizeof(out));memcpy(&before,&out,sizeof(out));
    CHECK(UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16LifecycleStage(&lcOwner,&lcRequest,&out)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16LifecycleFinish(&lcOwner,&out)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_BUSY);
    CommitEqual(&out,&before,sizeof(out));++lcReentries;
}
static UmicomBoolean LCAllowed(void *context)
{ CHECK(context==&model);if(lcPolicyReentry) LCReenter();return model.allowed; }
static UmicomU64 LCClock(void *context)
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
        now=commitClockRollback?(commitClockInside?commitPreviousClock-1U:lcOwner.commit.updater.operationClock-1U):
            now+(commitClockInside?domain.slots[0].timeoutTicks:UMICOM_FAT16_UPDATE_OPERATION_TICKS);
        model.now=now;
    }
    commitPreviousClock=now;return now;
}
static UmicomBoolean LCFatSector(UmicomU64 sector)
{ return sector>=COMMIT_PRIMARY && sector<COMMIT_MIRROR+UMICOM_DISK_FIXTURE_FAT_SECTORS?UMICOM_TRUE:UMICOM_FALSE; }
static void LCExpectedFatDirty(UmicomU8 *sector,UmicomU64 lba)
{
    memcpy(sector,lcExpected+(UmicomSize)lba*512U,512U);
    if(CommitHeaderSector(lba)) sector[3U]&=0x7fU;
}
static void LCAssertFatDurable(void)
{
    UmicomU8 expected[512];
    for(UmicomU64 sector=COMMIT_PRIMARY;sector<COMMIT_MIRROR+UMICOM_DISK_FIXTURE_FAT_SECTORS;++sector) {
        LCExpectedFatDirty(expected,sector);CommitEqual(commitDurable+(UmicomSize)sector*512U,expected,512U);
    }
}
static void LCAssertDataDurable(void)
{
    for(UmicomU64 sector=UMICOM_DISK_FIXTURE_DATA;sector<UMICOM_DISK_FIXTURE_FIRST+UMICOM_DISK_FIXTURE_LENGTH;++sector) {
        if(sector==lcDirectory||sector==lcNextDirectory) continue;
        if(memcmp(commitInitial+(UmicomSize)sector*512U,lcExpected+(UmicomSize)sector*512U,512U))
            CommitEqual(commitDurable+(UmicomSize)sector*512U,lcExpected+(UmicomSize)sector*512U,512U);
    }
}
static void LCWriteRegister(void *context,UmicomAddress address,UmicomU32 value)
{
    if(Offset(address)==UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header=(UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU32 command=*(const UmicomU32 *)header;const UmicomU64 sector=*(const UmicomU64 *)(header+8U);
        if(command==UMICOM_VIRTIO_REQUEST_WRITE && lcStrict) {
            const UmicomU32 writes=commitWrites-lcBaseWrites;
            const UmicomBoolean directory=sector==lcDirectory||sector==lcNextDirectory?UMICOM_TRUE:UMICOM_FALSE;
            const UmicomSize sectorsPerCluster=commitInitial[(UmicomSize)UMICOM_DISK_FIXTURE_FIRST*512U+13U];
            const UmicomU16 cluster=(UmicomU16)((sector>=UMICOM_DISK_FIXTURE_DATA?(sector-UMICOM_DISK_FIXTURE_DATA):0U)/sectorsPerCluster+2U);
            const UmicomBoolean allocatedData=sector>=UMICOM_DISK_FIXTURE_DATA && sector<UMICOM_DISK_FIXTURE_SECTORS &&
                !LCFat(commitInitial,cluster) && LCFat(lcExpected,cluster)?UMICOM_TRUE:UMICOM_FALSE;
            CHECK(LCFatSector(sector)||directory||allocatedData||
                memcmp(commitInitial+(UmicomSize)sector*512U,lcExpected+(UmicomSize)sector*512U,512U));
            if(writes<2U) {
                CHECK(sector==(writes?COMMIT_PRIMARY:COMMIT_MIRROR));
                CHECK(commitFlushCompletions-lcBaseFlushes==writes);
            } else {
                CHECK(!(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK)||
                    (CommitFlags(commitDurable,COMMIT_MIRROR)&UMICOM_FAT16_CLEAN_MASK));
                /* WRITE follows growth ordering even when every link stays unchanged. */
#if 0
                const UmicomBoolean allocation=lcRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE||lcRequest.operation==UMICOM_FAT16_LIFECYCLE_APPEND?UMICOM_TRUE:UMICOM_FALSE;
#endif
                const UmicomBoolean allocation=lcRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE||
                    lcRequest.operation==UMICOM_FAT16_LIFECYCLE_APPEND||
                    lcRequest.operation==UMICOM_FAT16_LIFECYCLE_WRITE?UMICOM_TRUE:UMICOM_FALSE;
                if(directory && allocation) {LCAssertDataDurable();LCAssertFatDurable();}
                if(sector==lcDirectory && lcNextDirectory!=lcDirectory &&
                    memcmp(commitInitial+(UmicomSize)lcNextDirectory*512U,lcExpected+(UmicomSize)lcNextDirectory*512U,512U))
                    CommitEqual(commitDurable+(UmicomSize)lcNextDirectory*512U,lcExpected+(UmicomSize)lcNextDirectory*512U,512U);
                if(LCFatSector(sector) && allocation) LCAssertDataDurable();
                if(LCFatSector(sector) && !allocation)
                    CommitEqual(commitDurable+(UmicomSize)lcDirectory*512U,lcExpected+(UmicomSize)lcDirectory*512U,512U);
            }
        }
        if(lcReentry) LCReenter();
        if(lcConsoleQueue) LCConsoleReenter();
        if(lcSnapshot && command==UMICOM_VIRTIO_REQUEST_READ && commitReads==lcBaseReads) {
            memset(lcInput,0xea,sizeof(lcInput));memset(lcPath,'q',sizeof(lcPath));memset(&lcRequest,0,sizeof(lcRequest));
        }
    }
    CommitWriteRegister(context,address,value);
}
static void LCDefaults(UmicomKernelFat16LifecycleOperation operation)
{
    lcDirectory=UMICOM_DISK_FIXTURE_ROOT;lcEntry=operation==UMICOM_FAT16_LIFECYCLE_CREATE?160U:96U;
    lcNextDirectory=lcDirectory;lcNextEntry=lcEntry+32U;
    memcpy(lcPath,operation==UMICOM_FAT16_LIFECYCLE_CREATE?"/LIFE.BIN":"/FRAG.BIN",10U);
    for(UmicomSize i=0U;i<sizeof(lcInput);++i) lcInput[i]=(UmicomU8)(i*29U+0x31U);
    /* Named fields keep optional request extensions zero-initialized.
     * The earlier positional initializer is retained for review. */
#if 0
    lcRequest=(UmicomKernelFat16LifecycleRequest){operation,lcPath,NULL,0U,0U,{2044U,2U,29U,23U,58U,57U}};
#endif
    lcRequest = (UmicomKernelFat16LifecycleRequest){
        .operation=operation, .path=lcPath,
        .time={2044U,2U,29U,23U,58U,57U}};

    /* WRITE exercises the existing allocation and transport oracle as well. */
#if 0
    if(operation==UMICOM_FAT16_LIFECYCLE_CREATE||operation==UMICOM_FAT16_LIFECYCLE_APPEND) {
#endif
    if(operation==UMICOM_FAT16_LIFECYCLE_CREATE||operation==UMICOM_FAT16_LIFECYCLE_APPEND||
       operation==UMICOM_FAT16_LIFECYCLE_WRITE) {
        lcRequest.input=lcInput;lcRequest.bytes=operation==UMICOM_FAT16_LIFECYCLE_CREATE?700U:900U;
        if(operation==UMICOM_FAT16_LIFECYCLE_WRITE) lcRequest.offset=1020U;
    } else if(operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) lcRequest.size=513U;
    else memset(&lcRequest.time,0,sizeof(lcRequest.time));
    lcBaseReads=commitReads;lcBaseWrites=commitWrites;lcBaseFlushes=commitFlushes;lcBaseEvents=commitEventCount;
}
static void LCStart(const char *fixture,UmicomKernelFat16LifecycleOperation operation)
{
    FileStart(fixture);memset(&lcOwner,0,sizeof(lcOwner));memset(&lcResult,0xa5,sizeof(lcResult));memcpy(&lcResultBefore,&lcResult,sizeof(lcResult));
    lcReentry=UMICOM_FALSE;lcPolicyReentry=UMICOM_FALSE;lcSnapshot=UMICOM_FALSE;lcStrict=UMICOM_TRUE;lcReentries=0U;
    lcConsoleQueue=UMICOM_FALSE;lcConsoleOutput=UMICOM_FALSE;lcInitialGet=UMICOM_FALSE;lcConsoleReentries=0U;
    domain.operations.write32=LCWriteRegister;domain.operations.clock=LCClock;domain.operations.allowed=LCAllowed;
    /* Stale free-space bytes are intentionally nonzero so every newly
     * allocated sector must actually be initialised, including trailing slack. */
    for(UmicomU16 cluster=2U;cluster<12000U;++cluster)
        if(!LCFat(commitVisible,cluster)) memset(CommitCluster(cluster),0xb6,512U);
    CommitRebase();LCDefaults(operation);
}
static void LCOpen(void)
{
    CHECK(UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_OK);
    CHECK(lcOwner.commit.state==UMICOM_FAT16_COMMIT_READY && lcOwner.commit.updater.admitted && Allocated()==2U);
    CHECK(!commitWrites && !commitFlushes);lcBaseReads=commitReads;
}
static void LCClose(void)
{
    UmicomKernelFat16LifecycleResult before;memcpy(&before,&lcOwner.lastResult,sizeof(before));
    const UmicomU32 mutations=commitMutations;const UmicomU64 accepted=lcOwner.committedOperations;
    CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_OK);
    CHECK(!Allocated() && !lcOwner.commit.updater.handle && !lcOwner.busy && lcOwner.committedOperations==accepted);
    CHECK(UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK && commitMutations==mutations);
    CommitEqual(&before,&lcOwner.lastResult,sizeof(before));FileZero(&lcOwner.plan,sizeof(lcOwner.plan));
    FileZero(lcOwner.path,sizeof(lcOwner.path));FileZero(lcOwner.input,sizeof(lcOwner.input));
    FileZero(lcOwner.finalDirtyHeader,sizeof(lcOwner.finalDirtyHeader));FileZero(&lcOwner.request,sizeof(lcOwner.request));
}
static void LCCompare(UmicomBoolean dirty)
{
    memcpy(commitExpected,lcExpected,sizeof(commitExpected));
    if(dirty) {commitExpected[(UmicomSize)COMMIT_PRIMARY*512U+3U]&=0x7fU;commitExpected[(UmicomSize)COMMIT_MIRROR*512U+3U]&=0x7fU;}
    CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
}
static void LCStageEvidence(void)
{
    CHECK(lcOwner.commit.state==UMICOM_FAT16_COMMIT_STAGED && lcResult.planned);
    CHECK(lcResult.commit.status==UMICOM_FAT16_UPDATE_OK && !lcResult.commit.commitAccepted);
    CHECK(lcResult.commit.dirtyDurable && lcResult.commit.dirtyVerified && lcResult.directoryDurable && lcResult.directoryVerified);
    CHECK(lcResult.fatMirrorVerified && lcResult.fatPrimaryVerified);
    CHECK(lcResult.fatMirrorDurable==(lcResult.changedFatSectors?UMICOM_TRUE:UMICOM_FALSE));
    CHECK(lcResult.fatPrimaryDurable==(lcResult.changedFatSectors?UMICOM_TRUE:UMICOM_FALSE));
    CHECK(lcResult.allocatedClusters==lcExpectedAllocated && lcResult.freedClusters==lcExpectedFreed);
    CHECK(lcResult.completedFatSectors==2U*lcResult.changedFatSectors && lcResult.submittedFatSectors==lcResult.completedFatSectors);
    CHECK(lcResult.completedDirectorySectors==lcResult.plannedDirectorySectors);
    CHECK(lcResult.commit.completedMetadataSectors==2U+lcResult.completedFatSectors+lcResult.completedDirectorySectors);
    CHECK(lcResult.commit.completedDataSectors==lcResult.plannedDataSectors);
    CHECK(lcResult.completedDirectoryFlushes==lcResult.plannedDirectorySectors);
    CHECK(lcResult.commit.completedFlushes==2U+lcResult.plannedDirectorySectors+(lcResult.plannedDataSectors?1U:0U)+(lcResult.changedFatSectors?2U:0U));
    CHECK(commitWrites-lcBaseWrites==lcResult.commit.completedMetadataSectors+lcResult.commit.completedDataSectors);
    CHECK(commitFlushes-lcBaseFlushes==lcResult.commit.completedFlushes);
    CHECK(lcResult.commit.confirmedBytes==lcResult.commit.requestedBytes && lcResult.commit.submittedBytes==lcResult.commit.requestedBytes);
    if(!lcResult.plannedDataSectors) {
        CHECK(!lcResult.commit.dataDurable && !lcResult.commit.dataVerified && !lcResult.commit.requestedBytes);
        CHECK(lcResult.commit.dataOutcome==UMICOM_FAT16_UPDATE_NOT_SUBMITTED);
    } else CHECK(lcResult.commit.dataDurable && lcResult.commit.dataVerified);
    if((lcResult.operation==UMICOM_FAT16_LIFECYCLE_APPEND && lcResult.originalEntry.bytes && lcResult.allocatedClusters)||
        (lcResult.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE && lcResult.updatedEntry.bytes && lcResult.freedClusters)) {
        const UmicomSize clusterBytes=(UmicomSize)commitInitial[(UmicomSize)UMICOM_DISK_FIXTURE_FIRST*512U+13U]*512U;
        const UmicomSize retain=lcResult.operation==UMICOM_FAT16_LIFECYCLE_APPEND?
            ((UmicomSize)lcResult.originalEntry.bytes+clusterBytes-1U)/clusterBytes:
            ((UmicomSize)lcResult.updatedEntry.bytes+clusterBytes-1U)/clusterBytes;
        UmicomU16 boundary=lcResult.originalEntry.firstCluster;
        for(UmicomSize i=1U;i<retain;++i) boundary=LCFat(commitInitial,boundary);
        for(UmicomSize copy=0U;copy<2U;++copy) {
            const UmicomU64 first=copy?COMMIT_MIRROR:COMMIT_PRIMARY,target=first+boundary/256U;
            UmicomSize position=0U,boundaryPosition=~(UmicomSize)0U,seenWrites=0U;
            for(UmicomSize i=lcBaseEvents;i<commitEventCount;++i) if(commitEvents[i].command==UMICOM_VIRTIO_REQUEST_WRITE) {
                ++seenWrites;if(seenWrites<=2U) continue;
                const UmicomU64 sector=commitEvents[i].sector;
                if(sector>=first && sector<first+UMICOM_DISK_FIXTURE_FAT_SECTORS) {
                    if(sector==target) boundaryPosition=position;
                    ++position;
                }
            }
            CHECK(position && boundaryPosition!=(~(UmicomSize)0U));
            CHECK(boundaryPosition==(lcResult.operation==UMICOM_FAT16_LIFECYCLE_APPEND?position-1U:0U));
        }
    }
    LCCompare(UMICOM_TRUE);CommitEqual(&lcResult,&lcOwner.lastResult,sizeof(lcResult));
}
static void LCFinishEvidence(UmicomU64 accepted)
{
    CHECK(lcOwner.commit.state==UMICOM_FAT16_COMMIT_COMMITTED && lcOwner.committedOperations==accepted);
    CHECK(lcResult.committedOperations==accepted && lcResult.commit.commitAccepted && lcResult.commit.cleanDurable && lcResult.commit.cleanVerified);
    CHECK(lcResult.commit.completedMetadataSectors==4U+lcResult.completedFatSectors+lcResult.completedDirectorySectors);
    CHECK(lcResult.commit.completedFlushes==4U+lcResult.plannedDirectorySectors+(lcResult.plannedDataSectors?1U:0U)+(lcResult.changedFatSectors?2U:0U));
    CHECK(commitWrites-lcBaseWrites==lcResult.commit.completedMetadataSectors+lcResult.commit.completedDataSectors);
    LCCompare(UMICOM_FALSE);CommitEqual(&lcResult,&lcOwner.lastResult,sizeof(lcResult));
}
static void LCFailed(void)
{
    CHECK(lcOwner.commit.state==UMICOM_FAT16_COMMIT_FAILED && !lcResult.commit.commitAccepted && lcResult.commit.mediaTouched);
    if(lcResult.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE||lcResult.operation==UMICOM_FAT16_LIFECYCLE_DELETE) {
        CHECK(!lcResult.commit.submittedDataSectors && !lcResult.commit.completedDataSectors && !lcResult.commit.submittedBytes && !lcResult.commit.confirmedBytes);
        CHECK(!lcResult.commit.dataDurable && !lcResult.commit.dataVerified && lcResult.commit.dataOutcome==UMICOM_FAT16_UPDATE_NOT_SUBMITTED);
    }
    UmicomKernelFat16LifecycleResult saved;memcpy(&saved,&lcOwner.lastResult,sizeof(saved));const UmicomU32 events=commitEventCount;
    CHECK(LCStage()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));
    CHECK(LCFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));
    CHECK(events==commitEventCount);CommitEqual(&saved,&lcOwner.lastResult,sizeof(saved));memcpy(&lcResult,&saved,sizeof(lcResult));
}
static void LCFreshRead(void)
{
    UmicomKernelFat16 fresh={0};UmicomKernelFat16Metadata metadata;
    const UmicomKernelDiskReader reader={model.capacity,CommitDirectRead,commitVisible};
    CHECK(UmicomKernelFat16Open(&fresh,&reader,0U)==UMICOM_DISK_OK);
    if(lcResult.operation==UMICOM_FAT16_LIFECYCLE_DELETE) CHECK(UmicomKernelFat16MetadataRead(&fresh,lcPath,&metadata)==UMICOM_DISK_NOT_FOUND);
    else {
        CHECK(UmicomKernelFat16MetadataRead(&fresh,lcPath,&metadata)==UMICOM_DISK_OK);
        CHECK(metadata.entry.bytes==lcResult.updatedEntry.bytes && metadata.entry.firstCluster==lcResult.updatedEntry.firstCluster);
        UmicomU8 data[4096];UmicomU64 offset=0U;UmicomU16 cluster=metadata.entry.firstCluster;
        const UmicomSize sectorsPerCluster=commitVisible[(UmicomSize)UMICOM_DISK_FIXTURE_FIRST*512U+13U],clusterBytes=sectorsPerCluster*512U;
        while(offset<metadata.entry.bytes) {
            UmicomSize got=0U;CHECK(UmicomKernelFat16Read(&fresh,lcPath,offset,data,512U,&got)==UMICOM_DISK_OK);
            const UmicomSize expected=(metadata.entry.bytes-offset)<512U?(UmicomSize)(metadata.entry.bytes-offset):512U;
            CHECK(got==expected);CommitEqual(data,lcExpected+((UmicomSize)UMICOM_DISK_FIXTURE_DATA+(cluster-2U)*sectorsPerCluster)*512U+(UmicomSize)(offset%clusterBytes),got);
            offset+=got;if(offset%clusterBytes==0U) cluster=LCFat(lcExpected,cluster);
        }
    }
    CHECK(UmicomKernelFat16Close(&fresh)==UMICOM_DISK_OK);
}
static void LCSuccess(const char *name)
{
    const UmicomBoolean eager=!strcmp(name,"eager")?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomBoolean policy=!strcmp(name,"policy_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    LCFixture(eager||policy?"ordinary":name);CommitRebase();LCOracle();LCOpen();
    lcReentry=!strcmp(name,"reentry")?UMICOM_TRUE:UMICOM_FALSE;lcPolicyReentry=policy;
    lcSnapshot=!strcmp(name,"snapshot")?UMICOM_TRUE:UMICOM_FALSE;
    if(lcSnapshot) lcStrict=UMICOM_FALSE;
    commitPersistWrites=eager;CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);LCStageEvidence();
    CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);LCFinishEvidence(1U);
    if(lcReentry||lcPolicyReentry) CHECK(lcReentries>0U);
    lcReentry=UMICOM_FALSE;lcPolicyReentry=UMICOM_FALSE;
    if(!lcSnapshot) LCFreshRead();
    LCClose();
}
static void LCAllFailures(const char *fixture,const char *name,UmicomKernelFat16LifecycleOperation operation)
{
    const char *profile="ordinary";
    if(!strncmp(name,"write_overwrite.",16U)) {profile="write_overwrite";name+=16U;}
    if(!strncmp(name,"fragmented_boundary.",20U)) {profile="fragmented_boundary";name+=20U;}
    else if(!strncmp(name,"spread_chain.",13U)) {profile="spread_chain";name+=13U;}
    LCFixture(profile);CommitRebase();
    LCOracle();LCOpen();CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 totalReads=commitReads,totalWrites=commitWrites,totalFlushes=commitFlushes;
    UmicomU32 verified[1024],countVerified=0U,ordinal=0U;UmicomBoolean mutation=UMICOM_FALSE;
    for(UmicomU32 i=0U;i<commitEventCount;++i) {
        if(commitEvents[i].command==UMICOM_VIRTIO_REQUEST_READ) {++ordinal;if(mutation) {CHECK(countVerified<1024U);verified[countVerified++]=ordinal;}}
        else mutation=UMICOM_TRUE;
    }
    LCClose();const UmicomBoolean readError=!strcmp(name,"read_errors"),mismatch=!strcmp(name,"read_mismatches");
    const UmicomBoolean writeError=!strcmp(name,"write_errors"),flushError=!strcmp(name,"flush_errors"),drop=!strcmp(name,"dropped_writes");
    CHECK(readError||mismatch||writeError||flushError||drop||!strcmp(name,"altered_writes"));
    const UmicomU32 count=readError?totalReads:(mismatch?countVerified:(flushError?totalFlushes:totalWrites));
    for(UmicomU32 at=1U;at<=count;++at) {
        LCStart(fixture,operation);LCFixture(profile);CommitRebase();LCOracle();lcStrict=UMICOM_FALSE;
        if(readError) commitFailRead=at;else if(mismatch) commitCorruptRead=verified[at-1U];
        else if(writeError) {commitFailWrite=at;commitPartialWrite=UMICOM_TRUE;}
        else if(flushError) commitFailFlush=at;else if(drop) commitDropWrite=at;else commitAlterWrite=at;
        UmicomKernelFat16UpdateStatus status=UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U);
        if(status==UMICOM_FAT16_UPDATE_OK) {status=LCStage();if(status==UMICOM_FAT16_UPDATE_OK) status=LCFinish();}
        CHECK(status!=UMICOM_FAT16_UPDATE_OK);
        if(commitMutations) {
            CHECK(!lcResult.commit.commitAccepted && !lcOwner.committedOperations);
            if(writeError) CHECK(lcResult.commit.writeUncertain && lcResult.commit.uncertainSectorValid && lcResult.commit.needsFlush);
            if(flushError) CHECK(lcResult.commit.needsFlush && !lcResult.commit.uncertainSectorValid);
            LCFailed();
        } else CommitUnchanged();
        if(readError) CHECK(commitReads==at);
        if(mismatch) CHECK(commitReads==verified[at-1U] && lcResult.commit.diskStatus==UMICOM_DISK_CORRUPT);
        if(writeError) CHECK(commitWrites==at);
        if(flushError) CHECK(commitFlushes==at);
        LCClose();
    }
    printf("lifecycle operation=%u profile=%s injected-%s=%u baseline-reads=%u writes=%u flushes=%u\n",(unsigned)operation,profile,name,count,totalReads,totalWrites,totalFlushes);
}
static void LCCuts(const char *fixture,UmicomKernelFat16LifecycleOperation operation,UmicomBoolean eager)
{
    LCOracle();LCOpen();CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 count=commitMutations;LCClose();
    for(UmicomU32 at=1U;at<=count;++at) {
        LCStart(fixture,operation);LCOracle();lcStrict=UMICOM_FALSE;commitPersistWrites=eager;commitCutMutation=at;LCOpen();
        UmicomKernelFat16UpdateStatus status=LCStage();if(status==UMICOM_FAT16_UPDATE_OK) status=LCFinish();
        CHECK(status!=UMICOM_FAT16_UPDATE_OK && commitMutations==at && !model.allowed);LCFailed();
        CommitReplay(UMICOM_FALSE);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));
        CommitReplay(UMICOM_TRUE);CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
        const UmicomBoolean clean=(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK)&&
            (CommitFlags(commitDurable,COMMIT_MIRROR)&UMICOM_FAT16_CLEAN_MASK)?UMICOM_TRUE:UMICOM_FALSE;
        if(clean) CHECK(!memcmp(commitDurable,commitInitial,sizeof(commitDurable))||!memcmp(commitDurable,lcExpected,sizeof(commitDurable)));
        model.allowed=UMICOM_TRUE;LCClose();
    }
    printf("lifecycle operation=%u %s cuts=%u\n",(unsigned)operation,eager?"eager":"cached",count);
}

static void LCLifecycle(const char *name)
{
    if(!strcmp(name,"zero_close")) {CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_OK);CHECK(!model.notifications);return;}
    if(!strcmp(name,"readonly_retry")) {
        model.featuresLow|=UMICOM_VIRTIO_READ_ONLY;CHECK(UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_READ_ONLY);
        CHECK(!Allocated());model.featuresLow&=~UMICOM_VIRTIO_READ_ONLY;
    } else if(!strcmp(name,"no_flush_retry")) {
        model.featuresLow&=~UMICOM_VIRTIO_FEATURE_FLUSH;CHECK(UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR);
        CHECK(!Allocated());model.featuresLow|=UMICOM_VIRTIO_FEATURE_FLUSH;
    } else if(!strcmp(name,"failed_open_retained")) {
        model.refuseDriver=UMICOM_TRUE;model.stickAfterDriver=UMICOM_TRUE;
        CHECK(UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(Allocated()==2U && !model.freeCalls);CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        model.stuckReset=UMICOM_FALSE;model.refuseDriver=UMICOM_FALSE;model.stickAfterDriver=UMICOM_FALSE;LCClose();return;
    }
    LCOracle();LCOpen();
    if(!strcmp(name,"ready_close")) {LCClose();CommitUnchanged();return;}
    if(!strcmp(name,"finish_before_stage")) {CHECK(LCFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));CommitUnchanged();LCClose();return;}
    if(!strcmp(name,"timeout_retained")) {
        commitHangMutation=5U;model.stuckReset=UMICOM_TRUE;CHECK(LCStage()==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(lcResult.commit.writeUncertain && lcResult.commit.uncertainSectorValid && Allocated()==2U && !model.freeCalls);
        CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);CHECK(Allocated()==2U && !model.freeCalls);
        model.stuckReset=UMICOM_FALSE;model.noCompletion=UMICOM_FALSE;LCClose();return;
    }
    CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);LCStageEvidence();const UmicomU32 mutations=commitMutations;
    if(!strcmp(name,"stage_twice")) {
        const UmicomKernelFat16LifecycleResult history=lcOwner.lastResult;const UmicomU32 events=commitEventCount;
        CHECK(LCStage()==UMICOM_FAT16_UPDATE_BAD_STATE);CHECK(commitEventCount==events);
        CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));CommitEqual(&history,&lcOwner.lastResult,sizeof(history));
    } else if(!strcmp(name,"reset_retry")||!strcmp(name,"first_release_retry")||!strcmp(name,"second_release_retry")) {
        const UmicomKernelBlockHandle handle=lcOwner.commit.updater.handle;
        if(!strcmp(name,"reset_retry")) model.stuckReset=UMICOM_TRUE;else model.failFree=!strcmp(name,"first_release_retry")?1U:2U;
        CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_RELEASE_FAILED);
        CHECK(lcOwner.commit.updater.handle==handle && lcOwner.commit.state==UMICOM_FAT16_COMMIT_CLOSING);
        CHECK(Allocated()==(!strcmp(name,"second_release_retry")?1U:2U));model.stuckReset=UMICOM_FALSE;model.failFree=0U;
    } else if(!strcmp(name,"unsafe_close_retry")) {
        model.allowed=UMICOM_FALSE;CHECK(UmicomKernelFat16LifecycleClose(&lcOwner)==UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT);
        CHECK(Allocated()==2U);model.allowed=UMICOM_TRUE;
    } else if(!strcmp(name,"finish_twice")||!strcmp(name,"single_lifetime")) {
        CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);LCFinishEvidence(1U);
        if(!strcmp(name,"finish_twice")) {CHECK(LCFinish()==UMICOM_FAT16_UPDATE_BAD_STATE);CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));}
    }
    LCClose();
    if(!strcmp(name,"single_lifetime")) CHECK(UmicomKernelFat16LifecycleOpen(&lcOwner,&domain,0U,0U,32U)==UMICOM_FAT16_UPDATE_BAD_STATE);
    if(strcmp(name,"finish_twice") && strcmp(name,"single_lifetime")) {
        CHECK(commitMutations==mutations);CommitEqual(commitVisible,commitExpected,sizeof(commitVisible));CommitEqual(commitDurable,commitExpected,sizeof(commitDurable));
        CHECK(!(CommitFlags(commitDurable,COMMIT_PRIMARY)&UMICOM_FAT16_CLEAN_MASK));
    }
}
static void LCTamper(const char *name)
{
    if(!strncmp(name,"later_fat",9U)) LCFixture("spread_chain");
    CommitRebase();LCOracle();LCOpen();CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);LCStageEvidence();
    const UmicomU32 mutations=commitMutations;UmicomSize address=0U;
    if(!strcmp(name,"primary_header")) address=(UmicomSize)COMMIT_PRIMARY*512U+123U;
    else if(!strcmp(name,"mirror_header")) address=(UmicomSize)COMMIT_MIRROR*512U+345U;
    else if(!strcmp(name,"later_fat_primary")) address=((UmicomSize)COMMIT_PRIMARY+1U)*512U;
    else if(!strcmp(name,"later_fat_mirror")) address=((UmicomSize)COMMIT_MIRROR+1U)*512U;
    else if(!strcmp(name,"data")) address=(UmicomSize)lcOwner.plan.dataSectors[0].sector*512U+7U;
    else if(!strcmp(name,"zero_slack")) address=(UmicomSize)lcOwner.plan.dataSectors[lcOwner.plan.dataSectorCount-1U].sector*512U+511U;
    else {
        address=(UmicomSize)lcDirectory*512U+lcEntry;
        if(!strcmp(name,"name")) address+=3U;else if(!strcmp(name,"attribute")) address+=11U;
        else if(!strcmp(name,"time")) address+=22U;else if(!strcmp(name,"cluster")) address+=26U;
        else if(!strcmp(name,"size")) address+=28U;else if(!strcmp(name,"neighbour")) address+=37U;else CHECK(0);
    }
    commitVisible[address]^=0x53U;CHECK(LCFinish()==UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
    CHECK(commitMutations==mutations && !lcResult.commit.cleanFinalisationStarted && !lcResult.commit.cleanDurable);
    CHECK(lcResult.directoryDurable && lcResult.directoryVerified);LCFailed();LCClose();
}
static void LCGuard(const char *name,UmicomBoolean finish)
{
    LCOracle();LCOpen();if(finish) {CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);LCStageEvidence();}
    UmicomKernelFat16LifecycleResult history;memcpy(&history,&lcOwner.lastResult,sizeof(history));const UmicomU32 events=commitEventCount;
    UmicomKernelFat16LifecycleCommitter *owner=&lcOwner;UmicomKernelFat16LifecycleResult *out=&lcResult;
    const UmicomKernelFat16LifecycleRequest *request=&lcRequest;UmicomKernelFat16UpdateStatus expected=UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    static UmicomKernelFat16LifecycleCommitter copied;
    _Alignas(UmicomKernelFat16LifecycleResult) UmicomU8 unaligned[sizeof(UmicomKernelFat16LifecycleResult)+8U];memset(unaligned,0x49,sizeof(unaligned));
    memset(&lcResult,0xa5,sizeof(lcResult));memcpy(&lcResultBefore,&lcResult,sizeof(lcResult));
    if(!strcmp(name,"result_owner")) out=&lcOwner.lastResult;
    else if(!strcmp(name,"result_plan")) out=(UmicomKernelFat16LifecycleResult *)(void *)&lcOwner.plan;
    else if(!strcmp(name,"result_domain")) out=(UmicomKernelFat16LifecycleResult *)(void *)&domain;
    else if(!strcmp(name,"result_dma")) out=(UmicomKernelFat16LifecycleResult *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"result_queue")) out=(UmicomKernelFat16LifecycleResult *)domain.slots[0].queueFrame;
    else if(!strcmp(name,"result_alignment")) out=(UmicomKernelFat16LifecycleResult *)(void *)(unaligned+1U);
    else if(!strcmp(name,"result_overflow")) out=(UmicomKernelFat16LifecycleResult *)(~(UmicomAddress)0U&~((UmicomAddress)alignof(UmicomKernelFat16LifecycleResult)-1U));
    else if(!strcmp(name,"result_null")) out=NULL;
    else if(!strcmp(name,"result_request")) out=(UmicomKernelFat16LifecycleResult *)(void *)&lcRequest;
    else if(!strcmp(name,"owner_null")) owner=NULL;
    else if(!strcmp(name,"owner_alignment")) owner=(UmicomKernelFat16LifecycleCommitter *)((UmicomU8 *)&lcOwner+1U);
    else if(!strcmp(name,"copied_owner")) {memcpy(&copied,&lcOwner,sizeof(copied));owner=&copied;expected=UMICOM_FAT16_UPDATE_BAD_STATE;}
    else if(!strcmp(name,"request_owner")) request=&lcOwner.request;
    else if(!strcmp(name,"request_domain")) request=(const UmicomKernelFat16LifecycleRequest *)(const void *)&domain;
    else if(!strcmp(name,"request_result")) request=(const UmicomKernelFat16LifecycleRequest *)(const void *)&lcResult;
    else if(!strcmp(name,"request_dma")) request=(const UmicomKernelFat16LifecycleRequest *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"request_null")) request=NULL;
    else if(!strcmp(name,"request_alignment")) request=(const UmicomKernelFat16LifecycleRequest *)((const UmicomU8 *)&lcRequest+1U);
    else if(!strcmp(name,"path_owner")) lcRequest.path=lcOwner.path;
    else if(!strcmp(name,"path_result")) lcRequest.path=(const char *)&lcResult;
    else if(!strcmp(name,"path_request")) lcRequest.path=(const char *)&lcRequest;
    else if(!strcmp(name,"path_dma")) lcRequest.path=(const char *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"path_domain")) lcRequest.path=(const char *)&domain;
    else if(!strcmp(name,"path_input")) lcRequest.path=(const char *)lcInput;
    else if(!strcmp(name,"input_owner")) lcRequest.input=lcOwner.input;
    else if(!strcmp(name,"input_result")) lcRequest.input=&lcResult;
    else if(!strcmp(name,"input_request")) lcRequest.input=&lcRequest;
    else if(!strcmp(name,"input_dma")) lcRequest.input=(const void *)domain.slots[0].dataFrame;
    else if(!strcmp(name,"input_path")) lcRequest.input=lcPath;
    else if(!strcmp(name,"busy_owner")) lcOwner.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_embedded")) lcOwner.commit.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_updater")) lcOwner.commit.updater.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_workspace")) lcOwner.workspace.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_base")) lcOwner.commit.updater.workspace.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_volume")) lcOwner.commit.updater.volume.busy=UMICOM_TRUE;
    else if(!strcmp(name,"busy_domain")) domain.busy=UMICOM_TRUE;
    else if(!strcmp(name,"unsafe_context")) {model.allowed=UMICOM_FALSE;expected=UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT;}
    else CHECK(0);
    if(!strncmp(name,"busy_",5U)) expected=UMICOM_FAT16_UPDATE_BUSY;
    CHECK((finish?UmicomKernelFat16LifecycleFinish(owner,out):UmicomKernelFat16LifecycleStage(owner,request,out))==expected);
    lcOwner.busy=UMICOM_FALSE;lcOwner.commit.busy=UMICOM_FALSE;lcOwner.commit.updater.busy=UMICOM_FALSE;
    lcOwner.workspace.busy=UMICOM_FALSE;lcOwner.commit.updater.workspace.busy=UMICOM_FALSE;lcOwner.commit.updater.volume.busy=UMICOM_FALSE;
    domain.busy=UMICOM_FALSE;model.allowed=UMICOM_TRUE;
    CHECK(commitEventCount==events);CommitEqual(&history,&lcOwner.lastResult,sizeof(history));CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));
    for(UmicomSize i=0U;i<sizeof(unaligned);++i) CHECK(unaligned[i]==0x49U);
    if(!finish) CommitUnchanged();else LCCompare(UMICOM_TRUE);LCClose();
}
static void LCFormat(const char *name)
{
    UmicomKernelDiskStatus expected=CommitCorrupt(name);LCOpen();lcStrict=UMICOM_FALSE;
    if(!strcmp(name,"io_budget")) expected=UMICOM_DISK_IO_ERROR;
    const UmicomKernelFat16UpdateStatus status=LCStage();
    if(status==UMICOM_FAT16_UPDATE_OK||lcResult.commit.diskStatus!=expected)
        fprintf(stderr,"format=%s status=%u disk=%u expected=%u reads=%u\n",name,(unsigned)status,(unsigned)lcResult.commit.diskStatus,(unsigned)expected,commitReads);
    CHECK(status!=UMICOM_FAT16_UPDATE_OK && lcResult.commit.diskStatus==expected);
    CHECK(!lcResult.commit.mediaTouched && lcOwner.commit.state==UMICOM_FAT16_COMMIT_READY && !lcResult.planned);
    if(!strcmp(name,"io_budget")) CHECK(lcOwner.commit.updater.operationReads==4096U && commitReads==4100U &&
        status==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR && lcResult.commit.blockStatus==UMICOM_BLOCK_TIMEOUT);
    CommitUnchanged();LCClose();
}
static void LCSession(const char *name)
{
    LCOpen();const UmicomKernelBlockHandle lease=lcOwner.commit.updater.handle;
    const UmicomKernelFat16LifecycleOperation operations[]={UMICOM_FAT16_LIFECYCLE_CREATE,UMICOM_FAT16_LIFECYCLE_APPEND,
        UMICOM_FAT16_LIFECYCLE_TRUNCATE,UMICOM_FAT16_LIFECYCLE_DELETE,UMICOM_FAT16_LIFECYCLE_CREATE};
    for(UmicomSize i=0U;i<5U;++i) {
        if(i) {CommitRebase();LCDefaults(operations[i]);}
        lcEntry=160U;lcNextEntry=192U;memcpy(lcPath,i==4U?"/REUSE.BIN":"/LIFE.BIN",i==4U?11U:10U);
        if(!strcmp(name,"empty")) {
            if(i==0U) {lcRequest.bytes=0U;lcRequest.input=NULL;}
            if(i==1U) lcRequest.bytes=600U;
            if(i==2U) lcRequest.size=0U;
        }
        LCOracle();CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);LCStageEvidence();
        CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);LCFinishEvidence(i+1U);
        CHECK(lcOwner.commit.updater.handle==lease && Allocated()==2U);LCFreshRead();
        if(i==0U && !strcmp(name,"retry_preflight")) {
            UmicomKernelFat16LifecycleRequest bad=lcRequest;bad.operation=UMICOM_FAT16_LIFECYCLE_CREATE;
            const UmicomU32 writes=commitWrites,flushes=commitFlushes;
            CHECK(UmicomKernelFat16LifecycleStage(&lcOwner,&bad,&lcResult)==UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR);
            CHECK(lcResult.commit.diskStatus==UMICOM_DISK_EXISTS && lcOwner.commit.state==UMICOM_FAT16_COMMIT_READY);
            CHECK(lcOwner.committedOperations==1U && commitWrites==writes && commitFlushes==flushes);
        }
    }
    LCClose();
}
static void LCClocks(const char *fixture,const char *name,UmicomKernelFat16LifecycleOperation operation)
{
    LCOracle();LCOpen();CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK);const UmicomU32 stageReads=commitReads;
    CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);const UmicomU32 finishReads=commitReads,count=commitMutations;LCClose();
    const UmicomBoolean acceptance=strstr(name,"accept_")?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomU32 positions=acceptance?1U:count;
    for(UmicomU32 at=1U;at<=positions;++at) {
        LCStart(fixture,operation);LCOracle();lcStrict=UMICOM_FALSE;LCOpen();
        commitClockInside=!strncmp(name,"inner_",6U)?UMICOM_TRUE:UMICOM_FALSE;
        commitClockRollback=strstr(name,"rollback")?UMICOM_TRUE:UMICOM_FALSE;
        if(acceptance) {commitClockRead=strstr(name,"finish")?finishReads:stageReads;commitClockSkip=1U;}
        else commitClockMutation=at;
        UmicomKernelFat16UpdateStatus status=LCStage();if(status==UMICOM_FAT16_UPDATE_OK) status=LCFinish();
        CHECK(status==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR && commitClockInjected);
        CHECK(lcResult.commit.blockStatus==(commitClockRollback?UMICOM_BLOCK_CLOCK_ERROR:UMICOM_BLOCK_TIMEOUT));
        CHECK(!lcResult.commit.commitAccepted && !lcOwner.committedOperations);
        if(acceptance) {
            const UmicomBoolean finished=strstr(name,"finish")?UMICOM_TRUE:UMICOM_FALSE;
            CHECK(!commitClockSkip && lcResult.directoryVerified && lcResult.commit.cleanVerified==finished);
            CHECK(!lcResult.commit.needsFlush && !lcResult.commit.writeUncertain);
        } else {
            CHECK(commitMutations==at);
            CHECK(lcResult.commit.lastBlockOutcome==(commitClockInside?UMICOM_BLOCK_SUBMITTED_UNCONFIRMED:UMICOM_BLOCK_COMPLETED));
        }
        LCFailed();LCClose();
    }
    printf("lifecycle operation=%u clock=%s positions=%u\n",(unsigned)operation,name,positions);
}
static void LCGeometry(const char *name)
{
    /* The64-sector geometry advertises a larger sparse capacity. Its untouched
     * unallocated tail is defined as zero, but any actual unbacked request is
     * rejected by the model's bounds assertion. We compare all8MiB backed bytes
     * and report the highest real request; this is not a whole virtual-image
     * byte comparison. No allocation in this case reaches the sparse tail. */
    const UmicomBoolean maximum=!strcmp(name,"maximum_initialisation")?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomU32 sectorsPerCluster=maximum?64U:2U;
    const UmicomU32 length=maximum?129U+4085U*64U:UMICOM_DISK_FIXTURE_LENGTH;
    UmicomU8 *const boot=LCSector(UMICOM_DISK_FIXTURE_FIRST);boot[13U]=(UmicomU8)sectorsPerCluster;
    CommitPut16(boot+19U,length<=65535U?(UmicomU16)length:0U);CommitPut32(boot+32U,length>65535U?length:0U);
    CommitPut32(commitVisible+446U+12U,length);model.capacity=UMICOM_DISK_FIXTURE_FIRST+length;
    for(UmicomSize copy=0U;copy<2U;++copy)
        memset(LCSector(COMMIT_PRIMARY+copy*UMICOM_DISK_FIXTURE_FAT_SECTORS)+4U,0,UMICOM_DISK_FIXTURE_FAT_SECTORS*512U-4U);
    memset(LCSector(UMICOM_DISK_FIXTURE_ROOT),0,32U*512U);lcEntry=0U;lcNextEntry=32U;
    lcRequest.bytes=maximum?4096U:1U;CommitRebase();LCOracle();LCOpen();
    if(!strcmp(name,"suffix_before_submit")) {commitClockMutation=5U;lcStrict=UMICOM_FALSE;}
    else if(!strcmp(name,"suffix_write_failure")) {commitFailWrite=4U;lcStrict=UMICOM_FALSE;}
    const UmicomKernelFat16UpdateStatus status=LCStage();
    if(maximum||!strcmp(name,"two_sector_initialisation")) {
        CHECK(status==UMICOM_FAT16_UPDATE_OK);LCStageEvidence();
        CHECK(lcResult.plannedDataSectors==sectorsPerCluster && lcResult.allocatedClusters==1U);
        CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK);LCFinishEvidence(1U);LCFreshRead();
    } else {
        CHECK(status==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR && lcResult.commit.confirmedBytes==1U && lcResult.commit.completedDataSectors==1U);
        CHECK(!lcResult.commit.dataDurable && !lcResult.commit.dataVerified);
        CHECK(lcResult.commit.dataOutcome==(!strcmp(name,"suffix_before_submit")?UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED:UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED));
        CHECK(lcResult.commit.submittedDataSectors==(!strcmp(name,"suffix_before_submit")?1U:2U));LCFailed();
    }
    UmicomU64 highest=0U;
    for(UmicomSize i=0U;i<commitEventCount;++i) if(commitEvents[i].command!=UMICOM_VIRTIO_REQUEST_FLUSH) {
        CHECK(commitEvents[i].sector<UMICOM_DISK_FIXTURE_SECTORS);if(commitEvents[i].sector>highest) highest=commitEvents[i].sector;
    }
    printf("lifecycle geometry spc=%u device-sectors=%llu partition-sectors=%u backed-bytes=%zu highest-request-lba=%llu\n",
        sectorsPerCluster,(unsigned long long)model.capacity,length,sizeof(commitVisible),(unsigned long long)highest);
    LCClose();
}
static void LCConsoleReenter(void)
{
    const UmicomSize before=transcriptBytes;
    const char *const commands[]={"fatfsinfo","fatfscommit","fatfsclose","fatdelete /LIFE.BIN","fatfileinfo","fatrenameinfo","fatfileappend /FRAG.BIN x"};
    for(UmicomSize i=0U;i<sizeof(commands)/sizeof(commands[0]);++i) CHECK(FileConsoleCommand(&commitShell,commands[i])==UMICOM_SHELL_BUSY);
    CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_BUSY);
    CHECK(transcriptBytes==before);++lcConsoleReentries;
}
static void LCConsoleOutput(void *context,const char *text,UmicomSize bytes)
{ Output(context,text,bytes);if(lcConsoleOutput) LCConsoleReenter(); }
static void LCConsole(const char *name)
{
    memset(&commitShell,0,sizeof(commitShell));memset(&commitForeignShell,0,sizeof(commitForeignShell));
    commitShell.output=LCConsoleOutput;commitForeignShell.output=LCConsoleOutput;
    if(!strcmp(name,"arguments")) {
        const char *const invalid[]={"fatfsopen","fatfsopen 0","fatfsopen -1 0","fatfsopen 0 4","fatfstime","fatcreate /LIFE.BIN",
            "fatappend /LIFE.BIN","fattruncate /LIFE.BIN -1","fattruncate /LIFE.BIN 4294967296","fatdelete","fatdelete /LIFE.BIN extra","fatfscommit x","fatfsinfo x","fatfsclose x"};
        for(UmicomSize i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i) CHECK(FileConsoleCommand(&commitShell,invalid[i])==UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(!Allocated() && !commitEventCount);return;
    }
    if(!strncmp(name,"foreign_",8U)) {
        const UmicomBoolean historicalFirst=strstr(name,"after_file")||strstr(name,"after_rename")?UMICOM_TRUE:UMICOM_FALSE;
        const UmicomBoolean rename=strstr(name,"rename")?UMICOM_TRUE:UMICOM_FALSE;
        const UmicomBoolean failed=strstr(name,"failed")?UMICOM_TRUE:UMICOM_FALSE;
        const char *const historical=rename?"fatrenameopen 0 0":"fatfileopen 0 0";
        if(failed) model.featuresLow|=UMICOM_VIRTIO_READ_ONLY;
        CHECK(FileConsoleCommand(&commitShell,historicalFirst?historical:"fatfsopen 0 0")==
            (failed?UMICOM_SHELL_IO_ERROR:UMICOM_SHELL_OK));
        model.featuresLow&=~UMICOM_VIRTIO_READ_ONLY;const UmicomU32 events=commitEventCount;const UmicomSize output=transcriptBytes;
        CHECK(FileConsoleCommand(&commitForeignShell,historicalFirst?"fatfsopen 0 0":historical)==UMICOM_SHELL_BAD_STATE);
        CHECK(events==commitEventCount && output==transcriptBytes);
        CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);CHECK(!Allocated());return;
    }
    lcInitialGet=!strcmp(name,"initial_get")?UMICOM_TRUE:UMICOM_FALSE;
    CHECK(FileConsoleCommand(&commitShell,"fatfsopen 0 0")==UMICOM_SHELL_OK);lcInitialGet=UMICOM_FALSE;
    CHECK(strstr(transcript,"fat.lifecycle.open=ok") && Allocated()==2U);CommitClearTranscript();
    if(!strcmp(name,"initial_get")) {
        CHECK(lcConsoleReentries>0U);CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);return;
    }
    if(!strcmp(name,"missing_time")) {
        CHECK(FileConsoleCommand(&commitShell,"fatcreate /LIFE.BIN hello")==UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript,"select an explicit calendar") && !commitWrites && !commitFlushes);CommitClearTranscript();
    }
    if(!strcmp(name,"calendar")) {
        CHECK(FileConsoleCommand(&commitShell,"fatfstime 2100-02-29T12:00:00")==UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(FileConsoleCommand(&commitShell,"fatfstime 1979-12-31T23:59:59")==UMICOM_SHELL_INVALID_ARGUMENT);CommitClearTranscript();
    }
    CHECK(FileConsoleCommand(&commitShell,"fatfstime 2044-02-29T23:58:57")==UMICOM_SHELL_OK);
    CHECK(strstr(transcript,"stored-time=2044-02-29T23:58:56"));CommitClearTranscript();
    const UmicomBoolean sequence=!strcmp(name,"sequence")?UMICOM_TRUE:UMICOM_FALSE;
    lcConsoleQueue=!strcmp(name,"queue_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    lcConsoleOutput=!strcmp(name,"output_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomSize operations=sequence?4U:1U;
    for(UmicomSize i=0U;i<operations;++i) {
        if(i) CommitRebase();
        LCDefaults(i==0U?UMICOM_FAT16_LIFECYCLE_CREATE:i==1U?UMICOM_FAT16_LIFECYCLE_APPEND:i==2U?UMICOM_FAT16_LIFECYCLE_TRUNCATE:UMICOM_FAT16_LIFECYCLE_DELETE);
        memcpy(lcPath,"/LIFE.BIN",10U);lcEntry=160U;lcNextEntry=192U;
        const char *command="fatcreate /LIFE.BIN hello";
        if(i<2U) {memcpy(lcInput,"hello",5U);lcRequest.bytes=5U;}
        if(sequence && i==0U) {lcRequest.input=NULL;lcRequest.bytes=0U;command="fatcreate /LIFE.BIN \"\"";}
        else if(i==1U) command="fatappend /LIFE.BIN hello";
        else if(i==2U) {command="fattruncate /LIFE.BIN 3";lcRequest.size=3U;}
        else if(i==3U) command="fatdelete /LIFE.BIN";
        LCOracle();if(!strcmp(name,"write_failure")) {commitFailWrite=3U;lcStrict=UMICOM_FALSE;}
        const UmicomKernelShellStatus status=FileConsoleCommand(&commitShell,command);
        if(!strcmp(name,"write_failure")) {
            CHECK(status==UMICOM_SHELL_IO_ERROR && commitWrites==3U && strstr(transcript,"commit-accepted=0"));break;
        }
        CHECK(status==UMICOM_SHELL_OK && strstr(transcript,"commit-accepted=0"));LCCompare(UMICOM_TRUE);
        if(!strcmp(name,"staged_close")||!strcmp(name,"shutdown")) break;
        if(!strcmp(name,"stage_twice")) {
            const UmicomU32 events=commitEventCount;CommitClearTranscript();
            CHECK(FileConsoleCommand(&commitShell,"fatdelete /LIFE.BIN")==UMICOM_SHELL_IO_ERROR);
            CHECK(events==commitEventCount && strstr(transcript,"previous-result"));
        }
        CommitClearTranscript();CHECK(FileConsoleCommand(&commitShell,"fatfscommit")==UMICOM_SHELL_OK);
        char accepted[80];CHECK(snprintf(accepted,sizeof(accepted),"fat.lifecycle.committed-operations=%u",(unsigned)(i+1U))>0);
        CHECK(strstr(transcript,accepted) && strstr(transcript,"commit-accepted=1"));LCCompare(UMICOM_FALSE);CommitClearTranscript();
        CHECK(FileConsoleCommand(&commitShell,"fatfsinfo")==UMICOM_SHELL_OK);CHECK(strstr(transcript,accepted));CommitClearTranscript();
    }
    const UmicomU32 mutations=commitMutations;
    if(!strcmp(name,"shutdown")) CHECK(UmicomKernelFat16FileCommitConsoleClose(&commitShell)==UMICOM_FAT16_UPDATE_OK);
    else CHECK(FileConsoleCommand(&commitShell,"fatfsclose")==UMICOM_SHELL_OK);
    CHECK(!Allocated() && commitMutations==mutations);
    if(lcConsoleQueue||lcConsoleOutput) CHECK(lcConsoleReentries>0U);
}
/* Reuse the existing device fault model while keeping the directory oracle
 * independent of both the regular-file oracle and production plan decisions. */
#include "directory_cases.inc"
#include "write_cases.inc"
int __wrap_main(int argc,char **argv)
{
    if (argc == 3 && !strncmp(argv[1], "write.", 6U)) {
        UmicomKernelFat16WriteCase(argv[2], argv[1] + 6U);
        printf("fat16-lifecycle.transport.%s: ok\n", argv[1]); return 0;
    }
    if (argc == 3 && !strncmp(argv[1], "directory.", 10U))
        return DirectoryCase(argv[2], argv[1] + 10U);
    CHECK(argc==3);const char *name=argv[1];UmicomKernelFat16LifecycleOperation operation=UMICOM_FAT16_LIFECYCLE_CREATE;
    if(!strncmp(name,"create.",7U)) name+=7U;
    else if(!strncmp(name,"append.",7U)) {operation=UMICOM_FAT16_LIFECYCLE_APPEND;name+=7U;}
    else if(!strncmp(name,"truncate.",9U)) {operation=UMICOM_FAT16_LIFECYCLE_TRUNCATE;name+=9U;}
    else if(!strncmp(name,"delete.",7U)) {operation=UMICOM_FAT16_LIFECYCLE_DELETE;name+=7U;}
    LCStart(argv[2],operation);
    if(!strncmp(name,"success.",8U)) LCSuccess(name+8U);
    else if(!strncmp(name,"fault.",6U)) LCAllFailures(argv[2],name+6U,operation);
    else if(!strcmp(name,"cuts.cached")||!strcmp(name,"cuts.eager")) LCCuts(argv[2],operation,!strcmp(name,"cuts.eager")?UMICOM_TRUE:UMICOM_FALSE);
    else if(!strncmp(name,"tamper.",7U)) LCTamper(name+7U);
    else if(!strncmp(name,"guard.",6U)) LCGuard(name+6U,UMICOM_FALSE);
    else if(!strncmp(name,"finish_guard.",13U)) LCGuard(name+13U,UMICOM_TRUE);
    else if(!strncmp(name,"lifetime.",9U)) LCLifecycle(name+9U);
    else if(!strncmp(name,"format.",7U)) LCFormat(name+7U);
    else if(!strncmp(name,"session.",8U)) LCSession(name+8U);
    else if(!strncmp(name,"clock.",6U)) LCClocks(argv[2],name+6U,operation);
    else if(!strncmp(name,"geometry.",9U)) LCGeometry(name+9U);
    else if(!strncmp(name,"console.",8U)) LCConsole(name+8U);
    else CHECK(0);
    printf("fat16-lifecycle.transport.%s: ok\n",argv[1]);return 0;
}
