/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/fat16_integrity_audit.h
 *
 * PURPOSE:
 *   Independently examine the on-disk FAT16 allocation graph and bounded
 *   directory namespace, following the established read-only recovery audit.
 *
 * SAFETY AND OWNERSHIP:
 *   No write or repair capability exists. A clean result describes only the
 *   sectors observed during this invocation; it is not proof of complete
 *   filesystem integrity or permission to mount a previously damaged disk.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_FAT16_INTEGRITY_AUDIT_H
#define UMICOM_FAT16_INTEGRITY_AUDIT_H

#include "fat16_recovery_audit.h"
#include <stdint.h>

#define UMICOM_FAT16_INTEGRITY_DEFAULT_READ_BUDGET UINT32_C(32768)
#define UMICOM_FAT16_INTEGRITY_DEFAULT_ENTRY_BUDGET UINT32_C(32768)
#define UMICOM_FAT16_INTEGRITY_DEFAULT_DIRECTORY_BUDGET UINT32_C(256)

typedef enum UmicomFat16IntegrityClassification {
    UMICOM_FAT16_INTEGRITY_CONSISTENT = 0,
    UMICOM_FAT16_INTEGRITY_NOT_ADMITTED,
    UMICOM_FAT16_INTEGRITY_CROSSLINK,
    UMICOM_FAT16_INTEGRITY_LOOP,
    UMICOM_FAT16_INTEGRITY_INVALID_CHAIN,
    UMICOM_FAT16_INTEGRITY_TRUNCATED_CHAIN,
    UMICOM_FAT16_INTEGRITY_INVALID_DIRECTORY,
    UMICOM_FAT16_INTEGRITY_ORPHAN_CLUSTERS,
    UMICOM_FAT16_INTEGRITY_IO_FAILURE,
    UMICOM_FAT16_INTEGRITY_LIMIT,
    UMICOM_FAT16_INTEGRITY_INVALID_ARGUMENT,
    UMICOM_FAT16_INTEGRITY_NO_MEMORY
} UmicomFat16IntegrityClassification;

typedef struct UmicomFat16IntegrityOptions {
    uint32_t maximumAdditionalReads; /* Zero chooses a bounded default. */
    uint32_t maximumDirectoryEntries; /* Zero chooses a bounded default. */
    uint32_t maximumDirectories;      /* Zero chooses a bounded default. */
} UmicomFat16IntegrityOptions;

typedef struct UmicomFat16IntegrityReport {
    UmicomFat16AuditReport header;
    UmicomFat16IntegrityClassification classification;
    uint32_t additionalSectorsRead;
    uint32_t directoriesVisited;
    uint32_t filesVisited;
    uint32_t directoryEntriesVisited;
    uint32_t allocatedClusters;
    uint32_t referencedClusters;
    uint32_t orphanClusters;
    uint32_t crossLinkedClusters;
    uint32_t loopedChains;
    uint32_t invalidChains;
    uint32_t truncatedChains;
    uint32_t invalidEntries;
    uint32_t badClusterMarkers; /* Correctly reserved BAD cluster markers. */
    bool fatCopiesStable;
    bool scannedNamespace;
} UmicomFat16IntegrityReport;

/* Each invocation owns its bounded host allocations, freed before returning.
 * The callback must describe a stable snapshot of regular-file media. */
UmicomFat16IntegrityClassification UmicomFat16IntegrityInspect(
    const UmicomFat16AuditSource *source,
    const UmicomFat16IntegrityOptions *options,
    UmicomFat16IntegrityReport *report);

const char *UmicomFat16IntegrityClassificationName(
    UmicomFat16IntegrityClassification classification);

#endif
