/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_rename_commit.h
 *
 * Ordered metadata-only short-name rename with explicit clean publication.
 * This dedicated owner retains the established exclusive lease, dirty-header
 * guards and failure evidence without fabricating any file-data operation.
 * It provides interruption detection, not atomicity, rollback, repair, moves,
 * replacement, allocation or general writable VFS access.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_RENAME_COMMIT_H
#define UMICOM_KERNEL_FAT16_RENAME_COMMIT_H
#include "umicom/kernel/fat16_commit.h"
#include "umicom/kernel/fat16_rename.h"

/* directoryPlanned qualifies the entry, replacement alias and sector address.
 * Metadata counters include both FAT headers and the complete directory WRITE.
 * Data counters, offset and requested/confirmed/submitted bytes remain zero;
 * dataOutcome remains NOT_SUBMITTED and dataDurable/dataVerified remain false.
 * All durability/verification flags are historical observations. A late error
 * may follow clean publication and never grants permission to retry or repair. */
typedef struct UmicomKernelFat16RenameResult {
    UmicomKernelFat16CommitResult commit;
    UmicomKernelFat16Entry originalEntry;
    char updatedName[13];
    UmicomU64 directorySector;
    UmicomSize entryOffset;
    UmicomBoolean directoryPlanned;
    UmicomBoolean directorySubmitted;
    UmicomBoolean directoryCompleted;
    UmicomBoolean directoryDurable;
    UmicomBoolean directoryVerified;
} UmicomKernelFat16RenameResult;

/* Supply stable, initially zero-filled trusted storage and serialise calls.
 * The embedded committer, updater and workspaces are private implementation
 * storage; the outer caller must never invoke their public APIs directly. */
typedef struct UmicomKernelFat16RenameCommitter {
    const struct UmicomKernelFat16RenameCommitter *self;
    UmicomKernelFat16Committer commit;
    UmicomKernelFat16RenameWorkspace renameWorkspace;
    UmicomKernelFat16RenamePlan renamePlan;
    UmicomKernelFat16RenameResult lastResult;
    UmicomBoolean busy;
} UmicomKernelFat16RenameCommitter;

/* Acquire the qualified writable/FLUSH lease without mutation. Failed Open
 * may retain resources: retry only Close until their release succeeds. An
 * admitted owner is single-use, including after successful Finish and Close.
 * Never copy or reinitialise it. Retain exclusive medium ownership throughout
 * Open, Stage, Finish, Close and every idle interval between these calls. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16RenameOpen(
    UmicomKernelFat16RenameCommitter *owner, UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks);

/* Plan one same-parent rename using the exact PlanRename contract, then close
 * the inspector. Make FAT2 dirty and FLUSH, make FAT1 dirty and FLUSH, verify
 * both headers, then WRITE/FLUSH/verify the complete planned directory sector.
 * No file-data WRITE or data FLUSH phase exists. Success leaves STAGED and the
 * medium dirty, with three completed metadata writes and three FLUSHes.
 *
 * Path, newName and result extents must be independent and non-overflowing,
 * disjoint from the complete owner, domain and every DMA frame. Typed objects
 * must be aligned. Callbacks must not edit caller/owner storage or the medium,
 * retain pointers or re-enter active owners. Pre-admission refusals preserve
 * output and prior evidence. Read-only preflight refusal leaves READY; failure
 * after metadata submission leaves FAILED, closeable only. The total operation
 * read budget and monotonic deadline include preflight and verification. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16RenameStage(
    UmicomKernelFat16RenameCommitter *owner, const char *path, const char *newName,
    UmicomKernelFat16RenameResult *outResult);

/* Re-verify both dirty headers and the complete renamed directory sector.
 * Restore clean FAT2 then clean FAT1, each with its own FLUSH, and verify both
 * headers and the final deadline. Only accepted completion sets COMMITTED:
 * five total metadata writes and five FLUSHes, with no data-byte evidence.
 * Failure never rolls back the alias or re-dirties a possible clean commit. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16RenameFinish(
    UmicomKernelFat16RenameCommitter *owner, UmicomKernelFat16RenameResult *outResult);

/* Resource release only: no WRITE, FLUSH, Finish or flag repair. Cleanup
 * failure retains CLOSING state and backing resources for another Close.
 * Successful release scrubs plans while retaining historical result evidence.
 * Closing a zero-filled, never-admitted owner is harmless. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16RenameClose(
    UmicomKernelFat16RenameCommitter *owner);

void UmicomKernelFat16RenameValidate(void);
void UmicomKernelFat16RenameReadbackValidate(void);
void UmicomKernelFat16RenameInterruptedValidate(void);
void UmicomKernelFat16RenameRejectedValidate(void);
_Noreturn void UmicomKernelFat16RenameBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_RENAME_COMMIT_H */
