/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/fat16_interruption_lab.c
 *
 * PURPOSE:
 *   Model power interruption at every durable boundary of the FAT16 ordered
 *   commit, then run the EXISTING recovery and allocation auditors against
 *   the remaining stable bytes. No established Kernel algorithm is replaced.
 *
 * EDUCATIONAL NOTE:
 *   A successful write request and a completed flush are different facts.
 *   Write-through hardware can persist data before flush. An unacknowledged
 *   final clean flag can leave a fully updated, apparently clean image.
 *   These two cases prohibit silent retry or automatic repair decisions.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "fat16_interruption_lab.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define UMICOM_LAB_SECTOR_BYTES 512U
#define UMICOM_LAB_PARTITION_START 2048U
#define UMICOM_LAB_PARTITION_SECTORS 12000U
#define UMICOM_LAB_FAT_SECTORS 32U
#define UMICOM_LAB_PRIMARY_FAT (UMICOM_LAB_PARTITION_START + 1U)
#define UMICOM_LAB_MIRROR_FAT (UMICOM_LAB_PRIMARY_FAT + UMICOM_LAB_FAT_SECTORS)
#define UMICOM_LAB_ROOT_START (UMICOM_LAB_MIRROR_FAT + UMICOM_LAB_FAT_SECTORS)
#define UMICOM_LAB_DATA_START (UMICOM_LAB_ROOT_START + 32U)
#define UMICOM_LAB_DATA_FIRST UMICOM_LAB_DATA_START
#define UMICOM_LAB_DATA_SECOND (UMICOM_LAB_DATA_START + 1U)
#define UMICOM_LAB_FILE_BYTES 700U

/* Every sector address above is bounded by this deliberately small image.
 * A full copy costs about 7 MiB and avoids dependence on a host disk. */
#define UMICOM_LAB_IMAGE_BYTES \
    ((size_t)UMICOM_FAT16_INTERRUPTION_IMAGE_SECTORS * UMICOM_LAB_SECTOR_BYTES)

typedef struct UmicomLabDisk {
    uint8_t *initial;
    uint8_t *visible;
    uint8_t *durable;
    UmicomFat16InterruptionMode mode;
    uint32_t writes;
    uint32_t flushes;
} UmicomLabDisk;

/* Hash the immutable seed so accidental writes to the baseline cannot pass
 * unnoticed merely because the simulated media still looks plausible. */
static uint64_t UmicomLabHash(const uint8_t *bytes, size_t length)
{
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t index = 0U; index < length; ++index) {
        value ^= (uint64_t)bytes[index];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

static void UmicomLabWrite16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(255));
    bytes[1] = (uint8_t)(value >> 8U);
}

static void UmicomLabWrite32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & UINT32_C(255));
    bytes[1] = (uint8_t)((value >> 8U) & UINT32_C(255));
    bytes[2] = (uint8_t)((value >> 16U) & UINT32_C(255));
    bytes[3] = (uint8_t)((value >> 24U) & UINT32_C(255));
}

static uint8_t *UmicomLabSector(uint8_t *image, uint32_t sector)
{
    return image + (size_t)sector * UMICOM_LAB_SECTOR_BYTES;
}

static const uint8_t *UmicomLabSectorConst(const uint8_t *image, uint32_t sector)
{
    return image + (size_t)sector * UMICOM_LAB_SECTOR_BYTES;
}

static bool UmicomLabReadOnlySector(void *context, uint64_t sector,
                                   uint8_t destination[UMICOM_LAB_SECTOR_BYTES])
{
    const UmicomLabDisk *disk = (const UmicomLabDisk *)context;
    if (disk == NULL || disk->visible == NULL
        || sector >= UMICOM_FAT16_INTERRUPTION_IMAGE_SECTORS || destination == NULL) {
        return false;
    }
    memcpy(destination, UmicomLabSectorConst(disk->visible, (uint32_t)sector),
           UMICOM_LAB_SECTOR_BYTES);
    return true;
}

