/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/tests/recovery_tests.c
 *
 * PURPOSE:
 *   Exercise read-only recovery classification with a deterministic sector
 *   model, including untrusted geometry, dirty flags and truncated reads.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_recovery_audit.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PARTITION_START UINT32_C(2048)
#define TEST_PARTITION_SECTORS UINT32_C(12000)
#define TEST_FAT_SECTORS UINT32_C(32)

/* Each variation is independent: test order does not change the disk model. */
typedef enum UmicomTestVariation {
    CASE_CLEAN,
    CASE_MBR_SIGNATURE,
    CASE_MBR_TYPE,
    CASE_MBR_EMPTY,
    CASE_MBR_OUTSIDE,
    CASE_MBR_OVERRUN,
    CASE_BOOT_SIGNATURE,
    CASE_BOOT_JUMP,
    CASE_BOOT_1024_BYTES,
    CASE_BOOT_FAT_COUNT,
    CASE_BOOT_CLUSTER_ZERO,
    CASE_BOOT_CLUSTER_THREE,
    CASE_BOOT_RESERVED_ZERO,
    CASE_BOOT_ROOT_ZERO,
    CASE_BOOT_ROOT_ODD,
    CASE_BOOT_FAT_ZERO,
    CASE_BOOT_FAT_OVERSIZE,
    CASE_BOOT_TOTAL_ZERO,
    CASE_BOOT_TOTAL_BOTH,
    CASE_BOOT_TOTAL_OVERRUN,
    CASE_BOOT_TOO_MANY_RESERVED,
    CASE_BOOT_FAT_CAPACITY,
    CASE_BOOT_FAT12_GEOMETRY,
    CASE_BOOT_FAT32_GEOMETRY,
    CASE_BOOT_HIDDEN_SECTORS,
    CASE_BOOT_MEDIA_INVALID,
    CASE_PRIMARY_BAD_RESERVED,
    CASE_MIRROR_BAD_RESERVED,
    CASE_PRIMARY_DIRTY,
    CASE_MIRROR_DIRTY,
    CASE_BOTH_DIRTY,
    CASE_PRIMARY_IO_ERROR,
    CASE_MIRROR_IO_ERROR,
    CASE_BOTH_IO_ERROR,
    CASE_MISMATCH_FIRST,
    CASE_MISMATCH_LAST,
    CASE_MISMATCH_DIRTY,
    CASE_UNREADABLE_MBR,
    CASE_UNREADABLE_BOOT,
    CASE_UNREADABLE_PRIMARY,
    CASE_UNREADABLE_MIRROR,
    CASE_BUDGET_TOO_SMALL,
    CASE_PARTITION_INDEX_ONE,
    CASE_PARTITION_INDEX_THREE,
    CASE_COUNT
} UmicomTestVariation;

typedef struct UmicomTestDisk {
    UmicomTestVariation variation;
    uint64_t reads;
} UmicomTestDisk;

static void UmicomWriteLe16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & UINT16_C(0x00ff));
    destination[1] = (uint8_t)(value >> 8U);
}

static void UmicomWriteLe32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & UINT32_C(0xff));
    destination[1] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
    destination[2] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
    destination[3] = (uint8_t)((value >> 24U) & UINT32_C(0xff));
}

