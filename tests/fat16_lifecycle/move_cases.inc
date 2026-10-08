/* Umicom Kernel move qualification.
 * Build the expected complete medium from known raw records, independently of
 * the production planner. Metadata success must never hide payload changes.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
static char moveDestination[UMICOM_FAT16_PATH_BYTES];
static UmicomU64 moveSourceSector, moveDestinationSector;
static UmicomSize moveSourceOffset, moveDestinationOffset;
static UmicomU16 moveChild, moveParent;
static UmicomBoolean moveSameParent;
static UmicomBoolean moveSnapshot;
static UmicomU32 moveCallbacks;

static void MovePrepare(const char *fixture, const char *profile)
{
    DirectoryPrepare(fixture, "remove.root");
    moveChild = directoryChild;
    moveParent = 3U;
    moveSourceSector = UMICOM_DISK_FIXTURE_ROOT;
    moveSourceOffset = 160U;
    moveDestinationSector = UMICOM_DISK_FIXTURE_DATA + 1U;
    moveDestinationOffset = 96U;
    moveSameParent = UMICOM_FALSE;
    moveSnapshot = UMICOM_FALSE; moveCallbacks = 0U;
    memset(&lcRequest, 0, sizeof(lcRequest));
    lcRequest.operation = UMICOM_FAT16_LIFECYCLE_MOVE;
    lcRequest.path = lcPath; lcRequest.destination = moveDestination;
    strcpy(lcPath, "/WORK"); strcpy(moveDestination, "/DOCS/MOVED");
    /* A live child proves nonempty subtrees survive moves and fresh reads. */
    LCEntryMake(LCCluster(moveChild) + 64U, "CHILD   TXT", 0U, 0U);
    if (!strcmp(profile, "rename-directory")) {
        strcpy(moveDestination, "/RENAMED"); moveSameParent = UMICOM_TRUE;
    } else if (!strcmp(profile, "rename-file") || !strcmp(profile, "move-file") ||
               !strcmp(profile, "move-empty")) {
        moveChild = 0U;
        moveSourceOffset = !strcmp(profile, "move-empty") ? 128U : 96U;
        strcpy(lcPath, !strcmp(profile, "move-empty") ? "/EMPTY.TXT" : "/FRAG.BIN");
        strcpy(moveDestination, !strcmp(profile, "rename-file") ? "/RENAMED.BIN" : "/DOCS/MOVED.BIN");
        moveSameParent = !strcmp(profile, "rename-file") ? UMICOM_TRUE : UMICOM_FALSE;
    } else if (!strcmp(profile, "to-root")) {
        /* Place WORK below DOCS directly; the operation itself must not be used
         * to prepare the starting media of its own test. */
        memcpy(LCCluster(3U) + 96U, LCSector(UMICOM_DISK_FIXTURE_ROOT) + 160U, 32U);
        LCSector(UMICOM_DISK_FIXTURE_ROOT)[160U] = 0U;
        CommitPut16(LCCluster(moveChild) + 58U, 3U);
        moveSourceSector = UMICOM_DISK_FIXTURE_DATA + 1U;
        moveSourceOffset = 96U;
        moveDestinationSector = UMICOM_DISK_FIXTURE_ROOT; moveDestinationOffset = 160U;
        moveParent = 0U;
        strcpy(lcPath, "/DOCS/WORK"); strcpy(moveDestination, "/MOVED");
    } else if (!strcmp(profile, "boundary")) {
        /* End-of-sector publication must clear an otherwise hidden following
         * record before the destination entry can make that record visible. */
        CommitFat(3U, 300U); CommitFat(300U, 0xffffU);
        memset(LCCluster(300U), 0, 512U);
        for (UmicomSize i = 3U; i < 15U; ++i) {
            char name[12];
            CHECK(snprintf(name, sizeof(name), "D%07uTXT", (unsigned)i) == 11);
            LCEntryMake(LCCluster(3U) + i * 32U, name, 0U, 0U);
        }
        LCCluster(300U)[0] = 'X';
        moveDestinationOffset = 480U;
    } else CHECK(!strcmp(profile, "move-directory"));
    /* Nontrivial raw fields distinguish preservation from regenerated metadata. */
    UmicomU8 *const record = LCSector(moveSourceSector) + moveSourceOffset;
    record[12] = 0x18U; record[13] = 101U;
    CommitPut16(record + 14U, 0x4567U); CommitPut16(record + 16U, 0x805dU);
    CommitPut16(record + 18U, 0x805cU);
    CommitRebase();
    memcpy(lcExpected, commitVisible, sizeof(lcExpected));
    UmicomU8 *const expected = lcExpected +
        (UmicomSize)(moveSameParent ? moveSourceSector : moveDestinationSector) * 512U +
        (moveSameParent ? moveSourceOffset : moveDestinationOffset);
    memcpy(expected, record, 32U);
    memcpy(expected, moveChild ? (moveSameParent ? "RENAMED    " : "MOVED      ") :
        (moveSameParent ? "RENAMED BIN" : "MOVED   BIN"), 11U);
    expected[12] = 0U;
    if (!moveSameParent) {
        lcExpected[(UmicomSize)moveSourceSector * 512U + moveSourceOffset] = 0xe5U;
        if (moveChild) CommitPut16(DirectoryImageCluster(lcExpected, moveChild) + 58U, moveParent);
        if (!strcmp(profile, "boundary")) DirectoryImageCluster(lcExpected, 300U)[0] = 0U;
    }
}