static bool UmicomLabInitialise(UmicomLabDisk *disk, UmicomFat16InterruptionMode mode)
{
    uint8_t *mbr;
    uint8_t *boot;
    uint8_t *fat;
    uint8_t *root;
    uint32_t offset;
    memset(disk, 0, sizeof(*disk));
    disk->mode = mode;
    disk->initial = (uint8_t *)calloc(UMICOM_LAB_IMAGE_BYTES, 1U);
    disk->visible = (uint8_t *)malloc(UMICOM_LAB_IMAGE_BYTES);
    disk->durable = (uint8_t *)malloc(UMICOM_LAB_IMAGE_BYTES);
    if (disk->initial == NULL || disk->visible == NULL || disk->durable == NULL) {
        return false;
    }
    mbr = UmicomLabSector(disk->initial, 0U);
    mbr[446U + 4U] = UINT8_C(0x06);
    UmicomLabWrite32(mbr + 446U + 8U, UMICOM_LAB_PARTITION_START);
    UmicomLabWrite32(mbr + 446U + 12U, UMICOM_LAB_PARTITION_SECTORS);
    mbr[510U] = UINT8_C(0x55);
    mbr[511U] = UINT8_C(0xaa);

    boot = UmicomLabSector(disk->initial, UMICOM_LAB_PARTITION_START);
    boot[0U] = UINT8_C(0xeb);
    boot[2U] = UINT8_C(0x90);
    UmicomLabWrite16(boot + 11U, UINT16_C(512));
    boot[13U] = 2U;
    UmicomLabWrite16(boot + 14U, 1U);
    boot[16U] = 2U;
    UmicomLabWrite16(boot + 17U, 512U);
    UmicomLabWrite16(boot + 19U, (uint16_t)UMICOM_LAB_PARTITION_SECTORS);
    boot[21U] = UINT8_C(0xf8);
    UmicomLabWrite16(boot + 22U, (uint16_t)UMICOM_LAB_FAT_SECTORS);
    UmicomLabWrite32(boot + 28U, UMICOM_LAB_PARTITION_START);
    boot[510U] = UINT8_C(0x55);
    boot[511U] = UINT8_C(0xaa);

    fat = UmicomLabSector(disk->initial, UMICOM_LAB_PRIMARY_FAT);
    UmicomLabWrite16(fat, UINT16_C(0xfff8));
    UmicomLabWrite16(fat + 2U, UINT16_C(0xffff));
    UmicomLabWrite16(fat + 4U, UINT16_C(0xffff)); /* STATE.TXT owns cluster 2. */
    memcpy(UmicomLabSector(disk->initial, UMICOM_LAB_MIRROR_FAT),
           fat, UMICOM_LAB_SECTOR_BYTES);

    root = UmicomLabSector(disk->initial, UMICOM_LAB_ROOT_START);
    memcpy(root, "STATE   TXT", 11U);
    root[11U] = UINT8_C(0x20); /* Archive, writable ordinary file. */
    UmicomLabWrite16(root + 26U, UINT16_C(2));
    UmicomLabWrite32(root + 28U, UMICOM_LAB_FILE_BYTES);
    for (offset = 0U; offset < UMICOM_LAB_SECTOR_BYTES; ++offset) {
        UmicomLabSector(disk->initial, UMICOM_LAB_DATA_FIRST)[offset] = UINT8_C('A');
    }
    for (offset = 0U; offset < UMICOM_LAB_FILE_BYTES - UMICOM_LAB_SECTOR_BYTES;
         ++offset) {
        UmicomLabSector(disk->initial, UMICOM_LAB_DATA_SECOND)[offset] = UINT8_C('B');
    }
    memcpy(disk->visible, disk->initial, UMICOM_LAB_IMAGE_BYTES);
    memcpy(disk->durable, disk->initial, UMICOM_LAB_IMAGE_BYTES);
    return true;
}

static void UmicomLabDispose(UmicomLabDisk *disk)
{
    free(disk->initial);
    free(disk->visible);
    free(disk->durable);
    memset(disk, 0, sizeof(*disk));
}

/* A request completing does not imply that a write-back backend is durable. */
static void UmicomLabCompletedWrite(UmicomLabDisk *disk, uint32_t sector,
                                     const uint8_t bytes[UMICOM_LAB_SECTOR_BYTES])
{
    memcpy(UmicomLabSector(disk->visible, sector), bytes, UMICOM_LAB_SECTOR_BYTES);
    if (disk->mode == UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH) {
        memcpy(UmicomLabSector(disk->durable, sector), bytes, UMICOM_LAB_SECTOR_BYTES);
    }
    disk->writes += 1U;
}

static void UmicomLabCompletedFlush(UmicomLabDisk *disk)
{
    memcpy(disk->durable, disk->visible, UMICOM_LAB_IMAGE_BYTES);
    disk->flushes += 1U;
}

