/*-----------------------------------------------------------------------------
 * Umicom Kernel - independent FAT16 guest commit evidence
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *
 * Verify every byte of two immutable sector sources after the Kernel's
 * documented FRAG.BIN console Stage or Finish exercise. This is qualification
 * evidence for a SPECIFIC synthetic fixture, not a general filesystem repair
 * or recovery API. No write callback, disk lease, or Kernel dependency exists.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_FAT16_GUEST_COMMIT_EVIDENCE_H
#define UMICOM_FAT16_GUEST_COMMIT_EVIDENCE_H

#include "fat16_integrity_audit.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum UmicomFat16GuestEvidenceMode {
    UMICOM_FAT16_GUEST_EVIDENCE_STAGED = 1,
    UMICOM_FAT16_GUEST_EVIDENCE_FINISHED = 2
} UmicomFat16GuestEvidenceMode;

typedef enum UmicomFat16GuestEvidenceStatus {
    UMICOM_FAT16_GUEST_EVIDENCE_VERIFIED = 0,
    UMICOM_FAT16_GUEST_EVIDENCE_INVALID_ARGUMENT,
    UMICOM_FAT16_GUEST_EVIDENCE_SOURCE_REFUSED,
    UMICOM_FAT16_GUEST_EVIDENCE_FORMAT_REFUSED,
    UMICOM_FAT16_GUEST_EVIDENCE_AFTER_STATE_REFUSED,
    UMICOM_FAT16_GUEST_EVIDENCE_READ_FAILED,
    UMICOM_FAT16_GUEST_EVIDENCE_BYTES_DIFFER
} UmicomFat16GuestEvidenceStatus;

typedef struct UmicomFat16GuestEvidenceReport {
    UmicomFat16GuestEvidenceStatus status;
    UmicomFat16AuditClassification beforeHeader;
    UmicomFat16AuditClassification afterHeader;
    UmicomFat16IntegrityClassification beforeIntegrity;
    UmicomFat16IntegrityClassification afterIntegrity;
    uint64_t mediaSectorsCompared;
    uint64_t actualDifferenceBytes;
    uint64_t expectedDifferenceBytes;
    uint64_t unexpectedBytes;
    uint64_t firstUnexpectedSector;
    uint32_t firstUnexpectedOffset;
    uint8_t firstExpected;
    uint8_t firstActual;
    uint64_t firstDataSector;
    uint64_t secondDataSector;
    bool fullImageCompared;
    bool repairPerformed;
} UmicomFat16GuestEvidenceReport;

/* The known test is: fatstage /FRAG.BIN 511 "Umicom ordered update".
 * The comparison proves only that specific accepted mutation, with precisely
 * two dirty-header bytes for Staged or original clean headers for Finished.
 * The source/after snapshots must be stable throughout the call. */
UmicomFat16GuestEvidenceStatus UmicomFat16GuestEvidenceInspect(
    const UmicomFat16AuditSource *before,
    const UmicomFat16AuditSource *after,
    UmicomFat16GuestEvidenceMode mode,
    UmicomFat16GuestEvidenceReport *report);

const char *UmicomFat16GuestEvidenceStatusName(UmicomFat16GuestEvidenceStatus status);

#endif /* UMICOM_FAT16_GUEST_COMMIT_EVIDENCE_H */
