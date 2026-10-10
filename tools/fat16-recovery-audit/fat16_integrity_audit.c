/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/fat16_integrity_audit.c
 *
 * PURPOSE:
 *   Bounded, non-mutating FAT16 file-chain and directory-ownership audit.
 *
 * DESIGN:
 *   Existing sector/MRB/dirty-flag checks are admission prerequisites. The
 *   hosted audit builds a bounded FAT snapshot, compares both copies again,
 *   then follows directory entries and verifies referenced cluster chains.
 *   Independent owners of one cluster are a cross-link; repeated clusters in
 *   one chain are loops; allocated clusters without a namespace owner are
 *   reported, never reclaimed. All I/O is through the READ-only callback.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_integrity_audit.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define UMICOM_MAX_FAT16_CLUSTERS UINT32_C(65524)
#define UMICOM_FAT16_END_MIN UINT16_C(0xfff8)
#define UMICOM_FAT16_BAD_CLUSTER UINT16_C(0xfff7)

typedef struct UmicomDirectoryWork {
    uint16_t firstCluster;
    uint16_t parentCluster;
} UmicomDirectoryWork;

typedef struct UmicomIntegrityScan {
    const UmicomFat16AuditSource *source;
    UmicomFat16IntegrityReport *report;
    uint32_t readBudget;
    uint32_t entryBudget;
    uint32_t directoryBudget;
    uint32_t clusterCount;
    uint32_t sectorsPerCluster;
    uint8_t mediaByte;
    uint32_t clusterBytes;
    uint32_t rootSectors;
    uint64_t primaryStart;
    uint64_t mirrorStart;
    uint64_t rootStart;
    uint64_t dataStart;
    uint64_t partitionEnd;
    uint16_t *fat;
    uint32_t *owners;
    UmicomDirectoryWork *directories;
    uint32_t directoryCount;
    uint32_t nextDirectory;
    uint32_t nextOwner;
    bool limitReached;
    bool ioFailed;
    bool fatChanged;
} UmicomIntegrityScan;

static uint16_t UmicomIntegrityReadLe16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8U));
}

static uint32_t UmicomIntegrityReadLe32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U)
           | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool UmicomIntegrityRead(UmicomIntegrityScan *scan, uint64_t sector,
    uint8_t bytes[UMICOM_FAT16_AUDIT_SECTOR_BYTES])
{
    if (scan->report->additionalSectorsRead >= scan->readBudget) {
        scan->limitReached = true;
        return false;
    }
    if (sector >= scan->source->mediaSectors || sector >= scan->partitionEnd
        || !scan->source->readSector(scan->source->context, sector, bytes)) {
        scan->ioFailed = true;
        return false;
    }
    scan->report->additionalSectorsRead += 1U;
    return true;
}

