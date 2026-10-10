/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/fat16_interruption_lab.h
 *
 * PURPOSE:
 *   Deterministic host-only interruption qualification for the ordered FAT16
 *   dirty-guard/write/flush/clean protocol. It models acknowledged device
 *   writes and a separate durable image; a power cut loses unflushed writes
 *   in the write-back profile and retains them in the write-through profile.
 *
 * BOUNDARY:
 *   All media are freshly allocated, synthetic byte arrays. No host pathname,
 *   device handle, actual disk write, kernel service or repair operation is
 *   exposed by this API. Observations are from the real read-only audit cores.
 *   Results are evidence about this model, not physical power-loss proof.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_FAT16_INTERRUPTION_LAB_H
#define UMICOM_FAT16_INTERRUPTION_LAB_H

#include "fat16_integrity_audit.h"
#include <stdbool.h>
#include <stdint.h>

#define UMICOM_FAT16_INTERRUPTION_FINAL_STEP UINT32_C(12)
#define UMICOM_FAT16_INTERRUPTION_IMAGE_SECTORS UINT32_C(14048)

typedef enum UmicomFat16InterruptionMode {
    UMICOM_FAT16_INTERRUPTION_WRITE_BACK = 0,
    UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH
} UmicomFat16InterruptionMode;

typedef enum UmicomFat16InterruptionPayload {
    UMICOM_FAT16_INTERRUPTION_OLD_BYTES = 0,
    UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
    UMICOM_FAT16_INTERRUPTION_MIXED_BYTES,
    UMICOM_FAT16_INTERRUPTION_INVALID_BYTES
} UmicomFat16InterruptionPayload;

typedef enum UmicomFat16InterruptionStatus {
    UMICOM_FAT16_INTERRUPTION_OK = 0,
    UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT,
    UMICOM_FAT16_INTERRUPTION_NO_MEMORY,
    UMICOM_FAT16_INTERRUPTION_INTERNAL_ERROR
} UmicomFat16InterruptionStatus;

typedef struct UmicomFat16InterruptionEvidence {
    UmicomFat16InterruptionMode mode;
    uint32_t stopAfterStep; /* 0: no mutation, 1..12: operations below. */
    uint32_t publishedWrites;
    uint32_t acknowledgedFlushes;
    uint32_t modifiedDurableSectors;
    bool finalAcknowledgementLost;
    bool writerReportedAccepted;
    bool finalReadbackVerified;
    bool requiresIndependentRecoveryDecision;
    bool mutationsRestrictedToExpectedSectors;
    bool originalSourceUnchanged;
    UmicomFat16AuditClassification headerClassification;
    UmicomFat16IntegrityClassification allocationClassification;
    UmicomFat16InterruptionPayload payload;
} UmicomFat16InterruptionEvidence;

typedef struct UmicomFat16InterruptionSummary {
    uint32_t scenarios;
    uint32_t cleanHeaderCases;
    uint32_t dirtyHeaderCases;
    uint32_t mismatchedHeaderCases;
    uint32_t oldPayloadCases;
    uint32_t newPayloadCases;
    uint32_t mixedPayloadCases;
    uint32_t cleanHeaderUnconfirmedMutationCases;
    uint32_t acceptedWriterCases;
    uint32_t independentReviewCases;
} UmicomFat16InterruptionSummary;

/* Stages follow mirror dirty WRITE/FLUSH, primary dirty WRITE/FLUSH,
 * two data-sector WRITEs/FLUSH, mirror clean WRITE/FLUSH, primary clean
 * WRITE/FLUSH, readback and acceptance. No device callback exists here.
 * stopAfterStep 12 includes the final readback and completion decision.
 * 'loseFinalAcknowledgement' is admitted only at step 12 and represents the
 * caller not receiving final acceptance despite a potentially clean disk. */
UmicomFat16InterruptionStatus UmicomFat16InterruptionObserve(
    UmicomFat16InterruptionMode mode,
    uint32_t stopAfterStep,
    bool loseFinalAcknowledgement,
    UmicomFat16InterruptionEvidence *evidence);

/* Independently verifies the expected outcome for every interruption point
 * in both durability models, including lost final acknowledgement. */
UmicomFat16InterruptionStatus UmicomFat16InterruptionRunMatrix(
    UmicomFat16InterruptionSummary *summary);

/* Demonstrates that an externally modified clean FAT can contain mixed
 * application data while BOTH existing audits still report consistency.
 * Returns the two observed audit statuses and the actual payload state. */
UmicomFat16InterruptionStatus UmicomFat16InterruptionDemonstrateCleanHybrid(
    UmicomFat16InterruptionEvidence *evidence);

const char *UmicomFat16InterruptionModeName(UmicomFat16InterruptionMode mode);
const char *UmicomFat16InterruptionPayloadName(UmicomFat16InterruptionPayload payload);
const char *UmicomFat16InterruptionStatusName(UmicomFat16InterruptionStatus status);

#endif
