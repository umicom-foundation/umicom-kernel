/* Independent whole-image lifecycle planning oracle. Existing parser-fixture
 * helpers remain unchanged; the expected allocator below consumes raw FAT16
 * words and never reads production plan decisions.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_lifecycle.h"
#define main UmicomLifecycleOriginalPlannerEntry
#include "../fat16_file_commit/file_plan_tests.c"
#undef main

static UmicomKernelFat16LifecycleWorkspace lifeWorkspace;
static UmicomKernelFat16LifecyclePlan lifePlan;
static UmicomKernelFat16LifecycleRequest lifeRequest;
static UmicomU8 lifeInput[4097], lifeExpected[FILE_PLAN_MEDIA_BYTES], lifeApplied[FILE_PLAN_MEDIA_BYTES];
static UmicomU64 lifeDirectory, lifeNextDirectory;
static UmicomSize lifeEntry, lifeNextEntry, lifeExpectedAllocated, lifeExpectedFreed;
static UmicomBoolean lifeSnapshot, lifeReentry;
static UmicomSize lifeReentries;
static UmicomU16 LifeWord(const UmicomU8 *p)
{ return (UmicomU16)((UmicomU16)p[0] | (UmicomU16)((UmicomU16)p[1]<<8U)); }
static UmicomU32 LifeLong(const UmicomU8 *p)
{ return (UmicomU32)p[0]|((UmicomU32)p[1]<<8U)|((UmicomU32)p[2]<<16U)|((UmicomU32)p[3]<<24U); }
static void LifeLongPut(UmicomU8 *p,UmicomU32 value)
{ for(UmicomSize i=0U;i<4U;++i) p[i]=(UmicomU8)(value>>(i*8U)); }
static UmicomU16 LifeFat(const UmicomU8 *image,UmicomU16 cluster)
{ return LifeWord(image+(UMICOM_DISK_FIXTURE_FIRST+1U)*512U+(UmicomSize)cluster*2U); }
static void LifeFatPut(UmicomU8 *image,UmicomU16 cluster,UmicomU16 next)
{
    for(UmicomSize copy=0U;copy<2U;++copy)
        Put16(image+(UMICOM_DISK_FIXTURE_FIRST+1U+copy*UMICOM_DISK_FIXTURE_FAT_SECTORS)*512U+(UmicomSize)cluster*2U,next);
}
static void LifeEntryMake(UmicomU8 *p,const char *alias,UmicomU16 first,UmicomU32 size)
{ memset(p,0,32U);memcpy(p,alias,11U);p[11U]=0x20U;Put16(p+26U,first);LifeLongPut(p+28U,size); }
static UmicomKernelDiskStatus LifeCall(void)
{ return UmicomKernelFat16PlanLifecycle(&volume,&lifeRequest,&workspace,&lifeWorkspace,&lifePlan); }
static UmicomBoolean LifeRead(void *context,UmicomU64 lba,UmicomU8 *output)
{
    const UmicomBoolean ok=Read(context,lba,output);
    if(reads==1U && lifeSnapshot) {
        memset(lifeInput,0xee,sizeof(lifeInput));memset(path,'q',sizeof(path));memset(&lifeRequest,0,sizeof(lifeRequest));
    }
    if(reads==1U && lifeReentry) {
        CHECK(LifeCall()==UMICOM_DISK_BUSY);CHECK(UmicomKernelFat16Close(&volume)==UMICOM_DISK_BUSY);++lifeReentries;
    }
    return ok;
}
static void LifeStart(void)
{
    Start();volume.reader.read=LifeRead;memset(&lifeWorkspace,0,sizeof(lifeWorkspace));memset(&lifePlan,0xa5,sizeof(lifePlan));
    lifeSnapshot=UMICOM_FALSE;lifeReentry=UMICOM_FALSE;lifeReentries=0U;
}
static void LifeDefaults(UmicomKernelFat16LifecycleOperation operation)
{
    lifeDirectory=UMICOM_DISK_FIXTURE_ROOT;lifeEntry=operation==UMICOM_FAT16_LIFECYCLE_CREATE?160U:96U;
    lifeNextDirectory=lifeDirectory;lifeNextEntry=lifeEntry+32U;
    memcpy(path,operation==UMICOM_FAT16_LIFECYCLE_CREATE?"/LIFE.BIN":"/FRAG.BIN",10U);
    for(UmicomSize i=0U;i<sizeof(lifeInput);++i) lifeInput[i]=(UmicomU8)(i*29U+0x31U);
    /* Named fields keep optional request extensions zero-initialized.
     * The earlier positional initializer is retained for review. */