static bool UmicomModelReadSector(
    void *context, uint64_t sectorNumber,
    uint8_t destination[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    UmicomTestDisk *disk = (UmicomTestDisk *)context;
    UmicomTestVariation variation = disk->variation;
    bool isPrimary;
    bool isMirror;
    uint64_t offset;
    uint16_t flags;

    ++disk->reads;
    if ((variation == CASE_UNREADABLE_MBR && sectorNumber == 0U)
        || (variation == CASE_UNREADABLE_BOOT && sectorNumber == TEST_PARTITION_START)
        || (variation == CASE_UNREADABLE_PRIMARY && sectorNumber == TEST_PARTITION_START + 1U)
        || (variation == CASE_UNREADABLE_MIRROR && sectorNumber == TEST_PARTITION_START + 33U)) {
        return false;
    }
    memset(destination, 0, UMICOM_FAT16_AUDIT_SECTOR_BYTES);
    if (sectorNumber == 0U) {
        uint8_t *partition = destination + 446U;
        destination[510] = 0x55U;
        destination[511] = 0xaaU;
        partition[4] = 0x06U;
        UmicomWriteLe32(partition + 8U, TEST_PARTITION_START);
        UmicomWriteLe32(partition + 12U, TEST_PARTITION_SECTORS);
        if (variation == CASE_MBR_SIGNATURE) destination[510] = 0U;
        if (variation == CASE_MBR_TYPE) partition[4] = 0x83U;
        if (variation == CASE_MBR_EMPTY) UmicomWriteLe32(partition + 12U, 0U);
        if (variation == CASE_MBR_OUTSIDE) UmicomWriteLe32(partition + 8U, 35000U);
        if (variation == CASE_MBR_OVERRUN) UmicomWriteLe32(partition + 12U, UINT32_MAX);
        if (variation == CASE_BOOT_FAT32_GEOMETRY) UmicomWriteLe32(partition + 12U, 200000U);
        if (variation == CASE_PARTITION_INDEX_ONE) {
            memcpy(destination + 462U, partition, 16U);
            memset(partition, 0, 16U);
        }
        if (variation == CASE_PARTITION_INDEX_THREE) {
            memcpy(destination + 494U, partition, 16U);
            memset(partition, 0, 16U);
        }
        return true;
    }
    if (sectorNumber == TEST_PARTITION_START) {
        destination[0] = 0xebU;
        destination[2] = 0x90U;
        UmicomWriteLe16(destination + 11U, 512U);
        destination[13] = 2U;
        UmicomWriteLe16(destination + 14U, 1U);
        destination[16] = 2U;
        UmicomWriteLe16(destination + 17U, 512U);
        UmicomWriteLe16(destination + 19U, (uint16_t)TEST_PARTITION_SECTORS);
        destination[21] = 0xf8U;
        UmicomWriteLe16(destination + 22U, (uint16_t)TEST_FAT_SECTORS);
        UmicomWriteLe32(destination + 28U, TEST_PARTITION_START);
        destination[510] = 0x55U;
        destination[511] = 0xaaU;
        switch (variation) {
            case CASE_BOOT_SIGNATURE: destination[510] = 0U; break;
            case CASE_BOOT_JUMP: destination[0] = 0U; break;
            case CASE_BOOT_1024_BYTES: UmicomWriteLe16(destination + 11U, 1024U); break;
            case CASE_BOOT_FAT_COUNT: destination[16] = 1U; break;
            case CASE_BOOT_CLUSTER_ZERO: destination[13] = 0U; break;
            case CASE_BOOT_CLUSTER_THREE: destination[13] = 3U; break;
            case CASE_BOOT_RESERVED_ZERO: UmicomWriteLe16(destination + 14U, 0U); break;
            case CASE_BOOT_ROOT_ZERO: UmicomWriteLe16(destination + 17U, 0U); break;
            case CASE_BOOT_ROOT_ODD: UmicomWriteLe16(destination + 17U, 513U); break;
            case CASE_BOOT_FAT_ZERO: UmicomWriteLe16(destination + 22U, 0U); break;
            case CASE_BOOT_FAT_OVERSIZE: UmicomWriteLe16(destination + 22U, 513U); break;
            case CASE_BOOT_TOTAL_ZERO: UmicomWriteLe16(destination + 19U, 0U); break;
            case CASE_BOOT_TOTAL_BOTH: UmicomWriteLe32(destination + 32U, 12000U); break;
            case CASE_BOOT_TOTAL_OVERRUN: UmicomWriteLe16(destination + 19U, 12001U); break;
            case CASE_BOOT_TOO_MANY_RESERVED: UmicomWriteLe16(destination + 14U, 11999U); break;
            case CASE_BOOT_FAT_CAPACITY: UmicomWriteLe16(destination + 22U, 1U); break;
            case CASE_BOOT_FAT12_GEOMETRY: destination[13] = 4U; break;
            case CASE_BOOT_FAT32_GEOMETRY:
                UmicomWriteLe16(destination + 19U, 0U);
                UmicomWriteLe32(destination + 32U, 200000U);
                UmicomWriteLe16(destination + 22U, 512U);
                break;
            case CASE_BOOT_HIDDEN_SECTORS: UmicomWriteLe32(destination + 28U, 2049U); break;
            case CASE_BOOT_MEDIA_INVALID: destination[21] = 0x80U; break;
            default: break;
        }
        return true;
    }
    isPrimary = sectorNumber >= TEST_PARTITION_START + 1U
        && sectorNumber < TEST_PARTITION_START + 1U + TEST_FAT_SECTORS;
    isMirror = sectorNumber >= TEST_PARTITION_START + 1U + TEST_FAT_SECTORS
        && sectorNumber < TEST_PARTITION_START + 1U + 2U * TEST_FAT_SECTORS;
    if (isPrimary || isMirror) {
        offset = isPrimary
            ? sectorNumber - (TEST_PARTITION_START + 1U)
            : sectorNumber - (TEST_PARTITION_START + 1U + TEST_FAT_SECTORS);
        if (offset == 0U) {
            UmicomWriteLe16(destination, 0xfff8U);
            flags = UINT16_C(0xffff);
            if ((isPrimary && (variation == CASE_PRIMARY_DIRTY || variation == CASE_BOTH_DIRTY || variation == CASE_MISMATCH_DIRTY))
                || (isMirror && (variation == CASE_MIRROR_DIRTY || variation == CASE_BOTH_DIRTY))) {
                flags &= (uint16_t)~UINT16_C(0x8000);
            }
            if ((isPrimary && (variation == CASE_PRIMARY_IO_ERROR || variation == CASE_BOTH_IO_ERROR))
                || (isMirror && (variation == CASE_MIRROR_IO_ERROR || variation == CASE_BOTH_IO_ERROR))) {
                flags &= (uint16_t)~UINT16_C(0x4000);
            }
            UmicomWriteLe16(destination + 2U, flags);
            if (isPrimary && variation == CASE_PRIMARY_BAD_RESERVED) destination[0] = 0U;
            if (isMirror && variation == CASE_MIRROR_BAD_RESERVED) destination[3] = 0U;
            if (isMirror && variation == CASE_MISMATCH_FIRST) destination[16] ^= UINT8_C(0x20);
        }
        if (isMirror && offset == TEST_FAT_SECTORS - 1U && variation == CASE_MISMATCH_LAST) {
            destination[511] ^= UINT8_C(0x01);
        }
    }
    return true;
}

typedef struct UmicomTestCase {
    UmicomTestVariation variation;
    UmicomFat16AuditClassification expected;
} UmicomTestCase;

static const UmicomTestCase cases[] = {
    {CASE_CLEAN, UMICOM_FAT16_AUDIT_CLEAN_MIRRORED},
    {CASE_MBR_SIGNATURE, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_MBR_TYPE, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_MBR_EMPTY, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_MBR_OUTSIDE, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_MBR_OVERRUN, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_SIGNATURE, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_JUMP, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_1024_BYTES, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_FAT_COUNT, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_CLUSTER_ZERO, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_CLUSTER_THREE, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_RESERVED_ZERO, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_ROOT_ZERO, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_ROOT_ODD, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_FAT_ZERO, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_FAT_OVERSIZE, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_TOTAL_ZERO, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_TOTAL_BOTH, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_TOTAL_OVERRUN, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_TOO_MANY_RESERVED, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_FAT_CAPACITY, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_FAT12_GEOMETRY, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_FAT32_GEOMETRY, UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE},
    {CASE_BOOT_HIDDEN_SECTORS, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_BOOT_MEDIA_INVALID, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_PRIMARY_BAD_RESERVED, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_MIRROR_BAD_RESERVED, UMICOM_FAT16_AUDIT_INVALID_FORMAT},
    {CASE_PRIMARY_DIRTY, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_MIRROR_DIRTY, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_BOTH_DIRTY, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR},
    {CASE_PRIMARY_IO_ERROR, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_MIRROR_IO_ERROR, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_BOTH_IO_ERROR, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR},
    {CASE_MISMATCH_FIRST, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_MISMATCH_LAST, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_MISMATCH_DIRTY, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH},
    {CASE_UNREADABLE_MBR, UMICOM_FAT16_AUDIT_INCOMPLETE_READ},
    {CASE_UNREADABLE_BOOT, UMICOM_FAT16_AUDIT_INCOMPLETE_READ},
    {CASE_UNREADABLE_PRIMARY, UMICOM_FAT16_AUDIT_INCOMPLETE_READ},
    {CASE_UNREADABLE_MIRROR, UMICOM_FAT16_AUDIT_INCOMPLETE_READ},
    {CASE_BUDGET_TOO_SMALL, UMICOM_FAT16_AUDIT_INCOMPLETE_READ},
    {CASE_PARTITION_INDEX_ONE, UMICOM_FAT16_AUDIT_CLEAN_MIRRORED},
    {CASE_PARTITION_INDEX_THREE, UMICOM_FAT16_AUDIT_CLEAN_MIRRORED}
};

int main(void)
{
    size_t index;
    unsigned failures = 0U;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        UmicomTestDisk disk = {.variation = cases[index].variation, .reads = 0U};
        UmicomFat16AuditSource source = {
            .context = &disk,
            .readSector = UmicomModelReadSector,
            .mediaSectors = cases[index].variation == CASE_BOOT_FAT32_GEOMETRY
                ? 300000U : 32768U,
            .partitionIndex = 0U,
            .readBudget = cases[index].variation == CASE_BUDGET_TOO_SMALL ? 2U : 0U
        };
        UmicomFat16AuditReport report;
        UmicomFat16AuditClassification result;
        if (cases[index].variation == CASE_PARTITION_INDEX_ONE) source.partitionIndex = 1U;
        if (cases[index].variation == CASE_PARTITION_INDEX_THREE) source.partitionIndex = 3U;
        result = UmicomFat16AuditInspect(&source, &report);
        if (result != cases[index].expected || report.classification != result
            || (result == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED
                && (report.sectorsRead != 66U || !report.fatMirrorsMatch
                    || !report.recognisedFat16Geometry || report.fatCleanFlagClear))) {
            fprintf(stderr, "CASE %zu failed: expected %s got %s, sectors %" PRIu32 "\n",
                index, UmicomFat16AuditClassificationName(cases[index].expected),
                UmicomFat16AuditClassificationName(result), report.sectorsRead);
            failures++;
        }
        if (disk.reads > UINT64_C(1100)) {
            fprintf(stderr, "CASE %zu exceeded the read limit\n", index);
            failures++;
        }
    }
    /* Invalid inputs must never dereference untrusted pointers or call media. */
    {
        UmicomTestDisk disk = {0};
        UmicomFat16AuditSource source = {&disk, UmicomModelReadSector, 32768U, 4U, 0U};
        UmicomFat16AuditReport report;
        if (UmicomFat16AuditInspect(&source, &report) != UMICOM_FAT16_AUDIT_INVALID_ARGUMENT
            || UmicomFat16AuditInspect(NULL, &report) != UMICOM_FAT16_AUDIT_INVALID_ARGUMENT
            || UmicomFat16AuditInspect(&source, NULL) != UMICOM_FAT16_AUDIT_INVALID_ARGUMENT
            || disk.reads != 0U) {
            fputs("Invalid-argument classification failed\n", stderr);
            failures++;
        }
    }
    printf("Umicom FAT16 read-only recovery audit: %zu scenario cases, %u failures\n",
        sizeof(cases) / sizeof(cases[0]) + 3U, failures);
    return failures == 0U ? 0 : 1;
}
