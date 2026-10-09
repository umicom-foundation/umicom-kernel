/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_lifecycle.h
 *
 * Bounded, read-only preparation for regular-file creation, allocating append,
 * truncation and deletion. Plans contain complete sector value snapshots, not
 * transport authority. A separate exclusive owner orders their persistence.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_LIFECYCLE_H
#define UMICOM_KERNEL_FAT16_LIFECYCLE_H
#include "umicom/kernel/fat16_file_plan.h"

#define UMICOM_FAT16_LIFECYCLE_DATA_SECTORS 72U
#define UMICOM_FAT16_LIFECYCLE_NEW_CLUSTERS 8U
#define UMICOM_FAT16_LIFECYCLE_FAT_SECTORS 32U
/* A move can touch destination, end marker, source and dot-parent sectors.
 * The earlier bound is retained to explain the additional plan storage. */
#if 0
#define UMICOM_FAT16_LIFECYCLE_DIRECTORY_SECTORS 2U
#endif
#define UMICOM_FAT16_LIFECYCLE_DIRECTORY_SECTORS 4U

typedef enum UmicomKernelFat16LifecycleOperation {
    UMICOM_FAT16_LIFECYCLE_NONE,
    UMICOM_FAT16_LIFECYCLE_CREATE,
    UMICOM_FAT16_LIFECYCLE_APPEND,
    UMICOM_FAT16_LIFECYCLE_TRUNCATE,
    UMICOM_FAT16_LIFECYCLE_DELETE,
    /* Directory operations share the same exclusive owner and explicit finish. */
    UMICOM_FAT16_LIFECYCLE_CREATE_DIRECTORY,
    UMICOM_FAT16_LIFECYCLE_REMOVE_DIRECTORY,
    UMICOM_FAT16_LIFECYCLE_MOVE,
    /* Appended so the existing operation values remain stable. */
    UMICOM_FAT16_LIFECYCLE_WRITE
} UmicomKernelFat16LifecycleOperation;

typedef struct UmicomKernelFat16LifecycleRequest {
    UmicomKernelFat16LifecycleOperation operation;
    const char *path;
    const void *input;
    UmicomSize bytes;
    UmicomU32 size;
    UmicomKernelFat16FileTime time;
    /* MOVE alone reads this absolute destination. Other operations ignore it. */
    const char *destination;
    /* WRITE alone reads this byte offset. Older operations ignore it. */
    UmicomU64 offset;
} UmicomKernelFat16LifecycleRequest;