#if 0
    lifeRequest=(UmicomKernelFat16LifecycleRequest){operation,path,NULL,0U,0U,{2044U,2U,29U,23U,58U,57U}};
#endif
    lifeRequest = (UmicomKernelFat16LifecycleRequest){
        .operation=operation, .path=path,
        .time={2044U,2U,29U,23U,58U,57U}};

    /* WRITE exercises the existing allocation and transport oracle as well. */
#if 0
    if(operation==UMICOM_FAT16_LIFECYCLE_CREATE||operation==UMICOM_FAT16_LIFECYCLE_APPEND) {
#endif
    if(operation==UMICOM_FAT16_LIFECYCLE_CREATE||operation==UMICOM_FAT16_LIFECYCLE_APPEND||
       operation==UMICOM_FAT16_LIFECYCLE_WRITE) {
        lifeRequest.input=lifeInput;lifeRequest.bytes=operation==UMICOM_FAT16_LIFECYCLE_CREATE?700U:900U;
        if(operation==UMICOM_FAT16_LIFECYCLE_WRITE) lifeRequest.offset=1020U;
    } else if(operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) lifeRequest.size=513U;
    else memset(&lifeRequest.time,0,sizeof(lifeRequest.time));
}
static void LifeScrubbed(void)
{
    CHECK(workspace.self==&workspace && lifeWorkspace.self==&lifeWorkspace);
    Filled((const UmicomU8 *)&workspace+sizeof(workspace.self),sizeof(workspace)-sizeof(workspace.self),0U);
    Filled((const UmicomU8 *)&lifeWorkspace+sizeof(lifeWorkspace.self),sizeof(lifeWorkspace)-sizeof(lifeWorkspace.self),0U);
    CHECK(!volume.busy && !volume.fatCached);
}
static void LifeOracle(void)
{
    memcpy(lifeExpected,media,sizeof(media));lifeExpectedAllocated=0U;lifeExpectedFreed=0U;
    UmicomU8 *const record=lifeExpected+(UmicomSize)lifeDirectory*512U+lifeEntry;
    const UmicomBoolean create=lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomU32 oldSize=create?0U:LifeLong(record+28U);
    UmicomU16 chain[256];UmicomSize count=0U;
    if(!create) for(UmicomU16 cluster=LifeWord(record+26U);cluster>=2U && cluster<0xfff8U;cluster=LifeFat(media,cluster)) {
        CHECK(count<256U);chain[count++]=cluster;
    }
    UmicomU32 newSize=0U;
    if(create) newSize=(UmicomU32)lifeRequest.bytes;
    else if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_APPEND) newSize=oldSize+(UmicomU32)lifeRequest.bytes;
    else if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_WRITE) {
        const UmicomU64 end=lifeRequest.offset+lifeRequest.bytes;
        newSize=end>oldSize?(UmicomU32)end:oldSize;
    }
    else if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) newSize=lifeRequest.size;
    const UmicomSize required=((UmicomSize)newSize+511U)/512U;
    const UmicomSize originalCount=count;
    for(UmicomU16 candidate=2U;count<required;++candidate) {
        CHECK(candidate<12000U);if(LifeFat(media,candidate)) continue;
        chain[count++]=candidate;++lifeExpectedAllocated;
        memset(lifeExpected+((UmicomSize)UMICOM_DISK_FIXTURE_DATA+candidate-2U)*512U,0,512U);
    }
    for(UmicomSize i=0U;i<required;++i) LifeFatPut(lifeExpected,chain[i],i+1U<required?chain[i+1U]:0xffffU);
    for(UmicomSize i=required;i<originalCount;++i) {LifeFatPut(lifeExpected,chain[i],0U);++lifeExpectedFreed;}
    if(lifeRequest.input) {
        /* Preserve the former EOF-only overlay beside its positional form. */
#if 0
        const UmicomSize offset=create?0U:oldSize;
#endif
        const UmicomSize offset=create?0U:(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_WRITE?
            (UmicomSize)lifeRequest.offset:oldSize);
        for(UmicomSize i=0U;i<lifeRequest.bytes;++i)
            lifeExpected[((UmicomSize)UMICOM_DISK_FIXTURE_DATA+chain[(offset+i)/512U]-2U)*512U+(offset+i)%512U]=lifeInput[i];
    }
    if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_DELETE) {record[0]=0xe5U;return;}
    if(create) {
        const UmicomBoolean ended=record[0]==0U?UMICOM_TRUE:UMICOM_FALSE;
        memset(record,0,32U);memset(record,' ',11U);const char *alias=strrchr(path,'/');CHECK(alias);++alias;
        UmicomSize field=0U,at=0U;
        for(UmicomSize i=0U;alias[i];++i) {
            if(alias[i]=='.') {field=8U;at=0U;continue;}
            const unsigned char byte=(unsigned char)alias[i];record[field+at++]=(UmicomU8)(byte>='a'&&byte<='z'?byte-'a'+'A':byte);
        }
        if(ended && lifeNextDirectory) lifeExpected[(UmicomSize)lifeNextDirectory*512U+lifeNextEntry]=0U;
        Put16(record+14U,0xbf5cU);Put16(record+16U,0x805dU);Put16(record+18U,0x805dU);
    }
    record[11U]|=0x20U;Put16(record+22U,0xbf5cU);Put16(record+24U,0x805dU);
    Put16(record+26U,required?chain[0]:0U);LifeLongPut(record+28U,newSize);
}
static void LifeVerify(void)
{
    CHECK(!memcmp(media,mediumBefore,sizeof(media)));CHECK(lifePlan.operation==lifeRequest.operation||lifeSnapshot);
    CHECK(lifePlan.allocatedClusters==lifeExpectedAllocated && lifePlan.freedClusters==lifeExpectedFreed);
    CHECK(lifePlan.fatSectorCount>=1U && lifePlan.fatSectorCount<=32U && lifePlan.directorySectorCount>=1U && lifePlan.directorySectorCount<=2U);
    CHECK(lifePlan.fatSectors[0].primarySector==UMICOM_DISK_FIXTURE_FIRST+1U);
    CHECK(lifePlan.entryOffset==lifeEntry && lifePlan.directorySectors[0].sector==lifeDirectory);
    memcpy(lifeApplied,media,sizeof(media));UmicomSize payload=0U;
    for(UmicomSize i=0U;i<lifePlan.dataSectorCount;++i) {
        const UmicomKernelFat16UpdateSector *const sector=&lifePlan.dataSectors[i];
        CHECK(sector->sector>=UMICOM_DISK_FIXTURE_DATA && sector->sector<UMICOM_DISK_FIXTURE_FIRST+UMICOM_DISK_FIXTURE_LENGTH);
        CHECK(sector->offset+sector->bytes<=512U);payload+=sector->bytes;
        for(UmicomSize j=0U;j<i;++j) CHECK(sector->sector!=lifePlan.dataSectors[j].sector);
        memcpy(lifeApplied+(UmicomSize)sector->sector*512U,sector->data,512U);
    }
    CHECK(payload==lifePlan.requestedBytes);
    for(UmicomSize i=0U;i<lifePlan.fatSectorCount;++i) {
        const UmicomKernelFat16LifecycleFatSector *const sector=&lifePlan.fatSectors[i];
        CHECK(sector->primarySector>=UMICOM_DISK_FIXTURE_FIRST+1U && sector->primarySector<UMICOM_DISK_FIXTURE_FIRST+1U+UMICOM_DISK_FIXTURE_FAT_SECTORS);
        CHECK(sector->mirrorSector==sector->primarySector+UMICOM_DISK_FIXTURE_FAT_SECTORS);
        CHECK(!memcmp(sector->original,Sector(sector->primarySector),512U));
        CHECK(sector->changed==(!memcmp(sector->original,sector->data,512U)?UMICOM_FALSE:UMICOM_TRUE));
        for(UmicomSize j=0U;j<i;++j) CHECK(sector->primarySector!=lifePlan.fatSectors[j].primarySector);
        memcpy(lifeApplied+(UmicomSize)sector->primarySector*512U,sector->data,512U);
        memcpy(lifeApplied+(UmicomSize)sector->mirrorSector*512U,sector->data,512U);
    }
    for(UmicomSize i=0U;i<lifePlan.directorySectorCount;++i) {
        const UmicomKernelFat16LifecycleDirectorySector *const sector=&lifePlan.directorySectors[i];
        CHECK(!memcmp(sector->original,Sector(sector->sector),512U));
        memcpy(lifeApplied+(UmicomSize)sector->sector*512U,sector->data,512U);
    }
    CHECK(!memcmp(lifeApplied,lifeExpected,sizeof(lifeApplied)));LifeScrubbed();
}
static void LifeReserveBelow(UmicomU16 stop)
{
    UmicomSize slot=5U,count=0U;UmicomU16 first=0U,previous=0U;
    for(UmicomU16 cluster=2U;cluster<stop;++cluster) {
        if(LifeFat(media,cluster)) continue;
        if(!count) first=cluster;else FatLink(previous,cluster);
        previous=cluster;FatLink(cluster,0xffffU);++count;
        if(count==256U) {
            char name[12];CHECK(snprintf(name,sizeof(name),"F%07uBIN",(unsigned)slot)==11);
            LifeEntryMake(Sector(UMICOM_DISK_FIXTURE_ROOT)+slot++*32U,name,first,(UmicomU32)(count*512U));count=0U;
        }
    }
    if(count) {
        char name[12];CHECK(snprintf(name,sizeof(name),"F%07uBIN",(unsigned)slot)==11);
        LifeEntryMake(Sector(UMICOM_DISK_FIXTURE_ROOT)+slot++*32U,name,first,(UmicomU32)(count*512U));
    }
    if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE) {
        lifeDirectory=UMICOM_DISK_FIXTURE_ROOT+slot/16U;lifeEntry=(slot%16U)*32U;
        lifeNextDirectory=UMICOM_DISK_FIXTURE_ROOT+(slot+1U)/16U;lifeNextEntry=((slot+1U)%16U)*32U;
    }
}
static void LifeFixture(const char *name)
{
    UmicomU8 *const root=Sector(UMICOM_DISK_FIXTURE_ROOT);
    if(!strcmp(name,"empty")) {
        if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE) {lifeRequest.bytes=0U;lifeRequest.input=NULL;}
        else {memcpy(path,"/EMPTY.TXT",11U);lifeEntry=128U;if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) lifeRequest.size=0U;}
    } else if(!strcmp(name,"one")) lifeRequest.bytes=1U;
    else if(!strcmp(name,"maximum")) lifeRequest.bytes=4096U;
    else if(!strcmp(name,"sector")) lifeRequest.bytes=512U;
    else if(!strcmp(name,"partial")) lifeRequest.bytes=197U;
    else if(!strcmp(name,"zero")) lifeRequest.size=0U;
    else if(!strcmp(name,"equal")) lifeRequest.size=1300U;
    else if(!strcmp(name,"cluster_boundary")) lifeRequest.size=512U;
    else if(!strcmp(name,"mixed_case")) memcpy(path,"/lIfE.bIn",10U);
    else if(!strcmp(name,"preserved_fields")) {PreserveFields(root+96U);root[107U]=6U;root[108U]=0x18U;}
    else if(!strcmp(name,"deleted_slot")) {memset(root+160U,0x91,32U);root[160U]=0xe5U;root[192U]=0U;}
    else if(!strcmp(name,"nested")) {
        if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE) {memcpy(path,"/DOCS/LIFE.BIN",15U);lifeEntry=96U;}
        else {memcpy(Cluster(3U)+96U,root+96U,32U);root[96U]=0xe5U;memcpy(path,"/DOCS/FRAG.BIN",15U);lifeEntry=96U;}
        lifeDirectory=UMICOM_DISK_FIXTURE_DATA+1U;lifeNextDirectory=lifeDirectory;lifeNextEntry=lifeEntry+32U;
    } else if(!strcmp(name,"end_boundary")||!strcmp(name,"fragmented_boundary")) {
        const UmicomBoolean nested=!strcmp(name,"fragmented_boundary")?UMICOM_TRUE:UMICOM_FALSE;
        UmicomU8 *first=root;UmicomSize begin=5U;
        if(nested) {
            FatLink(3U,30U);FatLink(30U,0xffffU);first=Cluster(3U);begin=3U;
            memcpy(path,"/DOCS/LIFE.BIN",15U);lifeDirectory=UMICOM_DISK_FIXTURE_DATA+1U;lifeNextDirectory=UMICOM_DISK_FIXTURE_DATA+28U;
        } else lifeNextDirectory=UMICOM_DISK_FIXTURE_ROOT+1U;
        for(UmicomSize i=begin;i<15U;++i) {char alias[12];CHECK(snprintf(alias,sizeof(alias),"E%07uTXT",(unsigned)i)==11);LifeEntryMake(first+i*32U,alias,0U,0U);}
        first[480U]=0U;memset(Sector(lifeNextDirectory),0x79,512U);lifeEntry=480U;lifeNextEntry=0U;
    } else if(!strcmp(name,"fat_crossing")) LifeReserveBelow(255U);
    else if(!strcmp(name,"fat_later_crossing")) LifeReserveBelow(511U);
    else if(!strcmp(name,"spread_chain")||!strcmp(name,"fat_limit")) {
        FatLink(9U,0U);FatLink(6U,0U);const UmicomSize count=!strcmp(name,"fat_limit")?32U:3U;
        FatLink(4U,256U);
        for(UmicomSize i=1U;i<count;++i) FatLink((UmicomU16)(i*256U),i+1U<count?(UmicomU16)((i+1U)*256U):0xffffU);
        LifeLongPut(root+124U,(UmicomU32)(count*512U));
        if(lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_TRUNCATE) lifeRequest.size=513U;
    } else CHECK(!strcmp(name,"ordinary")||!strcmp(name,"snapshot")||!strcmp(name,"reentry"));
}
/* An allocated parent now grows when its last slot is occupied. Construct
 * the expected extra chain independently, including the entire zeroed cluster.
 * The earlier NO_SPACE expectation remains in LifeRefusal for source review. */
