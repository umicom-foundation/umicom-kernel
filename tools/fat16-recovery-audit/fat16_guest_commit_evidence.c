/*-----------------------------------------------------------------------------
 * Umicom Kernel - independent full-image FAT16 guest commit comparison
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *
 * The existing auditors remain the authority for header and allocation
 * admission. This verifier independently maps a known SHORT-NAME file's
 * selected bytes to on-disk sectors, then compares the WHOLE disk image.
 * It refuses unexpected changes, including non-target directory or slack
 * bytes, instead of interpreting a clean FAT as proof of file-data success.
 *---------------------------------------------------------------------------*/
#include "fat16_guest_commit_evidence.h"
#include <stddef.h>
#include <string.h>

#define UMICOM_GUEST_EVIDENCE_MAX_MEDIA_SECTORS UINT64_C(32768)
#define UMICOM_GUEST_EVIDENCE_MAX_ROOT_SECTORS UINT32_C(128)
#define UMICOM_GUEST_EVIDENCE_PATCH_OFFSET UINT32_C(511)
#define UMICOM_GUEST_EVIDENCE_PATCH_LENGTH UINT32_C(21)
#define UMICOM_GUEST_EVIDENCE_MAX_CLUSTER_HOPS UINT32_C(32)

/* These values are on-disk byte sequences, not terminated C strings.
 * The original fixed-size string initialisers below silently discarded the
 * trailing NUL. GCC diagnoses that truncation with -Werror in newer versions.
 * Retain the historical form for review, but compile the exact payload bytes
 * explicitly so the FAT short name and patch never acquire an extra byte. */
#if 0
static const uint8_t umicomGuestExpectedPatch[UMICOM_GUEST_EVIDENCE_PATCH_LENGTH] =
    "Umicom ordered update";
static const uint8_t umicomGuestShortName[11] = "FRAG    BIN";
#endif

static const uint8_t umicomGuestExpectedPatch[] = {
    'U', 'm', 'i', 'c', 'o', 'm', ' ', 'o', 'r', 'd', 'e',
    'r', 'e', 'd', ' ', 'u', 'p', 'd', 'a', 't', 'e'
};
static const uint8_t umicomGuestShortName[] = {
    'F', 'R', 'A', 'G', ' ', ' ', ' ', ' ', 'B', 'I', 'N'
};
_Static_assert(sizeof umicomGuestExpectedPatch == UMICOM_GUEST_EVIDENCE_PATCH_LENGTH,
    "The ordered FAT16 patch must occupy exactly 21 on-disk bytes");
_Static_assert(sizeof umicomGuestShortName == 11U,
    "A FAT short-name directory entry must contain exactly 11 bytes");

typedef struct UmicomGuestPatchByte {
    uint64_t sector;
    uint32_t offset;
    uint8_t value;
} UmicomGuestPatchByte;

static uint16_t UmicomReadLe16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}
static uint32_t UmicomReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U)
         | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}
