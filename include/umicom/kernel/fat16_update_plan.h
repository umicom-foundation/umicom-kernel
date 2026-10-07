/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_update_plan.h
 *
 * Inspect an existing FAT16 allocation before a separate owner changes its
 * data. This API performs reads only. A plan is a value snapshot, not a lease
 * or authority to write a disk; its consumer must own the original transport
 * exclusively and keep the medium unchanged while planning. Close the original
 * inspector before issuing any WRITE, preserving its immutable-medium contract.
 *
 * The strict profile proves unique allocation across the whole bounded live
 * namespace, rejects orphan allocation and compares both complete FAT copies.
 * It does not repair a filesystem or change metadata, attributes or timestamps.
 * Read-only inspection retains its existing, less restrictive profile.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_UPDATE_PLAN_H
#define UMICOM_KERNEL_FAT16_UPDATE_PLAN_H
#include "umicom/kernel/fat16_inspector.h"

#define UMICOM_FAT16_UPDATE_BYTES 4096U
/* An unaligned 4096-byte input can touch nine complete 512-byte sectors. */
#define UMICOM_FAT16_UPDATE_SECTOR_LIMIT 9U
#define UMICOM_FAT16_UPDATE_BITMAP_BYTES 8192U
#define UMICOM_FAT16_UPDATE_DIRECTORY_LIMIT 64U
#define UMICOM_FAT16_UPDATE_OBJECT_LIMIT 256U
#define UMICOM_FAT16_UPDATE_FAT_SECTORS 256U

typedef struct UmicomKernelFat16UpdateSector {
    UmicomU64 sector; /* Absolute device LBA, never a caller-supplied address. */
    UmicomSize inputOffset;
    UmicomSize offset; /* First changed byte within this complete sector. */
    UmicomSize bytes; /* Caller bytes represented by this sector request. */
    UmicomU8 data[UMICOM_DISK_SECTOR_BYTES];
} UmicomKernelFat16UpdateSector;

typedef struct UmicomKernelFat16UpdatePlan {
    UmicomKernelFat16Entry entry;
    UmicomU64 offset;
    UmicomSize bytes;
    UmicomSize count;
    UmicomKernelFat16UpdateSector sectors[UMICOM_FAT16_UPDATE_SECTOR_LIMIT];
} UmicomKernelFat16UpdatePlan;

typedef struct UmicomKernelFat16UpdateDirectory {
    UmicomU16 firstCluster;
    UmicomU16 parentCluster;
    UmicomSize depth;
} UmicomKernelFat16UpdateDirectory;

/* Stable, initially zero-filled storage avoids a recursive directory walk or
 * large Kernel-stack arrays. The root counts towards both directory/object
 * limits. Fields are private scratch, exposed only for static allocation and
 * deliberate fault tests. Never copy a workspace after its first admitted use.
 * Scratch is scrubbed on completion; self remains for ownership validation. */
typedef struct UmicomKernelFat16UpdateWorkspace {
    const struct UmicomKernelFat16UpdateWorkspace *self;
    UmicomBoolean busy;
    UmicomU8 owned[UMICOM_FAT16_UPDATE_BITMAP_BYTES];
    UmicomKernelFat16UpdateDirectory directories[UMICOM_FAT16_UPDATE_DIRECTORY_LIMIT];
    UmicomSize directoryCount;
    UmicomSize nextDirectory;
    UmicomSize objects;
    UmicomU16 chain[UMICOM_FAT16_CHAIN_LIMIT];
    UmicomU16 targetChain[UMICOM_FAT16_CHAIN_LIMIT];
    UmicomSize targetClusters;
    UmicomU8 input[UMICOM_FAT16_UPDATE_BYTES];
    UmicomKernelFat16UpdatePlan stage;
} UmicomKernelFat16UpdateWorkspace;

/* Prepare one bounded, fixed-size data overwrite. bytes must be 1..4096 and
 * the complete range must already lie inside an existing regular file. A
 * read-only target returns READ_ONLY. Its archive attribute must already be
 * set because this operation preserves every metadata byte. Live long-name
 * records and bad-cluster markers are outside this strict update profile.
 *
 * All typed owners/results must be aligned. volume, workspace, outPlan, the
 * complete input span and the terminated path (including NUL) must be pairwise
 * disjoint, with non-overflowing address extents. These remain trusted Kernel
 * pointers, not a user-memory probing interface. The reader callback must not
 * edit these objects, re-enter either active owner or retain their pointers.
 *
 * Input is copied before the first media callback. Every original touched
 * sector is read before success; neighbour/slack bytes are retained unchanged
 * in the patched sector data. An error leaves outPlan completely unchanged.
 * No operation submits a WRITE or FLUSH, owns DMA, or releases the transport.
 * The planner has one total UMICOM_FAT16_IO_LIMIT read budget; the reader must
 * additionally enforce the enclosing monotonic deadline at each completion.
 */
UmicomKernelDiskStatus UmicomKernelFat16PlanUpdate(UmicomKernelFat16 *volume,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16UpdatePlan *outPlan);

#endif /* UMICOM_KERNEL_FAT16_UPDATE_PLAN_H */
