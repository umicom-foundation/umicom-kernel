/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_commit.h
 *
 * Ordered, bounded FAT16 data updates with persistent interruption detection.
 * A staged update first makes both FAT clean flags durably dirty. Finish may
 * restore the original clean headers only after complete file-sector readback.
 * These are ordered writes, not an atomic transaction, journal or repair tool.
 * File size/allocation, directory records and timestamps remain unchanged;
 * eligible files must already have ARCHIVE set and READ_ONLY clear.
 *
 * Supply stable, initially zero-filled trusted Kernel storage. Serialise calls,
 * retain exclusive backing-medium ownership even between Stage and Finish,
 * and never copy/reinitialise a live or previously admitted owner. The embedded
 * updater is private implementation storage: never call its public API directly.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_COMMIT_H
#define UMICOM_KERNEL_FAT16_COMMIT_H
#include "umicom/kernel/fat16_update.h"

#define UMICOM_FAT16_CLEAN_MASK 0x8000U
#define UMICOM_FAT16_NO_ERROR_MASK 0x4000U

typedef enum UmicomKernelFat16CommitState {
    UMICOM_FAT16_COMMIT_UNUSED,
    UMICOM_FAT16_COMMIT_READY,
    UMICOM_FAT16_COMMIT_STAGED,
    UMICOM_FAT16_COMMIT_FAILED,
    UMICOM_FAT16_COMMIT_COMMITTED,
    UMICOM_FAT16_COMMIT_CLOSING,
    UMICOM_FAT16_COMMIT_CLOSED
} UmicomKernelFat16CommitState;

typedef enum UmicomKernelFat16CommitPhase {
    UMICOM_FAT16_COMMIT_NONE,
    UMICOM_FAT16_COMMIT_PREFLIGHT,
    UMICOM_FAT16_COMMIT_DIRTY_MIRROR,
    UMICOM_FAT16_COMMIT_DIRTY_MIRROR_FLUSH,
    UMICOM_FAT16_COMMIT_DIRTY_PRIMARY,
    UMICOM_FAT16_COMMIT_DIRTY_PRIMARY_FLUSH,
    UMICOM_FAT16_COMMIT_DIRTY_VERIFY,
    UMICOM_FAT16_COMMIT_DATA_WRITE,
    UMICOM_FAT16_COMMIT_DATA_FLUSH,
    UMICOM_FAT16_COMMIT_DATA_VERIFY,
    UMICOM_FAT16_COMMIT_FINISH_VERIFY,
    UMICOM_FAT16_COMMIT_CLEAN_MIRROR,
    UMICOM_FAT16_COMMIT_CLEAN_MIRROR_FLUSH,
    UMICOM_FAT16_COMMIT_CLEAN_PRIMARY,
    UMICOM_FAT16_COMMIT_CLEAN_PRIMARY_FLUSH,
    UMICOM_FAT16_COMMIT_CLEAN_VERIFY,
    UMICOM_FAT16_COMMIT_COMPLETE
    /* Append new phases without changing any established value or line. */
    , UMICOM_FAT16_COMMIT_DIRECTORY_WRITE
    , UMICOM_FAT16_COMMIT_DIRECTORY_FLUSH
    , UMICOM_FAT16_COMMIT_DIRECTORY_VERIFY
} UmicomKernelFat16CommitPhase;

/* Cumulative evidence for this owner's one update. Completion of a request,
 * persistence acknowledgement, byte readback and accepted Finish are separate
 * observations. A late clock failure preserves completed observations while
 * returning an error. An uncertain sector identifies the entire physical
 * sector, whether it held a FAT header or caller data. A failed FLUSH has no
 * sector of its own; preceding WRITE evidence remains available.
 *
 * cleanFinalisationStarted does not promise the disk is still dirty: once clean
 * writes are submitted, lost acknowledgement can leave clean complete media.
 * Nothing in this record authorises retry, rollback or automatic flag repair. */
/* All durable/verified flags record historical observations, not the current
 * clean/dirty state after later phases. lastBlockOutcome belongs to the latest
 * attempted WRITE/FLUSH, including during subsequent verification reads. */