static void LifeParentGrowth(void)
{
    memcpy(path, "/DOCS/LIFE.BIN", 15U);
    for (UmicomSize i = 3U; i < 16U; ++i) {
        char alias[12];
        CHECK(snprintf(alias, sizeof(alias), "F%07uTXT", (unsigned)i) == 11);
        LifeEntryMake(Cluster(3U) + i * 32U, alias, 0U, 0U);
    }
    lifeDirectory = UMICOM_DISK_FIXTURE_DATA + 8U; /* File takes 5,8; parent takes 10. */
    lifeEntry = 0U;
    lifeNextDirectory = lifeDirectory;
    lifeNextEntry = 32U;
    memcpy(mediumBefore, media, sizeof(media));
    LifeOracle();
    UmicomU8 entry[32];
    memcpy(entry, lifeExpected + (UmicomSize)lifeDirectory * 512U, sizeof(entry));
    memset(lifeExpected + (UmicomSize)lifeDirectory * 512U, 0, 512U);
    memcpy(lifeExpected + (UmicomSize)lifeDirectory * 512U, entry, sizeof(entry));
    LifeFatPut(lifeExpected, 3U, 10U);
    LifeFatPut(lifeExpected, 10U, 0xffffU);
    ++lifeExpectedAllocated;
    LifeStart();
    CHECK(LifeCall() == UMICOM_DISK_OK);
    CHECK(lifePlan.parentGrown && lifePlan.parentAddedCluster == 10U);
    LifeVerify();
}
static void LifeSuccess(const char *name)
{
    LifeFixture(name);memcpy(mediumBefore,media,sizeof(media));LifeOracle();LifeStart();
    lifeSnapshot=!strcmp(name,"snapshot")?UMICOM_TRUE:UMICOM_FALSE;lifeReentry=!strcmp(name,"reentry")?UMICOM_TRUE:UMICOM_FALSE;
    CHECK(LifeCall()==UMICOM_DISK_OK);LifeVerify();if(lifeReentry) CHECK(lifeReentries==1U);
}
static void LifeRefusal(const char *name)
{
    UmicomKernelDiskStatus expected=UMICOM_DISK_INVALID_ARGUMENT;UmicomU8 *const root=Sector(UMICOM_DISK_FIXTURE_ROOT);
    if(!strcmp(name,"exists")) {memcpy(path,"/frag.bin",10U);expected=UMICOM_DISK_EXISTS;}
    else if(!strcmp(name,"missing")) {memcpy(path,"/MISSING.BIN",13U);expected=UMICOM_DISK_NOT_FOUND;}
    else if(!strcmp(name,"root")) {memcpy(path,"/",2U);expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"directory")) {memcpy(path,"/DOCS",6U);expected=lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_CREATE?UMICOM_DISK_EXISTS:UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"readonly")) {root[107U]|=1U;expected=UMICOM_DISK_READ_ONLY;}
    else if(!strcmp(name,"grow_truncate")) {lifeRequest.size=1301U;expected=UMICOM_DISK_RANGE;}
    else if(!strcmp(name,"invalid_time")) lifeRequest.time.day=30U;
    else if(!strcmp(name,"unexpected_time")) lifeRequest.time.year=2044U;
    else if(!strcmp(name,"zero_append")) {lifeRequest.bytes=0U;lifeRequest.input=NULL;}
    else if(!strcmp(name,"too_many_bytes")) lifeRequest.bytes=4097U;
    else if(!strcmp(name,"unused_size")) lifeRequest.size=1U;
    else if(!strcmp(name,"unused_input")) lifeRequest.input=lifeInput;
    else if(!strcmp(name,"null_input")) lifeRequest.input=NULL;
    else if(!strcmp(name,"none")) lifeRequest.operation=UMICOM_FAT16_LIFECYCLE_NONE;
    else if(!strcmp(name,"bad_operation")) lifeRequest.operation=(UmicomKernelFat16LifecycleOperation)99;
    else if(!strcmp(name,"relative")) memcpy(path,"LIFE.BIN",9U);
    else if(!strcmp(name,"parent_path")) memcpy(path,"/DOCS/../LIFE.BIN",18U);
    else if(!strcmp(name,"full_parent")) {
        memcpy(path,"/DOCS/LIFE.BIN",15U);
        for(UmicomSize i=3U;i<16U;++i) {char alias[12];CHECK(snprintf(alias,sizeof(alias),"F%07uTXT",(unsigned)i)==11);LifeEntryMake(Cluster(3U)+i*32U,alias,0U,0U);}
        expected=UMICOM_DISK_NO_SPACE;
    } else if(!strcmp(name,"no_clusters")) {
        const UmicomU16 stop=(UmicomU16)((UMICOM_DISK_FIXTURE_LENGTH-129U)+2U);LifeReserveBelow(stop);expected=UMICOM_DISK_NO_SPACE;
    } else if(!strcmp(name,"io_budget")) {
        for(UmicomSize i=0U;i<123U;++i) {
            char alias[12];CHECK(snprintf(alias,sizeof(alias),"F%07uBIN",(unsigned)i)==11);
            LifeEntryMake(root+(5U+i)*32U,alias,(UmicomU16)(100U+i),20U*512U);
            for(UmicomSize j=0U;j<20U;++j) FatLink((UmicomU16)(100U+i+j*256U),j==19U?0xffffU:(UmicomU16)(100U+i+(j+1U)*256U));
        }
        expected=UMICOM_DISK_LIMIT;
    } else if(!strcmp(name,"fat_limit")) {
        LifeFixture("fat_limit");FatLink(31U*256U,32U*256U);FatLink(32U*256U,0xffffU);LifeLongPut(root+124U,33U*512U);expected=UMICOM_DISK_LIMIT;
    } else if(!strcmp(name,"orphan")) {FatLink(20U,0xffffU);expected=UMICOM_DISK_CORRUPT;}
    else if(!strcmp(name,"shared_tail")) {FatLink(2U,9U);LifeLongPut(root+60U,1300U);expected=UMICOM_DISK_CORRUPT;}
    else if(!strcmp(name,"allocated_empty")) {Put16(root+154U,20U);FatLink(20U,0xffffU);expected=UMICOM_DISK_CORRUPT;}
    else if(!strcmp(name,"surplus_chain")) {FatLink(6U,10U);FatLink(10U,0xffffU);expected=UMICOM_DISK_CORRUPT;}
    else CHECK(0);
    memcpy(mediumBefore,media,sizeof(media));LifeStart();CHECK(LifeCall()==expected);Filled(&lifePlan,sizeof(lifePlan),0xa5U);
    if(!strcmp(name,"io_budget")) CHECK(reads==4096U);
    CHECK(!memcmp(media,mediumBefore,sizeof(media)));
    if(lifeWorkspace.self) LifeScrubbed();else {Filled(&lifeWorkspace,sizeof(lifeWorkspace),0U);Filled(&workspace,sizeof(workspace),0U);}
}
static void LifeGuard(const char *name)
{
    LifeStart();UmicomKernelFat16 *v=&volume;UmicomKernelFat16UpdateWorkspace *base=&workspace;
    UmicomKernelFat16LifecycleWorkspace *scratch=&lifeWorkspace;UmicomKernelFat16LifecyclePlan *out=&lifePlan;
    const UmicomKernelFat16LifecycleRequest *request=&lifeRequest;UmicomKernelDiskStatus expected=UMICOM_DISK_INVALID_ARGUMENT;
    static UmicomKernelFat16LifecycleWorkspace savedScratch,copied;static UmicomKernelFat16LifecyclePlan savedPlan;
    if(!strcmp(name,"null_volume")) v=NULL;
    else if(!strcmp(name,"null_request")) request=NULL;
    else if(!strcmp(name,"null_base")) base=NULL;
    else if(!strcmp(name,"null_workspace")) scratch=NULL;
    else if(!strcmp(name,"null_output")) out=NULL;
    else if(!strcmp(name,"volume_alignment")) v=(UmicomKernelFat16 *)((UmicomU8 *)&volume+1U);
    else if(!strcmp(name,"request_alignment")) request=(const UmicomKernelFat16LifecycleRequest *)((const UmicomU8 *)&lifeRequest+1U);
    else if(!strcmp(name,"workspace_alignment")) scratch=(UmicomKernelFat16LifecycleWorkspace *)((UmicomU8 *)&lifeWorkspace+1U);
    else if(!strcmp(name,"output_alignment")) out=(UmicomKernelFat16LifecyclePlan *)((UmicomU8 *)&lifePlan+1U);
    else if(!strcmp(name,"output_overflow")) out=(UmicomKernelFat16LifecyclePlan *)(~(UmicomAddress)0U-7U);
    else if(!strcmp(name,"output_volume")) out=(UmicomKernelFat16LifecyclePlan *)(void *)&volume;
    else if(!strcmp(name,"output_workspace")) out=&lifeWorkspace.stage;
    else if(!strcmp(name,"workspaces_overlap")) scratch=(UmicomKernelFat16LifecycleWorkspace *)(void *)&workspace;
    else if(!strcmp(name,"request_workspace")) request=(const UmicomKernelFat16LifecycleRequest *)(const void *)&lifeWorkspace;
    else if(!strcmp(name,"request_output")) request=(const UmicomKernelFat16LifecycleRequest *)(const void *)&lifePlan;
    else if(!strcmp(name,"path_output")) lifeRequest.path=(const char *)&lifePlan;
    else if(!strcmp(name,"path_workspace")) lifeRequest.path=lifeWorkspace.path;
    else if(!strcmp(name,"path_request")) lifeRequest.path=(const char *)&lifeRequest;
    else if(!strcmp(name,"path_input")) lifeRequest.path=(const char *)lifeInput;
    else if(!strcmp(name,"input_output")) lifeRequest.input=&lifePlan;
    else if(!strcmp(name,"input_workspace")) lifeRequest.input=&lifeWorkspace;
    else if(!strcmp(name,"input_request")) lifeRequest.input=&lifeRequest;
    else if(!strcmp(name,"input_path")) lifeRequest.input=path;
    else if(!strcmp(name,"busy_volume")) {volume.busy=UMICOM_TRUE;expected=UMICOM_DISK_BUSY;}
    else if(!strcmp(name,"busy_base")) {workspace.busy=UMICOM_TRUE;expected=UMICOM_DISK_BUSY;}
    else if(!strcmp(name,"busy_workspace")) {lifeWorkspace.busy=UMICOM_TRUE;expected=UMICOM_DISK_BUSY;}
    else if(!strcmp(name,"copied_volume")) {volumeCopy=volume;v=&volumeCopy;expected=UMICOM_DISK_BAD_STATE;}
    else if(!strcmp(name,"copied_workspace")) {memset(&copied,0,sizeof(copied));copied.self=&copied;lifeWorkspace=copied;expected=UMICOM_DISK_BAD_STATE;}
    else if(!strcmp(name,"dirty_workspace")) {lifeWorkspace.path[0]='x';expected=UMICOM_DISK_BAD_STATE;}
    else if(!strcmp(name,"unterminated_path")) {memset(path,'x',sizeof(path));expected=UMICOM_DISK_LIMIT;}
    else CHECK(0);
    memcpy(&volumeBefore,&volume,sizeof(volume));memcpy(&workspaceBefore,&workspace,sizeof(workspace));
    memcpy(&savedScratch,&lifeWorkspace,sizeof(lifeWorkspace));memcpy(&savedPlan,&lifePlan,sizeof(lifePlan));
    CHECK(UmicomKernelFat16PlanLifecycle(v,request,base,scratch,out)==expected);CHECK(!reads);
    CHECK(!memcmp(&volume,&volumeBefore,sizeof(volume)) && !memcmp(&workspace,&workspaceBefore,sizeof(workspace)));
    CHECK(!memcmp(&lifeWorkspace,&savedScratch,sizeof(lifeWorkspace)) && !memcmp(&lifePlan,&savedPlan,sizeof(lifePlan)));
}
static void LifeFailures(void)
{
    memcpy(mediumBefore,media,sizeof(media));LifeStart();CHECK(LifeCall()==UMICOM_DISK_OK);const UmicomSize count=reads;
    for(UmicomSize i=1U;i<=count;++i) {
        LifeStart();failAt=i;CHECK(LifeCall()==UMICOM_DISK_IO_ERROR);CHECK(reads==i);
        Filled(&lifePlan,sizeof(lifePlan),0xa5U);CHECK(!memcmp(media,mediumBefore,sizeof(media)));LifeScrubbed();
    }
    printf("lifecycle planner operation=%u read-failure positions=%llu\n",(unsigned)lifeRequest.operation,(unsigned long long)count);
}
/* The test dispatcher now selects successful parent growth explicitly.
 * Its former full-parent refusal dispatch is retained for engineering review. */