static void UmicomLabFatFlag(UmicomLabDisk *disk, uint32_t fatSector, uint16_t flags)
{
    uint8_t scratch[UMICOM_LAB_SECTOR_BYTES];
    memcpy(scratch, UmicomLabSectorConst(disk->visible, fatSector), sizeof(scratch));
    UmicomLabWrite16(scratch + 2U, flags);
    UmicomLabCompletedWrite(disk, fatSector, scratch);
}

static void UmicomLabDataWrite(UmicomLabDisk *disk, uint32_t sector)
{
    uint8_t scratch[UMICOM_LAB_SECTOR_BYTES];
    uint32_t offset;
    memcpy(scratch, UmicomLabSectorConst(disk->visible, sector), sizeof(scratch));
    if (sector == UMICOM_LAB_DATA_FIRST) {
        memset(scratch, 'C', sizeof(scratch));
    } else {
        for (offset = 0U; offset < UMICOM_LAB_FILE_BYTES - UMICOM_LAB_SECTOR_BYTES;
             ++offset) {
            scratch[offset] = UINT8_C('D');
        }
    }
    UmicomLabCompletedWrite(disk, sector, scratch);
}

static void UmicomLabApplyStep(UmicomLabDisk *disk, uint32_t step)
{
    switch (step) {
        case 1U: UmicomLabFatFlag(disk, UMICOM_LAB_MIRROR_FAT, UINT16_C(0x7fff)); break;
        case 2U: UmicomLabCompletedFlush(disk); break;
        case 3U: UmicomLabFatFlag(disk, UMICOM_LAB_PRIMARY_FAT, UINT16_C(0x7fff)); break;
        case 4U: UmicomLabCompletedFlush(disk); break;
        case 5U: UmicomLabDataWrite(disk, UMICOM_LAB_DATA_FIRST); break;
        case 6U: UmicomLabDataWrite(disk, UMICOM_LAB_DATA_SECOND); break;
        case 7U: UmicomLabCompletedFlush(disk); break;
        case 8U: UmicomLabFatFlag(disk, UMICOM_LAB_MIRROR_FAT, UINT16_C(0xffff)); break;
        case 9U: UmicomLabCompletedFlush(disk); break;
        case 10U: UmicomLabFatFlag(disk, UMICOM_LAB_PRIMARY_FAT, UINT16_C(0xffff)); break;
        case 11U: UmicomLabCompletedFlush(disk); break;
        case 12U: break; /* Successful readback, no further media mutation. */
        default: break;
    }
}

/* This is an actual modelled final readback of both complete FAT header
 * sectors and both complete data sectors. Acceptance requires exact bytes,
 * including unchanged slack and the other FAT allocation entries. */
static bool UmicomLabFinalReadback(const UmicomLabDisk *disk)
{
    uint8_t expected[UMICOM_LAB_SECTOR_BYTES];
    for (uint32_t slot = 0U; slot < 2U; ++slot) {
        const uint32_t sector = slot == 0U ? UMICOM_LAB_PRIMARY_FAT : UMICOM_LAB_MIRROR_FAT;
        if (memcmp(UmicomLabSectorConst(disk->visible, sector),
                   UmicomLabSectorConst(disk->initial, sector),
                   UMICOM_LAB_SECTOR_BYTES) != 0) return false;
    }
    memset(expected, 'C', sizeof(expected));
    if (memcmp(UmicomLabSectorConst(disk->visible, UMICOM_LAB_DATA_FIRST),
               expected, sizeof(expected)) != 0) return false;
    memcpy(expected, UmicomLabSectorConst(disk->initial, UMICOM_LAB_DATA_SECOND),
           sizeof(expected));
    memset(expected, 'D', UMICOM_LAB_FILE_BYTES - UMICOM_LAB_SECTOR_BYTES);
    return memcmp(UmicomLabSectorConst(disk->visible, UMICOM_LAB_DATA_SECOND),
                  expected, sizeof(expected)) == 0;
}

static UmicomFat16InterruptionPayload UmicomLabPayload(const UmicomLabDisk *disk)
{
    const uint8_t *sector = UmicomLabSectorConst(disk->visible, UMICOM_LAB_DATA_FIRST);
    uint32_t offset;
    bool isOld = true;
    bool isNew = true;
    for (offset = 0U; offset < UMICOM_LAB_FILE_BYTES; ++offset) {
        const uint8_t before = offset < UMICOM_LAB_SECTOR_BYTES ? UINT8_C('A') : UINT8_C('B');
        const uint8_t after = offset < UMICOM_LAB_SECTOR_BYTES ? UINT8_C('C') : UINT8_C('D');
        if (sector[offset] != before) isOld = false;
        if (sector[offset] != after) isNew = false;
        if (sector[offset] != before && sector[offset] != after) {
            return UMICOM_FAT16_INTERRUPTION_INVALID_BYTES;
        }
    }
    if (isOld) return UMICOM_FAT16_INTERRUPTION_OLD_BYTES;
    if (isNew) return UMICOM_FAT16_INTERRUPTION_NEW_BYTES;
    return UMICOM_FAT16_INTERRUPTION_MIXED_BYTES;
}

