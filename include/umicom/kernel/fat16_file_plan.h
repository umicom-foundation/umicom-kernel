/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_file_plan.h
 *
 * Read-only preparation for an existing file's data and write metadata. The
 * data plan retains the established bounded allocation proof. A separate
 * directory-sector plan preserves the complete original sector and changes
 * only the selected entry's ARCHIVE bit, write time and write date.
 *
 * A plan is a checked value snapshot, not disk authority. Its consumer must
 * keep the medium exclusively owned and unchanged during inspection, close
 * the inspector before mutation, and order data and metadata persistence.
 * No callback in this interface writes, flushes, resets or releases storage.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_FILE_PLAN_H
#define UMICOM_KERNEL_FAT16_FILE_PLAN_H
#include "umicom/kernel/fat16_update_plan.h"

/* Explicit calendar fields supplied by the caller. No RTC, host timezone or
 * monotonic counter is interpreted as a calendar. FAT does not encode a zone;
 * the caller chooses the calendar convention consistently for this medium. */
typedef struct UmicomKernelFat16FileTime {
    UmicomU16 year;
    UmicomU16 month;
    UmicomU16 day;
    UmicomU16 hour;
    UmicomU16 minute;
    UmicomU16 second;
} UmicomKernelFat16FileTime;

typedef struct UmicomKernelFat16FileTimeEncoding {
    UmicomU16 writeTime;
    UmicomU16 writeDate;
    UmicomU8 storedSecond; /* 0, 2, ... 58: floor(requested second / 2) * 2. */
} UmicomKernelFat16FileTimeEncoding;

/* A metadata-only companion to UmicomKernelFat16UpdatePlan. Keeping these
 * values separate lets a transport owner reuse its existing data workspace
 * and data plan without duplicating their large, statically allocated arrays.
 * entryOffset names one complete 32-byte entry inside the 512-byte sector. */
typedef struct UmicomKernelFat16FileUpdatePlan {
    UmicomU64 directorySector;
    UmicomSize entryOffset;
    UmicomKernelFat16FileTime requestedTime;
    UmicomKernelFat16FileTimeEncoding encodedTime;
    UmicomU8 original[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 data[UMICOM_DISK_SECTOR_BYTES];
} UmicomKernelFat16FileUpdatePlan;

/* Stable, initially zero-filled scratch. The companion base workspace follows
 * its established self/busy contract. Both workspaces are scrubbed after each
 * admitted operation, keeping only self; neither may be copied after use. */
typedef struct UmicomKernelFat16FileUpdateWorkspace {
    const struct UmicomKernelFat16FileUpdateWorkspace *self;
    UmicomBoolean busy;
    char path[UMICOM_FAT16_PATH_BYTES];
    UmicomKernelFat16FileUpdatePlan stage;
} UmicomKernelFat16FileUpdateWorkspace;

/* Validate Gregorian fields for 1980..2107 inclusive, including the century
 * leap-year rule. Invalid fields or storage return INVALID_ARGUMENT and leave
 * outEncoded untouched. The aligned, non-overflowing input and output objects
 * must be independent. This helper performs no callbacks or allocation. */
UmicomKernelDiskStatus UmicomKernelFat16FileTimeEncode(
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16FileTimeEncoding *outEncoded);

/* Plan one 1..4096-byte overwrite wholly inside an existing regular file.
 * READ_ONLY is refused; ARCHIVE may initially be clear or set and will be set
 * in the staged directory entry. Size, allocation, name, creation time, access
 * date and every other byte of the complete directory sector remain intact.
 * The existing data-only PlanUpdate interface keeps its original ARCHIVE rule.
 *
 * All eight storage objects (volume, workspace, fileWorkspace, outDataPlan,
 * outFilePlan, time, input, and terminated path) must have non-overflowing,
 * pairwise disjoint extents. Typed objects must also have their type alignment.
 * These are trusted Kernel pointers, not a user-pointer probing interface.
 * Reader callbacks must not edit this storage, re-enter either active owner,
 * mutate the medium or retain pointers. Input, path and timestamp are staged
 * before the first callback. Both output plans are published only on success;
 * every error leaves both outputs unchanged, including their padding bytes.
 *
 * Root and fragmented subdirectory locations are derived from the checked
 * parser and FAT chain, never supplied by the caller. The strict whole-volume
 * ownership/FAT proof, long-name refusal and one total 4096-read budget apply.
 * The reader must additionally enforce its enclosing monotonic deadline. */
UmicomKernelDiskStatus UmicomKernelFat16PlanFileUpdate(UmicomKernelFat16 *volume,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16FileUpdateWorkspace *fileWorkspace,
    UmicomKernelFat16UpdatePlan *outDataPlan, UmicomKernelFat16FileUpdatePlan *outFilePlan);

#endif /* UMICOM_KERNEL_FAT16_FILE_PLAN_H */
