/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_update.h
 *
 * Explicit data updates within an existing, exclusively owned FAT16 allocation.
 * This trusted Kernel utility preserves every metadata byte. It is not a
 * writable VFS mount: creating, resizing, deleting, updating timestamps and
 * maintaining on-disk dirty/error flags require a separate metadata protocol.
 *
 * Supply stable, initially zero-filled storage. Serialise all calls and keep
 * the backing medium exclusively owned for the entire lease, including idle
 * intervals. Local block ownership cannot prevent an external host writer.
 * Never copy, overwrite or reinitialise an admitted or retained owner.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_UPDATE_H
#define UMICOM_KERNEL_FAT16_UPDATE_H
#include "umicom/kernel/fat16_update_plan.h"
#include "umicom/kernel/virtio_block.h"

/* Whole-operation acceptance bound, independently restarted for Open, Write
 * and Flush. The qualified QEMU clock is 10 MHz. Each submitted block request
 * also retains its own timeout and finite polling bound. */
#define UMICOM_FAT16_UPDATE_OPERATION_TICKS 100000000U

typedef enum UmicomKernelFat16UpdateStatus {
    UMICOM_FAT16_UPDATE_OK,
    UMICOM_FAT16_UPDATE_INVALID_ARGUMENT,
    UMICOM_FAT16_UPDATE_BAD_STATE,
    UMICOM_FAT16_UPDATE_BUSY,
    UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT,
    UMICOM_FAT16_UPDATE_READ_ONLY,
    UMICOM_FAT16_UPDATE_RANGE,
    UMICOM_FAT16_UPDATE_INSPECTION_LIMIT,
    UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR,
    UMICOM_FAT16_UPDATE_TRANSPORT_ERROR,
    UMICOM_FAT16_UPDATE_RELEASE_FAILED,
    UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN
} UmicomKernelFat16UpdateStatus;

typedef enum UmicomKernelFat16UpdaterState {
    UMICOM_FAT16_UPDATER_UNUSED,
    UMICOM_FAT16_UPDATER_OPEN,
    UMICOM_FAT16_UPDATER_CLOSING,
    UMICOM_FAT16_UPDATER_CLOSED
} UmicomKernelFat16UpdaterState;

typedef enum UmicomKernelFat16UpdateOutcome {
    UMICOM_FAT16_UPDATE_NOT_SUBMITTED,
    UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED,
    UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED,
    UMICOM_FAT16_UPDATE_COMPLETED
} UmicomKernelFat16UpdateOutcome;

typedef struct UmicomKernelFat16UpdateResult {
    UmicomKernelFat16UpdateStatus status;
    UmicomKernelDiskStatus diskStatus;
    UmicomKernelBlockStatus blockStatus;
    UmicomKernelFat16UpdateOutcome outcome;
    UmicomKernelBlockMutationOutcome lastBlockOutcome;
    UmicomU64 offset;
    UmicomSize requestedBytes;
    UmicomSize confirmedBytes;
    UmicomSize submittedBytes;
    UmicomU64 uncertainOffset;
    UmicomSize uncertainBytes;
    UmicomU64 uncertainSector;
    UmicomSize completedSectors;
    UmicomSize submittedSectors;
    UmicomBoolean needsFlush;
    UmicomBoolean writeUncertain;
} UmicomKernelFat16UpdateResult;

typedef struct UmicomKernelFat16Updater {
    const struct UmicomKernelFat16Updater *self;
    UmicomKernelFat16UpdaterState state;
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
    UmicomSize slot;
    UmicomSize partition;
    UmicomU64 timeoutTicks;
    UmicomU64 sectors;
    UmicomKernelFat16Info info;
    UmicomKernelFat16 volume;
    UmicomKernelFat16UpdateWorkspace workspace;
    UmicomKernelFat16UpdatePlan plan;
    UmicomKernelFat16UpdateResult lastWrite;
    UmicomKernelBlockMutationOutcome lastFlush;
    UmicomKernelDiskStatus lastDiskStatus;
    UmicomKernelBlockStatus lastBlockStatus;
    UmicomKernelBlockStatus lastCleanupStatus;
    UmicomKernelFat16UpdateStatus lastStatus;
    UmicomBoolean busy;
    UmicomBoolean admitted;
    UmicomBoolean needsFlush;
    UmicomBoolean writeUncertain;
    UmicomU64 operationStarted;
    UmicomU64 operationClock;
    UmicomSize operationReads;
} UmicomKernelFat16Updater;

