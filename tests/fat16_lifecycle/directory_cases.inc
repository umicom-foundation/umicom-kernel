/* Umicom Kernel directory lifecycle qualification.
 * The raw-media oracle below chooses free clusters and constructs FAT directory
 * records independently of the production planner. Every successful operation
 * compares the entire backing image, including unrelated files and free space.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */

static UmicomU16 directoryChild, directoryExtension;
static UmicomU16 directoryParent, directoryTail;
static UmicomU64 directoryRecordSector;
static UmicomSize directoryRecordOffset;
static UmicomU32 directoryOpenReads;

/* Calendar constants are independently encoded for 2044-02-29 23:58:57.
 * FAT stores even seconds, so the expected time represents 23:58:56. */
static void DirectoryRecord(UmicomU8 *record, const char *name, UmicomU16 cluster)
{
    memset(record, 0, 32U);
    memcpy(record, name, 11U);
    record[11] = 0x10U;
    CommitPut16(record + 14U, 0xbf5cU);
    CommitPut16(record + 16U, 0x805dU);
    CommitPut16(record + 18U, 0x805dU);
    CommitPut16(record + 22U, 0xbf5cU);
    CommitPut16(record + 24U, 0x805dU);
    CommitPut16(record + 26U, cluster);
}
static UmicomU8 *DirectoryImageCluster(UmicomU8 *image, UmicomU16 cluster)
{
    CHECK(cluster >= 2U);
    return image + ((UmicomSize)UMICOM_DISK_FIXTURE_DATA + cluster - 2U) * 512U;
}
static UmicomU16 DirectoryFree(const UmicomU8 *image)
{
    for (UmicomU16 cluster = 2U; cluster < 12000U; ++cluster)
        if (!LCFat(image, cluster)) return cluster;
    CHECK(0);
    return 0U;
}
static void DirectoryRequest(UmicomKernelFat16LifecycleOperation operation, const char *name)
{
    memset(&lcRequest, 0, sizeof(lcRequest));
    CHECK(strlen(name) < sizeof(lcPath));
    strcpy(lcPath, name);
    lcRequest.operation = operation;
    lcRequest.path = lcPath;
    if (operation != UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY &&
        operation != UMICOM_FAT16_LIFECYCLE_DELETE)
        lcRequest.time = (UmicomKernelFat16FileTime){2044U,2U,29U,23U,58U,57U};
}

/* The fixture constructor never calls the directory implementation to prepare
 * the state being tested. Removal therefore cannot pass merely because mkdir
 * and rmdir happen to agree on the same incorrect directory representation. */
static void DirectoryPrepare(const char *fixture, const char *profile)
{
    LCStart(fixture, UMICOM_FAT16_LIFECYCLE_CREATE);
    lcStrict = UMICOM_FALSE; /* A dedicated directory ordering oracle is below. */
    directoryParent = 0U;
    directoryTail = 0U;
    directoryRecordSector = UMICOM_DISK_FIXTURE_ROOT;
    directoryRecordOffset = 160U;
    directoryExtension = 0U;
    const UmicomBoolean nested = strstr(profile, "nested") || strstr(profile, "growth") ? UMICOM_TRUE : UMICOM_FALSE;
    const UmicomBoolean remove = strstr(profile, "remove") ? UMICOM_TRUE : UMICOM_FALSE;
    if (nested) {
        directoryParent = 3U;
        directoryTail = 3U;
        directoryRecordSector = UMICOM_DISK_FIXTURE_DATA + 1U;
        directoryRecordOffset = 96U;
    }
    if (strstr(profile, "growth")) {
        for (UmicomSize i = 3U; i < 16U; ++i) {
            char alias[12];
            CHECK(snprintf(alias, sizeof(alias), "D%07uTXT", (unsigned)i) == 11);
            LCEntryMake(LCCluster(3U) + i * 32U, alias, 0U, 0U);
        }
    }
    if (strstr(profile, "fragmented")) {
        /* A tail in a later FAT sector exercises the separate bridge phase. */
        directoryTail = 300U;
        CommitFat(3U, directoryTail);
        CommitFat(directoryTail, 0xffffU);
        memset(LCCluster(directoryTail), 0, 512U);
        for (UmicomSize i = 0U; i < 16U; ++i) {
            char alias[12];
            CHECK(snprintf(alias, sizeof(alias), "T%07uTXT", (unsigned)i) == 11);
            LCEntryMake(LCCluster(directoryTail) + i * 32U, alias, 0U, 0U);
        }
    }
    directoryChild = DirectoryFree(commitVisible);
    if (remove) {
        CommitFat(directoryChild, 0xffffU);
        memset(LCCluster(directoryChild), 0, 512U);
        DirectoryRecord(LCCluster(directoryChild), ".          ", directoryChild);
        DirectoryRecord(LCCluster(directoryChild) + 32U, "..         ", directoryParent);
        DirectoryRecord(LCSector(directoryRecordSector) + directoryRecordOffset, "WORK       ", directoryChild);
        if (strstr(profile, "chain")) {
            const UmicomU16 tail = DirectoryFree(commitVisible);
            CommitFat(directoryChild, tail);
            CommitFat(tail, 0xffffU);
            memset(LCCluster(tail), 0, 512U);
            /* Deleted entries do not make an otherwise empty directory live. */
            for (UmicomSize i = 2U; i < 16U; ++i) LCCluster(directoryChild)[i * 32U] = 0xe5U;
        }
    }
    DirectoryRequest(remove ? UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY : UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY,
        nested ? "/DOCS/WORK" : "/WORK");
    CommitRebase();
}

