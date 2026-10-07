/*-----------------------------------------------------------------------------
 * Umicom Kernel native partition and FAT16 tests.
 *
 * The byte reader stands in for an immutable device. Actual parsing, chain
 * validation, snapshots and file copying are the production implementations.
 * No expected filesystem result is supplied by the reader callback itself.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_inspector.h"
#include "fixture_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

static UmicomU8 *image;
static UmicomKernelDiskReader reader;
static UmicomKernelFat16 volume;
static UmicomKernelFat16Directory listing;
static UmicomU8 output[UMICOM_FAT16_READ_BYTES];
static UmicomSize reads, failRead, outside;
static UmicomBoolean reenter;
static UmicomKernelDiskStatus reentryStatus;
static UmicomU8 *Boot(void) { return image + UMICOM_DISK_FIXTURE_FIRST * 512U; }
static UmicomU8 *Root(void) { return image + UMICOM_DISK_FIXTURE_ROOT * 512U; }
static UmicomU8 *Data(unsigned cluster) { return image + (UMICOM_DISK_FIXTURE_DATA + cluster - 2U) * 512U; }
static UmicomU8 *Fat(unsigned copy) { return Boot() + (1U + copy * UMICOM_DISK_FIXTURE_FAT_SECTORS) * 512U; }
static void Put16(UmicomU8 *p, UmicomU16 v) { p[0] = (UmicomU8)v; p[1] = (UmicomU8)(v >> 8U); }
static void Put32(UmicomU8 *p, UmicomU32 v) { for (unsigned i = 0U; i < 4U; ++i) p[i] = (UmicomU8)(v >> (i * 8U)); }
static void Link(unsigned cluster, UmicomU16 next) { Put16(Fat(0) + cluster * 2U, next); Put16(Fat(1) + cluster * 2U, next); }
static UmicomBoolean Read(void *context, UmicomU64 sector, UmicomU8 *target)
{
    CHECK(context == &reader);
    if (sector >= reader.sectors || sector >= UMICOM_DISK_FIXTURE_SECTORS) { ++outside; return UMICOM_FALSE; }
    ++reads;
    if (reenter) {
        reenter = UMICOM_FALSE;
        UmicomKernelFat16Entry ignored = {0};
        reentryStatus = UmicomKernelFat16Stat(&volume, "/README.TXT", &ignored);
    }
    if (reads == failRead) {
        memset(target, 0xdd, 512U); /* Device failure need not preserve private scratch. */
        return UMICOM_FALSE;
    }
    memcpy(target, image + sector * 512U, 512U);
    return UMICOM_TRUE;
}
static void Open(void)
{
    CHECK(UmicomKernelFat16Open(&volume, &reader, 0U) == UMICOM_DISK_OK);
}
static void Close(void)
{
    CHECK(UmicomKernelFat16Close(&volume) == UMICOM_DISK_OK);
}
static void OpenRefused(UmicomKernelDiskStatus expected)
{
    UmicomKernelFat16 before = volume;
    CHECK(UmicomKernelFat16Open(&volume, &reader, 0U) == expected);
    CHECK(memcmp(&before, &volume, sizeof(volume)) == 0);
    CHECK(outside == 0U);
}
static void MbrRefused(UmicomKernelDiskStatus expected)
{
    UmicomKernelPartitionTable table, before;
    memset(&table, 0xa5, sizeof(table)); before = table;
    CHECK(UmicomKernelDiskPartitionsInspect(&reader, &table) == expected);
    CHECK(memcmp(&table, &before, sizeof(table)) == 0);
}
static void ReadRefused(const char *path, UmicomKernelDiskStatus expected)
{
    memset(output, 0xa5, sizeof(output)); UmicomSize count = 99U;
    CHECK(UmicomKernelFat16Read(&volume, path, 0U, output, sizeof(output), &count) == expected);
    CHECK(count == 99U);
    for (UmicomSize i = 0U; i < sizeof(output); ++i) CHECK(output[i] == 0xa5U);
    CHECK(!volume.busy && outside == 0U);
}
static void ListRefused(const char *path, UmicomKernelDiskStatus expected)
{
    memset(&listing, 0xa5, sizeof(listing));
    CHECK(UmicomKernelFat16List(&volume, path, &listing) == expected);
    const UmicomU8 *p = (const UmicomU8 *)&listing;
    for (UmicomSize i = 0U; i < sizeof(listing); ++i) CHECK(p[i] == 0xa5U);
}
static void RootEntry(unsigned index, const char *name, UmicomU8 attr, UmicomU16 cluster, UmicomU32 size)
{
    UmicomU8 *entry = Root() + index * 32U;
    memset(entry, 0, 32U); memcpy(entry, name, 11U);
    entry[11] = attr; Put16(entry + 26U, cluster); Put32(entry + 28U, size);
}
static void Run(const char *test)
{
    if (!strcmp(test, "mbr_valid")) {
        UmicomKernelPartitionTable table = {0};
        CHECK(UmicomKernelDiskPartitionsInspect(&reader, &table) == UMICOM_DISK_OK);
        CHECK(table.count == 1U && table.entries[0].firstSector == 2048U && table.entries[0].sectors == 12288U);
        CHECK(reads == 1U && !table.entries[3].present);
    } else if (!strcmp(test, "mbr_signature")) { image[510] = 0; MbrRefused(UMICOM_DISK_SIGNATURE);
    } else if (!strcmp(test, "mbr_io")) { failRead = 1U; MbrRefused(UMICOM_DISK_IO_ERROR);
    } else if (!strcmp(test, "mbr_gpt")) { image[450] = 0xee; MbrRefused(UMICOM_DISK_UNSUPPORTED_TABLE);
    } else if (!strcmp(test, "mbr_extended")) {
        const UmicomU8 types[] = {0x05U, 0x0fU, 0x85U};
        for (unsigned i = 0; i < 3U; ++i) { image[450] = types[i]; MbrRefused(UMICOM_DISK_UNSUPPORTED_TABLE); }
    } else if (!strcmp(test, "mbr_empty")) {
        memset(image + 446U, 0, 64U); UmicomKernelPartitionTable table = {0};
        CHECK(UmicomKernelDiskPartitionsInspect(&reader, &table) == UMICOM_DISK_OK && table.count == 0U);
        OpenRefused(UMICOM_DISK_NOT_FOUND);
    } else if (!strcmp(test, "mbr_partial_empty")) { image[450] = 0; MbrRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "mbr_flag")) { image[446] = 1; MbrRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "mbr_zero_start")) { Put32(image + 454U, 0U); MbrRefused(UMICOM_DISK_RANGE);
    } else if (!strcmp(test, "mbr_outside")) { Put32(image + 458U, 0xffffffffU); MbrRefused(UMICOM_DISK_RANGE);
    } else if (!strcmp(test, "mbr_overlap")) {
        memcpy(image + 462U, image + 446U, 16U); MbrRefused(UMICOM_DISK_OVERLAP);
    } else if (!strcmp(test, "mbr_adjacent")) {
        memcpy(image + 462U, image + 446U, 16U); Put32(image + 470U, 14336U); Put32(image + 474U, 2048U);
        UmicomKernelPartitionTable table = {0};
        CHECK(UmicomKernelDiskPartitionsInspect(&reader, &table) == UMICOM_DISK_OK && table.count == 2U);
    } else if (!strcmp(test, "mbr_wide_arithmetic")) {
        reader.sectors = 0x200000000ULL; Put32(image + 454U, 0xfffffff0U); Put32(image + 458U, 0x100U);
        UmicomKernelPartitionTable table = {0};
        CHECK(UmicomKernelDiskPartitionsInspect(&reader, &table) == UMICOM_DISK_OK);
        CHECK(table.entries[0].firstSector + table.entries[0].sectors > 0x100000000ULL && reads == 1U);
    } else if (!strcmp(test, "sparse_partition_slot")) {
        memcpy(image + 494U, image + 446U, 16U); memset(image + 446U, 0U, 16U);
        CHECK(UmicomKernelFat16Open(&volume, &reader, 3U) == UMICOM_DISK_OK); Close();
    } else if (!strcmp(test, "two_sector_clusters")) {
        Boot()[13] = 2U; Link(9U, 0xffffU);
        UmicomU8 *data = image + UMICOM_DISK_FIXTURE_DATA * 512U;
        for (unsigned i = 0U; i < 1024U; ++i) data[4U * 512U + i] = UmicomDiskFixturePattern(i);
        for (unsigned i = 0U; i < 276U; ++i) data[14U * 512U + i] = UmicomDiskFixturePattern(i + 1024U);
        Open(); CHECK(volume.info.clusters == 6079U); UmicomSize count = 0U;
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 0U, output, sizeof(output), &count) == UMICOM_DISK_OK && count == 1300U);
        for (UmicomSize i = 0U; i < count; ++i) CHECK(output[i] == UmicomDiskFixturePattern(i));
        Close();
    } else if (!strcmp(test, "valid_geometry")) {
        Open(); CHECK(volume.info.clusters == 12159U && volume.info.rootEntries == 512U);
        CHECK(!strcmp(volume.info.label, "UMICOMDISK") && reads == 4U); Close();
    } else if (!strcmp(test, "invalid_arguments")) {
        CHECK(UmicomKernelDiskPartitionsInspect(0, 0) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16Open(0, &reader, 0U) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16Open(&volume, &reader, 4U) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(UmicomKernelFat16Read(&volume, 0, 0U, output, 1U, 0) == UMICOM_DISK_INVALID_ARGUMENT);
        CHECK(reads == 0U);
    } else if (!strcmp(test, "partition_selection")) {
        CHECK(UmicomKernelFat16Open(&volume, &reader, 3U) == UMICOM_DISK_NOT_FOUND);
    } else if (!strcmp(test, "wrong_partition_type")) { image[450] = 0x0b; OpenRefused(UMICOM_DISK_UNSUPPORTED_FILESYSTEM);
    } else if (!strcmp(test, "bpb_signature")) { Boot()[511] = 0; OpenRefused(UMICOM_DISK_SIGNATURE);
    } else if (!strcmp(test, "sector_size")) { Put16(Boot() + 11U, 4096U); OpenRefused(UMICOM_DISK_UNSUPPORTED_FILESYSTEM);
    } else if (!strcmp(test, "cluster_geometry")) {
        const UmicomU8 values[] = {0U, 3U, 128U};
        for (unsigned i = 0U; i < 3U; ++i) { Boot()[13] = values[i]; OpenRefused(UMICOM_DISK_UNSUPPORTED_FILESYSTEM); }
    } else if (!strcmp(test, "single_fat")) { Boot()[16] = 1; OpenRefused(UMICOM_DISK_UNSUPPORTED_FILESYSTEM);
    } else if (!strcmp(test, "root_geometry")) {
        Put16(Boot() + 17U, 0U); OpenRefused(UMICOM_DISK_CORRUPT);
        Put16(Boot() + 17U, 17U); OpenRefused(UMICOM_DISK_CORRUPT);
        Put16(Boot() + 17U, 528U); OpenRefused(UMICOM_DISK_LIMIT);
    } else if (!strcmp(test, "missing_reserved")) { Put16(Boot() + 14U, 0U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "missing_fat_size")) { Put16(Boot() + 22U, 0U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "ambiguous_size")) { Put32(Boot() + 32U, 12288U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "zero_size")) { Put16(Boot() + 19U, 0U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "hidden_offset")) { Put32(Boot() + 28U, 0U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "volume_outside_partition")) { Put16(Boot() + 19U, 14000U); OpenRefused(UMICOM_DISK_RANGE);
    } else if (!strcmp(test, "no_data")) { Put16(Boot() + 19U, 129U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "fat12_refused")) { Boot()[13] = 4U; OpenRefused(UMICOM_DISK_UNSUPPORTED_FILESYSTEM);
    } else if (!strcmp(test, "fat32_refused")) {
        reader.sectors = 100000U; Put32(image + 458U, 90000U); Put16(Boot() + 19U, 0U); Put32(Boot() + 32U, 80000U);
        OpenRefused(UMICOM_DISK_UNSUPPORTED_FILESYSTEM);
    } else if (!strcmp(test, "fat_too_short")) { Put16(Boot() + 22U, 1U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "label_not_type")) { memcpy(Boot() + 54U, "FAT32   ", 8U); Open(); Close();
    } else if (!strcmp(test, "label_sanitised")) { Boot()[43] = 27U; Open(); CHECK(volume.info.label[0] == '?'); Close();
    } else if (!strcmp(test, "media_and_reserved")) {
        Boot()[21] = 0xf1U; OpenRefused(UMICOM_DISK_CORRUPT); Boot()[21] = 0xf8U;
        Link(0U, 0U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "dirty_flags")) {
        Link(1U, 0x7fffU); OpenRefused(UMICOM_DISK_DIRTY);
        Link(1U, 0xbfffU); OpenRefused(UMICOM_DISK_DIRTY);
        Link(1U, 0xc000U); OpenRefused(UMICOM_DISK_CORRUPT);
    } else if (!strcmp(test, "fat_mirror_open")) { Fat(1)[9] ^= 1U; OpenRefused(UMICOM_DISK_FAT_MISMATCH);
    } else if (!strcmp(test, "open_io_unchanged")) {
        for (UmicomSize i = 1U; i <= 4U; ++i) { reads = 0U; failRead = i; OpenRefused(UMICOM_DISK_IO_ERROR); }
    } else if (!strcmp(test, "owner_lifetime")) {
        Open(); UmicomKernelFat16 copied = volume;
        CHECK(UmicomKernelFat16Close(&copied) == UMICOM_DISK_BAD_STATE);
        OpenRefused(UMICOM_DISK_BAD_STATE); Close();
        CHECK(UmicomKernelFat16List(&volume, "/", &listing) == UMICOM_DISK_BAD_STATE);
        Open(); Close();
    } else if (!strcmp(test, "root_and_nested")) {
        Open(); CHECK(UmicomKernelFat16List(&volume, "/", &listing) == UMICOM_DISK_OK && listing.count == 4U);
        CHECK(UmicomKernelFat16List(&volume, "/docs", &listing) == UMICOM_DISK_OK && listing.count == 1U);
        CHECK(!strcmp(listing.entries[0].name, "GUIDE.TXT")); Close();
    } else if (!strcmp(test, "path_grammar")) {
        const char *bad[] = {"", "relative", "//DOCS", "/DOCS/", "/./README.TXT", "/../README.TXT",
            "/DOCS//GUIDE.TXT", "/A.B.C", "/A.", "/NINECHARS.TXT", "/A.TOOLONG", "/A B", "/A\\B", "/\x80"};
        Open();
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            const UmicomSize before = reads; ReadRefused(bad[i], UMICOM_DISK_INVALID_ARGUMENT); CHECK(reads == before);
        }
        char longPath[257]; memset(longPath, 'A', sizeof(longPath)); longPath[0] = '/'; longPath[256] = 0;
        ReadRefused(longPath, UMICOM_DISK_LIMIT);
        ReadRefused("/A/A/A/A/A/A/A/A/A", UMICOM_DISK_LIMIT); Close();
    } else if (!strcmp(test, "missing_and_wrong_type")) {
        Open(); ReadRefused("/NOTHERE", UMICOM_DISK_NOT_FOUND);
        ReadRefused("/DOCS", UMICOM_DISK_IS_DIRECTORY);
        ReadRefused("/README.TXT/CHILD", UMICOM_DISK_NOT_DIRECTORY);
        ListRefused("/README.TXT", UMICOM_DISK_NOT_DIRECTORY); Close();
    } else if (!strcmp(test, "long_name_skipped")) {
        memmove(Root() + 192U, Root() + 128U, 32U);
        memset(Root() + 128U, 0xff, 64U); Root()[128] = 0x41U; Root()[139] = 0x0fU;
        Root()[160] = 0x42U; Root()[171] = 0x0fU; Root()[224] = 0;
        Open(); CHECK(UmicomKernelFat16List(&volume, "/", &listing) == UMICOM_DISK_OK && listing.count == 4U);
        CHECK(listing.longNameRecords == 2U); Close();
    } else if (!strcmp(test, "deleted_ignored")) {
        Root()[32] = 0xe5U; Open(); CHECK(UmicomKernelFat16List(&volume, "/", &listing) == UMICOM_DISK_OK && listing.count == 3U); Close();
    } else if (!strcmp(test, "short_name_invalid")) {
        Root()[32] = 0x80U; Open(); ListRefused("/", UMICOM_DISK_UNSUPPORTED_FILESYSTEM); Close();
    } else if (!strcmp(test, "short_name_padding")) {
        Root()[33] = ' '; Open(); ListRefused("/", UMICOM_DISK_UNSUPPORTED_FILESYSTEM); Close();
    } else if (!strcmp(test, "duplicate_short_alias")) {
        RootEntry(5U, "readme  txt", 0x20U, 0U, 0U); Open(); ListRefused("/", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "entry_attributes")) {
        Root()[43] |= 0x80U; Open(); ListRefused("/", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "entry_high_cluster")) {
        Put16(Root() + 32U + 20U, 1U); Open(); ListRefused("/", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "entry_cluster_bounds")) {
        Put16(Root() + 32U + 26U, 1U); Open(); ListRefused("/", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "zero_file_cluster")) {
        Put16(Root() + 128U + 26U, 2U); Open(); ListRefused("/", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "directory_size")) {
        Put32(Root() + 64U + 28U, 1U); Open(); ListRefused("/", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "directory_dot_corrupt")) {
        Put16(Data(3U) + 26U, 4U); Open(); ListRefused("/DOCS", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "listing_capacity")) {
        memset(Root(), 0, 32U * 512U);
        for (unsigned i = 0U; i < 129U; ++i) {
            char name[12]; CHECK(snprintf(name, sizeof(name), "F%07uTXT", i) == 11);
            RootEntry(i, name, 0x20U, 0U, 0U);
        }
        Open(); ListRefused("/", UMICOM_DISK_LIMIT); Close();
    } else if (!strcmp(test, "directory_scan_limit")) {
        for (unsigned i = 0U; i < 33U; ++i) Link(i == 0U ? 3U : 30U + i, (UmicomU16)(i == 32U ? 0xffffU : 31U + i));
        Open(); ListRefused("/DOCS", UMICOM_DISK_LIMIT); Close();
    } else if (!strcmp(test, "directory_cycle")) {
        Link(3U, 3U); Open(); ListRefused("/DOCS", UMICOM_DISK_CHAIN_CYCLE); Close();
    } else if (!strcmp(test, "fragmented_read")) {
        Open(); UmicomSize count = 0U;
        memset(output, 0xa5, sizeof(output));
        CHECK(UmicomKernelFat16Read(&volume, "/frag.bin", 0U, output, sizeof(output), &count) == UMICOM_DISK_OK && count == 1300U);
        for (UmicomSize i = 0U; i < count; ++i) CHECK(output[i] == UmicomDiskFixturePattern(i));
        for (UmicomSize i = count; i < sizeof(output); ++i) CHECK(output[i] == 0xa5U);
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 511U, output, 700U, &count) == UMICOM_DISK_OK && count == 700U);
        for (UmicomSize i = 0U; i < count; ++i) {
            CHECK(output[i] == UmicomDiskFixturePattern(i + 511U));
        }
        Close();
    } else if (!strcmp(test, "eof_and_empty")) {
        Open(); UmicomSize count = 99U; output[0] = 0xa5U;
        CHECK(UmicomKernelFat16Read(&volume, "/EMPTY.TXT", 0U, output, 1U, &count) == UMICOM_DISK_OK && count == 0U);
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", ~(UmicomU64)0U, output, 1U, &count) == UMICOM_DISK_OK && count == 0U);
        CHECK(output[0] == 0xa5U); Close();
    } else if (!strcmp(test, "bad_chain_codes")) {
        const UmicomU16 bad[] = {0U, 1U, 0xfff0U, 0xfff6U, 0xfff7U, 20000U}; Open();
        for (unsigned i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) { Link(4U, bad[i]); ReadRefused("/FRAG.BIN", UMICOM_DISK_CORRUPT); }
        Close();
    } else if (!strcmp(test, "file_cycle")) { Link(9U, 4U); Open(); ReadRefused("/FRAG.BIN", UMICOM_DISK_CHAIN_CYCLE); Close();
    } else if (!strcmp(test, "short_chain")) { Link(4U, 0xffffU); Open(); ReadRefused("/FRAG.BIN", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "long_chain")) { Link(6U, 7U); Open(); ReadRefused("/FRAG.BIN", UMICOM_DISK_CORRUPT); Close();
    } else if (!strcmp(test, "chain_limit")) {
        Put32(Root() + 96U + 28U, 257U * 512U); Open(); ReadRefused("/FRAG.BIN", UMICOM_DISK_LIMIT); Close();
    } else if (!strcmp(test, "later_mirror_mismatch")) {
        Link(4U, 260U); Link(260U, 6U); Put16(Fat(1) + 260U * 2U, 7U);
        Open(); ReadRefused("/FRAG.BIN", UMICOM_DISK_FAT_MISMATCH); Close();
    } else if (!strcmp(test, "all_read_failures_unchanged")) {
        Open(); reads = 0U; UmicomSize count = 0U;
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 0U, output, sizeof(output), &count) == UMICOM_DISK_OK);
        const UmicomSize required = reads;
        for (UmicomSize i = 1U; i <= required; ++i) { reads = 0U; failRead = i; ReadRefused("/FRAG.BIN", UMICOM_DISK_IO_ERROR); }
        failRead = 0U; reads = 0U;
        CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", 0U, output, sizeof(output), &count) == UMICOM_DISK_OK);
        Close();
    } else if (!strcmp(test, "list_io_unchanged")) {
        Open(); reads = 0U; failRead = 1U; ListRefused("/", UMICOM_DISK_IO_ERROR); Close();
    } else if (!strcmp(test, "stat_failure_unchanged")) {
        Open(); UmicomKernelFat16Entry entry, before; memset(&entry, 0xa5, sizeof(entry)); before = entry;
        CHECK(UmicomKernelFat16Stat(&volume, "/MISSING", &entry) == UMICOM_DISK_NOT_FOUND);
        CHECK(memcmp(&entry, &before, sizeof(entry)) == 0); Close();
    } else if (!strcmp(test, "reentry_refused")) {
        Open(); reenter = UMICOM_TRUE;
        CHECK(UmicomKernelFat16List(&volume, "/", &listing) == UMICOM_DISK_OK && reentryStatus == UMICOM_DISK_BUSY); Close();
    } else if (!strcmp(test, "repeated_lifetimes")) {
        for (unsigned i = 0; i < 1000U; ++i) {
            Open(); UmicomSize count = 0U;
            CHECK(UmicomKernelFat16Read(&volume, "/FRAG.BIN", i % 1300U, output, 256U, &count) == UMICOM_DISK_OK);
            CHECK(count && output[0] == UmicomDiskFixturePattern(i % 1300U)); Close();
        }
    } else if (!strcmp(test, "deterministic_mutations")) {
        UmicomU32 state = 0x9e3779b9U;
        for (unsigned i = 0U; i < 4000U; ++i) {
            state = state * 1664525U + 1013904223U;
            const size_t offset = (i & 1U) ? (size_t)UMICOM_DISK_FIXTURE_FIRST * 512U + (state % 64U) : state % 512U;
            const UmicomU8 old = image[offset]; image[offset] ^= (UmicomU8)(1U << ((state >> 16U) & 7U));
            if (UmicomKernelFat16Open(&volume, &reader, 0U) == UMICOM_DISK_OK) {
                UmicomSize count = 0U;
                (void)UmicomKernelFat16List(&volume, "/", &listing);
                (void)UmicomKernelFat16Read(&volume, "/FRAG.BIN", 0U, output, sizeof(output), &count);
                Close();
            }
            CHECK(outside == 0U); image[offset] = old;
        }
    } else { fprintf(stderr, "unknown test: %s\n", test); exit(2); }
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const size_t size = (size_t)UMICOM_DISK_FIXTURE_SECTORS * 512U;
    image = malloc(size); CHECK(image != NULL);
    FILE *file = fopen(argv[2], "rb"); CHECK(file != NULL);
    CHECK(fread(image, 1U, size, file) == size && fgetc(file) == EOF); CHECK(fclose(file) == 0);
    reader.sectors = UMICOM_DISK_FIXTURE_SECTORS; reader.read = Read; reader.context = &reader;
    Run(argv[1]); CHECK(outside == 0U); free(image);
    printf("disk.%s: pass\n", argv[1]); return 0;
}
