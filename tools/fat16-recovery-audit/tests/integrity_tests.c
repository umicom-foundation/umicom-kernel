/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/tests/integrity_tests.c
 *
 * PURPOSE:
 *   Exercise genuine on-disk FAT16 sector layouts through the read-only
 *   audit callback. Synthetic images are generated in memory, never by
 *   overwriting real disks or committing a repair.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_integrity_audit.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PARTITION UINT64_C(2048)
#define TEST_SECTORS UINT64_C(14048)
#define TEST_VOLUME UINT32_C(12000)
#define TEST_FAT_SECTORS UINT32_C(32)
#define TEST_FAT_CAPACITY UINT32_C(8192)
#define TEST_DATA_SECTOR (TEST_PARTITION + UINT64_C(97))
#define TEST_ROOT_SECTOR (TEST_PARTITION + UINT64_C(65))

typedef struct UmicomIntegritySyntheticDisk {
    uint16_t fat[TEST_FAT_CAPACITY];
    uint8_t root[512];
    uint8_t subdirectory[512];
    uint32_t totalReads;
    uint64_t failSector;
    bool mirrorMismatch;
    bool corruptMbr;
    bool refuseAllReads;
    bool mutateAfterAdmission;
} UmicomIntegritySyntheticDisk;

static void UmicomTestLe16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 255U);
    out[1] = (uint8_t)(value >> 8U);
}
static void UmicomTestLe32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 255U);
    out[1] = (uint8_t)((value >> 8U) & 255U);
    out[2] = (uint8_t)((value >> 16U) & 255U);
    out[3] = (uint8_t)((value >> 24U) & 255U);
}

static void UmicomTestEntry(uint8_t *target, const char *name,
    uint8_t attributes, uint16_t cluster, uint32_t length)
{
    uint32_t i;
    memset(target, 0, 32U);
    for (i = 0U; i < 11U; ++i) target[i] = (uint8_t)' ';
    for (i = 0U; i < 11U && name[i] != '\0'; ++i) target[i] = (uint8_t)name[i];
    target[11] = attributes;
    UmicomTestLe16(target + 26U, cluster);
    UmicomTestLe32(target + 28U, length);
}

static void UmicomTestBase(UmicomIntegritySyntheticDisk *disk)
{
    memset(disk, 0, sizeof(*disk));
    disk->fat[0] = UINT16_C(0xfff8);
    disk->fat[1] = UINT16_C(0xffff);
    disk->failSector = UINT64_MAX;
}