static bool UmicomGuestRead(const UmicomFat16AuditSource *source,
    uint64_t sector, uint8_t destination[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    return sector < source->mediaSectors &&
        source->readSector(source->context, sector, destination);
}
static uint16_t UmicomGuestNextCluster(const UmicomFat16AuditSource *source,
    uint64_t fatSector, uint32_t fatSectors, uint16_t cluster, bool *ok)
{
    uint8_t sector[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    const uint32_t bytes = (uint32_t)cluster * UINT32_C(2);
    if (bytes / UMICOM_FAT16_AUDIT_SECTOR_BYTES >= fatSectors ||
        !UmicomGuestRead(source, fatSector + bytes / UMICOM_FAT16_AUDIT_SECTOR_BYTES, sector)) {
        *ok = false;
        return 0U;
    }
    return UmicomReadLe16(sector + bytes % UMICOM_FAT16_AUDIT_SECTOR_BYTES);
}

/* Locate an explicit 8.3 root file without assuming that its fragmented
 * chain occupies consecutive physical clusters. All results are validated
 * against the source's independently admitted FAT16 geometry first. */
static bool UmicomGuestBuildPositions(const UmicomFat16AuditSource *source,
    const UmicomFat16AuditReport *header,
    UmicomGuestPatchByte positions[UMICOM_GUEST_EVIDENCE_PATCH_LENGTH],
    uint64_t *firstData, uint64_t *secondData)
{
    uint8_t boot[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint8_t sector[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint16_t visited[UMICOM_GUEST_EVIDENCE_MAX_CLUSTER_HOPS];
    const uint64_t first = header->partitionStart;
    if (!UmicomGuestRead(source, first, boot)) return false;
    const uint32_t bytesPerSector = UmicomReadLe16(boot + 11U);
    const uint32_t sectorsPerCluster = boot[13];
    const uint32_t reserved = UmicomReadLe16(boot + 14U);
    const uint32_t fatCopies = boot[16];
    const uint32_t rootEntries = UmicomReadLe16(boot + 17U);
    const uint32_t fatSectors = UmicomReadLe16(boot + 22U);
    if (bytesPerSector != 512U || !sectorsPerCluster ||
        (sectorsPerCluster & (sectorsPerCluster - 1U)) != 0U ||
        reserved != 1U || fatCopies != 2U || !rootEntries ||
        fatSectors != header->fatSectors) return false;
    const uint32_t rootSectors = (rootEntries * 32U + 511U) / 512U;
    if (rootSectors > UMICOM_GUEST_EVIDENCE_MAX_ROOT_SECTORS) return false;
    const uint64_t fat = first + reserved;
    const uint64_t root = fat + UINT64_C(2) * fatSectors;
    const uint64_t data = root + rootSectors;
    if (data >= first + header->partitionSectors || data >= source->mediaSectors) return false;
    uint16_t startCluster = 0U;
    uint32_t fileSize = 0U;
    unsigned matched = 0U;
    for (uint32_t n = 0U; n < rootEntries; ++n) {
        const uint64_t location = root + n / 16U;
        if (n % 16U == 0U && !UmicomGuestRead(source, location, sector)) return false;
        const uint8_t *entry = sector + (n % 16U) * 32U;
        if (entry[0] == 0U) break;
        if (entry[0] == 0xe5U || entry[11] == 0x0fU) continue;
        if (memcmp(entry, umicomGuestShortName, 11U) == 0) {
            if (entry[11] != 0x20U || ++matched != 1U) return false;
            startCluster = UmicomReadLe16(entry + 26U);
            fileSize = UmicomReadLe32(entry + 28U);
        }
    }
    if (matched != 1U || fileSize < UMICOM_GUEST_EVIDENCE_PATCH_OFFSET +
        UMICOM_GUEST_EVIDENCE_PATCH_LENGTH || startCluster < 2U) return false;
    const uint32_t clusterBytes = sectorsPerCluster * 512U;
    const uint32_t firstHop = UMICOM_GUEST_EVIDENCE_PATCH_OFFSET / clusterBytes;
    const uint32_t lastHop = (UMICOM_GUEST_EVIDENCE_PATCH_OFFSET +
        UMICOM_GUEST_EVIDENCE_PATCH_LENGTH - 1U) / clusterBytes;
    if (lastHop >= UMICOM_GUEST_EVIDENCE_MAX_CLUSTER_HOPS) return false;
    uint16_t clusters[UMICOM_GUEST_EVIDENCE_MAX_CLUSTER_HOPS];
    clusters[0] = startCluster;
    visited[0] = startCluster;
    for (uint32_t hop = 1U; hop <= lastHop; ++hop) {
        bool ok = true;
        const uint16_t next = UmicomGuestNextCluster(source, fat, fatSectors,
            clusters[hop - 1U], &ok);
        if (!ok || next < 2U || next >= 0xfff0U ||
            next >= header->clusterCount + 2U) return false;
        for (uint32_t n = 0U; n < hop; ++n) {
            if (visited[n] == next) return false;
        }
        visited[hop] = next;
        clusters[hop] = next;
    }
    for (uint32_t i = 0U; i < UMICOM_GUEST_EVIDENCE_PATCH_LENGTH; ++i) {
        const uint32_t logical = UMICOM_GUEST_EVIDENCE_PATCH_OFFSET + i;
        const uint32_t hop = logical / clusterBytes;
        const uint32_t local = logical % clusterBytes;
        const uint64_t physical = data +
            ((uint64_t)clusters[hop] - 2U) * sectorsPerCluster + local / 512U;
        if (physical >= first + header->partitionSectors || physical >= source->mediaSectors)
            return false;
        positions[i] = (UmicomGuestPatchByte){physical, local % 512U,
            umicomGuestExpectedPatch[i]};
    }
    if (firstHop > lastHop || positions[0].sector == positions[UMICOM_GUEST_EVIDENCE_PATCH_LENGTH - 1U].sector)
        return false; /* This exercise must actually cross a fragmented boundary. */
    *firstData = positions[0].sector;
    *secondData = positions[UMICOM_GUEST_EVIDENCE_PATCH_LENGTH - 1U].sector;
    return true;
}

UmicomFat16GuestEvidenceStatus UmicomFat16GuestEvidenceInspect(
    const UmicomFat16AuditSource *before, const UmicomFat16AuditSource *after,
    UmicomFat16GuestEvidenceMode mode, UmicomFat16GuestEvidenceReport *report)
{
    if (!report) return UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT;
    *report = (UmicomFat16GuestEvidenceReport){0};
    report->status = UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT;
    if (!before || !after || !before->readSector || !after->readSector ||
        !before->mediaSectors || before->mediaSectors != after->mediaSectors ||
        before->mediaSectors > UMICOM_GUEST_EVIDENCE_MAX_MEDIA_SECTORS ||
        before->partitionIndex != after->partitionIndex ||
        (mode != UMICOM_FAT16_GUEST_EVIDENCE_STAGED &&
         mode != UMICOM_FAT16_GUEST_EVIDENCE_FINISHED)) return report->status;

    UmicomFat16AuditReport admission = {0};
    UmicomFat16IntegrityReport graph = {0};
    UmicomFat16AuditReport afterHeader = {0};
    UmicomFat16IntegrityReport afterGraph = {0};
    report->beforeHeader = UmicomFat16AuditInspect(before, &admission);
    report->beforeIntegrity = UmicomFat16IntegrityInspect(before, NULL, &graph);
    if (report->beforeHeader != UMICOM_FAT16_AUDIT_CLEAN_MIRRORED ||
        report->beforeIntegrity != UMICOM_FAT16_INTEGRITY_CONSISTENT)
        return report->status = UMICOM_FAT16_GUEST_EVIDENCE_SOURCE_REFUSED;

    UmicomGuestPatchByte positions[UMICOM_GUEST_EVIDENCE_PATCH_LENGTH];
    if (!UmicomGuestBuildPositions(before, &admission, positions,
            &report->firstDataSector, &report->secondDataSector))
        return report->status = UMICOM_FAT16_GUEST_EVIDENCE_FORMAT_REFUSED;
    report->afterHeader = UmicomFat16AuditInspect(after, &afterHeader);
    report->afterIntegrity = UmicomFat16IntegrityInspect(after, NULL, &afterGraph);
    if (mode == UMICOM_FAT16_GUEST_EVIDENCE_STAGED) {
        if (report->afterHeader != UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR ||
            !afterHeader.fatMirrorsMatch || !afterHeader.fatCleanFlagClear ||
            afterHeader.fatIoErrorFlagClear)
            return report->status = UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED;
    } else if (report->afterHeader != UMICOM_FAT16_AUDIT_CLEAN_MIRRORED ||
               report->afterIntegrity != UMICOM_FAT16_INTEGRITY_CONSISTENT) {
        return report->status = UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED;
    }

    const uint64_t fatPrimary = admission.partitionStart + 1U; /* fixture reserved sectors == 1 */
    const uint64_t fatMirror = fatPrimary + admission.fatSectors;
    uint8_t expected[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint8_t actual[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    for (uint64_t sector = 0U; sector < before->mediaSectors; ++sector) {
        if (!UmicomGuestRead(before, sector, expected) ||
            !UmicomGuestRead(after, sector, actual))
            return report->status = UMICOM_FAT16_GUEST_EVIDENCE_READ_FAILED;
        for (uint32_t byte = 0U; byte < 512U; ++byte) {
            const uint8_t original = expected[byte];
            if (mode == UMICOM_FAT16_GUEST_EVIDENCE_STAGED && byte == 3U &&
                (sector == fatPrimary || sector == fatMirror)) expected[byte] &= UINT8_C(0x7f);
            for (uint32_t i = 0U; i < UMICOM_GUEST_EVIDENCE_PATCH_LENGTH; ++i) {
                if (positions[i].sector == sector && positions[i].offset == byte)
                    expected[byte] = positions[i].value;
            }
            if (expected[byte] != original) ++report->expectedDifferenceBytes;
            if (actual[byte] != original) ++report->actualDifferenceBytes;
            if (expected[byte] != actual[byte]) {
                if (++report->unexpectedBytes == 1U) {
                    report->firstUnexpectedSector = sector;
                    report->firstUnexpectedOffset = byte;
                    report->firstExpected = expected[byte];
                    report->firstActual = actual[byte];
                }
            }
        }
        ++report->mediaSectorsCompared;
    }
    report->fullImageCompared = true;
    if (report->expectedDifferenceBytes == 0U || report->unexpectedBytes != 0U)
        return report->status = UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER;
    return report->status = UMICOM_FAT16_GUEST_EVIDENCE_VERIFIED;
}

const char *UmicomFat16GuestEvidenceStatusName(UmicomFat16GuestEvidenceStatus status)
{
    switch (status) {
        case UMICOM_FAT16_GUEST_EVIDENCE_VERIFIED: return "verified";
        case UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_FAT16_GUEST_EVIDENCE_SOURCE_REFUSED: return "source-refused";
        case UMICOM_FAT16_GUEST_EVIDENCE_FORMAT_REFUSED: return "format-refused";
        case UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED: return "state-refused";
        case UMICOM_FAT16_GUEST_EVIDENCE_READ_FAILED: return "read-failed";
        case UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER: return "bytes-differ";
    }
    return "unknown";
}