/* Observe the device boundary independently of plan order. A destination cannot
 * become durable until the old name is tombstoned and dot-parent is correct. */
static void MoveWriteRegister(void *context, UmicomAddress address, UmicomU32 value)
{
    if (Offset(address) == UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU32 command = *(const UmicomU32 *)header;
        const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
        if (command == UMICOM_VIRTIO_REQUEST_WRITE && !moveSameParent &&
            sector == moveDestinationSector) {
            CHECK(commitDurable[(UmicomSize)moveSourceSector * 512U + moveSourceOffset] == 0xe5U);
            if (moveChild) CHECK(LCWord(DirectoryImageCluster(commitDurable, moveChild) + 58U) == moveParent);
        }
        if (moveSnapshot && command == UMICOM_VIRTIO_REQUEST_READ) {
            /* The owner must already have copied both paths. */
            strcpy(lcPath, "/CALLER"); strcpy(moveDestination, "/CHANGED");
            ++moveCallbacks;
        }
    }
    LCWriteRegister(context, address, value);
}
static void MoveSuccess(const char *fixture, const char *profile)
{
    MovePrepare(fixture, profile);
    domain.operations.write32 = MoveWriteRegister;
    LCOpen();
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.operation == UMICOM_FAT16_LIFECYCLE_MOVE && lcResult.planned);
    CHECK(!lcResult.allocatedClusters && !lcResult.freedClusters && !lcResult.plannedDataSectors);
    CHECK(!lcResult.changedFatSectors && !lcResult.submittedFatSectors);
    CHECK(lcResult.commit.dataOutcome == UMICOM_FAT16_UPDATE_NOT_SUBMITTED);
    CHECK(LCFinish() == UMICOM_FAT16_UPDATE_OK && lcOwner.committedOperations == 1U);
    CHECK(lcResult.submittedDirectorySectors == lcResult.plannedDirectorySectors);
    CommitEqual(commitVisible, lcExpected, sizeof(lcExpected));
    CommitEqual(commitDurable, lcExpected, sizeof(lcExpected));
    LCClose();
    DirectoryReadBack(lcPath, UMICOM_FALSE, UMICOM_FALSE);
    DirectoryReadBack(moveDestination, UMICOM_TRUE, moveChild ? UMICOM_TRUE : UMICOM_FALSE);
}

static void MoveRefusal(const char *fixture, const char *reason)
{
    MovePrepare(fixture, "move-directory");
    UmicomKernelDiskStatus expected = UMICOM_DISK_READ_ONLY;
    if (!strcmp(reason, "cycle")) {
        strcpy(moveDestination, "/WORK/INSIDE"); expected = UMICOM_DISK_CHAIN_CYCLE;
    } else if (!strcmp(reason, "root")) {
        strcpy(lcPath, "/"); expected = UMICOM_DISK_IS_DIRECTORY;
    } else if (!strcmp(reason, "collision")) {
        strcpy(moveDestination, "/DOCS"); expected = UMICOM_DISK_EXISTS;
    } else if (!strcmp(reason, "same-name")) {
        strcpy(moveDestination, "/work"); expected = UMICOM_DISK_EXISTS;
    } else if (!strcmp(reason, "missing-parent")) {
        strcpy(moveDestination, "/ABSENT/NEW"); expected = UMICOM_DISK_NOT_FOUND;
    } else if (!strcmp(reason, "readonly-source")) {
        LCSector(moveSourceSector)[moveSourceOffset + 11U] |= 1U;
    } else if (!strcmp(reason, "readonly-destination")) {
        LCSector(UMICOM_DISK_FIXTURE_ROOT)[64U + 11U] |= 1U;
    } else if (!strcmp(reason, "readonly-parent")) {
        MovePrepare(fixture, "to-root");
        LCSector(UMICOM_DISK_FIXTURE_ROOT)[64U + 11U] |= 1U;
    } else if (!strcmp(reason, "full")) {
        for (UmicomSize i = 3U; i < 16U; ++i) {
            char name[12];
            CHECK(snprintf(name, sizeof(name), "D%07uTXT", (unsigned)i) == 11);
            LCEntryMake(LCCluster(3U) + i * 32U, name, 0U, 0U);
        }
        expected = UMICOM_DISK_NO_SPACE;
    } else CHECK(0);
    CommitRebase(); LCOpen();
    CHECK(LCStage() != UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.commit.diskStatus == expected && !lcResult.commit.mediaTouched);
    CommitUnchanged(); LCClose();
}

