/* Additional directory capacity and admission checks.
 * These cases construct filesystem geometry and malformed caller requests
 * without relying on a successful production mkdir to prepare the fixtures.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
static void DirectoryCapacity(const char *fixture, const char *name)
{
    DirectoryPrepare(fixture, !strcmp(name, "growth-allocation-bound") ? "mkdir.growth" : "mkdir.root");
    UmicomKernelDiskStatus expected = UMICOM_DISK_NO_SPACE;
    if (!strcmp(name, "fixed-root")) {
        /* Shrink the synthetic reserved root to one sector and move its data
         * area accordingly. The FAT16 root cannot gain an allocation chain. */
        const UmicomSize shift = 31U * 512U;
        const UmicomSize first = (UmicomSize)UMICOM_DISK_FIXTURE_DATA * 512U;
        const UmicomSize end = (UmicomSize)(UMICOM_DISK_FIXTURE_FIRST + UMICOM_DISK_FIXTURE_LENGTH) * 512U;
        memmove(commitVisible + first - shift, commitVisible + first, end - first);
        memset(commitVisible + end - shift, 0, shift);
        CommitPut16(LCSector(UMICOM_DISK_FIXTURE_FIRST) + 17U, 16U);
        for (UmicomSize i = 5U; i < 16U; ++i) {
            char alias[12];
            CHECK(snprintf(alias, sizeof(alias), "R%07uTXT", (unsigned)i) == 11);
            LCEntryMake(LCSector(UMICOM_DISK_FIXTURE_ROOT) + i * 32U, alias, 0U, 0U);
        }
    } else if (!strcmp(name, "growth-allocation-bound")) {
        /* Eight new file clusters exhaust this operation's allocation budget.
         * It must refuse rather than secretly allocate a ninth parent cluster. */
        DirectoryRequest(UMICOM_FAT16_LIFECYCLE_CREATE, "/DOCS/LARGE.BIN");
        lcRequest.input = lcInput;
        lcRequest.bytes = 4096U;
        expected = UMICOM_DISK_LIMIT;
    } else if (!strcmp(name, "no-free-cluster")) LCReserveBelow(12161U);
    else CHECK(0);
    CommitRebase(); LCOpen();
    CHECK(LCStage() != UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.commit.diskStatus == expected && !lcResult.commit.mediaTouched);
    CHECK(!commitWrites && !commitFlushes && lcOwner.commit.state == UMICOM_FAT16_COMMIT_READY);
    CommitUnchanged(); LCClose();
}
static void DirectoryArguments(const char *fixture, const char *name)
{
    const UmicomBoolean remove = !strncmp(name, "remove-", 7U);
    DirectoryPrepare(fixture, remove ? "remove.root" : "mkdir.root");
    LCOpen();
    if (strstr(name, "input")) lcRequest.input = lcInput;
    else if (strstr(name, "bytes")) { lcRequest.input = lcInput; lcRequest.bytes = 1U; }
    else if (strstr(name, "size")) lcRequest.size = 1U;
    else if (!strcmp(name, "remove-time")) lcRequest.time.year = 2044U;
    else if (!strcmp(name, "mkdir-time")) lcRequest.time.day = 30U;
    else CHECK(0);
    const UmicomU32 readsBefore = commitReads;
    CHECK(LCStage() == UMICOM_FAT16_UPDATE_INVALID_ARGUMENT);
    CHECK(commitReads == readsBefore && !commitWrites && !commitFlushes);
    CommitEqual(&lcResult, &lcResultBefore, sizeof(lcResult));
    CommitUnchanged(); LCClose();
}
static void DirectoryCollision(const char *fixture, const char *name)
{
    DirectoryPrepare(fixture, "mkdir.root");
    UmicomKernelDiskStatus expected = UMICOM_DISK_EXISTS;
    if (!strcmp(name, "existing-file")) strcpy(lcPath, "/frag.bin");
    else if (!strcmp(name, "existing-directory")) strcpy(lcPath, "/docs");
    else if (!strcmp(name, "missing-parent")) { strcpy(lcPath, "/ABSENT/CHILD"); expected = UMICOM_DISK_NOT_FOUND; }
    else if (!strcmp(name, "file-parent")) { strcpy(lcPath, "/FRAG.BIN/CHILD"); expected = UMICOM_DISK_NOT_DIRECTORY; }
    else if (!strcmp(name, "readonly-parent")) {
        strcpy(lcPath, "/DOCS/CHILD");
        LCSector(UMICOM_DISK_FIXTURE_ROOT)[64U + 11U] |= 1U;
        expected = UMICOM_DISK_READ_ONLY;
    } else CHECK(0);
    CommitRebase(); LCOpen();
    CHECK(LCStage() != UMICOM_FAT16_UPDATE_OK);
    CHECK(lcResult.commit.diskStatus == expected && !lcResult.commit.mediaTouched);
    CommitUnchanged(); LCClose();
}
/* Directory initialization also obeys the reader's raw-entry scan bound.
 * A valid FAT16 cluster size can exceed this Kernel's bounded directory profile;
 * accepting such a mkdir would create an object the next boot cannot inspect. */