/* Construct the complete expected medium without consulting production plans. */
static void DirectoryOracle(const char *profile)
{
    memcpy(lcExpected, commitVisible, sizeof(lcExpected));
    if (lcRequest.operation == UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY) {
        lcExpected[(UmicomSize)directoryRecordSector * 512U + directoryRecordOffset] = 0xe5U;
        UmicomU16 cluster = directoryChild;
        while (cluster < 0xfff8U) {
            const UmicomU16 next = LCFat(commitVisible, cluster);
            LCFatPut(lcExpected, cluster, 0U);
            cluster = next;
        }
        return;
    }
    LCFatPut(lcExpected, directoryChild, 0xffffU);
    UmicomU8 *const child = DirectoryImageCluster(lcExpected, directoryChild);
    memset(child, 0, 512U);
    DirectoryRecord(child, ".          ", directoryChild);
    DirectoryRecord(child + 32U, "..         ", directoryParent);
    if (strstr(profile, "growth")) {
        directoryExtension = DirectoryFree(lcExpected);
        LCFatPut(lcExpected, directoryTail, directoryExtension);
        LCFatPut(lcExpected, directoryExtension, 0xffffU);
        memset(DirectoryImageCluster(lcExpected, directoryExtension), 0, 512U);
        directoryRecordSector = UMICOM_DISK_FIXTURE_DATA + directoryExtension - 2U;
        directoryRecordOffset = 0U;
    }
    DirectoryRecord(lcExpected + (UmicomSize)directoryRecordSector * 512U + directoryRecordOffset,
        "WORK       ", directoryChild);
}

/* Observe ordering at the device boundary. Allocation words must never expose
 * uninitialized directory storage. Conversely, removal must persist the parent
 * tombstone before either FAT copy reclaims the old directory chain. */
static void DirectoryWriteRegister(void *context, UmicomAddress address, UmicomU32 value)
{
    if (Offset(address) == UMICOM_VIRTIO_QUEUE_NOTIFY) {
        const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
        const UmicomU32 command = *(const UmicomU32 *)header;
        const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
        if (command == UMICOM_VIRTIO_REQUEST_WRITE && commitWrites >= 2U && LCFatSector(sector)) {
            if (lcRequest.operation == UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY) {
                CHECK(commitDurable[(UmicomSize)directoryRecordSector * 512U + directoryRecordOffset] == 0xe5U);
            } else {
                CommitEqual(DirectoryImageCluster(commitDurable, directoryChild),
                    DirectoryImageCluster(lcExpected, directoryChild), 512U);
                if (directoryExtension)
                    CommitEqual(DirectoryImageCluster(commitDurable, directoryExtension),
                        DirectoryImageCluster(lcExpected, directoryExtension), 512U);
            }
        }
    }
    LCWriteRegister(context, address, value);
}
static void DirectorySuccess(const char *fixture, const char *profile)
{
    DirectoryPrepare(fixture, profile);
    DirectoryOracle(profile);
    domain.operations.write32 = DirectoryWriteRegister;
    LCOpen();
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.planned && !lcResult.commit.commitAccepted);
    CHECK(lcOwner.plan.parentGrown == (directoryExtension ? UMICOM_TRUE : UMICOM_FALSE));
    CHECK(lcResult.commit.requestedBytes == 0U && lcResult.commit.confirmedBytes == 0U);
    CHECK(!(CommitFlags(commitDurable, COMMIT_PRIMARY) & UMICOM_FAT16_CLEAN_MASK));
    CHECK(LCFinish() == UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.commit.commitAccepted && lcOwner.committedOperations == 1U);
    CommitEqual(commitVisible, lcExpected, sizeof(lcExpected));
    CommitEqual(commitDurable, lcExpected, sizeof(lcExpected));
    LCClose();
}

