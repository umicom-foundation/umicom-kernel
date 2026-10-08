/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_lifecycle_commit.h
 *
 * Exclusive persistent regular-file management: create, allocating append,
 * truncate and delete. Each operation requires an explicit clean Finish. An
 * accepted operation permits the next operation under the same live lease;
 * failure after submission permits only resource release, never repair.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_LIFECYCLE_COMMIT_H
#define UMICOM_KERNEL_FAT16_LIFECYCLE_COMMIT_H
#include "umicom/kernel/fat16_commit.h"
#include "umicom/kernel/fat16_lifecycle.h"

/* Evidence belongs to the latest admitted operation, not a sum over the
 * session. committedOperations counts only accepted Finish calls. planned
 * qualifies entries, sizes, calendar and planned-sector counts. Data counters
 * include real zero-initialisation WRITEs; byte counters include only caller
 * payload. No-data operations retain NOT_SUBMITTED and false data flags.
 *
 * FAT counters below exclude initial dirty and final clean header guards,
 * which remain in commit's total metadata counters. changedFatSectors counts
 * distinct planned sectors; submitting both copies can double that count.
 * When no FAT links change, the per-copy durable flags remain false, while
 * verification can prove unchanged links without synthetic durability evidence.
 * Every durable/verified flag is historical evidence, not retry authority. */
typedef struct UmicomKernelFat16LifecycleResult {
    UmicomKernelFat16CommitResult commit;
    UmicomKernelFat16LifecycleOperation operation;
    UmicomU64 committedOperations;
    UmicomBoolean planned;
    UmicomBoolean originalEntryPresent;
    UmicomBoolean updatedEntryPresent;
    UmicomKernelFat16Entry originalEntry;
    UmicomKernelFat16Entry updatedEntry;
    UmicomKernelFat16FileTime requestedTime;
    UmicomKernelFat16FileTimeEncoding encodedTime;
    UmicomSize allocatedClusters;
    UmicomSize freedClusters;
    UmicomSize plannedDataSectors;
    UmicomSize plannedFatSectors;
    UmicomSize changedFatSectors;
    UmicomSize plannedDirectorySectors;
    UmicomU64 directorySector;
    UmicomSize entryOffset;
    UmicomSize submittedFatSectors;
    UmicomSize completedFatSectors;
    UmicomSize submittedDirectorySectors;
    UmicomSize completedDirectorySectors;
    UmicomSize completedDirectoryFlushes;
    UmicomBoolean fatMirrorDurable;
    UmicomBoolean fatPrimaryDurable;
    UmicomBoolean fatMirrorVerified;
    UmicomBoolean fatPrimaryVerified;
    UmicomBoolean directoryDurable;
    UmicomBoolean directoryVerified;
} UmicomKernelFat16LifecycleResult;

/* Stable initially zero-filled storage. All embedded owners, snapshots and
 * request buffers are private. They are deliberately outside the Kernel stack
 * for normal callers. Never copy/reinitialise an admitted owner or call an
 * embedded owner's public API. Serialise the complete outer lifetime. */
typedef struct UmicomKernelFat16LifecycleCommitter {
    const struct UmicomKernelFat16LifecycleCommitter *self;
    UmicomKernelFat16Committer commit;
    UmicomKernelFat16LifecycleWorkspace workspace;
    UmicomKernelFat16LifecyclePlan plan;
    UmicomKernelFat16LifecycleRequest request;
    char path[UMICOM_FAT16_PATH_BYTES];
    char destination[UMICOM_FAT16_PATH_BYTES];
    UmicomU8 input[UMICOM_FAT16_UPDATE_BYTES];
    UmicomU8 finalDirtyHeader[UMICOM_DISK_SECTOR_BYTES];
    UmicomU64 committedOperations;
    UmicomKernelFat16LifecycleResult lastResult;
    UmicomBoolean busy;
} UmicomKernelFat16LifecycleCommitter;

