/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/fat16_lifecycle/write_cases.inc
 *
 * Real transport positional writes, using raw FAT words and complete media
 * comparisons for expected results. The existing failure model supplies every
 * read, verification, write, flush and cached/eager interruption position.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
static void UmicomKernelFat16WriteProfile(const char *name)
{
    if (!strcmp(name,"overwrite")) { lcRequest.offset=17U; lcRequest.bytes=97U; }
    else if (!strcmp(name,"beginning")) { lcRequest.offset=0U; lcRequest.bytes=1U; }
    else if (!strcmp(name,"fragmented")) { lcRequest.offset=511U; lcRequest.bytes=700U; }
    else if (!strcmp(name,"whole-existing")) { lcRequest.offset=0U; lcRequest.bytes=1300U; }
    else if (!strcmp(name,"eof-slack")) { lcRequest.offset=1300U; lcRequest.bytes=236U; }
    else if (!strcmp(name,"empty")) { LCFixture("empty"); lcRequest.offset=0U; lcRequest.bytes=1U; }
    else if (!strcmp(name,"maximum")) { lcRequest.offset=1299U; lcRequest.bytes=4096U; }
    else if (!strcmp(name,"preserved-fields")) {
        LCFixture("preserved_fields"); lcRequest.offset=17U; lcRequest.bytes=97U;
    } else if (!strcmp(name,"nested")) { LCFixture("nested"); lcRequest.offset=511U; }
    else if (!strcmp(name,"fat-crossing")) { LCFixture("fat_crossing"); lcRequest.bytes=1900U; }
    else if (!strcmp(name,"full-volume-overwrite")) {
        LCReserveBelow((UmicomU16)((UMICOM_DISK_FIXTURE_LENGTH-129U)+2U));
        lcRequest.offset=17U; lcRequest.bytes=97U;
    } else if (!strcmp(name,"append-ignores-offset")) {
        LCDefaults(UMICOM_FAT16_LIFECYCLE_APPEND); lcRequest.offset=~(UmicomU64)0U;
    } else CHECK(!strcmp(name,"extension") || !strcmp(name,"snapshot") ||
        !strcmp(name,"reentry") || !strcmp(name,"eager") || !strcmp(name,"policy-reentry"));
}

static void UmicomKernelFat16WriteBoundary(void)
{
    if (!lcResult.allocatedClusters || !lcResult.originalEntry.bytes) return;
    UmicomU16 last=lcResult.originalEntry.firstCluster;
    for (;;) {
        const UmicomU16 next=LCFat(commitInitial,last);
        if (next>=0xfff8U) break;
        CHECK(next>=2U); last=next;
    }
    /* All new-chain sectors precede the old-tail bridge in each FAT copy.
     * Check actual submitted sectors, independently of the plan's index. */
    for (UmicomSize copy=0U;copy<2U;++copy) {
        const UmicomU64 first=copy?COMMIT_MIRROR:COMMIT_PRIMARY;
        UmicomU64 latest=0U; UmicomSize writes=0U;
        for (UmicomSize i=lcBaseEvents;i<commitEventCount;++i) {
            if (commitEvents[i].command!=UMICOM_VIRTIO_REQUEST_WRITE) continue;
            if (++writes<=2U) continue;
            const UmicomU64 sector=commitEvents[i].sector;
            if (sector>=first && sector<first+UMICOM_DISK_FIXTURE_FAT_SECTORS) latest=sector;
        }
        CHECK(latest==first+last/256U);
    }
}

static void UmicomKernelFat16WriteSuccess(const char *name)
{
    UmicomKernelFat16WriteProfile(name); CommitRebase(); LCOracle(); LCOpen();
    const UmicomU64 expectedOffset=lcRequest.operation==UMICOM_FAT16_LIFECYCLE_APPEND?1300U:lcRequest.offset;
    const UmicomSize expectedBytes=lcRequest.bytes;
    lcSnapshot=!strcmp(name,"snapshot")?UMICOM_TRUE:UMICOM_FALSE;
    lcReentry=!strcmp(name,"reentry")?UMICOM_TRUE:UMICOM_FALSE;
    lcPolicyReentry=!strcmp(name,"policy-reentry")?UMICOM_TRUE:UMICOM_FALSE;
    if (lcSnapshot) lcStrict=UMICOM_FALSE;
    commitPersistWrites=!strcmp(name,"eager")?UMICOM_TRUE:UMICOM_FALSE;
    CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK); LCStageEvidence(); UmicomKernelFat16WriteBoundary();
    CHECK(lcResult.commit.offset==expectedOffset && lcResult.commit.requestedBytes==expectedBytes);
    CHECK(!lcResult.freedClusters);
    CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK); LCFinishEvidence(1U);
    if (lcReentry || lcPolicyReentry) CHECK(lcReentries>0U);
    lcReentry=UMICOM_FALSE; lcPolicyReentry=UMICOM_FALSE;
    if (!lcSnapshot) LCFreshRead();
    if (!strcmp(name,"maximum")) CHECK(lcResult.allocatedClusters==8U);
    if (!strcmp(name,"fat-crossing")) CHECK(lcResult.changedFatSectors==2U);
    LCClose();
}

