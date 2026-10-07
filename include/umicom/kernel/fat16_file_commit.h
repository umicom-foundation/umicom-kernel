/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_file_commit.h
 *
 * Ordered existing-file data and directory-metadata commits. The checked plan
 * sets ARCHIVE and writes an explicitly supplied FAT calendar time, preserving
 * size, allocation, names and every other directory-sector byte. Persistent
 * dirty flags enclose data and metadata writes until explicit Finish succeeds.
 * This is interruption detection and ordered persistence, not atomicity,
 * journalling, rollback, repair, file creation or general writable VFS access.
 *
 * Supply stable, initially zero-filled trusted Kernel storage. Serialise calls
 * and retain exclusive backing-medium ownership through Stage and Finish,
 * including the idle interval. Never copy or reinitialise an admitted owner.
 * The embedded committer, updater and workspaces are private implementation
 * storage; their public APIs must not be called directly by the outer caller.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_FILE_COMMIT_H
#define UMICOM_KERNEL_FAT16_FILE_COMMIT_H
#include "umicom/kernel/fat16_commit.h"
#include "umicom/kernel/fat16_file_plan.h"

/* Cumulative evidence for the one staged file update. The embedded record's
 * metadata counters include directory WRITE as well as the FAT header WRITEs.
 * All durable/verified flags are historical observations. A failed Finish can
 * follow clean publication; no field grants permission to retry or repair.
 * directoryPlanned qualifies the address/time/attribute fields. A directory
 * WRITE is always a complete sector, even though only five entry bytes are
 * eligible to change. Uncertain-sector evidence is in commit. */
typedef struct UmicomKernelFat16FileCommitResult {
    UmicomKernelFat16CommitResult commit;
    UmicomU64 directorySector;
    UmicomSize entryOffset;
    UmicomKernelFat16FileTime requestedTime;
    UmicomKernelFat16FileTimeEncoding encodedTime;
    UmicomU8 originalAttributes;
    UmicomU8 updatedAttributes;
    UmicomBoolean directoryPlanned;
    UmicomBoolean directorySubmitted;
    UmicomBoolean directoryCompleted;
    UmicomBoolean directoryDurable;
    UmicomBoolean directoryVerified;
} UmicomKernelFat16FileCommitResult;

typedef struct UmicomKernelFat16FileCommitter {
    const struct UmicomKernelFat16FileCommitter *self;
    UmicomKernelFat16Committer commit;
    UmicomKernelFat16FileUpdateWorkspace fileWorkspace;
    UmicomKernelFat16FileUpdatePlan filePlan;
    UmicomKernelFat16FileCommitResult lastResult;
    UmicomBoolean busy;
} UmicomKernelFat16FileCommitter;

/* Read-only admission acquires the qualified writable/FLUSH lease. A failed
 * admission may retain resources: retry Close until release succeeds. An
 * admitted owner is single-use, even after successful Finish and Close. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitOpen(
    UmicomKernelFat16FileCommitter *owner, UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks);

/* Stage one 1..4096-byte overwrite wholly inside a checked existing file.
 * The timestamp must contain a valid Gregorian calendar date in 1980..2107.
 * Seconds are floored to FAT's two-second precision. No clock or timezone is
 * inferred. The caller chooses the calendar convention used on the medium.
 *
 * Full read-only planning precedes mutation. Stage makes FAT2 and FAT1 dirty
 * with a separate FLUSH after each; verifies both; writes, flushes and verifies
 * data; then writes, flushes and verifies the directory sector. Success leaves
 * the volume dirty and the owner STAGED. READ_ONLY is refused; ARCHIVE may
 * initially be clear or set and is set by this operation.
 *
 * Path, time, input and result must have independent non-overflowing extents,
 * disjoint from the entire owner, domain and all DMA frames. Typed objects
 * must be aligned. Callbacks must not edit caller/owner storage or the medium,
 * retain pointers or re-enter active owners. Pre-admission refusals leave
 * output and prior evidence untouched. A read-only preflight refusal leaves
 * READY; any failure after metadata submission leaves FAILED, closeable only.
 * The existing bounded read budget and enclosing Stage deadline both apply. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitStage(
    UmicomKernelFat16FileCommitter *owner, const char *path, UmicomU64 offset,
    const void *input, UmicomSize bytes, const UmicomKernelFat16FileTime *time,
    UmicomKernelFat16FileCommitResult *outResult);

/* Reverify both dirty headers, all staged data and the complete directory
 * sector before any clean publication. Then restore FAT2 and FAT1 clean, with
 * a separate FLUSH each, and verify both headers within one Finish deadline.
 * Only accepted completion sets COMMITTED. Failure never retries file data,
 * rolls back metadata or re-dirties a possibly completed clean publication. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitFinish(
    UmicomKernelFat16FileCommitter *owner, UmicomKernelFat16FileCommitResult *outResult);

/* Resource release only: no WRITE, FLUSH, Finish, rollback or flag repair.
 * Cleanup failure retains CLOSING state and all backing resources for another
 * Close. Successful release scrubs payload plans, keeping historical result.
 * Closing a zero-filled, never-admitted owner is harmless. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitClose(
    UmicomKernelFat16FileCommitter *owner);

void UmicomKernelFat16FileCommitValidate(void);
void UmicomKernelFat16FileCommitReadbackValidate(void);
void UmicomKernelFat16FileCommitInterruptedValidate(void);
void UmicomKernelFat16FileCommitRejectedValidate(void);
_Noreturn void UmicomKernelFat16FileCommitBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_FILE_COMMIT_H */