static void DirectoryGeometry(const char *fixture, const char *name)
{
    DirectoryPrepare(fixture, "mkdir.root");
    const UmicomU32 sectors = !strcmp(name, "two-sector") ? 2U :
        !strcmp(name, "scan-boundary") ? 32U : 64U;
    CHECK(sectors != 64U || !strcmp(name, "beyond-scan-bound"));
    const UmicomU32 length = sectors == 2U ? UMICOM_DISK_FIXTURE_LENGTH : 129U + 4085U * sectors;
    UmicomU8 *const boot = LCSector(UMICOM_DISK_FIXTURE_FIRST);
    boot[13U] = (UmicomU8)sectors;
    CommitPut16(boot + 19U, length <= 65535U ? (UmicomU16)length : 0U);
    CommitPut32(boot + 32U, length > 65535U ? length : 0U);
    CommitPut32(commitVisible + 446U + 12U, length);
    model.capacity = UMICOM_DISK_FIXTURE_FIRST + length;
    for (UmicomSize copy = 0U; copy < 2U; ++copy)
        memset(LCSector(COMMIT_PRIMARY + copy * UMICOM_DISK_FIXTURE_FAT_SECTORS) + 4U,
            0, UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U - 4U);
    memset(LCSector(UMICOM_DISK_FIXTURE_ROOT), 0, 32U * 512U);
    CommitRebase();
    memcpy(lcExpected, commitVisible, sizeof(lcExpected));
    LCFatPut(lcExpected, 2U, 0xffffU);
    memset(lcExpected + (UmicomSize)UMICOM_DISK_FIXTURE_DATA * 512U, 0, (UmicomSize)sectors * 512U);
    DirectoryRecord(lcExpected + (UmicomSize)UMICOM_DISK_FIXTURE_DATA * 512U, ".          ", 2U);
    DirectoryRecord(lcExpected + (UmicomSize)UMICOM_DISK_FIXTURE_DATA * 512U + 32U, "..         ", 0U);
    DirectoryRecord(lcExpected + (UmicomSize)UMICOM_DISK_FIXTURE_ROOT * 512U, "WORK       ", 2U);
    LCOpen();
    if (sectors == 64U) {
        CHECK(LCStage() == UMICOM_FAT16_UPDATE_INSPECTION_LIMIT);
        CHECK(lcResult.commit.diskStatus == UMICOM_DISK_LIMIT && !lcResult.commit.mediaTouched);
        CommitUnchanged();
    } else {
        CHECK(LCStage() == UMICOM_FAT16_UPDATE_OK);
        CHECK(lcResult.plannedDataSectors == sectors && lcResult.allocatedClusters == 1U);
        CHECK(LCFinish() == UMICOM_FAT16_UPDATE_OK);
        CommitEqual(commitVisible, lcExpected, sizeof(lcExpected));
        CommitEqual(commitDurable, lcExpected, sizeof(lcExpected));
        DirectoryReadBack("/WORK", UMICOM_TRUE, UMICOM_TRUE);
        UmicomKernelFat16 fresh = {0};
        const UmicomKernelDiskReader reader = {model.capacity, DirectoryDurableRead, commitDurable};
        UmicomKernelFat16Directory listing;
        CHECK(UmicomKernelFat16Open(&fresh, &reader, 0U) == UMICOM_DISK_OK);
        CHECK(UmicomKernelFat16List(&fresh, "/WORK", &listing) == UMICOM_DISK_OK && listing.count == 0U);
        CHECK(UmicomKernelFat16Close(&fresh) == UMICOM_DISK_OK);
    }
    /* Only the backed prefix is compared; the advertised unused tail is sparse.
     * Every real device request must stay inside the 8 MiB native backing. */
    for (UmicomSize i = 0U; i < commitEventCount; ++i)
        if (commitEvents[i].command != UMICOM_VIRTIO_REQUEST_FLUSH)
            CHECK(commitEvents[i].sector < UMICOM_DISK_FIXTURE_SECTORS);
    LCClose();
}