static void UmicomKernelFat16WriteRefusal(const char *name)
{
    UmicomKernelDiskStatus expected=UMICOM_DISK_RANGE;
    UmicomBoolean beforeAdmission=UMICOM_FALSE;
    if (!strcmp(name,"hole")) lcRequest.offset=1301U;
    else if (!strcmp(name,"offset-overflow")) lcRequest.offset=~(UmicomU64)0U;
    else if (!strcmp(name,"size-overflow")) {
        LCLongPut(LCSector(UMICOM_DISK_FIXTURE_ROOT)+124U,0xffffffffU);
        lcRequest.offset=0xffffffffU; lcRequest.bytes=1U;
    } else if (!strcmp(name,"readonly")) {
        LCSector(UMICOM_DISK_FIXTURE_ROOT)[107U]|=1U; expected=UMICOM_DISK_READ_ONLY;
    } else if (!strcmp(name,"no-space")) {
        LCReserveBelow((UmicomU16)((UMICOM_DISK_FIXTURE_LENGTH-129U)+2U)); expected=UMICOM_DISK_NO_SPACE;
    } else if (!strcmp(name,"missing")) { memcpy(lcPath,"/MISSING.BIN",13U); expected=UMICOM_DISK_NOT_FOUND; }
    else if (!strcmp(name,"directory")) { memcpy(lcPath,"/DOCS",6U); expected=UMICOM_DISK_IS_DIRECTORY; }
    else {
        beforeAdmission=UMICOM_TRUE;
        if (!strcmp(name,"calendar")) lcRequest.time.day=30U;
        else if (!strcmp(name,"zero")) { lcRequest.bytes=0U; lcRequest.input=NULL; }
        else if (!strcmp(name,"too-many")) lcRequest.bytes=4097U;
        else if (!strcmp(name,"unused-size")) lcRequest.size=1U;
        else if (!strcmp(name,"null-input")) lcRequest.input=NULL;
        else CHECK(0);
    }
    CommitRebase(); LCOpen(); const UmicomU32 readsBefore=commitReads;
    UmicomKernelFat16LifecycleResult previous=lcOwner.lastResult;
    const UmicomKernelFat16UpdateStatus status=LCStage(); CHECK(status!=UMICOM_FAT16_UPDATE_OK);
    CHECK(lcOwner.commit.state==UMICOM_FAT16_COMMIT_READY && !lcOwner.committedOperations);
    if (beforeAdmission) {
        CHECK(status==UMICOM_FAT16_UPDATE_INVALID_ARGUMENT && commitReads==readsBefore);
        CommitEqual(&lcResult,&lcResultBefore,sizeof(lcResult));
        CommitEqual(&lcOwner.lastResult,&previous,sizeof(previous));
    } else {
        CHECK(lcResult.commit.diskStatus==expected && !lcResult.commit.mediaTouched && !lcResult.planned);
        CHECK(lcResult.commit.dataOutcome==UMICOM_FAT16_UPDATE_NOT_SUBMITTED);
    }
    CHECK(!commitWrites && !commitFlushes); CommitUnchanged(); LCClose();
}

static void UmicomKernelFat16WriteSequence(void)
{
    LCOpen(); const UmicomKernelBlockHandle lease=lcOwner.commit.updater.handle;
    const UmicomKernelFat16LifecycleOperation operations[]={UMICOM_FAT16_LIFECYCLE_WRITE,
        UMICOM_FAT16_LIFECYCLE_WRITE,UMICOM_FAT16_LIFECYCLE_TRUNCATE,
        UMICOM_FAT16_LIFECYCLE_WRITE,UMICOM_FAT16_LIFECYCLE_DELETE,UMICOM_FAT16_LIFECYCLE_CREATE};
    for (UmicomSize i=0U;i<sizeof(operations)/sizeof(operations[0]);++i) {
        if (i) CommitRebase();
        LCDefaults(operations[i]);
        if (i==0U) { lcRequest.offset=17U; lcRequest.bytes=97U; }
        if (i==2U) lcRequest.size=512U;
        if (i==3U) { lcRequest.offset=511U; lcRequest.bytes=1200U; }
        if (i==5U) { lcEntry=96U; lcNextEntry=128U; memcpy(lcPath,"/REUSE.BIN",11U); }
        LCOracle(); CHECK(LCStage()==UMICOM_FAT16_UPDATE_OK); LCStageEvidence();
        CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK); LCFinishEvidence(i+1U); LCFreshRead();
        CHECK(lcOwner.commit.updater.handle==lease && Allocated()==2U);
        if (i==5U) CHECK(lcResult.updatedEntry.firstCluster==4U);
    }
    LCClose();
}