static void MoveGuard(const char *fixture, const char *reason)
{
    MovePrepare(fixture, "move-directory"); LCOpen();
    if (!strcmp(reason, "null")) lcRequest.destination = NULL;
    else if (!strcmp(reason, "owner")) lcRequest.destination = (const char *)&lcOwner;
    else if (!strcmp(reason, "output")) lcRequest.destination = (const char *)&lcResult;
    else if (!strcmp(reason, "request")) lcRequest.destination = (const char *)&lcRequest;
    else if (!strcmp(reason, "source")) lcRequest.destination = lcRequest.path;
    else if (!strcmp(reason, "domain")) lcRequest.destination = (const char *)&domain;
    else if (!strcmp(reason, "unterminated")) memset(moveDestination, 'x', sizeof(moveDestination));
    else if (!strcmp(reason, "calendar")) lcRequest.time.year = 2044U;
    else if (!strcmp(reason, "payload")) { lcRequest.input = "x"; lcRequest.bytes = 1U; }
    else CHECK(0);
    const UmicomU32 events = commitEventCount;
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    CommitEqual(&lcResult, &lcResultBefore, sizeof(lcResult));
    CHECK(commitEventCount == events);
    CommitUnchanged(); LCClose();
}

static void MoveConsole(const char *fixture)
{
    MovePrepare(fixture, "move-directory");
    memset(&commitShell, 0, sizeof(commitShell)); commitShell.output = LCConsoleOutput;
    CHECK(FileConsoleCommand(&commitShell, "fatfsopen 0 0") == UMICOM_SHELL_OK);
    /* Rename needs no new calendar; it preserves the persisted one. */
    CHECK(FileConsoleCommand(&commitShell, "fatmove /WORK /DOCS/MOVED") == UMICOM_SHELL_OK);
    CHECK(!strstr(transcript, "requested-time="));
    CHECK(FileConsoleCommand(&commitShell, "fatmove /FRAG.BIN /MOVED.BIN") == UMICOM_SHELL_IO_ERROR);
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfsls /DOCS/MOVED") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfsclose") == UMICOM_SHELL_OK);
    CommitEqual(commitDurable, lcExpected, sizeof(lcExpected)); CHECK(!Allocated());
}

/* Enumerate every observed read, partial write, flush, altered/dropped write
 * and protocol interruption in both cached and eager persistence models. */
static void MoveFaults(const char *fixture, const char *profile, const char *fault)
{
    MovePrepare(fixture, profile); domain.operations.write32 = MoveWriteRegister; LCOpen();
    directoryOpenReads = commitReads;
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK); CHECK(LCFinish() == UMICOM_FAT16_UPDATE_OK);
    const UmicomU32 readsTotal = commitReads, writesTotal = commitWrites, flushesTotal = commitFlushes;
    const UmicomU32 mutationsTotal = commitMutations;
    LCClose();
    const UmicomBoolean readFault = !strcmp(fault, "read");
    const UmicomBoolean flushFault = !strcmp(fault, "flush");
    const UmicomBoolean cut = !strcmp(fault, "cut-cached") || !strcmp(fault, "cut-eager");
    const UmicomU32 count = cut ? mutationsTotal : readFault ? readsTotal : flushFault ? flushesTotal : writesTotal;
    for (UmicomU32 at = readFault ? directoryOpenReads + 1U : 1U; at <= count; ++at) {
        MovePrepare(fixture, profile); domain.operations.write32 = MoveWriteRegister; LCOpen();
        if (readFault) commitFailRead = at;
        else if (flushFault) commitFailFlush = at;
        else if (cut) { commitCutMutation = at; commitPersistWrites = !strcmp(fault, "cut-eager") ? UMICOM_TRUE : UMICOM_FALSE; }
        else if (!strcmp(fault, "drop")) commitDropWrite = at;
        else if (!strcmp(fault, "alter")) commitAlterWrite = at;
        else { CHECK(!strcmp(fault, "write")); commitFailWrite = at; commitPartialWrite = UMICOM_TRUE; }
        UmicomKernelFat16UpdateStatus status = LCStage();
        if (status == UMICOM_FAT16_UPDATE_OK) status = LCFinish();
        CHECK(status != UMICOM_FAT16_UPDATE_OK);
        CHECK(!lcOwner.committedOperations && !lcResult.commit.commitAccepted);
        if (lcResult.commit.mediaTouched) LCFailed();
        else CommitUnchanged();
        if (cut) {
            CommitReplay(UMICOM_FALSE); CommitEqual(commitVisible, commitExpected, sizeof(commitVisible));
            CommitReplay(UMICOM_TRUE); CommitEqual(commitDurable, commitExpected, sizeof(commitDurable));
            const UmicomBoolean clean = (CommitFlags(commitDurable, COMMIT_PRIMARY) & UMICOM_FAT16_CLEAN_MASK) &&
                (CommitFlags(commitDurable, COMMIT_MIRROR) & UMICOM_FAT16_CLEAN_MASK);
            if (clean) CHECK(!memcmp(commitDurable, commitInitial, sizeof(commitDurable)) ||
                !memcmp(commitDurable, lcExpected, sizeof(commitDurable)));
            model.allowed = UMICOM_TRUE;
        }
        LCClose();
    }
    printf("move profile=%s injected=%s positions=%u\n", profile, fault, count);
}

