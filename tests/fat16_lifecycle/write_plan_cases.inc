/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/fat16_lifecycle/write_plan_cases.inc
 *
 * Positional lifecycle planning against the independent complete-media oracle.
 * Every success compares all image bytes, including untouched file suffixes,
 * old allocation slack, neighbouring entries and newly initialised clusters.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
static void UmicomKernelFat16WritePlanProfile(const char *name)
{
    if (!strcmp(name, "overwrite")) { lifeRequest.offset=17U; lifeRequest.bytes=97U; }
    else if (!strcmp(name, "beginning")) { lifeRequest.offset=0U; lifeRequest.bytes=1U; }
    else if (!strcmp(name, "fragmented")) { lifeRequest.offset=511U; lifeRequest.bytes=700U; }
    else if (!strcmp(name, "whole-existing")) { lifeRequest.offset=0U; lifeRequest.bytes=1300U; }
    else if (!strcmp(name, "eof-slack")) { lifeRequest.offset=1300U; lifeRequest.bytes=236U; }
    else if (!strcmp(name, "empty")) {
        LifeFixture("empty"); lifeRequest.offset=0U; lifeRequest.bytes=1U;
    } else if (!strcmp(name, "maximum")) { lifeRequest.offset=1299U; lifeRequest.bytes=4096U; }
    else if (!strcmp(name, "preserved-fields")) {
        LifeFixture("preserved_fields"); lifeRequest.offset=17U; lifeRequest.bytes=97U;
    } else if (!strcmp(name, "nested")) { LifeFixture("nested"); lifeRequest.offset=511U; }
    else if (!strcmp(name, "fat-crossing")) {
        LifeFixture("fat_crossing"); lifeRequest.bytes=1900U;
    } else if (!strcmp(name, "full-volume-overwrite")) {
        LifeReserveBelow((UmicomU16)((UMICOM_DISK_FIXTURE_LENGTH-129U)+2U));
        lifeRequest.offset=17U; lifeRequest.bytes=97U;
    } else if (!strcmp(name, "append-ignores-offset")) {
        LifeDefaults(UMICOM_FAT16_LIFECYCLE_APPEND); lifeRequest.offset=~(UmicomU64)0U;
    } else CHECK(!strcmp(name, "extension") || !strcmp(name, "snapshot") || !strcmp(name, "reentry"));
}

static void UmicomKernelFat16WritePlanRefusal(const char *name)
{
    UmicomKernelDiskStatus expected=UMICOM_DISK_RANGE;
    if (!strcmp(name, "hole")) lifeRequest.offset=1301U;
    else if (!strcmp(name, "offset-overflow")) lifeRequest.offset=~(UmicomU64)0U;
    else if (!strcmp(name, "size-overflow")) {
        /* The range refusal precedes chain traversal. This oversized record
         * must never turn an overflowing end position into admitted I/O. */
        LifeLongPut(Sector(UMICOM_DISK_FIXTURE_ROOT)+124U,0xffffffffU);
        lifeRequest.offset=0xffffffffU; lifeRequest.bytes=1U;
    } else { LifeRefusal(name); return; }
    memcpy(mediumBefore,media,sizeof(media)); LifeStart();
    CHECK(LifeCall()==expected); Filled(&lifePlan,sizeof(lifePlan),0xa5U);
    CHECK(!memcmp(media,mediumBefore,sizeof(media))); LifeScrubbed();
}

static void UmicomKernelFat16WritePlanCase(const char *name)
{
    LifeDefaults(UMICOM_FAT16_LIFECYCLE_WRITE);
    if (!strncmp(name,"refusal.",8U)) { UmicomKernelFat16WritePlanRefusal(name+8U); return; }
    if (!strncmp(name,"guard.",6U)) { LifeGuard(name+6U); return; }
    if (!strcmp(name,"all-read-failures")) { LifeFailures(); return; }
    CHECK(!strncmp(name,"success.",8U)); name+=8U;
    UmicomKernelFat16WritePlanProfile(name);
    const UmicomU64 expectedOffset=lifeRequest.operation==UMICOM_FAT16_LIFECYCLE_APPEND?
        1300U:lifeRequest.offset;
    const UmicomSize expectedBytes=lifeRequest.bytes;
    memcpy(mediumBefore,media,sizeof(media)); LifeOracle(); LifeStart();
    lifeSnapshot=!strcmp(name,"snapshot")?UMICOM_TRUE:UMICOM_FALSE;
    lifeReentry=!strcmp(name,"reentry")?UMICOM_TRUE:UMICOM_FALSE;
    CHECK(LifeCall()==UMICOM_DISK_OK); LifeVerify();
    CHECK(lifePlan.offset==expectedOffset && lifePlan.requestedBytes==expectedBytes);
    CHECK(lifePlan.freedClusters==0U && lifePlan.originalEntryPresent && lifePlan.updatedEntryPresent);
    if (lifeReentry) CHECK(lifeReentries==1U);
    if (!strcmp(name,"maximum")) CHECK(lifePlan.allocatedClusters==8U);
    if (!strcmp(name,"fat-crossing")) CHECK(lifePlan.fatSectorCount==2U && lifePlan.chainBoundaryPresent);
    if (!strcmp(name,"full-volume-overwrite") || !strcmp(name,"eof-slack") ||
        !strcmp(name,"fragmented") || !strcmp(name,"overwrite"))
        CHECK(!lifePlan.allocatedClusters && !lifePlan.chainBoundaryPresent);
}
