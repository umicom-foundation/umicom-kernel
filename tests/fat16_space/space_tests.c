/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/fat16_space/space_tests.c
 *
 * PURPOSE:
 *   Verify allocation counts, failure atomicity and reader ownership on synthetic media.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#define main UmicomSpaceOriginalInspectionEntry
#include "../disk_inspection/disk_inspection_tests.c"
#undef main
#include "umicom/kernel/fat16_space.h"

static UmicomKernelFat16Space space, saved;
static UmicomBoolean spaceReentry;
static UmicomBoolean SpaceRead(void *context, UmicomU64 sector, UmicomU8 *target)
{
    if (spaceReentry) {
        spaceReentry = UMICOM_FALSE;
        UmicomKernelFat16Space local, before;
        memset(&local, 0x5a, sizeof(local)); memcpy(&before, &local, sizeof(local));
        CHECK(UmicomKernelFat16SpaceRead(&volume, &local) == UMICOM_DISK_BUSY);
        CHECK(!memcmp(&before, &local, sizeof(local)));
        CHECK(UmicomKernelFat16Close(&volume) == UMICOM_DISK_BUSY);
    }
    return Read(context, sector, target);
}
static void SpaceSentinel(void)
{
    memset(&space, 0xa5, sizeof(space)); memcpy(&saved, &space, sizeof(space));
}
static void SpaceRefused(UmicomKernelDiskStatus expected)
{
    SpaceSentinel();
    CHECK(UmicomKernelFat16SpaceRead(&volume, &space) == expected);
    CHECK(!memcmp(&space, &saved, sizeof(space)));
    CHECK(!volume.busy && !volume.fatCached && !outside);
}
static void SpaceCounts(UmicomU32 freeCount, UmicomU32 allocated, UmicomU32 bad)
{
    CHECK(UmicomKernelFat16SpaceRead(&volume, &space) == UMICOM_DISK_OK);
    CHECK(space.totalClusters == 12159U && space.clusterBytes == 512U);
    CHECK(space.freeClusters == freeCount && space.allocatedClusters == allocated && space.badClusters == bad);
    CHECK(space.freeClusters + space.allocatedClusters + space.badClusters == space.totalClusters);
    CHECK(space.dataBytes == 6225408U && space.freeBytes == (UmicomU64)freeCount * 512U);
    CHECK(space.allocatedBytes == (UmicomU64)allocated * 512U && space.badBytes == (UmicomU64)bad * 512U);
    CHECK(!volume.busy && !volume.fatCached && volume.reads == 96U);
}
static void SpaceCase(const char *name)
{
    Open();
    if (!strcmp(name, "ordinary")) SpaceCounts(12153U, 6U, 0U);
    else if (!strcmp(name, "bad_cluster")) {
        Link(20U, 0xfff7U); SpaceCounts(12152U, 6U, 1U);
    } else if (!strcmp(name, "orphan_not_ownership")) {
        Link(20U, 0xffffU); SpaceCounts(12152U, 7U, 0U);
    } else if (!strcmp(name, "cycle_not_ownership")) {
        Link(20U, 20U); SpaceCounts(12152U, 7U, 0U);
    } else if (!strcmp(name, "full")) {
        for (unsigned c = 2U; c <= 12160U; ++c) Link(c, 0xffffU);
        SpaceCounts(0U, 12159U, 0U);
    } else if (!strcmp(name, "empty")) {
        for (unsigned c = 2U; c <= 12160U; ++c) Link(c, 0U);
        SpaceCounts(12159U, 0U, 0U);
    } else if (!strcmp(name, "last_cluster")) {
        Link(12160U, 0xffffU); SpaceCounts(12152U, 7U, 0U);
    } else if (!strcmp(name, "padding_ignored")) {
        Link(12161U, 1U); SpaceCounts(12153U, 6U, 0U);
    } else if (!strcmp(name, "padding_mirror")) {
        Fat(1)[48U * 512U - 1U] ^= 1U; SpaceRefused(UMICOM_DISK_FAT_MISMATCH);
    } else if (!strcmp(name, "all_read_failures")) {
        for (UmicomSize i = 1U; i <= 96U; ++i) {
            reads = 0U; failRead = i;
            SpaceRefused(UMICOM_DISK_IO_ERROR); CHECK(volume.reads == i);
        }
        failRead = 0U; SpaceCounts(12153U, 6U, 0U);
    } else if (!strcmp(name, "cache_invalidated")) {
        UmicomSize count = 0U;
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 0U, output, 12U, &count) == UMICOM_DISK_OK);
        SpaceCounts(12153U, 6U, 0U);
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 511U, output, 12U, &count) == UMICOM_DISK_OK);
        for (UmicomSize i = 0U; i < count; ++i) CHECK(output[i] == UmicomDiskFixturePattern(511U + i));
    } else if (!strcmp(name, "callback_reentry")) {
        volume.reader.read = SpaceRead; spaceReentry = UMICOM_TRUE;
        SpaceCounts(12153U, 6U, 0U); CHECK(!spaceReentry);
    } else if (!strcmp(name, "reserved_link")) {
        Link(20U, 0xfff0U); SpaceRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(name, "one_link")) {
        Link(20U, 1U); SpaceRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(name, "outside_link")) {
        Link(20U, 12161U); SpaceRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(name, "dirty")) {
        Link(1U, 0x7fffU); SpaceRefused(UMICOM_DISK_DIRTY);
    } else if (!strcmp(name, "io_error_flag")) {
        Link(1U, 0xbfffU); SpaceRefused(UMICOM_DISK_DIRTY);
    } else if (!strcmp(name, "bad_flags")) {
        Link(1U, 0xfffeU); SpaceRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(name, "bad_media")) {
        Link(0U, 0xff01U); SpaceRefused(UMICOM_DISK_CORRUPT);
    } else if (!strncmp(name, "geometry.", 9U)) {
        const UmicomKernelFat16 original = volume;
        const char *field = name + 9U;
        UmicomKernelDiskStatus expected = UMICOM_DISK_BAD_STATE;
        if (!strcmp(field, "clusters")) volume.info.clusters = 0xffffffffU;
        else if (!strcmp(field, "cluster_bytes")) volume.info.sectorsPerCluster = 0U;
        else if (!strcmp(field, "fat_limit")) { volume.info.sectorsPerFat = 257U; expected = UMICOM_DISK_LIMIT; }
        else if (!strcmp(field, "fat_zero")) volume.info.sectorsPerFat = 0U;
        else if (!strcmp(field, "data_start")) ++volume.dataStart;
        else if (!strcmp(field, "partition")) volume.partitionSectors = ~(UmicomU64)0U;
        else if (!strcmp(field, "reader")) volume.reader.read = NULL;
        else CHECK(0);
        reads = 0U; SpaceRefused(expected); CHECK(!reads); volume = original;
    } else if (!strncmp(name, "guard.", 6U)) {
        const char *field = name + 6U;
        UmicomKernelFat16 *owner = &volume;
        UmicomKernelFat16Space *target = &space;
        UmicomKernelDiskStatus expected = UMICOM_DISK_INVALID_ARGUMENT;
        UmicomKernelFat16 copy = volume;
        if (!strcmp(field, "null_owner")) owner = NULL;
        else if (!strcmp(field, "null_result")) target = NULL;
        else if (!strcmp(field, "owner_alignment")) owner = (UmicomKernelFat16 *)((UmicomU8 *)&volume + 1U);
        else if (!strcmp(field, "result_alignment")) target = (UmicomKernelFat16Space *)((UmicomU8 *)&space + 1U);
        else if (!strcmp(field, "owner_overflow")) owner = (UmicomKernelFat16 *)(~(UmicomAddress)0U - 31U);
        else if (!strcmp(field, "result_overflow")) target = (UmicomKernelFat16Space *)(~(UmicomAddress)0U - 31U);
        else if (!strcmp(field, "overlap")) target = (UmicomKernelFat16Space *)&volume;
        else if (!strcmp(field, "copied")) { owner = &copy; expected = UMICOM_DISK_BAD_STATE; }
        else if (!strcmp(field, "busy")) { volume.busy = UMICOM_TRUE; expected = UMICOM_DISK_BUSY; }
        else if (!strcmp(field, "closed")) { volume.open = UMICOM_FALSE; expected = UMICOM_DISK_BAD_STATE; }
        else CHECK(0);
        SpaceSentinel(); reads = 0U;
        const UmicomKernelFat16 before = volume;
        CHECK(UmicomKernelFat16SpaceRead(owner, target) == expected);
        CHECK(!memcmp(&space, &saved, sizeof(space)) && !memcmp(&before, &volume, sizeof(volume)) && !reads);
        volume.busy = UMICOM_FALSE; volume.open = UMICOM_TRUE;
    } else CHECK(0);
    Close();
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    const size_t size = (size_t)UMICOM_DISK_FIXTURE_SECTORS * 512U;
    image = malloc(size); CHECK(image);
    FILE *file = fopen(argv[2], "rb"); CHECK(file);
    CHECK(fread(image, 1U, size, file) == size && fgetc(file) == EOF);
    CHECK(fclose(file) == 0);
    reader.sectors = UMICOM_DISK_FIXTURE_SECTORS; reader.read = Read; reader.context = &reader;
    SpaceCase(argv[1]); CHECK(!outside); free(image);
    printf("fat16-space.%s: pass\n", argv[1]);
    return 0;
}
