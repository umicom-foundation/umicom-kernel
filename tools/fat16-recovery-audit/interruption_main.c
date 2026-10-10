/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/interruption_main.c
 *
 * PURPOSE:
 *   Explain durable-ordering and crash ambiguity using freshly allocated
 *   synthetic media. This CLI has NO IMAGE PATH ARGUMENT and NO DISK OUTPUT.
 *   It is intentionally not a filesystem repair command or a Kernel build.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_interruption_lab.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void UmicomInterruptionUsage(void)
{
    fputs("Usage:\n"
          "  umicom-fat16-interruption-lab --matrix\n"
          "  umicom-fat16-interruption-lab --scenario write-back|write-through 0..12 [--lost-ack]\n"
          "  umicom-fat16-interruption-lab --hybrid\n"
          "No disk images are opened or written. All results are synthetic host-model evidence.\n",
          stderr);
}

static bool UmicomParseStep(const char *text, uint32_t *step)
{
    char *end = NULL;
    unsigned long parsed;
    if (text == NULL || step == NULL || text[0] == '\0' || text[0] == '-') return false;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0'
        || parsed > (unsigned long)UMICOM_FAT16_INTERRUPTION_FINAL_STEP) return false;
    *step = (uint32_t)parsed;
    return true;
}

static void UmicomPrintEvidence(const UmicomFat16InterruptionEvidence *evidence)
{
    printf("{\"machine\":\"host-synthetic-model\",\"mode\":\"%s\""
           ",\"stop_after_step\":%" PRIu32
           ",\"writes_completed\":%" PRIu32
           ",\"flushes_completed\":%" PRIu32
           ",\"durable_sectors_changed\":%" PRIu32
           ",\"fat\":\"%s\",\"allocation\":\"%s\",\"file_data\":\"%s\""
           ",\"final_ack_lost\":%s,\"writer_confirmed\":%s"
           ",\"final_readback_verified\":%s"
           ",\"operator_review_required\":%s"
           ",\"original_seed_unchanged\":%s"
           ",\"mutations_bounded\":%s"
           ",\"recovery_authorised\":false,\"repair_performed\":false}\n",
           UmicomFat16InterruptionModeName(evidence->mode),
           evidence->stopAfterStep,
           evidence->publishedWrites, evidence->acknowledgedFlushes,
           evidence->modifiedDurableSectors,
           UmicomFat16AuditClassificationName(evidence->headerClassification),
           UmicomFat16IntegrityClassificationName(evidence->allocationClassification),
           UmicomFat16InterruptionPayloadName(evidence->payload),
           evidence->finalAcknowledgementLost ? "true" : "false",
           evidence->writerReportedAccepted ? "true" : "false",
           evidence->finalReadbackVerified ? "true" : "false",
           evidence->requiresIndependentRecoveryDecision ? "true" : "false",
           evidence->originalSourceUnchanged ? "true" : "false",
           evidence->mutationsRestrictedToExpectedSectors ? "true" : "false");
}

int main(int argc, char **argv)
{
    UmicomFat16InterruptionEvidence evidence = {0};
    UmicomFat16InterruptionSummary summary = {0};
    UmicomFat16InterruptionStatus status;
    if (argc == 2 && strcmp(argv[1], "--matrix") == 0) {
        status = UmicomFat16InterruptionRunMatrix(&summary);
        printf("{\"machine\":\"host-synthetic-model\",\"status\":\"%s\""
               ",\"scenarios\":%" PRIu32 ",\"clean_header\":%" PRIu32
               ",\"dirty_header\":%" PRIu32 ",\"mirror_mismatch\":%" PRIu32
               ",\"old_payload\":%" PRIu32 ",\"new_payload\":%" PRIu32
               ",\"mixed_payload\":%" PRIu32
               ",\"unconfirmed_clean_after_submission\":%" PRIu32
               ",\"confirmed_writer\":%" PRIu32
               ",\"operator_review_required\":%" PRIu32
               ",\"repair_performed\":false}\n",
               UmicomFat16InterruptionStatusName(status), summary.scenarios,
               summary.cleanHeaderCases, summary.dirtyHeaderCases,
               summary.mismatchedHeaderCases, summary.oldPayloadCases,
               summary.newPayloadCases, summary.mixedPayloadCases,
               summary.cleanHeaderUnconfirmedMutationCases, summary.acceptedWriterCases,
               summary.independentReviewCases);
        return status == UMICOM_FAT16_INTERRUPTION_OK ? 0 : 70;
    }
    if (argc == 2 && strcmp(argv[1], "--hybrid") == 0) {
        status = UmicomFat16InterruptionDemonstrateCleanHybrid(&evidence);
        if (status != UMICOM_FAT16_INTERRUPTION_OK) return 70;
        UmicomPrintEvidence(&evidence);
        return evidence.headerClassification == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED
               && evidence.allocationClassification == UMICOM_FAT16_INTEGRITY_CONSISTENT
               && evidence.payload == UMICOM_FAT16_INTERRUPTION_MIXED_BYTES ? 0 : 70;
    }
    if ((argc == 4 || argc == 5) && strcmp(argv[1], "--scenario") == 0) {
        UmicomFat16InterruptionMode mode;
        uint32_t step;
        const bool loseAck = argc == 5 && strcmp(argv[4], "--lost-ack") == 0;
        if (strcmp(argv[2], "write-back") == 0) {
            mode = UMICOM_FAT16_INTERRUPTION_WRITE_BACK;
        } else if (strcmp(argv[2], "write-through") == 0) {
            mode = UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH;
        } else {
            UmicomInterruptionUsage();
            return 64;
        }
        if (!UmicomParseStep(argv[3], &step) || (argc == 5 && !loseAck)
            || (loseAck && step != UMICOM_FAT16_INTERRUPTION_FINAL_STEP)) {
            UmicomInterruptionUsage();
            return 64;
        }
        status = UmicomFat16InterruptionObserve(mode, step, loseAck, &evidence);
        if (status != UMICOM_FAT16_INTERRUPTION_OK) return 70;
        UmicomPrintEvidence(&evidence);
        return evidence.originalSourceUnchanged
               && evidence.mutationsRestrictedToExpectedSectors ? 0 : 70;
    }
    UmicomInterruptionUsage();
    return 64;
}