#if 0
int main(int argc,char **argv)
{
    CHECK(argc==3);Setup(argv[2]);const char *name=argv[1];UmicomKernelFat16LifecycleOperation operation=UMICOM_FAT16_LIFECYCLE_CREATE;
    if(!strncmp(name,"create.",7U)) name+=7U;
    else if(!strncmp(name,"append.",7U)) {operation=UMICOM_FAT16_LIFECYCLE_APPEND;name+=7U;}
    else if(!strncmp(name,"truncate.",9U)) {operation=UMICOM_FAT16_LIFECYCLE_TRUNCATE;name+=9U;}
    else if(!strncmp(name,"delete.",7U)) {operation=UMICOM_FAT16_LIFECYCLE_DELETE;name+=7U;}
    LifeDefaults(operation);
    if(!strncmp(name,"success.",8U)) LifeSuccess(name+8U);
    else if(!strncmp(name,"refusal.",8U)) LifeRefusal(name+8U);
    else if(!strncmp(name,"guard.",6U)) LifeGuard(name+6U);
    else if(!strcmp(name,"all_read_failures")) LifeFailures();
    else CHECK(0);
    printf("fat16-lifecycle.plan.%s: ok\n",argv[1]);return 0;
}
#endif

/* Direct API checks complement the transport and console journeys. */
#include "move_plan_cases.inc"
#include "write_plan_cases.inc"
int main(int argc,char **argv)
{
    CHECK(argc==3);Setup(argv[2]);
    if (!strncmp(argv[1], "write.", 6U)) {
        UmicomKernelFat16WritePlanCase(argv[1] + 6U);
        printf("fat16-lifecycle.plan.%s: ok\n", argv[1]); return 0;
    }
    if (!strncmp(argv[1], "move.", 5U)) {
        LifeMove(argv[1] + 5U); printf("fat16-lifecycle.plan.%s: ok\n", argv[1]); return 0;
    }
    const char *name=argv[1];UmicomKernelFat16LifecycleOperation operation=UMICOM_FAT16_LIFECYCLE_CREATE;
    if(!strncmp(name,"create.",7U)) name+=7U;
    else if(!strncmp(name,"append.",7U)) {operation=UMICOM_FAT16_LIFECYCLE_APPEND;name+=7U;}
    else if(!strncmp(name,"truncate.",9U)) {operation=UMICOM_FAT16_LIFECYCLE_TRUNCATE;name+=9U;}
    else if(!strncmp(name,"delete.",7U)) {operation=UMICOM_FAT16_LIFECYCLE_DELETE;name+=7U;}
    LifeDefaults(operation);
    if(!strcmp(name,"success.parent_growth")) LifeParentGrowth();
    else if(!strncmp(name,"success.",8U)) LifeSuccess(name+8U);
    else if(!strncmp(name,"refusal.",8U)) LifeRefusal(name+8U);
    else if(!strncmp(name,"guard.",6U)) LifeGuard(name+6U);
    else if(!strcmp(name,"all_read_failures")) LifeFailures();
    else CHECK(0);
    printf("fat16-lifecycle.plan.%s: ok\n",argv[1]);return 0;
}