/* Acquire the qualified writable/FLUSH lease without mutation. Keep the
 * medium exclusively owned through calls and idle intervals. A failed Open
 * may retain resources; retry Close until released. The outer lifetime is
 * single-use even though it can accept multiple separately finished changes. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16LifecycleOpen(
    UmicomKernelFat16LifecycleCommitter *owner, UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks);

/* MOVE uses this same lease and leaves allocation unchanged. With both FAT
 * headers durably dirty, it updates a directory's parent reference, tombstones
 * the old entry, prepares a successor marker if needed, then exposes the new
 * entry. Each distinct sector is flushed and verified. Same-parent rename
 * changes its existing sector alone. Finish rechecks all images before CLEAN.
 * Interruption can leave a missing or inconsistent namespace; release does not
 * repair it. An error after submission permits Close only. */

/* Directory operations use this same owner. CREATE_DIRECTORY initializes dot
 * entries and allocation before publication; REMOVE_DIRECTORY tombstones an
 * empty directory before freeing its chain. A parent extension publishes its
 * prepared entry with the new FAT link, so its initialization write is counted
 * as data and is not repeated as an identical directory-sector submission.
 * The original regular-file protocol description below is retained for review;
 * its per-directory-write statement is superseded for that extension case. */

/* Admit a request in READY or after the preceding accepted Finish. Snapshot
 * request/path/payload before media callbacks; re-open the clean inspector,
 * prove the complete plan, verify original FAT/directory sectors and close
 * the inspector before mutation. Each new operation gets fresh read/deadline
 * budgets and independent result counters. A second Stage while STAGED fails.
 *
 * Dirty FAT2 then FAT1, each WRITE/FLUSH, then verify both. CREATE/APPEND initialise and
 * verify data, publish FAT2 then FAT1 with separate FLUSHes/verification, then
 * publish the directory. TRUNCATE/DELETE publish the reduced/absent directory
 * reference before releasing allocation. Each directory sector has its own
 * WRITE/FLUSH/verify; a successor end marker precedes its new live entry.
 * A retained truncation tail detaches
 * before other FAT sectors are freed; an append bridge follows new links.
 * Stage success leaves both FATs dirty and STAGED. Only Finish accepts it.
 *
 * Typed request/result storage must be naturally aligned, non-overflowing and
 * independent of each other, the entire owner, domain and every DMA frame.
 * Pointed path/payload spans must also be independent. Callbacks must not edit
 * caller/owner storage or the medium, retain pointers or re-enter. These are
 * trusted Kernel pointers, not a user-pointer probing ABI. Pre-admission errors
 * preserve output/history. Read-only preflight refusal leaves READY; failure
 * after any submission leaves FAILED and closeable only. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16LifecycleStage(
    UmicomKernelFat16LifecycleCommitter *owner,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16LifecycleResult *outResult);

/* Re-verify all planned data, both final dirty FAT copies and every directory
 * sector. Publish final CLEAN FAT2 then FAT1, each with FLUSH and final header
 * verification. These headers retain new allocation words; old snapshots are
 * never restored. Accepted completion increments committedOperations and
 * permits a subsequent Stage under this lease. Every admitted Finish failure
 * retains the count and leaves FAILED. Pre-admission refusal preserves state
 * and evidence. Late failure can follow clean publication;
 * there is no rollback, re-dirtying, recovery or automatic retry. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16LifecycleFinish(
    UmicomKernelFat16LifecycleCommitter *owner,
    UmicomKernelFat16LifecycleResult *outResult);

/* Release only, without WRITE/FLUSH/Finish/repair. An unfinished image retains
 * interruption evidence. Cleanup failure retains CLOSING/resources for another
 * Close. Success scrubs plans/inputs but preserves result and accepted count. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16LifecycleClose(
    UmicomKernelFat16LifecycleCommitter *owner);
const char *UmicomKernelFat16LifecycleOperationName(UmicomKernelFat16LifecycleOperation operation);

void UmicomKernelFat16LifecycleValidate(void);
void UmicomKernelFat16LifecycleReadbackValidate(void);
void UmicomKernelFat16LifecycleInterruptedValidate(void);
void UmicomKernelFat16LifecycleRejectedValidate(void);
_Noreturn void UmicomKernelFat16LifecycleBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_LIFECYCLE_COMMIT_H */