typedef struct UmicomKernelFat16LifecycleFatSector {
    UmicomU64 primarySector;
    UmicomU64 mirrorSector;
    UmicomBoolean changed;
    UmicomU8 original[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 data[UMICOM_DISK_SECTOR_BYTES]; /* Final CLEAN image, including links. */
} UmicomKernelFat16LifecycleFatSector;

typedef struct UmicomKernelFat16LifecycleDirectorySector {
    UmicomU64 sector;
    UmicomU8 original[UMICOM_DISK_SECTOR_BYTES];
    UmicomU8 data[UMICOM_DISK_SECTOR_BYTES];
} UmicomKernelFat16LifecycleDirectorySector;

typedef struct UmicomKernelFat16LifecyclePlan {
    UmicomKernelFat16LifecycleOperation operation;
    UmicomBoolean originalEntryPresent;
    UmicomBoolean updatedEntryPresent;
    UmicomKernelFat16Entry originalEntry;
    UmicomKernelFat16Entry updatedEntry;
    UmicomKernelFat16FileTime requestedTime;
    UmicomKernelFat16FileTimeEncoding encodedTime;
    UmicomU64 offset;
    UmicomSize requestedBytes;
    UmicomSize allocatedClusters;
    UmicomSize freedClusters;
    UmicomBoolean chainBoundaryPresent;
    UmicomSize chainBoundaryFatIndex;
    UmicomSize dataSectorCount;
    UmicomKernelFat16UpdateSector dataSectors[UMICOM_FAT16_LIFECYCLE_DATA_SECTORS];
    UmicomSize fatSectorCount;
    UmicomKernelFat16LifecycleFatSector fatSectors[UMICOM_FAT16_LIFECYCLE_FAT_SECTORS];
    UmicomSize directorySectorCount;
    UmicomKernelFat16LifecycleDirectorySector directorySectors[UMICOM_FAT16_LIFECYCLE_DIRECTORY_SECTORS];
    UmicomSize entryOffset; /* Target entry in directorySectors[0]. */
    /* A full allocated parent may acquire one initialized cluster. Its first
     * data image also contains the new entry, before the old tail is linked.
     * These values identify the sole permitted data/directory image overlap. */
    UmicomBoolean parentGrown;
    UmicomU16 parentAddedCluster;
    UmicomU16 parentTailCluster;
} UmicomKernelFat16LifecyclePlan;

/* Stable zero-filled scratch, scrubbed after every admitted call except self.
 * Neither this owner nor the companion proof workspace may be copied or used
 * concurrently. newChain holds only newly selected free cluster identifiers. */
typedef struct UmicomKernelFat16LifecycleWorkspace {
    const struct UmicomKernelFat16LifecycleWorkspace *self;
    UmicomBoolean busy;
    char path[UMICOM_FAT16_PATH_BYTES];
    char destination[UMICOM_FAT16_PATH_BYTES];
    UmicomU16 newChain[UMICOM_FAT16_LIFECYCLE_NEW_CLUSTERS];
    UmicomKernelFat16LifecyclePlan stage;
} UmicomKernelFat16LifecycleWorkspace;

/* MOVE renames a file or directory, or transfers it to another existing parent.
 * It preserves allocation, payload, attributes and calendar fields. Only the
 * alias/display bits, old entry tombstone and moved directory's dot-parent
 * reference change. No replacement, parent allocation or implicit time occurs.
 * A full destination returns NO_SPACE. Same-parent rename reuses its entry.
 * Root moves, read-only source/parents, collisions, cycles and uninspectable
 * resulting depth are refused. Both independent paths are copied before I/O.
 * input, bytes, size and every time field must be zero for MOVE.
 * Four distinct metadata sectors suffice; no data/FAT allocation link changes.
 * Publication remains interruption-detecting Stage/Finish, never a journal or
 * promise of power-loss atomicity. The original two-sector statement below
 * still describes the earlier allocation operations. */

/* Directory lifecycle extension: CREATE_DIRECTORY has no input or size and
 * requires an explicit calendar. REMOVE_DIRECTORY has no input, size or time;
 * it refuses the root, regular files and directories containing live children.
 * New directories have canonical dot entries and one fully initialized cluster.
 * CREATE and CREATE_DIRECTORY can extend an allocated parent by one cluster,
 * subject to all existing bounds. The FAT16 fixed root never grows.
 * The regular-file description below records the original narrower contract;
 * its no-extension statement is superseded by this documented extension. */

/* WRITE supplies an explicit position for 1..4096 payload bytes and requires
 * size zero and a valid calendar. offset may be anywhere from zero through the
 * existing EOF; a position beyond EOF is refused rather than creating a hole.
 * The checked offset + bytes must fit FAT16's 32-bit file size. A write keeps
 * the larger of the original size and that end position, and can initialise up
 * to eight additional clusters through the same bounded allocation proof.
 * Existing bytes and allocation slack outside the written span are preserved;
 * every newly allocated cluster is zeroed before overlaying caller payload.
 * WRITE sets ARCHIVE and the supplied write calendar, preserving creation and
 * access fields. Its offset, payload and calendar are snapshotted before I/O.
 * Stage orders dirty guards, complete data images, new links and metadata;
 * Finish alone accepts the change. This is not power-loss atomicity: a failed
 * write may have changed existing file bytes, and release performs no rollback.
 * destination is ignored for WRITE. All earlier operations ignore offset.
 * The regular-file description below remains the original narrower contract. */

/* CREATE accepts 0..4096 initial bytes, input NULL exactly when bytes is zero,
 * size zero and a valid explicit calendar. APPEND accepts 1..4096 bytes, size
 * zero and a valid calendar; EOF is implicit and empty files may acquire their
 * first allocation. TRUNCATE requires input NULL, bytes zero, a valid calendar
 * and size <= old size, including equal size and zero. DELETE requires input
 * NULL, bytes/size zero and all calendar fields zero. NONE is never admitted.
 *
 * Paths use the existing absolute portable short-name grammar. CREATE refuses
 * every existing sibling with EXISTS and uses an existing deleted/end-marker
 * slot, without extending directories. A READ_ONLY parent refuses CREATE;
 * the fixed root has no directory-entry attributes. New entries have ARCHIVE,
 * supplied creation/write calendar and access date, and zero creation tenths.
 * APPEND and TRUNCATE set ARCHIVE/write calendar while preserving other
 * metadata. DELETE
 * changes only directory byte zero to E5 and frees the chain. Root, directory
 * and READ_ONLY targets are refused. There is no replacement or sparse growth.
 *
 * Exact chains, the complete bounded namespace and both complete FAT copies
 * are proved before planning. CREATE also checks the resulting object/parent
 * entry limits. Free clusters are selected by increasing identifier; NO_SPACE
 * denotes no free cluster or directory slot, LIMIT denotes a profile bound.
 * Up to eight clusters, 72 complete data sectors, 32 distinct FAT sectors
 * including header sector zero, and two directory sectors are planned. The
 * existing 256-cluster chain and one total 4096-read bounds remain in force.
 * Every newly allocated cluster is fully zero initialised, then payload is
 * overlaid. Existing storage outside an appended span and truncated/freed data
 * remain unchanged; this is not secure erasure. Data sectors with bytes zero
 * are initialisation only; payload counters sum bytes, not whole-sector sizes.
 *
 * fatSectors[0] always contains the original and final CLEAN header sector.
 * changed identifies link-byte changes, not dirty/clean guard transitions.
 * chainBoundaryFatIndex identifies the old append tail to publish last, or
 * the retained truncate tail to publish first, within each FAT copy's phase.
 * A consumer must derive dirty guards separately and finally publish this
 * final image, never restore old allocation words from the original header.
 * directorySectors[0] contains the target; a second preserves the next end
 * marker across a physical/fragmented directory-sector boundary when needed.
 *
 * volume, request, both workspaces, outPlan, complete input and terminated
 * path require non-overflowing pairwise disjoint extents; typed storage must
 * have natural alignment. Each path byte is checked against protected storage
 * before reading. These are trusted Kernel pointers, not user-memory probes.
 * Inputs are snapshotted before the first reader callback. Callbacks must not
 * mutate these objects/the medium, retain pointers or re-enter active owners.
 * Every error leaves every output byte unchanged, including padding. Refusals
 * before admission preserve scratch. Admitted exits scrub scratch and retire
 * the FAT cache. The reader must enforce its enclosing monotonic deadline.
 * Close this immutable-medium inspector before a separate owner writes. */
UmicomKernelDiskStatus UmicomKernelFat16PlanLifecycle(UmicomKernelFat16 *volume,
    const UmicomKernelFat16LifecycleRequest *request,
    UmicomKernelFat16UpdateWorkspace *workspace,
    UmicomKernelFat16LifecycleWorkspace *lifecycleWorkspace,
    UmicomKernelFat16LifecyclePlan *outPlan);

#endif /* UMICOM_KERNEL_FAT16_LIFECYCLE_H */
