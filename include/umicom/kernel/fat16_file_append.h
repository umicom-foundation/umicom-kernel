/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_file_append.h
 *
 * Bounded append using only the unused bytes of an existing final cluster.
 * This companion reuses the established file-update plan and commit records
 * under the append-specific contract below. The original PlanFileUpdate and
 * FileCommitStage interfaces remain fixed-size overwrite operations.
 *
 * No cluster is allocated or released, and no FAT chain, name or first-cluster
 * field is changed. Ordered dirty/data/directory/clean persistence detects
 * interruption; it does not provide atomicity, rollback, journalling, repair,
 * file creation, empty-file allocation or general writable VFS access.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_FILE_APPEND_H
#define UMICOM_KERNEL_FAT16_FILE_APPEND_H
#include "umicom/kernel/fat16_file_commit.h"

/* Prepare one 1..4096-byte append at the implicit current EOF of a checked,
 * nonempty regular file. The enlarged size must fit in its already allocated
 * final cluster. A valid empty file, a file ending at a cluster boundary, a
 * U32 size overflow or a request exceeding that cluster's slack returns RANGE.
 * Root/directories return IS_DIRECTORY; READ_ONLY attributes are refused.
 * Zero or oversized input and invalid calendar fields return INVALID_ARGUMENT.
 * Existing path, geometry, chain and strict whole-volume ownership failures
 * retain their established statuses. No medium mutation occurs in planning.
 *
 * outDataPlan.entry retains the original entry and original file size;
 * outDataPlan.offset is its original EOF and outDataPlan.bytes is the appended
 * count. Its complete sectors preserve every byte outside the append span.
 * outFilePlan.original retains the entire original directory sector. In its
 * data, only ARCHIVE, write time/date and the selected entry's four file-size
 * bytes may change. The planned new size is offset + bytes. Creation time,
 * access date, first cluster and every other directory-sector byte are intact.
 *
 * Supply stable, initially zero-filled workspaces with their existing self
 * and busy contracts. The volume, both workspaces, both outputs, input, time
 * and terminated path must have non-overflowing, pairwise disjoint extents;
 * typed objects must be aligned. These are trusted Kernel pointers. Callbacks
 * must not edit caller/owner storage, re-enter active owners, retain pointers
 * or mutate the exclusively owned medium. Path, payload and calendar fields
 * are copied before the first callback. Both outputs are published only on
 * complete success; every refusal leaves all output bytes, including padding,
 * unchanged. Admitted exits scrub both workspaces, retaining only self.
 *
 * The checked parser derives root and fragmented-parent directory locations.
 * The existing exact-chain and whole-volume allocation proof, long-name
 * refusal and one total 4096-read planning budget apply. Calendar dates must
 * be valid Gregorian fields in 1980..2107; seconds are floored to FAT's two-
 * second precision. The reader enforces its enclosing monotonic deadline.
 * A plan remains a snapshot, not authority to mutate an unowned medium. */
UmicomKernelDiskStatus UmicomKernelFat16PlanFileAppend(
    UmicomKernelFat16 *volume, const char *path, const void *input,
    UmicomSize bytes, const UmicomKernelFat16FileTime *time,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16FileUpdateWorkspace *fileWorkspace,
    UmicomKernelFat16UpdatePlan *outDataPlan,
    UmicomKernelFat16FileUpdatePlan *outFilePlan);

/* Use the existing FileCommitOpen owner for one bounded append. This shares
 * the established dirty FAT2/FLUSH, dirty FAT1/FLUSH, data WRITE/FLUSH/verify,
 * directory WRITE/FLUSH/verify protocol and enclosing deadline. Success leaves
 * the owner STAGED and the medium dirty; explicit FileCommitFinish performs
 * re-verification and clean publication. FileCommitClose only releases owned
 * resources. Retain exclusive medium ownership through these calls and their
 * idle intervals. An admitted owner remains single-use and must not be copied,
 * reinitialised, or accessed through its private embedded owners/workspaces.
 *
 * The append contract qualifies the reused FileCommitResult: directoryPlanned
 * also qualifies commit.offset as the original EOF; commit.requestedBytes is
 * the append count, and their sum is the planned enlarged size. The directory
 * WRITE may change nine entry bytes: ARCHIVE, write time/date and size. The
 * original fixed-size Stage operation still changes at most five entry bytes.
 * Historical durability/verification flags do not grant retry or repair.
 *
 * Path, time, input and result follow Stage's independent, aligned, disjoint
 * storage and callback rules. Pre-admission refusals preserve prior evidence
 * and output. A read-only preflight refusal leaves READY; failure after any
 * metadata submission leaves FAILED, closeable only. All planner bounds and
 * the complete Stage deadline apply; no RTC or timezone is inferred. */
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitAppend(
    UmicomKernelFat16FileCommitter *owner, const char *path, const void *input,
    UmicomSize bytes, const UmicomKernelFat16FileTime *time,
    UmicomKernelFat16FileCommitResult *outResult);

void UmicomKernelFat16AppendValidate(void);
void UmicomKernelFat16AppendReadbackValidate(void);
void UmicomKernelFat16AppendInterruptedValidate(void);
void UmicomKernelFat16AppendRejectedValidate(void);
_Noreturn void UmicomKernelFat16AppendBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_FILE_APPEND_H */