/* Reinitialize the inspector for each read, so the sequence tests exercise the
 * durable namespace rather than cached entries left behind by the updater. */
/* Read the durable device model directly, not the updater's visible cache. */
static UmicomBoolean DirectoryDurableRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    CHECK(context == commitDurable && sector < UMICOM_DISK_FIXTURE_SECTORS && output);
    memcpy(output, commitDurable + (UmicomSize)sector * 512U, 512U);
    return UMICOM_TRUE;
}
static void DirectoryReadBack(const char *name, UmicomBoolean exists, UmicomBoolean directory)
{
    UmicomKernelFat16Entry entry;
    UmicomKernelFat16 fileInspect = {0};
    const UmicomKernelDiskReader reader = {model.capacity, DirectoryDurableRead, commitDurable};
    CHECK(UmicomKernelFat16Open(&fileInspect, &reader, 0U) == UMICOM_DISK_OK);
    const UmicomKernelDiskStatus status = UmicomKernelFat16Stat(&fileInspect, name, &entry);
    CHECK(status == (exists ? UMICOM_DISK_OK : UMICOM_DISK_NOT_FOUND));
    if (exists) CHECK(entry.directory == directory);
    CHECK(UmicomKernelFat16Close(&fileInspect) == UMICOM_DISK_OK);
}
static void DirectorySequence(const char *fixture)
{
    DirectoryPrepare(fixture, "mkdir.root");
    LCOpen();
    const UmicomKernelFat16LifecycleOperation operations[] = {
        UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY, UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY,
        UMICOM_FAT16_LIFECYCLE_CREATE, UMICOM_FAT16_LIFECYCLE_APPEND,
        UMICOM_FAT16_LIFECYCLE_DELETE, UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY,
        UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY, UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY
    };
    const char *const paths[] = {"/WORK", "/WORK/SUB", "/WORK/SUB/NOTE.TXT", "/WORK/SUB/NOTE.TXT",
        "/WORK/SUB/NOTE.TXT", "/WORK/SUB", "/WORK", "/AGAIN"};
    UmicomU16 original = 0U;
    for (UmicomSize i = 0U; i < sizeof(operations) / sizeof(operations[0]); ++i) {
        DirectoryRequest(operations[i], paths[i]);
        if (i == 2U || i == 3U) { lcRequest.input = i == 2U ? "hello" : " world"; lcRequest.bytes = i == 2U ? 5U : 6U; }
        CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK);
        if (!i) original = lcResult.updatedEntry.firstCluster;
        if (i == 7U) CHECK(lcResult.updatedEntry.firstCluster == original);
        CHECK(LCFinish() == UMICOM_FAT16_UPDATE_OK);
        CHECK(lcOwner.committedOperations == i + 1U);
        DirectoryReadBack(paths[i], i < 4U || i == 7U, i < 2U || i >= 5U);
        if (i == 3U) {
            /* The raw allocation also proves append preserved the original prefix. */
            CHECK(!memcmp(LCCluster(lcResult.updatedEntry.firstCluster), "hello world", 11U));
        }
    }
    LCClose();
}

/* Each failure position is rerun from the same independent image. Failures
 * after a submitted write retire the owner; no test treats Close as rollback. */
