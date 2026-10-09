/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/fat16_recovery_audit.c
 *
 * PURPOSE:
 *   Bound read-only FAT16/Mirror/dirty-flag evidence gathering. This does NOT
 *   implement a filesystem, infer atomicity or reconstruct a failed commit.
 *
 * ARCHITECTURE:
 *   An independent read-only host inspector helps explain a rejected mount
 *   after power loss, without making the Kernel driver, mounted provider or
 *   ordered commit protocol depend on host filesystem APIs.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_recovery_audit.h"

#include <stddef.h>
#include <string.h>

static uint16_t UmicomReadLe16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8U));
}

static uint32_t UmicomReadLe32(const uint8_t *source)
{
    return (uint32_t)source[0]
           | ((uint32_t)source[1] << 8U)
           | ((uint32_t)source[2] << 16U)
           | ((uint32_t)source[3] << 24U);
}

static bool UmicomIsPowerOfTwo(uint32_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool UmicomReadSector(
    const UmicomFat16AuditSource *source,
    UmicomFat16AuditReport *report,
    uint64_t sector,
    uint8_t destination[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    const uint32_t budget = source->readBudget == 0U
                                ? UMICOM_FAT16_AUDIT_DEFAULT_READ_BUDGET
                                : source->readBudget;

    /* In particular, never let a hostile BPB wrap an address into the MBR. */
    if (sector >= source->mediaSectors || report->sectorsRead >= budget) {
        return false;
    }
    if (!source->readSector(source->context, sector, destination)) {
        return false;
    }
    report->sectorsRead += 1U;
    return true;
}

static bool UmicomHasSignature(const uint8_t sector[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    return sector[510] == UINT8_C(0x55) && sector[511] == UINT8_C(0xaa);
}

static bool UmicomIsFat16PartitionType(uint8_t partitionType)
{
    return partitionType == UINT8_C(0x04)
           || partitionType == UINT8_C(0x06)
           || partitionType == UINT8_C(0x0e);
}

static bool UmicomIsValidFatReservedEntries(
    const uint8_t sector[UMICOM_FAT16_AUDIT_SECTOR_BYTES],
    uint8_t mediaByte)
{
    const uint16_t fatEntryZero = UmicomReadLe16(sector);
    const uint16_t fatEntryOne = UmicomReadLe16(sector + 2U);
    return fatEntryZero == (uint16_t)(UINT16_C(0xff00) | mediaByte)
           && (fatEntryOne & UINT16_C(0x3fff)) == UINT16_C(0x3fff);
}

UmicomFat16AuditClassification UmicomFat16AuditInspect(
    const UmicomFat16AuditSource *source,
    UmicomFat16AuditReport *report)
{
    uint8_t mbr[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint8_t boot[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint8_t primary[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint8_t mirror[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint32_t partitionStart;
    uint32_t partitionSectors;
    uint32_t partitionTotal;
    uint32_t sectorsPerCluster;
    uint32_t reservedSectors;
    uint32_t fatSectors;
    uint32_t rootEntries;
    uint32_t rootSectors;
    uint32_t firstDataSector;
    uint32_t clusterCount;
    uint32_t fatIndex;
    uint8_t mediaByte;
    uint64_t primaryStart;
    uint64_t mirrorStart;
    const uint8_t *record;

    if (report == NULL) {
        return UMICOM_FAT16_AUDIT_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    report->classification = UMICOM_FAT16_AUDIT_INVALID_ARGUMENT;
    if (source == NULL || source->readSector == NULL
        || source->partitionIndex > 3U || source->mediaSectors == 0U) {
        return report->classification;
    }

    /* A classic primary MBR is required. GPT and extended partitions are
     * deliberately outside this narrow, inspect-only profile. */
    if (!UmicomReadSector(source, report, 0U, mbr)) {
        report->classification = UMICOM_FAT16_AUDIT_INCOMPLETE_READ;
        return report->classification;
    }
    if (!UmicomHasSignature(mbr)) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }
    record = mbr + 446U + (source->partitionIndex * 16U);
    if (!UmicomIsFat16PartitionType(record[4])) {
        report->classification = UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE;
        return report->classification;
    }
    partitionStart = UmicomReadLe32(record + 8U);
    partitionSectors = UmicomReadLe32(record + 12U);
    report->partitionStart = partitionStart;
    report->partitionSectors = partitionSectors;
    if (partitionStart == 0U || partitionSectors == 0U
        || ((uint64_t)partitionStart + (uint64_t)partitionSectors) > source->mediaSectors) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }
    if (!UmicomReadSector(source, report, partitionStart, boot)) {
        report->classification = UMICOM_FAT16_AUDIT_INCOMPLETE_READ;
        return report->classification;
    }
    if (!UmicomHasSignature(boot) || (boot[0] != UINT8_C(0xeb) && boot[0] != UINT8_C(0xe9))) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }
    if (UmicomReadLe16(boot + 11U) != UMICOM_FAT16_AUDIT_SECTOR_BYTES
        || !UmicomIsPowerOfTwo(boot[13]) || boot[13] > UINT8_C(128)
        || boot[16] != 2U) {
        report->classification = UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE;
        return report->classification;
    }
    sectorsPerCluster = boot[13];
    reservedSectors = UmicomReadLe16(boot + 14U);
    rootEntries = UmicomReadLe16(boot + 17U);
    fatSectors = UmicomReadLe16(boot + 22U);
    mediaByte = boot[21];
    partitionTotal = UmicomReadLe16(boot + 19U);
    if (partitionTotal == 0U) {
        partitionTotal = UmicomReadLe32(boot + 32U);
    } else if (UmicomReadLe32(boot + 32U) != 0U) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }

    if (reservedSectors == 0U || rootEntries == 0U || fatSectors == 0U
        || (rootEntries % 16U) != 0U || partitionTotal == 0U
        || partitionTotal > partitionSectors || UmicomReadLe32(boot + 28U) != partitionStart
        || mediaByte < UINT8_C(0xf0)) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }
    if (fatSectors > UMICOM_FAT16_AUDIT_MAX_FAT_SECTORS) {
        report->classification = UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE;
        return report->classification;
    }
    rootSectors = rootEntries / 16U;
    firstDataSector = reservedSectors + 2U * fatSectors + rootSectors;
    if (firstDataSector >= partitionTotal) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }
    clusterCount = (partitionTotal - firstDataSector) / sectorsPerCluster;
    if (clusterCount < 4085U || clusterCount >= 65525U) {
        report->classification = UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE;
        return report->classification;
    }
    if (fatSectors * (UMICOM_FAT16_AUDIT_SECTOR_BYTES / 2U) < clusterCount + 2U) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
        return report->classification;
    }

    report->recognisedFat16Geometry = true;
    report->clusterCount = clusterCount;
    report->fatSectors = fatSectors;
    report->reservedFatEntriesValid = true;
    report->fatMirrorsMatch = true;
    primaryStart = (uint64_t)partitionStart + reservedSectors;
    mirrorStart = primaryStart + fatSectors;

    /* A full, bounded two-FAT comparison is stricter than inspecting only
     * the dirty bits. Equal clean headers can still coexist with corrupt file
     * data, unreferenced clusters, a dishonest backend or an interrupted
     * publication after the final clean flag. Hence the limited result name. */
    for (fatIndex = 0U; fatIndex < fatSectors; ++fatIndex) {
        if (!UmicomReadSector(source, report, primaryStart + fatIndex, primary)
            || !UmicomReadSector(source, report, mirrorStart + fatIndex, mirror)) {
            report->classification = UMICOM_FAT16_AUDIT_INCOMPLETE_READ;
            return report->classification;
        }
        if (memcmp(primary, mirror, sizeof(primary)) != 0) {
            report->differingFatSectors += 1U;
            report->fatMirrorsMatch = false;
        }
        if (fatIndex == 0U) {
            const uint16_t primaryFlags = UmicomReadLe16(primary + 2U);
            const uint16_t mirrorFlags = UmicomReadLe16(mirror + 2U);
            report->reservedFatEntriesValid =
                UmicomIsValidFatReservedEntries(primary, mediaByte)
                && UmicomIsValidFatReservedEntries(mirror, mediaByte);
            report->fatCleanFlagClear = (primaryFlags & UINT16_C(0x8000)) == 0U
                                       || (mirrorFlags & UINT16_C(0x8000)) == 0U;
            report->fatIoErrorFlagClear = (primaryFlags & UINT16_C(0x4000)) == 0U
                                         || (mirrorFlags & UINT16_C(0x4000)) == 0U;
        }
    }
    if (!report->reservedFatEntriesValid) {
        report->classification = UMICOM_FAT16_AUDIT_INVALID_FORMAT;
    } else if (!report->fatMirrorsMatch) {
        report->classification = UMICOM_FAT16_AUDIT_MIRROR_MISMATCH;
    } else if (report->fatCleanFlagClear || report->fatIoErrorFlagClear) {
        report->classification = UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR;
    } else {
        report->classification = UMICOM_FAT16_AUDIT_CLEAN_MIRRORED;
    }
    return report->classification;
}

const char *UmicomFat16AuditClassificationName(
    UmicomFat16AuditClassification classification)
{
    switch (classification) {
        case UMICOM_FAT16_AUDIT_CLEAN_MIRRORED: return "clean-mirrored-fat";
        case UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR: return "dirty-or-io-error";
        case UMICOM_FAT16_AUDIT_MIRROR_MISMATCH: return "fat-mirror-mismatch";
        case UMICOM_FAT16_AUDIT_INVALID_FORMAT: return "invalid-media-format";
        case UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE: return "unsupported-fat16-profile";
        case UMICOM_FAT16_AUDIT_INCOMPLETE_READ: return "incomplete-read";
        case UMICOM_FAT16_AUDIT_INVALID_ARGUMENT: return "invalid-argument";
    }
    return "unknown-classification";
}