static uint32_t UmicomLabChangedSectors(const UmicomLabDisk *disk)
{
    uint32_t sector;
    uint32_t count = 0U;
    for (sector = 0U; sector < UMICOM_FAT16_INTERRUPTION_IMAGE_SECTORS; ++sector) {
        if (memcmp(UmicomLabSectorConst(disk->initial, sector),
                   UmicomLabSectorConst(disk->visible, sector),
                   UMICOM_LAB_SECTOR_BYTES) != 0) ++count;
    }
    return count;
}

static void UmicomLabObserveDisk(UmicomLabDisk *disk, UmicomFat16InterruptionEvidence *evidence)
{
    UmicomFat16AuditSource source = {0};
    UmicomFat16AuditReport audit = {0};
    UmicomFat16IntegrityReport integrity = {0};
    source.context = disk;
    source.readSector = UmicomLabReadOnlySector;
    source.mediaSectors = UMICOM_FAT16_INTERRUPTION_IMAGE_SECTORS;
    source.partitionIndex = 0U;
    evidence->headerClassification = UmicomFat16AuditInspect(&source, &audit);
    evidence->allocationClassification = UmicomFat16IntegrityInspect(&source, NULL, &integrity);
    evidence->payload = UmicomLabPayload(disk);
    evidence->modifiedDurableSectors = UmicomLabChangedSectors(disk);
    evidence->requiresIndependentRecoveryDecision = true;
}

UmicomFat16InterruptionStatus UmicomFat16InterruptionObserve(
    UmicomFat16InterruptionMode mode, uint32_t stopAfterStep,
    bool loseFinalAcknowledgement, UmicomFat16InterruptionEvidence *evidence)
{
    UmicomLabDisk disk;
    uint32_t step;
    uint64_t originalHash;
    if (evidence == NULL || (mode != UMICOM_FAT16_INTERRUPTION_WRITE_BACK
                            && mode != UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH)
        || stopAfterStep > UMICOM_FAT16_INTERRUPTION_FINAL_STEP
        || (loseFinalAcknowledgement && stopAfterStep != UMICOM_FAT16_INTERRUPTION_FINAL_STEP)) {
        return UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT;
    }
    memset(evidence, 0, sizeof(*evidence));
    if (!UmicomLabInitialise(&disk, mode)) {
        UmicomLabDispose(&disk);
        return UMICOM_FAT16_INTERRUPTION_NO_MEMORY;
    }
    originalHash = UmicomLabHash(disk.initial, UMICOM_LAB_IMAGE_BYTES);
    for (step = 1U; step <= stopAfterStep; ++step) {
        UmicomLabApplyStep(&disk, step);
    }
    if (stopAfterStep == UMICOM_FAT16_INTERRUPTION_FINAL_STEP) {
        evidence->finalReadbackVerified = UmicomLabFinalReadback(&disk);
        if (!evidence->finalReadbackVerified) {
            UmicomLabDispose(&disk);
            return UMICOM_FAT16_INTERRUPTION_INTERNAL_ERROR;
        }
    }
    /* Volatile write cache disappears: only durable storage survives reboot.
     * For write-through models, completed writes already reached durability. */
    memcpy(disk.visible, disk.durable, UMICOM_LAB_IMAGE_BYTES);
    evidence->mode = mode;
    evidence->stopAfterStep = stopAfterStep;
    evidence->publishedWrites = disk.writes;
    evidence->acknowledgedFlushes = disk.flushes;
    evidence->finalAcknowledgementLost = loseFinalAcknowledgement;
    evidence->writerReportedAccepted =
        stopAfterStep == UMICOM_FAT16_INTERRUPTION_FINAL_STEP
        && evidence->finalReadbackVerified && !loseFinalAcknowledgement;
    UmicomLabObserveDisk(&disk, evidence);
    evidence->originalSourceUnchanged =
        originalHash == UmicomLabHash(disk.initial, UMICOM_LAB_IMAGE_BYTES);
    /* The only bytes ever eligible to change are two FAT flag sectors and
     * two file-data sectors; the loader, MBR, root and allocation are fixed. */
    evidence->mutationsRestrictedToExpectedSectors = true;
    for (uint32_t sector = 0U; sector < UMICOM_FAT16_INTERRUPTION_IMAGE_SECTORS; ++sector) {
        bool changed = memcmp(UmicomLabSectorConst(disk.initial, sector),
                              UmicomLabSectorConst(disk.visible, sector),
                              UMICOM_LAB_SECTOR_BYTES) != 0;
        if (changed && sector != UMICOM_LAB_PRIMARY_FAT
            && sector != UMICOM_LAB_MIRROR_FAT && sector != UMICOM_LAB_DATA_FIRST
            && sector != UMICOM_LAB_DATA_SECOND) {
            evidence->mutationsRestrictedToExpectedSectors = false;
            break;
        }
    }
    UmicomLabDispose(&disk);
    return UMICOM_FAT16_INTERRUPTION_OK;
}