static void DirectoryFaults(const char *fixture, const char *profile, const char *fault)
{
    DirectoryPrepare(fixture, profile); DirectoryOracle(profile); LCOpen();
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
        DirectoryPrepare(fixture, profile); DirectoryOracle(profile); LCOpen();
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
    printf("directory profile=%s injected=%s positions=%u\n", profile, fault, count);
}
static void DirectoryRefusal(const char *fixture, const char *reason)
{
    DirectoryPrepare(fixture, "remove.root");
    UmicomKernelDiskStatus disk = UMICOM_DISK_CORRUPT;
    if (!strcmp(reason, "nonempty")) {
        LCEntryMake(LCCluster(directoryChild) + 64U, "CHILD   TXT", 0U, 0U);
        disk = UMICOM_DISK_NOT_EMPTY;
    } else if (!strcmp(reason, "root")) { strcpy(lcPath, "/"); disk = UMICOM_DISK_IS_DIRECTORY; }
    else if (!strcmp(reason, "file")) { strcpy(lcPath, "/FRAG.BIN"); disk = UMICOM_DISK_NOT_DIRECTORY; }
    else if (!strcmp(reason, "readonly")) {
        LCSector(directoryRecordSector)[directoryRecordOffset + 11U] |= 1U;
        disk = UMICOM_DISK_READ_ONLY;
    } else if (!strcmp(reason, "bad-dot")) CommitPut16(LCCluster(directoryChild) + 26U, 3U);
    else if (!strcmp(reason, "bad-parent")) CommitPut16(LCCluster(directoryChild) + 58U, 3U);
    else if (!strcmp(reason, "missing")) { strcpy(lcPath, "/ABSENT"); disk = UMICOM_DISK_NOT_FOUND; }
    else CHECK(0);
    CommitRebase(); LCOpen();
    CHECK(LCStage() != UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.commit.diskStatus == disk && !lcResult.commit.mediaTouched);
    CommitUnchanged(); LCClose();
}
static void DirectoryConsole(const char *fixture)
{
    DirectoryPrepare(fixture, "mkdir.root");
    memset(&commitShell, 0, sizeof(commitShell));
    commitShell.output = LCConsoleOutput;
    CHECK(FileConsoleCommand(&commitShell, "fatfsopen 0 0") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatmkdir /WORK") == UMICOM_SHELL_IO_ERROR);
    CHECK(!commitWrites);
    CHECK(FileConsoleCommand(&commitShell, "fatfstime 2044-02-29T23:58:57") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatmkdir /WORK") == UMICOM_SHELL_OK);
    const UmicomU32 writes = commitWrites;
    CHECK(FileConsoleCommand(&commitShell, "fatmkdir /WORK/SUB") == UMICOM_SHELL_IO_ERROR);
    CHECK(commitWrites == writes);
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatmkdir /WORK/SUB") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatrmdir /WORK") == UMICOM_SHELL_IO_ERROR);
    CommitClearTranscript();
    CHECK(FileConsoleCommand(&commitShell, "fatrmdir /WORK/SUB") == UMICOM_SHELL_OK);
    CHECK(!strstr(transcript, "requested-time="));
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatrmdir /WORK") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfscommit") == UMICOM_SHELL_OK);
    CHECK(FileConsoleCommand(&commitShell, "fatfsclose") == UMICOM_SHELL_OK);
    CHECK(!Allocated());
}
#include "directory_limits.inc"
#include "query_cases.inc"
#include "move_cases.inc"

static int DirectoryCase(const char *fixture, const char *name)
{
    if (!strncmp(name, "move.", 5U)) MoveCase(fixture, name + 5U);
    else if (!strncmp(name, "query.", 6U)) QueryCase(fixture, name + 6U);
    else if (!strncmp(name, "geometry.", 9U)) DirectoryGeometry(fixture, name + 9U);
    else if (!strncmp(name, "capacity.", 9U)) DirectoryCapacity(fixture, name + 9U);
    else if (!strncmp(name, "arguments.", 10U)) DirectoryArguments(fixture, name + 10U);
    else if (!strncmp(name, "collision.", 10U)) DirectoryCollision(fixture, name + 10U);
    else if (!strcmp(name, "sequence")) DirectorySequence(fixture);
    else if (!strcmp(name, "console")) DirectoryConsole(fixture);
    else if (!strncmp(name, "refusal.", 8U)) DirectoryRefusal(fixture, name + 8U);
    else {
        const char *fault = strstr(name, ".fault.");
        if (fault) {
            char profile[40];
            const UmicomSize length = (UmicomSize)(fault - name);
            CHECK(length < sizeof(profile));
            memcpy(profile, name, length); profile[length] = 0;
            DirectoryFaults(fixture, profile, fault + 7U);
        } else DirectorySuccess(fixture, name);
    }
    printf("fat16-directory.%s: ok\n", name);
    return 0;
}