static void MoveDepth(const char *fixture, const char *reason)
{
    MovePrepare(fixture, "move-directory");
    UmicomU16 parent = moveChild;
    const UmicomSize levels = !strcmp(reason, "empty") ? 7U : 6U;
    for (UmicomSize i = 0U; i < levels; ++i) {
        const UmicomU16 child = DirectoryFree(commitVisible);
        CommitFat(child, 0xffffU); memset(LCCluster(child), 0, 512U);
        DirectoryRecord(LCCluster(child), ".          ", child);
        DirectoryRecord(LCCluster(child) + 32U, "..         ", parent);
        DirectoryRecord(LCCluster(parent) + 64U, "NEXT       ", child);
        parent = child;
    }
    if (!strcmp(reason, "leaf")) LCEntryMake(LCCluster(parent) + 64U, "LEAF    TXT", 0U, 0U);
    else CHECK(!strcmp(reason, "empty"));
    CommitRebase(); LCOpen();
    CHECK(LCStage() != UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.commit.diskStatus == UMICOM_DISK_LIMIT && !lcResult.commit.mediaTouched);
    CommitUnchanged(); LCClose();
}
static void MoveSnapshot(const char *fixture)
{
    MovePrepare(fixture, "move-directory"); LCOpen();
    domain.operations.write32 = MoveWriteRegister; moveSnapshot = UMICOM_TRUE;
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK && LCFinish() == UMICOM_FAT16_UPDATE_OK);
    CHECK(moveCallbacks > 0U && !strcmp(lcResult.updatedEntry.name, "MOVED"));
    CommitEqual(commitDurable, lcExpected, sizeof(lcExpected)); LCClose();
}
/* Exhausted physical slots must not prevent a same-parent rename, because
 * the existing live entry can be updated without allocating another slot. */
static void MoveFullParent(const char *fixture)
{
    MovePrepare(fixture, "to-root");
    for (UmicomSize i = 4U; i < 16U; ++i) {
        char name[12];
        CHECK(snprintf(name, sizeof(name), "D%07uTXT", (unsigned)i) == 11);
        LCEntryMake(LCCluster(3U) + i * 32U, name, 0U, 0U);
    }
    strcpy(moveDestination, "/DOCS/RENAMED");
    CommitRebase(); memcpy(lcExpected, commitVisible, sizeof(lcExpected));
    memcpy(DirectoryImageCluster(lcExpected, 3U) + 96U, "RENAMED    ", 11U);
    DirectoryImageCluster(lcExpected, 3U)[108U] = 0U;
    LCOpen(); CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK && LCFinish() == UMICOM_FAT16_UPDATE_OK);
    CommitEqual(commitDurable, lcExpected, sizeof(lcExpected)); LCClose();
}
static void MoveCase(const char *fixture, const char *name)
{
    if (!strncmp(name, "refusal.", 8U)) MoveRefusal(fixture, name + 8U);
    else if (!strncmp(name, "guard.", 6U)) MoveGuard(fixture, name + 6U);
    else if (!strncmp(name, "depth.", 6U)) MoveDepth(fixture, name + 6U);
    else if (!strcmp(name, "console")) MoveConsole(fixture);
    else if (!strcmp(name, "snapshot")) MoveSnapshot(fixture);
    else if (!strcmp(name, "full-parent-rename")) MoveFullParent(fixture);
    else {
        const char *const fault = strstr(name, ".fault.");
        if (fault) {
            char profile[40];
            const UmicomSize length = (UmicomSize)(fault - name);
            CHECK(length < sizeof(profile));
            memcpy(profile, name, length); profile[length] = 0;
            MoveFaults(fixture, profile, fault + 7U);
        } else MoveSuccess(fixture, name);
    }
}