static bool UmicomSyntheticRead(void *context, uint64_t sector,
    uint8_t out[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    UmicomIntegritySyntheticDisk *disk = (UmicomIntegritySyntheticDisk *)context;
    uint32_t k;
    ++disk->totalReads;
    if (disk->refuseAllReads || sector == disk->failSector || sector >= TEST_SECTORS) {
        return false;
    }
    memset(out, 0, 512U);
    if (sector == 0U) {
        out[510] = disk->corruptMbr ? 0U : UINT8_C(0x55);
        out[511] = UINT8_C(0xaa);
        out[450] = UINT8_C(0x06);
        UmicomTestLe32(out + 454, (uint32_t)TEST_PARTITION);
        UmicomTestLe32(out + 458, TEST_VOLUME);
    } else if (sector == TEST_PARTITION) {
        out[0] = UINT8_C(0xeb);
        out[2] = UINT8_C(0x90);
        UmicomTestLe16(out + 11, 512U);
        out[13] = 2U;
        UmicomTestLe16(out + 14, 1U);
        out[16] = 2U;
        UmicomTestLe16(out + 17, 512U);
        UmicomTestLe16(out + 19, (uint16_t)TEST_VOLUME);
        out[21] = UINT8_C(0xf8);
        UmicomTestLe16(out + 22, (uint16_t)TEST_FAT_SECTORS);
        UmicomTestLe32(out + 28, (uint32_t)TEST_PARTITION);
        out[510] = UINT8_C(0x55);
        out[511] = UINT8_C(0xaa);
    } else if (sector >= TEST_PARTITION + 1U
               && sector < TEST_PARTITION + 1U + 2U * TEST_FAT_SECTORS) {
        const uint64_t offset = sector - (TEST_PARTITION + 1U);
        const bool mirror = offset >= TEST_FAT_SECTORS;
        const uint32_t within = (uint32_t)(offset % TEST_FAT_SECTORS);
        for (k = 0U; k < 256U; ++k) {
            const uint32_t index = within * 256U + k;
            uint16_t value = disk->fat[index];
            if (disk->mutateAfterAdmission && disk->totalReads > 67U && index == 1U) {
                value = UINT16_C(0x7fff);
            }
            if (disk->mirrorMismatch && mirror && index == 8U) value = 0xffffU;
            UmicomTestLe16(out + k * 2U, value);
        }
    } else if (sector == TEST_ROOT_SECTOR) {
        memcpy(out, disk->root, sizeof(disk->root));
    } else if (sector == TEST_DATA_SECTOR + 4U) {
        memcpy(out, disk->subdirectory, sizeof(disk->subdirectory));
    }
    return true;
}

typedef enum UmicomCase {
    CASE_EMPTY,
    CASE_FILE_ONE,
    CASE_FILE_TWO,
    CASE_FILE_FRAGMENTED,
    CASE_FILE_ZERO,
    CASE_DELETED_ENTRY,
    CASE_LFN_ENTRY,
    CASE_FOLDER_EMPTY,
    CASE_FOLDER_CHILD,
    CASE_FOLDER_TWO_CHILDREN,
    CASE_BAD_MARKER,
    CASE_CHAIN_CROSSLINK,
    CASE_CHAIN_LOOP,
    CASE_CHAIN_FREE,
    CASE_CHAIN_OUT_OF_RANGE,
    CASE_CHAIN_BAD_MARKER,
    CASE_CHAIN_RESERVED_MARKER,
    CASE_CHAIN_TRUNCATED,
    CASE_CHAIN_ORPHAN,
    CASE_FOLDER_CROSSLINK,
    CASE_FOLDER_LOOP,
    CASE_FOLDER_INVALID_SIZE,
    CASE_BAD_ENTRY_HIGH_CLUSTER,
    CASE_BAD_ENTRY_ATTRIBUTE,
    CASE_BAD_EMPTY_FILE_POINTER,
    CASE_BAD_DOT_POINTER,
    CASE_BAD_DOT_NAME,
    CASE_BAD_PARENT_POINTER,
    CASE_ROOT_DOT_ENTRY,
    CASE_FAT_MUTATES_AFTER_ADMISSION,
    CASE_BAD_MBR,
    CASE_DIRTY,
    CASE_MISMATCH,
    CASE_IO_ROOT,
    CASE_IO_BOOT,
    CASE_IO_FAT,
    CASE_IO_DATA,
    CASE_BUDGET_READ,
    CASE_BUDGET_ENTRY,
    CASE_BUDGET_DIRECTORY,
    CASE_BUDGET_TOO_LARGE,
    CASE_REFUSE_ALL_READS
} UmicomCase;

static void UmicomTestPrepare(UmicomIntegritySyntheticDisk *disk, UmicomCase which)
{
    UmicomTestBase(disk);
    if (which == CASE_EMPTY || which == CASE_BUDGET_READ
        || which == CASE_BUDGET_TOO_LARGE || which == CASE_REFUSE_ALL_READS) {
        return;
    }
    if (which == CASE_BAD_MBR) { disk->corruptMbr = true; return; }
    if (which == CASE_DIRTY) { disk->fat[1] = 0x7fffU; return; }
    if (which == CASE_MISMATCH) { disk->mirrorMismatch = true; return; }
    if (which == CASE_FAT_MUTATES_AFTER_ADMISSION) {
        disk->mutateAfterAdmission = true;
        return;
    }
    if (which == CASE_ROOT_DOT_ENTRY) {
        UmicomTestEntry(disk->root, ".", 0x10U, 4U, 0U);
        disk->fat[4] = UINT16_C(0xffff);
        return;
    }
    if (which == CASE_IO_BOOT) { disk->failSector = TEST_PARTITION; return; }
    if (which == CASE_IO_FAT) { disk->failSector = TEST_PARTITION + 1U; return; }
    if (which == CASE_IO_ROOT) { disk->failSector = TEST_ROOT_SECTOR; return; }
    if (which == CASE_FOLDER_EMPTY || which == CASE_FOLDER_CHILD
        || which == CASE_FOLDER_TWO_CHILDREN || which == CASE_FOLDER_CROSSLINK
        || which == CASE_FOLDER_LOOP || which == CASE_FOLDER_INVALID_SIZE
        || which == CASE_IO_DATA || which == CASE_BUDGET_DIRECTORY
        || which == CASE_BAD_DOT_POINTER || which == CASE_BAD_DOT_NAME
        || which == CASE_BAD_PARENT_POINTER) {
        UmicomTestEntry(disk->root, "WORK", 0x10U, 4U,
            which == CASE_FOLDER_INVALID_SIZE ? 2U : 0U);
        disk->fat[4] = which == CASE_FOLDER_LOOP ? 4U : UINT16_C(0xffff);
        UmicomTestEntry(disk->subdirectory, ".", 0x10U, 4U, 0U);
        UmicomTestEntry(disk->subdirectory + 32U, "..", 0x10U, 0U, 0U);
        if (which == CASE_FOLDER_CHILD || which == CASE_FOLDER_TWO_CHILDREN
            || which == CASE_FOLDER_CROSSLINK) {
            UmicomTestEntry(disk->subdirectory + 64U, "NOTE    TXT", 0x20U,
                which == CASE_FOLDER_CROSSLINK ? 4U : 5U, 512U);
            disk->fat[5] = UINT16_C(0xffff);
        }
        if (which == CASE_FOLDER_TWO_CHILDREN) {
            UmicomTestEntry(disk->subdirectory + 96U, "SECOND  TXT", 0x20U, 6U, 1U);
            disk->fat[6] = UINT16_C(0xffff);
        }
        if (which == CASE_BAD_DOT_POINTER) UmicomTestLe16(disk->subdirectory + 26U, 5U);
        if (which == CASE_BAD_DOT_NAME) disk->subdirectory[1] = (uint8_t)'Q';
        if (which == CASE_BAD_PARENT_POINTER)
            UmicomTestLe16(disk->subdirectory + 32U + 26U, 4U);
        if (which == CASE_IO_DATA) disk->failSector = TEST_DATA_SECTOR + 4U;
        return;
    }
    if (which == CASE_DELETED_ENTRY) {
        disk->root[0] = 0xe5U;
        UmicomTestEntry(disk->root + 32U, "NOTE    TXT", 0x20U, 2U, 1U);
        disk->fat[2] = UINT16_C(0xffff);
        return;
    }
    if (which == CASE_LFN_ENTRY) {
        UmicomTestEntry(disk->root, "ANAME", 0x0fU, 0U, 0U);
        UmicomTestEntry(disk->root + 32U, "NOTE    TXT", 0x20U, 2U, 1U);
        disk->fat[2] = UINT16_C(0xffff);
        return;
    }
    if (which == CASE_FILE_ZERO) {
        UmicomTestEntry(disk->root, "EMPTY   TXT", 0x20U, 0U, 0U);
        return;
    }
    UmicomTestEntry(disk->root, "NOTE    TXT", 0x20U, 2U, 512U);
    disk->fat[2] = UINT16_C(0xffff);
    if (which == CASE_FILE_TWO || which == CASE_CHAIN_CROSSLINK || which == CASE_BUDGET_ENTRY) {
        UmicomTestEntry(disk->root + 32U, "SECOND  TXT", 0x20U,
            which == CASE_CHAIN_CROSSLINK ? 2U : 3U, 1U);
        disk->fat[3] = UINT16_C(0xffff);
    }
    if (which == CASE_FILE_FRAGMENTED) {
        disk->fat[2] = 12U;
        disk->fat[12] = 7U;
        disk->fat[7] = UINT16_C(0xffff);
        UmicomTestLe32(disk->root + 28U, 2600U);
    }
    if (which == CASE_CHAIN_LOOP) disk->fat[2] = 2U;
    if (which == CASE_CHAIN_FREE) disk->fat[2] = 0U;
    if (which == CASE_CHAIN_OUT_OF_RANGE) disk->fat[2] = 9000U;
    if (which == CASE_CHAIN_BAD_MARKER) disk->fat[2] = UINT16_C(0xfff7);
    if (which == CASE_CHAIN_RESERVED_MARKER) disk->fat[2] = UINT16_C(0xfff2);
    if (which == CASE_CHAIN_TRUNCATED) UmicomTestLe32(disk->root + 28U, 4000U);
    if (which == CASE_CHAIN_ORPHAN) disk->fat[9] = UINT16_C(0xffff);
    if (which == CASE_BAD_MARKER) disk->fat[9] = UINT16_C(0xfff7);
    if (which == CASE_BAD_ENTRY_HIGH_CLUSTER) UmicomTestLe16(disk->root + 20U, 1U);
    if (which == CASE_BAD_ENTRY_ATTRIBUTE) disk->root[11] = UINT8_C(0x80);
    if (which == CASE_BAD_EMPTY_FILE_POINTER) UmicomTestLe32(disk->root + 28U, 0U);
}

typedef struct UmicomCaseSpec {
    const char *name;
    UmicomCase scenario;
    UmicomFat16IntegrityClassification expected;
} UmicomCaseSpec;

static const UmicomCaseSpec testCases[] = {
    {"empty", CASE_EMPTY, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"single-file", CASE_FILE_ONE, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"two-files", CASE_FILE_TWO, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"fragmented", CASE_FILE_FRAGMENTED, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"zero-file", CASE_FILE_ZERO, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"deleted-entry", CASE_DELETED_ENTRY, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"long-name-sidecar", CASE_LFN_ENTRY, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"empty-subdirectory", CASE_FOLDER_EMPTY, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"nested-file", CASE_FOLDER_CHILD, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"nested-two-files", CASE_FOLDER_TWO_CHILDREN, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"bad-cluster-marker", CASE_BAD_MARKER, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"cross-link", CASE_CHAIN_CROSSLINK, UMICOM_FAT16_INTEGRITY_CROSSLINK},
    {"cycle", CASE_CHAIN_LOOP, UMICOM_FAT16_INTEGRITY_LOOP},
    {"free-middle", CASE_CHAIN_FREE, UMICOM_FAT16_INTEGRITY_INVALID_CHAIN},
    {"out-of-range-next", CASE_CHAIN_OUT_OF_RANGE, UMICOM_FAT16_INTEGRITY_INVALID_CHAIN},
    {"bad-next", CASE_CHAIN_BAD_MARKER, UMICOM_FAT16_INTEGRITY_INVALID_CHAIN},
    {"reserved-next", CASE_CHAIN_RESERVED_MARKER, UMICOM_FAT16_INTEGRITY_INVALID_CHAIN},
    {"short-file", CASE_CHAIN_TRUNCATED, UMICOM_FAT16_INTEGRITY_TRUNCATED_CHAIN},
    {"orphan", CASE_CHAIN_ORPHAN, UMICOM_FAT16_INTEGRITY_ORPHAN_CLUSTERS},
    {"folder-cross-link", CASE_FOLDER_CROSSLINK, UMICOM_FAT16_INTEGRITY_CROSSLINK},
    {"folder-cycle", CASE_FOLDER_LOOP, UMICOM_FAT16_INTEGRITY_LOOP},
    {"folder-nonzero-size", CASE_FOLDER_INVALID_SIZE, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"high-word", CASE_BAD_ENTRY_HIGH_CLUSTER, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"invalid-attribute", CASE_BAD_ENTRY_ATTRIBUTE, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"zero-length-with-chain", CASE_BAD_EMPTY_FILE_POINTER, UMICOM_FAT16_INTEGRITY_CONSISTENT},
    {"wrong-dot-cluster", CASE_BAD_DOT_POINTER, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"forged-dot-name", CASE_BAD_DOT_NAME, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"wrong-parent-cluster", CASE_BAD_PARENT_POINTER, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"dot-entry-in-fixed-root", CASE_ROOT_DOT_ENTRY, UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY},
    {"clean-state-race", CASE_FAT_MUTATES_AFTER_ADMISSION, UMICOM_FAT16_INTEGRITY_NOT_ADMITTED},
    {"broken-mbr", CASE_BAD_MBR, UMICOM_FAT16_INTEGRITY_NOT_ADMITTED},
    {"dirty-flags", CASE_DIRTY, UMICOM_FAT16_INTEGRITY_NOT_ADMITTED},
    {"fat-mirror-mismatch", CASE_MISMATCH, UMICOM_FAT16_INTEGRITY_NOT_ADMITTED},
    {"root-read-failure", CASE_IO_ROOT, UMICOM_FAT16_INTEGRITY_IO_FAILURE},
    {"boot-read-failure", CASE_IO_BOOT, UMICOM_FAT16_INTEGRITY_IO_FAILURE},
    {"fat-read-failure", CASE_IO_FAT, UMICOM_FAT16_INTEGRITY_IO_FAILURE},
    {"directory-read-failure", CASE_IO_DATA, UMICOM_FAT16_INTEGRITY_IO_FAILURE},
    {"read-budget", CASE_BUDGET_READ, UMICOM_FAT16_INTEGRITY_LIMIT},
    {"entry-budget", CASE_BUDGET_ENTRY, UMICOM_FAT16_INTEGRITY_LIMIT},
    {"directory-budget", CASE_BUDGET_DIRECTORY, UMICOM_FAT16_INTEGRITY_LIMIT},
    {"overlarge-budget", CASE_BUDGET_TOO_LARGE, UMICOM_FAT16_INTEGRITY_LIMIT},
    {"all-reads-fail", CASE_REFUSE_ALL_READS, UMICOM_FAT16_INTEGRITY_IO_FAILURE}
};

static int UmicomRunCase(const UmicomCaseSpec *spec)
{
    UmicomIntegritySyntheticDisk disk;
    UmicomFat16AuditSource source = {0};
    UmicomFat16IntegrityOptions options = {0};
    UmicomFat16IntegrityReport report;
    UmicomFat16IntegrityClassification actual;
    UmicomTestPrepare(&disk, spec->scenario);
    source.context = &disk;
    source.readSector = UmicomSyntheticRead;
    source.mediaSectors = TEST_SECTORS;
    source.partitionIndex = 0U;
    if (spec->scenario == CASE_BUDGET_READ) options.maximumAdditionalReads = 1U;
    if (spec->scenario == CASE_BUDGET_ENTRY) options.maximumDirectoryEntries = 1U;
    if (spec->scenario == CASE_BUDGET_DIRECTORY) options.maximumDirectories = 1U;
    if (spec->scenario == CASE_BUDGET_TOO_LARGE) options.maximumAdditionalReads = 40000U;
    if (spec->scenario == CASE_REFUSE_ALL_READS) disk.refuseAllReads = true;
    if (spec->scenario == CASE_BUDGET_DIRECTORY) {
        UmicomTestEntry(disk.root + 32U, "OTHER", 0x10U, 6U, 0U);
        disk.fat[6] = UINT16_C(0xffff);
    }
    actual = UmicomFat16IntegrityInspect(&source, &options, &report);
    if (actual != spec->expected || report.classification != actual) {
        fprintf(stderr, "%s: expected %s; received %s (base %s)\n",
            spec->name, UmicomFat16IntegrityClassificationName(spec->expected),
            UmicomFat16IntegrityClassificationName(actual),
            UmicomFat16AuditClassificationName(report.header.classification));
        return 1;
    }
    if (report.header.sectorsRead > UMICOM_FAT16_AUDIT_DEFAULT_READ_BUDGET
        || report.additionalSectorsRead > UMICOM_FAT16_INTEGRITY_DEFAULT_READ_BUDGET) {
        fprintf(stderr, "%s exceeded bounded read work\n", spec->name);
        return 1;
    }
    if (actual == UMICOM_FAT16_INTEGRITY_CONSISTENT && !report.scannedNamespace) {
        fprintf(stderr, "%s did not scan namespace\n", spec->name);
        return 1;
    }
    if (spec->scenario == CASE_CHAIN_ORPHAN && report.orphanClusters != 1U) {
        fprintf(stderr, "orphan accounting should show one cluster\n");
        return 1;
    }
    if (spec->scenario == CASE_FOLDER_CHILD &&
        (report.directoriesVisited != 1U || report.filesVisited != 1U)) {
        fprintf(stderr, "nested path accounting is incorrect\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    uint32_t failures = 0U;
    size_t index;
    for (index = 0U; index < sizeof(testCases) / sizeof(testCases[0]); ++index) {
        failures += (uint32_t)UmicomRunCase(&testCases[index]);
    }
    /* The API must reject unowned result state rather than write to address 0. */
    if (UmicomFat16IntegrityInspect(NULL, NULL, NULL)
        != UMICOM_FAT16_INTEGRITY_INVALID_ARGUMENT) failures++;
    printf("Umicom FAT16 allocation graph audit: %zu scenarios, %" PRIu32 " failures\n",
        sizeof(testCases) / sizeof(testCases[0]) + 1U, failures);
    return failures == 0U ? 0 : 1;
}
