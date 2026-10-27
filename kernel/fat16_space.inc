/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_space.inc
 *
 * PURPOSE:
 *   Count allocation entries using the existing inspector ownership and read budget.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_space.h"

UmicomKernelDiskStatus UmicomKernelFat16SpaceRead(
    UmicomKernelFat16 *volume, UmicomKernelFat16Space *output)
{
    if (!UmicomFatPlanSpan(volume, sizeof(*volume)) ||
        !UmicomFatPlanSpan(output, sizeof(*output)) ||
        (UmicomAddress)volume % alignof(UmicomKernelFat16) ||
        (UmicomAddress)output % alignof(UmicomKernelFat16Space))
        return UMICOM_DISK_INVALID_ARGUMENT;
    if (UmicomFatPlanOverlap(volume, sizeof(*volume), output, sizeof(*output)))
        return UMICOM_DISK_INVALID_ARGUMENT;
    UmicomKernelDiskStatus status = UmicomFatBegin(volume);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatPlanGeometry(volume);
    UmicomKernelFat16Space staged;
    UmicomFatClear(&staged, sizeof(staged));
    if (status == UMICOM_DISK_OK) {
        staged.totalClusters = volume->info.clusters;
        staged.clusterBytes = (UmicomU64)volume->info.sectorsPerCluster * UMICOM_DISK_SECTOR_BYTES;
    }
    /* Stream the complete mirrored tables once instead of rereading one sector
     * per cluster. Counters remain local until every read and link is accepted. */
    for (UmicomU64 sector = 0U; status == UMICOM_DISK_OK &&
            sector < volume->info.sectorsPerFat; ++sector) {
        status = UmicomFatReadSector(volume, volume->fatStart + sector, volume->fatSector);
        if (status != UMICOM_DISK_OK) break;
        status = UmicomFatReadSector(volume,
            volume->fatStart + volume->info.sectorsPerFat + sector, volume->mirrorSector);
        if (status != UMICOM_DISK_OK) break;
        if (!UmicomFatEqual(volume->fatSector, volume->mirrorSector, UMICOM_DISK_SECTOR_BYTES)) {
            status = UMICOM_DISK_FAT_MISMATCH;
            break;
        }
        if (sector == 0U) {
            const UmicomU16 media = UmicomFat16Word(volume->fatSector);
            const UmicomU16 flags = UmicomFat16Word(volume->fatSector + 2U);
            if ((media & 0xff00U) != 0xff00U ||
                ((media & 0xffU) != 0xf0U && (media & 0xffU) < 0xf8U) ||
                (flags & 0x3fffU) != 0x3fffU) status = UMICOM_DISK_CORRUPT;
            else if ((flags & 0xc000U) != 0xc000U) status = UMICOM_DISK_DIRTY;
        }
        for (UmicomU64 entry = 0U; status == UMICOM_DISK_OK && entry < 256U; ++entry) {
            const UmicomU64 cluster = sector * 256U + entry;
            if (cluster < 2U || cluster > (UmicomU64)staged.totalClusters + 1U) continue;
            const UmicomU16 value = UmicomFat16Word(volume->fatSector + entry * 2U);
            if (value == 0U) ++staged.freeClusters;
            else if (value == 0xfff7U) ++staged.badClusters;
            else if (value >= 0xfff8U ||
                    (value >= 2U && value <= staged.totalClusters + 1U))
                ++staged.allocatedClusters;
            else status = UMICOM_DISK_CORRUPT;
        }
    }
    if (status == UMICOM_DISK_OK) {
        staged.dataBytes = staged.totalClusters * staged.clusterBytes;
        staged.freeBytes = staged.freeClusters * staged.clusterBytes;
        staged.allocatedBytes = staged.allocatedClusters * staged.clusterBytes;
        staged.badBytes = staged.badClusters * staged.clusterBytes;
        UmicomFatCopy(output, &staged, sizeof(staged));
    }
    /* The scan borrows the cache buffers; no earlier cached-sector identity
     * may survive it, including when an error interrupts the scan. */
    volume->fatCached = UMICOM_FALSE;
    UmicomFatClear(&staged, sizeof(staged));
    return UmicomFatFinish(volume, status);
}
