/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/fat16_recovery_audit.h
 *
 * PURPOSE:
 *   Interpret read-only FAT16 disk evidence after a potentially interrupted
 *   persistent write. This diagnostic contract is a native-host tool and does
 *   not replace the Kernel's checked mount and filesystem inspector.
 *
 * SAFETY:
 *   The only media callback is READ. A clean and mirrored FAT does not prove
 *   file data is intact, and NO result grants permission to repair the disk.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_FAT16_RECOVERY_AUDIT_H
#define UMICOM_FAT16_RECOVERY_AUDIT_H

#include <stdbool.h>
#include <stdint.h>

#define UMICOM_FAT16_AUDIT_SECTOR_BYTES UINT32_C(512)
#define UMICOM_FAT16_AUDIT_MAX_FAT_SECTORS UINT32_C(512)
#define UMICOM_FAT16_AUDIT_DEFAULT_READ_BUDGET UINT32_C(1100)

typedef bool (*UmicomFat16AuditReadSector)(
    void *context,
    uint64_t sectorNumber,
    uint8_t destination[UMICOM_FAT16_AUDIT_SECTOR_BYTES]);

typedef struct UmicomFat16AuditSource {
    void *context;
    UmicomFat16AuditReadSector readSector;
    uint64_t mediaSectors;
    uint32_t partitionIndex;
    uint32_t readBudget; /* Zero selects the documented default. */
} UmicomFat16AuditSource;

typedef enum UmicomFat16AuditClassification {
    UMICOM_FAT16_AUDIT_CLEAN_MIRRORED = 0,
    UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
    UMICOM_FAT16_AUDIT_MIRROR_MISMATCH,
    UMICOM_FAT16_AUDIT_INVALID_FORMAT,
    UMICOM_FAT16_AUDIT_UNSUPPORTED_PROFILE,
    UMICOM_FAT16_AUDIT_INCOMPLETE_READ,
    UMICOM_FAT16_AUDIT_INVALID_ARGUMENT
} UmicomFat16AuditClassification;

typedef struct UmicomFat16AuditReport {
    UmicomFat16AuditClassification classification;
    uint64_t partitionStart;
    uint64_t partitionSectors;
    uint32_t fatSectors;
    uint32_t clusterCount;
    uint32_t sectorsRead;
    uint32_t differingFatSectors;
    bool recognisedFat16Geometry;
    bool fatCleanFlagClear;
    bool fatIoErrorFlagClear;
    bool reservedFatEntriesValid;
    bool fatMirrorsMatch;
} UmicomFat16AuditReport;

/* No writes, repairs, mount, global state or heap allocation. */
UmicomFat16AuditClassification UmicomFat16AuditInspect(
    const UmicomFat16AuditSource *source,
    UmicomFat16AuditReport *report);

const char *UmicomFat16AuditClassificationName(
    UmicomFat16AuditClassification classification);

#endif