/* Acquire a separate writable block lease requiring FLUSH, check MBR/BPB and
 * close the temporary inspector. Whole-volume update qualification happens
 * again for every Write. An unsuccessful Open can retain a cleanup handle:
 * keep the owner alive and call Close until release succeeds. Failed admission
 * may be retried after cleanup; a successfully admitted lifetime is single-use.
 * The owner must be independent of the domain and every retained DMA frame. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateOpen(UmicomKernelFat16Updater *owner,
    UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize partition,
    UmicomU64 timeoutTicks);

/* Update 1..4096 bytes wholly inside an existing regular file with ARCHIVE set
 * and READ_ONLY clear. A bounded whole-volume ownership proof and all touched
 * sector reads finish before the inspector closes and the first WRITE starts.
 * Unaligned input can touch nine sectors, written one at a time in file order.
 * Successful writes preserve surrounding bytes and cluster slack.
 *
 * Inputs, the NUL-terminated bounded path and aligned outResult are trusted,
 * stable, pairwise disjoint buffers, independent of this owner, its domain and
 * all retained DMA frames. Invalid storage, invalid state, reentry, unsafe
 * context or a prior uncertain write leave outResult and lastWrite untouched.
 * After admission every result is published, including a preflight refusal.
 *
 * confirmedBytes is the fully validated caller-byte prefix. submittedBytes
 * includes any following published but unconfirmed sector's caller bytes.
 * uncertainOffset/uncertainBytes describe that caller intersection, whereas
 * uncertainSector names the ENTIRE 512-byte physical sector at risk, including
 * neighbouring file bytes or slack. Zero uncertainBytes means no uncertain
 * sector in this attempt. Completed writes still require explicit Flush.
 *
 * A non-OK status never implies rollback. A late whole-operation deadline can
 * fail even after all individual completions were validated; the counts and
 * outcome retain those observations. Never infer a retry point solely from
 * confirmedBytes. Any unconfirmed WRITE blocks further Write calls for this
 * owner, while Flush and Close remain available. No automatic retry occurs. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateWrite(UmicomKernelFat16Updater *owner,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    UmicomKernelFat16UpdateResult *outResult);

/* An acknowledged FLUSH clears needsFlush but cannot establish which bytes an
 * earlier uncertain WRITE changed. lastWrite and writeUncertain are preserved.
 * outOutcome is mandatory, aligned and independent of owner/domain/DMA. As for
 * Write, pre-admission refusals leave it untouched. A COMPLETED outcome remains
 * truthful even if the enclosing operation subsequently exceeds its deadline.
 * Persistence also depends on the backend honouring its flush acknowledgement. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateFlush(UmicomKernelFat16Updater *owner,
    UmicomKernelBlockMutationOutcome *outOutcome);

/* Cleanup never flushes, retries file data, repairs metadata or clears the last
 * write evidence. Reset/release failure retains the original handle and owner
 * in CLOSING state; retry Close. Successful Close proves resource release only.
 * Close on zero storage or a fully closed owner is harmless and idempotent. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateClose(UmicomKernelFat16Updater *owner);
const char *UmicomKernelFat16UpdateStatusName(UmicomKernelFat16UpdateStatus status);

void UmicomKernelFat16UpdateValidate(void);
void UmicomKernelFat16UpdateReadbackValidate(void);
_Noreturn void UmicomKernelFat16UpdateBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_UPDATE_H */
