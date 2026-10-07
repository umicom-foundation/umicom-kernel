/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_rename.h
 *
 * Read-only preparation for a same-directory FAT16 short-name rename. A
 * checked regular file keeps its data, allocation, size, attributes and every
 * timestamp. Only its short alias and supported lowercase-display bits change.
 * The plan is a value snapshot, not authority to mutate an unowned medium.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_RENAME_H
#define UMICOM_KERNEL_FAT16_RENAME_H
#include "umicom/kernel/fat16_update_plan.h"

typedef struct UmicomKernelFat16RenamePlan {
    UmicomKernelFat16Entry originalEntry;
    char updatedName[13]; /* Canonical uppercase alias, including its NUL. */
    UmicomU64 directorySector;
    UmicomSize entryOffset;
    UmicomU8 original[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 data[UMICOM_DISK_SECTOR_BYTES];
} UmicomKernelFat16RenamePlan;

/* Stable, initially zero-filled scratch. Both this owner and the companion
 * allocation-proof workspace are scrubbed after admitted planning, retaining
 * only self. Neither workspace may be copied or concurrently reused. */
typedef struct UmicomKernelFat16RenameWorkspace {
    const struct UmicomKernelFat16RenameWorkspace *self;
    UmicomBoolean busy;
    char path[UMICOM_FAT16_PATH_BYTES];
    char newName[13];
    UmicomKernelFat16RenamePlan stage;
} UmicomKernelFat16RenameWorkspace;

/* Rename a checked empty or nonempty regular file within its existing parent.
 * path is an absolute short-name path. newName is one portable 8.3 component,
 * at most twelve bytes before NUL: no slash, traversal, spaces or trailing dot.
 * ASCII case is folded. Any existing casefold-equal sibling, including the
 * source itself, returns EXISTS. Root/directories return IS_DIRECTORY and a
 * READ_ONLY source is refused. There is no replacement, move or LFN support.
 *
 * The complete planned directory sector changes only entry bytes 0..10 and
 * clears the supported lowercase-display bits 0x18 in byte 12. Attributes,
 * ARCHIVE, size, first cluster, all calendar fields and all unrelated bytes
 * remain unchanged. No data sector or allocation link is changed or planned.
 * The original exact-chain and strict bounded whole-volume ownership/FAT proof
 * apply, including to empty files. The one total 4096-read budget is retained;
 * the reader additionally enforces its enclosing monotonic deadline.
 *
 * The volume, both workspaces, output and complete terminated input strings
 * must have stable, non-overflowing, pairwise disjoint extents. Typed objects
 * require natural alignment. Each string byte is checked against protected
 * owner/output storage before it is read. These are trusted Kernel pointers,
 * not a user-pointer probing ABI. Input strings are copied before the first
 * reader callback. Callbacks must not mutate these objects or the medium,
 * retain pointers, or re-enter active owners. Keep the reader/context and
 * exclusively owned backing medium valid throughout inspection.
 *
 * Every error leaves all output bytes, including padding, unchanged. Refusals
 * before admission also preserve prior workspace contents. Admitted exits
 * scrub scratch, retire the FAT cache and release the busy guards. The caller
 * must close the inspector before any separate transport owner mutates media. */
UmicomKernelDiskStatus UmicomKernelFat16PlanRename(UmicomKernelFat16 *volume,
    const char *path, const char *newName, UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16RenameWorkspace *renameWorkspace, UmicomKernelFat16RenamePlan *outPlan);

#endif /* UMICOM_KERNEL_FAT16_RENAME_H */
