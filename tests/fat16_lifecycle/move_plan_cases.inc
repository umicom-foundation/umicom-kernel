/* Umicom Kernel move-planner value and pointer qualification.
 * The oracle applies only planned complete sectors to an independent image;
 * no test treats a plan as authority to write a device.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
static char lifeMoveDestination[UMICOM_FAT16_PATH_BYTES];
static void LifeMoveDefaults(void)
{
    LifeDefaults(UMICOM_FAT16_LIFECYCLE_MOVE);
    memset(&lifeRequest.time, 0, sizeof(lifeRequest.time));
    strcpy(path, "/FRAG.BIN"); strcpy(lifeMoveDestination, "/DOCS/SAVED.BIN");
    lifeRequest.destination = lifeMoveDestination;
}
static void LifeMove(const char *name)
{
    LifeMoveDefaults(); LifeStart();
    const UmicomSize root = UMICOM_DISK_FIXTURE_ROOT * 512U;
    const UmicomSize parent = (UMICOM_DISK_FIXTURE_DATA + 1U) * 512U;
    if (!strcmp(name, "success") || !strcmp(name, "read-failures")) {
        memcpy(lifeExpected, media, sizeof(media));
        memcpy(lifeExpected + parent + 96U, media + root + 96U, 32U);
        memcpy(lifeExpected + parent + 96U, "SAVED   BIN", 11U);
        lifeExpected[parent + 108U] &= (UmicomU8)~0x18U;
        lifeExpected[root + 96U] = 0xe5U;
        CHECK(LifeCall() == UMICOM_DISK_OK);
        CHECK(!lifePlan.dataSectorCount && !lifePlan.allocatedClusters && !lifePlan.freedClusters);
        CHECK(lifePlan.fatSectorCount == 1U && !lifePlan.fatSectors[0].changed);
        memcpy(lifeApplied, media, sizeof(media));
        for (UmicomSize i = 0U; i < lifePlan.directorySectorCount; ++i)
            memcpy(lifeApplied + (UmicomSize)lifePlan.directorySectors[i].sector * 512U,
                lifePlan.directorySectors[i].data, 512U);
        CHECK(!memcmp(lifeApplied, lifeExpected, sizeof(lifeApplied)));
        LifeScrubbed();
        if (!strcmp(name, "read-failures")) {
            const UmicomSize total = reads;
            for (UmicomSize i = 1U; i <= total; ++i) {
                LifeMoveDefaults(); LifeStart(); failAt = i;
                CHECK(LifeCall() == UMICOM_DISK_IO_ERROR && reads == i);
                Filled(&lifePlan, sizeof(lifePlan), 0xa5U); LifeScrubbed();
            }
            printf("move planner read failures=%llu\n", (unsigned long long)total);
        }
        return;
    }
    UmicomKernelDiskStatus expected = UMICOM_DISK_INVALID_ARGUMENT;
    if (!strcmp(name, "null")) lifeRequest.destination = NULL;
    else if (!strcmp(name, "volume")) lifeRequest.destination = (const char *)&volume;
    else if (!strcmp(name, "workspace")) lifeRequest.destination = (const char *)&workspace;
    else if (!strcmp(name, "scratch")) lifeRequest.destination = (const char *)&lifeWorkspace;
    else if (!strcmp(name, "output")) lifeRequest.destination = (const char *)&lifePlan;
    else if (!strcmp(name, "request")) lifeRequest.destination = (const char *)&lifeRequest;
    else if (!strcmp(name, "source")) lifeRequest.destination = path;
    else if (!strcmp(name, "unterminated")) {
        memset(lifeMoveDestination, 'x', sizeof(lifeMoveDestination)); expected = UMICOM_DISK_LIMIT;
    } else CHECK(0);
    const UmicomSize beforeReads = reads;
    CHECK(LifeCall() == expected && reads == beforeReads);
    Filled(&lifePlan, sizeof(lifePlan), 0xa5U);
}
