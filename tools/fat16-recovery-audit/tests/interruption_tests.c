/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/tests/interruption_tests.c
 *
 * PURPOSE:
 *   Independently check each synthetic durable step against explicit expected
 *   header/payload results, never accepting a clean flag as replay approval.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_interruption_lab.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t UmicomChecks = 0U;
static uint32_t UmicomFailures = 0U;

#define UMICOM_EXPECT(condition) do { \
    ++UmicomChecks; \
    if (!(condition)) { \
        ++UmicomFailures; \
        fprintf(stderr, "failed: %s line %d: %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

static void UmicomCheckStep(UmicomFat16InterruptionMode mode, uint32_t step,
    UmicomFat16AuditClassification expectedHeader,
    UmicomFat16InterruptionPayload expectedPayload, bool expectedAccepted)
{
    UmicomFat16InterruptionEvidence evidence = {0};
    UmicomFat16InterruptionStatus status =
        UmicomFat16InterruptionObserve(mode, step, false, &evidence);
    UMICOM_EXPECT(status == UMICOM_FAT16_INTERRUPTION_OK);
    if (status != UMICOM_FAT16_INTERRUPTION_OK) return;
    UMICOM_EXPECT(evidence.headerClassification == expectedHeader);
    UMICOM_EXPECT(evidence.payload == expectedPayload);
    UMICOM_EXPECT(evidence.writerReportedAccepted == expectedAccepted);
    UMICOM_EXPECT(evidence.finalReadbackVerified ==
                  (step == UMICOM_FAT16_INTERRUPTION_FINAL_STEP));
    UMICOM_EXPECT(evidence.requiresIndependentRecoveryDecision);
    UMICOM_EXPECT(evidence.originalSourceUnchanged);
    UMICOM_EXPECT(evidence.mutationsRestrictedToExpectedSectors);
    UMICOM_EXPECT(evidence.allocationClassification ==
        (expectedHeader == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED
             ? UMICOM_FAT16_INTEGRITY_CONSISTENT
             : UMICOM_FAT16_INTEGRITY_NOT_ADMITTED));
}

static void UmicomCheckFullMatrix(void)
{
    UmicomFat16InterruptionSummary summary = {0};
    UMICOM_EXPECT(UmicomFat16InterruptionRunMatrix(&summary) == UMICOM_FAT16_INTERRUPTION_OK);
    UMICOM_EXPECT(summary.scenarios == 28U);
    UMICOM_EXPECT(summary.cleanHeaderCases + summary.dirtyHeaderCases
                  + summary.mismatchedHeaderCases == summary.scenarios);
    UMICOM_EXPECT(summary.oldPayloadCases + summary.newPayloadCases
                  + summary.mixedPayloadCases == summary.scenarios);
    UMICOM_EXPECT(summary.mixedPayloadCases == 1U); /* Write-through before data FLUSH. */
    UMICOM_EXPECT(summary.cleanHeaderUnconfirmedMutationCases == 6U);
    UMICOM_EXPECT(summary.independentReviewCases == summary.scenarios);
    UMICOM_EXPECT(summary.acceptedWriterCases == 2U);
}

/* Independent expected tables (not the production model's formula) make
 * changes to the simulated protocol fail visibly at each interruption point. */
static void UmicomCheckEveryDurableBoundary(void)
{
    static const UmicomFat16AuditClassification expectedHeader[2][13] = {
        {UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_AUDIT_CLEAN_MIRRORED,
         UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH,
         UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
         UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
         UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH,
         UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_AUDIT_CLEAN_MIRRORED,
         UMICOM_FAT16_AUDIT_CLEAN_MIRRORED},
        {UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH,
         UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
         UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
         UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR,
         UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_AUDIT_MIRROR_MISMATCH,
         UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_AUDIT_CLEAN_MIRRORED,
         UMICOM_FAT16_AUDIT_CLEAN_MIRRORED}
    };
    static const UmicomFat16InterruptionPayload expectedData[2][13] = {
        {UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_OLD_BYTES,
         UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_OLD_BYTES,
         UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_OLD_BYTES,
         UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES, UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES, UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES},
        {UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_OLD_BYTES,
         UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_OLD_BYTES,
         UMICOM_FAT16_INTERRUPTION_OLD_BYTES, UMICOM_FAT16_INTERRUPTION_MIXED_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES, UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES, UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES, UMICOM_FAT16_INTERRUPTION_NEW_BYTES,
         UMICOM_FAT16_INTERRUPTION_NEW_BYTES}
    };
    static const uint32_t expectedDurableChanges[2][13] = {
        {0U, 0U, 1U, 1U, 2U, 2U, 2U, 4U, 4U, 3U, 3U, 2U, 2U},
        {0U, 1U, 1U, 2U, 2U, 3U, 4U, 4U, 3U, 3U, 2U, 2U, 2U}
    };
    static const uint32_t expectedWrites[13] =
        {0U, 1U, 1U, 2U, 2U, 3U, 4U, 4U, 5U, 5U, 6U, 6U, 6U};
    static const uint32_t expectedFlushes[13] =
        {0U, 0U, 1U, 1U, 2U, 2U, 2U, 3U, 3U, 4U, 4U, 5U, 5U};
    for (uint32_t mode = 0U; mode < 2U; ++mode) {
        for (uint32_t step = 0U; step <= UMICOM_FAT16_INTERRUPTION_FINAL_STEP; ++step) {
            UmicomFat16InterruptionEvidence evidence = {0};
            UMICOM_EXPECT(UmicomFat16InterruptionObserve((UmicomFat16InterruptionMode)mode,
                step, false, &evidence) == UMICOM_FAT16_INTERRUPTION_OK);
            UMICOM_EXPECT(evidence.headerClassification == expectedHeader[mode][step]);
            UMICOM_EXPECT(evidence.payload == expectedData[mode][step]);
            UMICOM_EXPECT(evidence.publishedWrites == expectedWrites[step]);
            UMICOM_EXPECT(evidence.acknowledgedFlushes == expectedFlushes[step]);
            UMICOM_EXPECT(evidence.modifiedDurableSectors == expectedDurableChanges[mode][step]);
            UMICOM_EXPECT(evidence.originalSourceUnchanged);
            UMICOM_EXPECT(evidence.mutationsRestrictedToExpectedSectors);
        }
    }
}

static void UmicomCheckBoundaryConditions(void)
{
    UmicomFat16InterruptionEvidence evidence = {0};
    UMICOM_EXPECT(UmicomFat16InterruptionObserve(UMICOM_FAT16_INTERRUPTION_WRITE_BACK,
        13U, false, &evidence) == UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT);
    UMICOM_EXPECT(UmicomFat16InterruptionObserve(UMICOM_FAT16_INTERRUPTION_WRITE_BACK,
        2U, true, &evidence) == UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT);
    UMICOM_EXPECT(UmicomFat16InterruptionObserve((UmicomFat16InterruptionMode)8,
        0U, false, &evidence) == UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT);
    UMICOM_EXPECT(UmicomFat16InterruptionObserve(UMICOM_FAT16_INTERRUPTION_WRITE_BACK,
        0U, false, NULL) == UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT);
    UMICOM_EXPECT(UmicomFat16InterruptionRunMatrix(NULL)
                  == UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT);
    UMICOM_EXPECT(UmicomFat16InterruptionDemonstrateCleanHybrid(NULL)
                  == UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT);
}

static void UmicomCheckLostAcknowledgement(void)
{
    for (uint32_t m = 0U; m < 2U; ++m) {
        UmicomFat16InterruptionEvidence evidence = {0};
        UMICOM_EXPECT(UmicomFat16InterruptionObserve((UmicomFat16InterruptionMode)m,
            12U, true, &evidence) == UMICOM_FAT16_INTERRUPTION_OK);
        UMICOM_EXPECT(evidence.headerClassification == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED);
        UMICOM_EXPECT(evidence.allocationClassification == UMICOM_FAT16_INTEGRITY_CONSISTENT);
        UMICOM_EXPECT(evidence.payload == UMICOM_FAT16_INTERRUPTION_NEW_BYTES);
        UMICOM_EXPECT(!evidence.writerReportedAccepted);
        UMICOM_EXPECT(evidence.finalAcknowledgementLost);
        UMICOM_EXPECT(evidence.finalReadbackVerified);
        UMICOM_EXPECT(evidence.requiresIndependentRecoveryDecision);
    }
}

static void UmicomCheckUndetectedCleanHybrid(void)
{
    UmicomFat16InterruptionEvidence evidence = {0};
    UMICOM_EXPECT(UmicomFat16InterruptionDemonstrateCleanHybrid(&evidence)
                  == UMICOM_FAT16_INTERRUPTION_OK);
    UMICOM_EXPECT(evidence.headerClassification == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED);
    UMICOM_EXPECT(evidence.allocationClassification == UMICOM_FAT16_INTEGRITY_CONSISTENT);
    UMICOM_EXPECT(evidence.payload == UMICOM_FAT16_INTERRUPTION_MIXED_BYTES);
    UMICOM_EXPECT(evidence.modifiedDurableSectors == 1U);
    UMICOM_EXPECT(evidence.originalSourceUnchanged);
    UMICOM_EXPECT(evidence.requiresIndependentRecoveryDecision);
}

int main(void)
{
    /* Write-back: unflushed dirty mirror is lost, but mirror flush persists. */
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 0U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 1U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 2U,
        UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 4U,
        UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 6U,
        UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 7U,
        UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 9U,
        UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 11U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_BACK, 12U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, true);

    /* Write-through can make both dirty guards and partially written data
     * durable before a flush acknowledgement is returned. */
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 0U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 1U,
        UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 3U,
        UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_INTERRUPTION_OLD_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 5U,
        UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_INTERRUPTION_MIXED_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 6U,
        UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 8U,
        UMICOM_FAT16_AUDIT_MIRROR_MISMATCH, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 10U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, false);
    UmicomCheckStep(UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH, 12U,
        UMICOM_FAT16_AUDIT_CLEAN_MIRRORED, UMICOM_FAT16_INTERRUPTION_NEW_BYTES, true);

    UmicomCheckFullMatrix();
    UmicomCheckEveryDurableBoundary();
    UmicomCheckBoundaryConditions();
    UmicomCheckLostAcknowledgement();
    UmicomCheckUndetectedCleanHybrid();
    printf("fat16-interruption-model-checks=%" PRIu32 " failures=%" PRIu32 "\n",
           UmicomChecks, UmicomFailures);
    return UmicomFailures == 0U ? 0 : 1;
}