static bool UmicomIntegrityReadFat(UmicomIntegrityScan *scan)
{
    uint8_t primary[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint8_t mirror[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    const uint32_t fatSectors = scan->report->header.fatSectors;
    uint32_t sectorIndex;
    for (sectorIndex = 0U; sectorIndex < fatSectors; ++sectorIndex) {
        uint32_t entry;
        if (!UmicomIntegrityRead(scan, scan->primaryStart + sectorIndex, primary)
            || !UmicomIntegrityRead(scan, scan->mirrorStart + sectorIndex, mirror)) {
            return false;
        }
        if (memcmp(primary, mirror, sizeof(primary)) != 0) {
            scan->fatChanged = true;
            return false;
        }
        for (entry = 0U; entry < 256U; ++entry) {
            const uint32_t index = sectorIndex * 256U + entry;
            if (index <= scan->clusterCount + 1U) {
                scan->fat[index] = UmicomIntegrityReadLe16(primary + entry * 2U);
            }
        }
    }
    /* Never assume FAT clean-state admission remains true after initial scan.
     * This does not establish a snapshot lock against external concurrent IO. */
    if (scan->fat[0] != (uint16_t)(UINT16_C(0xff00) | scan->mediaByte)
        || scan->fat[1] != UINT16_C(0xffff)) {
        scan->fatChanged = true;
        return false;
    }
    return true;
}

static bool UmicomIntegrityValidCluster(const UmicomIntegrityScan *scan, uint32_t cluster)
{
    return cluster >= 2U && cluster <= scan->clusterCount + 1U;
}

/* One first-cluster chain belongs to precisely one named directory entry.
 * Following a chain twice with the same owner is a cycle, whereas reaching a
 * cluster owned by another entry is a cross-link. */
static bool UmicomIntegrityClaimChain(UmicomIntegrityScan *scan, uint16_t firstCluster,
    uint32_t requiredBytes, bool directory)
{
    uint32_t owner = ++scan->nextOwner;
    uint32_t cluster = firstCluster;
    uint32_t traversed = 0U;
    if (!UmicomIntegrityValidCluster(scan, cluster)) {
        scan->report->invalidChains += 1U;
        return false;
    }
    while (true) {
        uint16_t next;
        if (!UmicomIntegrityValidCluster(scan, cluster)) {
            scan->report->invalidChains += 1U;
            return false;
        }
        if (scan->owners[cluster] != 0U) {
            if (scan->owners[cluster] == owner) {
                scan->report->loopedChains += 1U;
            } else {
                scan->report->crossLinkedClusters += 1U;
            }
            return false;
        }
        scan->owners[cluster] = owner;
        scan->report->referencedClusters += 1U;
        ++traversed;
        next = scan->fat[cluster];
        if (next >= UMICOM_FAT16_END_MIN) {
            break;
        }
        if (next < 2U || next == UMICOM_FAT16_BAD_CLUSTER
            || (next >= UINT16_C(0xfff0) && next <= UINT16_C(0xfff6))
            || !UmicomIntegrityValidCluster(scan, next)) {
            scan->report->invalidChains += 1U;
            return false;
        }
        cluster = next;
    }
    if (!directory && ((uint64_t)traversed * scan->clusterBytes) < requiredBytes) {
        scan->report->truncatedChains += 1U;
        return false;
    }
    return true;
}

/* Entries are processed from their on-disk 32-byte representation. No path or
 * filename is interpreted as a host path; long-name slots are skipped. */
static bool UmicomIntegrityEntry(UmicomIntegrityScan *scan, const uint8_t *entry,
    uint16_t currentDirectory, uint16_t parentDirectory)
{
    const uint8_t attr = entry[11];
    const uint16_t firstCluster = UmicomIntegrityReadLe16(entry + 26U);
    const uint32_t length = UmicomIntegrityReadLe32(entry + 28U);
    if (scan->report->directoryEntriesVisited >= scan->entryBudget) {
        scan->limitReached = true;
        return false;
    }
    scan->report->directoryEntriesVisited += 1U;
    if (entry[0] == 0xe5U || attr == 0x0fU || (attr & 0x08U) != 0U) {
        return true;
    }
    if ((attr & UINT8_C(0xc0)) != 0U || UmicomIntegrityReadLe16(entry + 20U) != 0U) {
        scan->report->invalidEntries += 1U;
        return false;
    }
    if ((attr & 0x10U) != 0U) {
        if (entry[0] == (uint8_t)'.') {
            /* Dot entries deliberately refer to an existing directory owner.
             * Check their exact 8.3 names and expected targets instead of
             * skipping every name with a leading dot (which could conceal
             * another cluster pointer). The fixed root has no dot entries. */
            static const uint8_t selfName[11] = {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
            static const uint8_t parentName[11] = {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};
            bool valid = length == 0U && currentDirectory != 0U
                && ((memcmp(entry, selfName, sizeof(selfName)) == 0
                     && firstCluster == currentDirectory)
                    || (memcmp(entry, parentName, sizeof(parentName)) == 0
                        && firstCluster == parentDirectory));
            if (!valid) scan->report->invalidEntries += 1U;
            return valid;
        }
        if (length != 0U) {
            scan->report->invalidEntries += 1U;
            return false;
        }
        if (scan->directoryCount >= scan->directoryBudget) {
            scan->limitReached = true;
            return false;
        }
        if (!UmicomIntegrityClaimChain(scan, firstCluster, 0U, true)) {
            return false;
        }
        scan->directories[scan->directoryCount].firstCluster = firstCluster;
        scan->directories[scan->directoryCount].parentCluster = currentDirectory;
        scan->directoryCount += 1U;
    } else {
        scan->report->filesVisited += 1U;
        if (firstCluster == 0U && length == 0U) {
            return true;
        }
        if (!UmicomIntegrityClaimChain(scan, firstCluster, length, false)) {
            return false;
        }
    }
    return true;
}

static bool UmicomIntegrityProcessSector(UmicomIntegrityScan *scan,
    const uint8_t sector[UMICOM_FAT16_AUDIT_SECTOR_BYTES], bool *ended,
    uint16_t currentDirectory, uint16_t parentDirectory)
{
    uint32_t slot;
    for (slot = 0U; slot < 16U; ++slot) {
        const uint8_t *entry = sector + slot * 32U;
        if (entry[0] == 0x00U) {
            *ended = true;
            return true;
        }
        if (!UmicomIntegrityEntry(scan, entry, currentDirectory, parentDirectory)) {
            return false;
        }
    }
    return true;
}

static bool UmicomIntegrityScanRoot(UmicomIntegrityScan *scan)
{
    uint32_t index;
    uint8_t bytes[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    for (index = 0U; index < scan->rootSectors; ++index) {
        bool ended = false;
        if (!UmicomIntegrityRead(scan, scan->rootStart + index, bytes)
            || !UmicomIntegrityProcessSector(scan, bytes, &ended, 0U, 0U)) {
            return false;
        }
        if (ended) {
            break;
        }
    }
    return true;
}

static bool UmicomIntegrityScanDirectories(UmicomIntegrityScan *scan)
{
    uint8_t bytes[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    while (scan->nextDirectory < scan->directoryCount) {
        uint32_t cluster = scan->directories[scan->nextDirectory].firstCluster;
        const uint16_t parentCluster = scan->directories[scan->nextDirectory].parentCluster;
        const uint16_t currentCluster = scan->directories[scan->nextDirectory].firstCluster;
        bool ended = false;
        scan->nextDirectory += 1U;
        scan->report->directoriesVisited += 1U;
        while (!ended) {
            uint32_t sectorOffset;
            uint16_t next;
            if (!UmicomIntegrityValidCluster(scan, cluster)) {
                scan->report->invalidChains += 1U;
                return false;
            }
            for (sectorOffset = 0U; sectorOffset < scan->sectorsPerCluster; ++sectorOffset) {
                const uint64_t absolute = scan->dataStart
                    + ((uint64_t)(cluster - 2U) * scan->sectorsPerCluster) + sectorOffset;
                if (!UmicomIntegrityRead(scan, absolute, bytes)
                    || !UmicomIntegrityProcessSector(scan, bytes, &ended,
                        currentCluster, parentCluster)) {
                    return false;
                }
                if (ended) {
                    break;
                }
            }
            if (ended) break;
            next = scan->fat[cluster];
            if (next >= UMICOM_FAT16_END_MIN) break;
            if (!UmicomIntegrityValidCluster(scan, next)) {
                scan->report->invalidChains += 1U;
                return false;
            }
            cluster = next;
        }
    }
    return true;
}

static void UmicomIntegrityCountOrphans(UmicomIntegrityScan *scan)
{
    uint32_t cluster;
    for (cluster = 2U; cluster <= scan->clusterCount + 1U; ++cluster) {
        const uint16_t link = scan->fat[cluster];
        if (link == UMICOM_FAT16_BAD_CLUSTER) {
            scan->report->badClusterMarkers += 1U;
        } else if (link != 0U) {
            scan->report->allocatedClusters += 1U;
            if (scan->owners[cluster] == 0U) {
                scan->report->orphanClusters += 1U;
            }
        }
    }
}

static UmicomFat16IntegrityClassification UmicomIntegritySelectResult(
    const UmicomIntegrityScan *scan)
{
    if (scan->limitReached) return UMICOM_FAT16_INTEGRITY_LIMIT;
    if (scan->ioFailed) return UMICOM_FAT16_INTEGRITY_IO_FAILURE;
    if (scan->fatChanged) return UMICOM_FAT16_INTEGRITY_NOT_ADMITTED;
    if (scan->report->crossLinkedClusters > 0U) return UMICOM_FAT16_INTEGRITY_CROSSLINK;
    if (scan->report->loopedChains > 0U) return UMICOM_FAT16_INTEGRITY_LOOP;
    if (scan->report->invalidChains > 0U) return UMICOM_FAT16_INTEGRITY_INVALID_CHAIN;
    if (scan->report->truncatedChains > 0U) return UMICOM_FAT16_INTEGRITY_TRUNCATED_CHAIN;
    if (scan->report->invalidEntries > 0U) return UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY;
    if (scan->report->orphanClusters > 0U) return UMICOM_FAT16_INTEGRITY_ORPHAN_CLUSTERS;
    return UMICOM_FAT16_INTEGRITY_CONSISTENT;
}

UmicomFat16IntegrityClassification UmicomFat16IntegrityInspect(
    const UmicomFat16AuditSource *source,
    const UmicomFat16IntegrityOptions *options,
    UmicomFat16IntegrityReport *report)
{
    UmicomIntegrityScan scan = {0};
    uint8_t boot[UMICOM_FAT16_AUDIT_SECTOR_BYTES];
    uint32_t reserved;
    uint32_t rootEntries;
    uint32_t dataOffset;
    uint32_t fatEntries;
    uint32_t partitionTotal;
    uint32_t calculatedClusterCount;
    uint32_t newFirstDataSector;
    if (report == NULL) {
        return UMICOM_FAT16_INTEGRITY_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    report->classification = UMICOM_FAT16_INTEGRITY_INVALID_ARGUMENT;
    if (source == NULL || source->readSector == NULL || source->mediaSectors == 0U) {
        return report->classification;
    }
    if (UmicomFat16AuditInspect(source, &report->header) != UMICOM_FAT16_AUDIT_CLEAN_MIRRORED) {
        report->classification = UMICOM_FAT16_INTEGRITY_NOT_ADMITTED;
        if (report->header.classification == UMICOM_FAT16_AUDIT_INCOMPLETE_READ) {
            report->classification = UMICOM_FAT16_INTEGRITY_IO_FAILURE;
        }
        return report->classification;
    }
    scan.source = source;
    scan.report = report;
    scan.readBudget = (options != NULL && options->maximumAdditionalReads != 0U)
                          ? options->maximumAdditionalReads : UMICOM_FAT16_INTEGRITY_DEFAULT_READ_BUDGET;
    scan.entryBudget = (options != NULL && options->maximumDirectoryEntries != 0U)
                          ? options->maximumDirectoryEntries : UMICOM_FAT16_INTEGRITY_DEFAULT_ENTRY_BUDGET;
    scan.directoryBudget = (options != NULL && options->maximumDirectories != 0U)
                          ? options->maximumDirectories : UMICOM_FAT16_INTEGRITY_DEFAULT_DIRECTORY_BUDGET;
    /* The caller may impose tighter limits. Never allocate unbounded queues. */
    if (scan.directoryBudget > UMICOM_FAT16_INTEGRITY_DEFAULT_DIRECTORY_BUDGET
        || scan.entryBudget > UMICOM_FAT16_INTEGRITY_DEFAULT_ENTRY_BUDGET
        || scan.readBudget > UMICOM_FAT16_INTEGRITY_DEFAULT_READ_BUDGET) {
        report->classification = UMICOM_FAT16_INTEGRITY_LIMIT;
        return report->classification;
    }
    scan.partitionEnd = report->header.partitionStart + report->header.partitionSectors;
    if (!UmicomIntegrityRead(&scan, report->header.partitionStart, boot)) {
        goto finish;
    }
    scan.sectorsPerCluster = boot[13];
    scan.mediaByte = boot[21];
    reserved = UmicomIntegrityReadLe16(boot + 14U);
    rootEntries = UmicomIntegrityReadLe16(boot + 17U);
    partitionTotal = UmicomIntegrityReadLe16(boot + 19U);
    if (partitionTotal == 0U) partitionTotal = UmicomIntegrityReadLe32(boot + 32U);
    /* Reject a changed boot/BPB interpretation between the preliminary
     * mirror/dirty audit and the namespace walk. This is a race detector,
     * not a snapshot lock or a substitute for a stable image copy. */
    if ((boot[0] != UINT8_C(0xeb) && boot[0] != UINT8_C(0xe9))
        || boot[510] != UINT8_C(0x55) || boot[511] != UINT8_C(0xaa)
        || UmicomIntegrityReadLe16(boot + 11U) != UMICOM_FAT16_AUDIT_SECTOR_BYTES
        || scan.sectorsPerCluster == 0U || scan.sectorsPerCluster > 128U
        || (scan.sectorsPerCluster & (scan.sectorsPerCluster - 1U)) != 0U
        || reserved == 0U || rootEntries == 0U || (rootEntries % 16U) != 0U
        || UmicomIntegrityReadLe16(boot + 22U) != report->header.fatSectors
        || boot[16] != 2U
        || UmicomIntegrityReadLe32(boot + 28U) != report->header.partitionStart
        || partitionTotal == 0U || partitionTotal > report->header.partitionSectors) {
        scan.fatChanged = true;
        goto finish;
    }
    scan.rootSectors = rootEntries / 16U;
    newFirstDataSector = reserved + 2U * report->header.fatSectors + scan.rootSectors;
    if (newFirstDataSector >= partitionTotal) {
        scan.fatChanged = true;
        goto finish;
    }
    calculatedClusterCount = (partitionTotal - newFirstDataSector) / scan.sectorsPerCluster;
    if (calculatedClusterCount != report->header.clusterCount) {
        scan.fatChanged = true;
        goto finish;
    }
    scan.clusterBytes = scan.sectorsPerCluster * UMICOM_FAT16_AUDIT_SECTOR_BYTES;
    scan.primaryStart = report->header.partitionStart + reserved;
    scan.mirrorStart = scan.primaryStart + report->header.fatSectors;
    scan.rootStart = scan.mirrorStart + report->header.fatSectors;
    dataOffset = reserved + 2U * report->header.fatSectors + scan.rootSectors;
    scan.dataStart = report->header.partitionStart + dataOffset;
    scan.clusterCount = report->header.clusterCount;
    fatEntries = scan.clusterCount + 2U;
    if (fatEntries > UMICOM_MAX_FAT16_CLUSTERS + 2U) {
        report->classification = UMICOM_FAT16_INTEGRITY_LIMIT;
        return report->classification;
    }
    scan.fat = (uint16_t *)calloc(fatEntries, sizeof(uint16_t));
    scan.owners = (uint32_t *)calloc(fatEntries, sizeof(uint32_t));
    scan.directories = (UmicomDirectoryWork *)calloc(scan.directoryBudget, sizeof(UmicomDirectoryWork));
    if (scan.fat == NULL || scan.owners == NULL || scan.directories == NULL) {
        report->classification = UMICOM_FAT16_INTEGRITY_NO_MEMORY;
        goto cleanup;
    }
    if (!UmicomIntegrityReadFat(&scan)) goto finish;
    report->fatCopiesStable = true;
    if (!UmicomIntegrityScanRoot(&scan)) goto finish;
    if (!UmicomIntegrityScanDirectories(&scan)) goto finish;
    report->scannedNamespace = true;
    UmicomIntegrityCountOrphans(&scan);
finish:
    report->classification = UmicomIntegritySelectResult(&scan);
cleanup:
    free(scan.directories);
    free(scan.owners);
    free(scan.fat);
    return report->classification;
}

const char *UmicomFat16IntegrityClassificationName(
    UmicomFat16IntegrityClassification classification)
{
    switch (classification) {
        case UMICOM_FAT16_INTEGRITY_CONSISTENT: return "consistent-allocation-graph";
        case UMICOM_FAT16_INTEGRITY_NOT_ADMITTED: return "not-admitted";
        case UMICOM_FAT16_INTEGRITY_CROSSLINK: return "cross-linked-cluster";
        case UMICOM_FAT16_INTEGRITY_LOOP: return "cluster-chain-loop";
        case UMICOM_FAT16_INTEGRITY_INVALID_CHAIN: return "invalid-cluster-chain";
        case UMICOM_FAT16_INTEGRITY_TRUNCATED_CHAIN: return "short-file-chain";
        case UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY: return "invalid-directory-entry";
        case UMICOM_FAT16_INTEGRITY_ORPHAN_CLUSTERS: return "orphaned-allocations";
        case UMICOM_FAT16_INTEGRITY_IO_FAILURE: return "incomplete-read";
        case UMICOM_FAT16_INTEGRITY_LIMIT: return "inspection-limit";
        case UMICOM_FAT16_INTEGRITY_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_FAT16_INTEGRITY_NO_MEMORY: return "allocation-unavailable";
    }
    return "unknown-classification";
}