static void UmicomKernelFat16WriteGeometry(const char *name)
{
    const UmicomBoolean maximum=!strcmp(name,"maximum-initialisation")?UMICOM_TRUE:UMICOM_FALSE;
    const UmicomU32 sectorsPerCluster=maximum?64U:2U;
    const UmicomU32 length=maximum?129U+4085U*64U:UMICOM_DISK_FIXTURE_LENGTH;
    UmicomU8 *const boot=LCSector(UMICOM_DISK_FIXTURE_FIRST); boot[13U]=(UmicomU8)sectorsPerCluster;
    CommitPut16(boot+19U,length<=65535U?(UmicomU16)length:0U); CommitPut32(boot+32U,length>65535U?length:0U);
    CommitPut32(commitVisible+446U+12U,length); model.capacity=UMICOM_DISK_FIXTURE_FIRST+length;
    for (UmicomSize copy=0U;copy<2U;++copy)
        memset(LCSector(COMMIT_PRIMARY+copy*UMICOM_DISK_FIXTURE_FAT_SECTORS)+4U,0,UMICOM_DISK_FIXTURE_FAT_SECTORS*512U-4U);
    memset(LCSector(UMICOM_DISK_FIXTURE_ROOT),0,32U*512U);
    LCEntryMake(LCSector(UMICOM_DISK_FIXTURE_ROOT),"EMPTY   TXT",0U,0U);
    memcpy(lcPath,"/EMPTY.TXT",11U); lcEntry=0U; lcNextEntry=32U;
    lcRequest.offset=0U; lcRequest.bytes=maximum?4096U:1U;
    /* The larger geometry has a sparse, untouched zero tail, just as the
     * existing geometry harness does. Every submitted sector remains backed. */
    CommitRebase(); LCOracle(); LCOpen();
    if (!strcmp(name,"suffix-before-submit")) { commitClockMutation=5U; lcStrict=UMICOM_FALSE; }
    else if (!strcmp(name,"suffix-write-failure")) { commitFailWrite=4U; lcStrict=UMICOM_FALSE; }
    const UmicomKernelFat16UpdateStatus status=LCStage();
    if (maximum || !strcmp(name,"two-sector-initialisation")) {
        CHECK(status==UMICOM_FAT16_UPDATE_OK); LCStageEvidence();
        CHECK(lcResult.plannedDataSectors==sectorsPerCluster && lcResult.allocatedClusters==1U);
        CHECK(LCFinish()==UMICOM_FAT16_UPDATE_OK); LCFinishEvidence(1U); LCFreshRead();
    } else {
        CHECK(status==UMICOM_FAT16_UPDATE_TRANSPORT_ERROR && lcResult.commit.confirmedBytes==1U);
        CHECK(lcResult.commit.completedDataSectors==1U && !lcResult.commit.dataDurable && !lcResult.commit.dataVerified);
        CHECK(lcResult.commit.dataOutcome==(!strcmp(name,"suffix-before-submit")?
            UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED:UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED));
        CHECK(lcResult.commit.submittedDataSectors==(!strcmp(name,"suffix-before-submit")?1U:2U)); LCFailed();
    }
    for (UmicomSize i=0U;i<commitEventCount;++i)
        if (commitEvents[i].command!=UMICOM_VIRTIO_REQUEST_FLUSH) CHECK(commitEvents[i].sector<UMICOM_DISK_FIXTURE_SECTORS);
    LCClose();
}

static void UmicomKernelFat16WriteCase(const char *fixture,const char *name)
{
    LCStart(fixture,UMICOM_FAT16_LIFECYCLE_WRITE);
    CHECK(!strcmp(UmicomKernelFat16LifecycleOperationName(UMICOM_FAT16_LIFECYCLE_WRITE),"write"));
    if (!strncmp(name,"success.",8U)) UmicomKernelFat16WriteSuccess(name+8U);
    else if (!strncmp(name,"refusal.",8U)) UmicomKernelFat16WriteRefusal(name+8U);
    else if (!strncmp(name,"fault.",6U)) LCAllFailures(fixture,name+6U,UMICOM_FAT16_LIFECYCLE_WRITE);
    else if (!strcmp(name,"cuts.cached") || !strcmp(name,"cuts.eager"))
        LCCuts(fixture,UMICOM_FAT16_LIFECYCLE_WRITE,!strcmp(name,"cuts.eager")?UMICOM_TRUE:UMICOM_FALSE);
    else if (!strncmp(name,"guard.",6U)) LCGuard(name+6U,UMICOM_FALSE);
    else if (!strncmp(name,"finish-guard.",13U)) LCGuard(name+13U,UMICOM_TRUE);
    else if (!strncmp(name,"clock.",6U)) LCClocks(fixture,name+6U,UMICOM_FAT16_LIFECYCLE_WRITE);
    else if (!strncmp(name,"geometry.",9U)) UmicomKernelFat16WriteGeometry(name+9U);
    else if (!strcmp(name,"sequence")) UmicomKernelFat16WriteSequence();
    else CHECK(0);
}