UmicomFat16InterruptionStatus UmicomFat16InterruptionDemonstrateCleanHybrid(
    UmicomFat16InterruptionEvidence *evidence)
{
    UmicomLabDisk disk;
    if (evidence == NULL) return UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT;
    memset(evidence, 0, sizeof(*evidence));
    if (!UmicomLabInitialise(&disk, UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH)) {
        UmicomLabDispose(&disk);
        return UMICOM_FAT16_INTERRUPTION_NO_MEMORY;
    }
    /* An external or broken writer can change data without the protocol's
     * dirty guards. This deliberate fault is NOT a permitted commit step. */
    const uint64_t originalHash = UmicomLabHash(disk.initial, UMICOM_LAB_IMAGE_BYTES);
    UmicomLabDataWrite(&disk, UMICOM_LAB_DATA_FIRST);
    memcpy(disk.visible, disk.durable, UMICOM_LAB_IMAGE_BYTES);
    evidence->mode = UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH;
    evidence->publishedWrites = disk.writes;
    UmicomLabObserveDisk(&disk, evidence);
    evidence->mutationsRestrictedToExpectedSectors = true;
    evidence->originalSourceUnchanged =
        originalHash == UmicomLabHash(disk.initial, UMICOM_LAB_IMAGE_BYTES);
    UmicomLabDispose(&disk);
    return UMICOM_FAT16_INTERRUPTION_OK;
}

static bool UmicomLabExpected(uint32_t stopAfterStep, UmicomFat16InterruptionMode mode,
                              const UmicomFat16InterruptionEvidence *evidence)
{
    bool primaryDirty = mode == UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH
                        ? stopAfterStep >= 3U && stopAfterStep < 10U
                        : stopAfterStep >= 4U && stopAfterStep < 11U;
    bool mirrorDirty = mode == UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH
                       ? stopAfterStep >= 1U && stopAfterStep < 8U
                       : stopAfterStep >= 2U && stopAfterStep < 9U;
    bool dataFirst = mode == UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH
                     ? stopAfterStep >= 5U : stopAfterStep >= 7U;
    bool dataSecond = mode == UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH
                      ? stopAfterStep >= 6U : stopAfterStep >= 7U;
    UmicomFat16AuditClassification header = primaryDirty != mirrorDirty
        ? UMICOM_FAT16_AUDIT_MIRROR_MISMATCH
        : (primaryDirty ? UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR
                        : UMICOM_FAT16_AUDIT_CLEAN_MIRRORED);
    UmicomFat16InterruptionPayload payload =
        dataFirst != dataSecond ? UMICOM_FAT16_INTERRUPTION_MIXED_BYTES
        : (dataFirst ? UMICOM_FAT16_INTERRUPTION_NEW_BYTES
                     : UMICOM_FAT16_INTERRUPTION_OLD_BYTES);
    return evidence->headerClassification == header
           && evidence->allocationClassification ==
               (header == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED
                    ? UMICOM_FAT16_INTEGRITY_CONSISTENT
                    : UMICOM_FAT16_INTEGRITY_NOT_ADMITTED)
           && evidence->payload == payload
           && evidence->mutationsRestrictedToExpectedSectors
           && evidence->originalSourceUnchanged
           && evidence->requiresIndependentRecoveryDecision
           && evidence->finalReadbackVerified ==
                  (stopAfterStep == UMICOM_FAT16_INTERRUPTION_FINAL_STEP);
}

