/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_space.h
 *
 * PURPOSE:
 *   Report bounded FAT16 allocation counts without granting write authority.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_SPACE_H
#define UMICOM_KERNEL_FAT16_SPACE_H
#include "umicom/kernel/fat16_inspector.h"

/* Counts describe data clusters, excluding partition/BPB/FAT/root metadata and
 * incomplete trailing sectors. Allocated includes any valid next-link or end
 * marker; no directory traversal proves who owns those clusters. */
typedef struct UmicomKernelFat16Space {
    UmicomU64 clusterBytes;
    UmicomU64 dataBytes;
    UmicomU64 freeBytes;
    UmicomU64 allocatedBytes;
    UmicomU64 badBytes;
    UmicomU32 totalClusters;
    UmicomU32 freeClusters;
    UmicomU32 allocatedClusters;
    UmicomU32 badClusters;
} UmicomKernelFat16Space;

/* Inspect every sector of both FAT copies, including padding comparison.
 * This bounded profile accepts at most 256 sectors per FAT (512 reads).
 * Bad-cluster markers are counted; reserved/out-of-range links are rejected.
 * Clean matching tables do not prove chain ownership or application integrity.
 *
 * Keep the medium immutable through the inspector lifetime and serialise
 * calls. Both naturally aligned objects must occupy valid, non-overflowing,
 * disjoint trusted Kernel storage; output must also be independent of reader
 * context/storage. This is not a user-memory probing interface.
 * Output, including padding, stays unchanged on any error. No allocation,
 * device write, repair, reservation or capacity guarantee is performed. */
UmicomKernelDiskStatus UmicomKernelFat16SpaceRead(
    UmicomKernelFat16 *volume, UmicomKernelFat16Space *output);
#endif