typedef struct UmicomKernelFat16CommitResult {
    UmicomKernelFat16UpdateStatus status;
    UmicomKernelDiskStatus diskStatus;
    UmicomKernelBlockStatus blockStatus;
    UmicomKernelFat16CommitPhase phase;
    UmicomKernelBlockMutationOutcome lastBlockOutcome;
    UmicomKernelFat16UpdateOutcome dataOutcome;
    UmicomU64 offset;
    UmicomSize requestedBytes;
    UmicomSize confirmedBytes;
    UmicomSize submittedBytes;
    UmicomSize completedDataSectors;
    UmicomSize submittedDataSectors;
    UmicomSize completedMetadataSectors;
    UmicomSize submittedMetadataSectors;
    UmicomSize completedFlushes;
    UmicomU64 uncertainSector;
    UmicomBoolean uncertainSectorValid;
    UmicomBoolean mediaTouched;
    UmicomBoolean dirtyDurable;
    UmicomBoolean dirtyVerified;
    UmicomBoolean dataDurable;
    UmicomBoolean dataVerified;
    UmicomBoolean cleanFinalisationStarted;
    UmicomBoolean cleanDurable;
    UmicomBoolean cleanVerified;
    UmicomBoolean commitAccepted;
    UmicomBoolean needsFlush;
    UmicomBoolean writeUncertain;
} UmicomKernelFat16CommitResult;

typedef struct UmicomKernelFat16Committer {
    const struct UmicomKernelFat16Committer *self;
    UmicomKernelFat16CommitState state;
    UmicomKernelFat16Updater updater;
    UmicomKernelFat16CommitResult lastResult;
    UmicomU64 headerSectors[2];
    UmicomU8 cleanHeader[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 dirtyHeader[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 readback[UMICOM_DISK_SECTOR_BYTES];
    UmicomBoolean busy;
} UmicomKernelFat16Committer;

/* Open only reads media and acquires the qualified writable/FLUSH lease. A
 * failed admission may retain resources: call Close until release succeeds.
 * A successfully admitted owner is single-use, including after a clean commit. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitOpen(UmicomKernelFat16Committer *owner,
    UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize partition,
    UmicomU64 timeoutTicks);

/* Stage applies one 1..4096-byte existing-file update. It plans while clean,
 * closes the inspector, then performs mirror-dirty WRITE/FLUSH, primary-dirty
 * WRITE/FLUSH, full header verification, data WRITEs, data FLUSH and full-sector
 * readback. Success leaves both FAT copies dirty and the owner STAGED.
 *
 * The same path/input/result ownership rules as UpdateWrite apply to the entire
 * outer owner and all domain/DMA storage. Pre-admission refusals leave result
 * storage and historical evidence untouched. A preflight refusal leaves READY;
 * an error after any metadata submission leaves FAILED, even without data I/O.
 * Stage does not permit a second update or repair an earlier failed attempt. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitStage(UmicomKernelFat16Committer *owner,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    UmicomKernelFat16CommitResult *outResult);

/* Finish rechecks both dirty headers and all staged file sectors, then performs
 * mirror-clean WRITE/FLUSH, primary-clean WRITE/FLUSH and full clean-header
 * readback. Only a fully accepted result enters COMMITTED. Failure never tries
 * to undo data or re-dirty a possibly completed commit. If clean publication
 * began, a later fresh reader may see clean complete data despite this error. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitFinish(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitResult *outResult);

/* Close never flushes, commits, clears on-disk flags or changes lastResult.
 * Closing a staged/failed update deliberately leaves any interruption evidence.
 * Reset/release failure retains the owner in CLOSING; retry only Close. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitClose(UmicomKernelFat16Committer *owner);
const char *UmicomKernelFat16CommitStateName(UmicomKernelFat16CommitState state);
const char *UmicomKernelFat16CommitPhaseName(UmicomKernelFat16CommitPhase phase);

void UmicomKernelFat16CommitValidate(void);
void UmicomKernelFat16CommitReadbackValidate(void);
void UmicomKernelFat16CommitInterruptedValidate(void);
void UmicomKernelFat16CommitRejectedValidate(void);
_Noreturn void UmicomKernelFat16CommitBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_COMMIT_H */