UmicomFat16InterruptionStatus UmicomFat16InterruptionRunMatrix(
    UmicomFat16InterruptionSummary *summary)
{
    UmicomFat16InterruptionEvidence evidence;
    UmicomFat16InterruptionStatus status;
    if (summary == NULL) return UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT;
    memset(summary, 0, sizeof(*summary));
    for (uint32_t modeValue = 0U; modeValue < 2U; ++modeValue) {
        UmicomFat16InterruptionMode mode = (UmicomFat16InterruptionMode)modeValue;
        for (uint32_t stopAfter = 0U; stopAfter <= UMICOM_FAT16_INTERRUPTION_FINAL_STEP + 1U;
             ++stopAfter) {
            const bool lostAck = stopAfter == UMICOM_FAT16_INTERRUPTION_FINAL_STEP + 1U;
            const uint32_t actualStop = lostAck ? UMICOM_FAT16_INTERRUPTION_FINAL_STEP : stopAfter;
            status = UmicomFat16InterruptionObserve(mode, actualStop, lostAck, &evidence);
            if (status != UMICOM_FAT16_INTERRUPTION_OK) return status;
            if (!UmicomLabExpected(actualStop, mode, &evidence)
                || (lostAck && evidence.writerReportedAccepted)
                || (actualStop == UMICOM_FAT16_INTERRUPTION_FINAL_STEP && !lostAck
                    && !evidence.writerReportedAccepted)) {
                return UMICOM_FAT16_INTERRUPTION_INTERNAL_ERROR;
            }
            summary->scenarios += 1U;
            if (evidence.headerClassification == UMICOM_FAT16_AUDIT_CLEAN_MIRRORED) {
                ++summary->cleanHeaderCases;
                if (!evidence.writerReportedAccepted && evidence.publishedWrites > 0U) {
                    ++summary->cleanHeaderUnconfirmedMutationCases;
                }
            } else if (evidence.headerClassification == UMICOM_FAT16_AUDIT_DIRTY_OR_IO_ERROR) {
                ++summary->dirtyHeaderCases;
            } else if (evidence.headerClassification == UMICOM_FAT16_AUDIT_MIRROR_MISMATCH) {
                ++summary->mismatchedHeaderCases;
            }
            if (evidence.payload == UMICOM_FAT16_INTERRUPTION_OLD_BYTES) ++summary->oldPayloadCases;
            if (evidence.payload == UMICOM_FAT16_INTERRUPTION_NEW_BYTES) ++summary->newPayloadCases;
            if (evidence.payload == UMICOM_FAT16_INTERRUPTION_MIXED_BYTES) ++summary->mixedPayloadCases;
            if (evidence.writerReportedAccepted) ++summary->acceptedWriterCases;
            if (evidence.requiresIndependentRecoveryDecision) ++summary->independentReviewCases;
        }
    }
    return summary->scenarios == 28U && summary->acceptedWriterCases == 2U
           ? UMICOM_FAT16_INTERRUPTION_OK : UMICOM_FAT16_INTERRUPTION_INTERNAL_ERROR;
}

const char *UmicomFat16InterruptionModeName(UmicomFat16InterruptionMode mode)
{
    switch (mode) {
        case UMICOM_FAT16_INTERRUPTION_WRITE_BACK: return "write-back";
        case UMICOM_FAT16_INTERRUPTION_WRITE_THROUGH: return "write-through";
        default: return "invalid";
    }
}

const char *UmicomFat16InterruptionPayloadName(UmicomFat16InterruptionPayload payload)
{
    switch (payload) {
        case UMICOM_FAT16_INTERRUPTION_OLD_BYTES: return "old";
        case UMICOM_FAT16_INTERRUPTION_NEW_BYTES: return "new";
        case UMICOM_FAT16_INTERRUPTION_MIXED_BYTES: return "mixed";
        default: return "invalid";
    }
}

const char *UmicomFat16InterruptionStatusName(UmicomFat16InterruptionStatus status)
{
    switch (status) {
        case UMICOM_FAT16_INTERRUPTION_OK: return "ok";
        case UMICOM_FAT16_INTERRUPTION_BAD_ARGUMENT: return "bad-argument";
        case UMICOM_FAT16_INTERRUPTION_NO_MEMORY: return "no-memory";
        case UMICOM_FAT16_INTERRUPTION_INTERNAL_ERROR: return "invariant-failure";
        default: return "invalid-status";
    }
}
