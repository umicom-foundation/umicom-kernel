/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/boot_reservations.c
 *
 * PURPOSE:
 *   Preflight and merge page reservations so firmware-owned RAM cannot enter the free-frame pool.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_reservations.h"
#include "umicom/kernel/address.h"
#include "umicom/kernel/physical_memory.h"

static UmicomBoolean Add(UmicomKernelBootReservationPlan *plan, UmicomAddress ramBase,
    UmicomAddress ramEnd, UmicomU64 address, UmicomU64 bytes)
{
    if (!bytes || address > ~(UmicomU64)0U - bytes) return UMICOM_FALSE;
    UmicomU64 end = address + bytes;
    if (end <= ramBase || address >= ramEnd) return UMICOM_TRUE;
    if (address < ramBase) address = ramBase;
    if (end > ramEnd) end = ramEnd;
    const UmicomU64 mask = (UmicomU64)UMICOM_KERNEL_PAGE_SIZE - 1U;
    address &= ~mask;
    /* RAM end is page aligned, so rounding an interior end cannot leave RAM.
     * Subtract-before-add avoids wrap at the top of the address space. */
    if (end & mask) {
        UmicomU64 extra = (UmicomU64)UMICOM_KERNEL_PAGE_SIZE - (end & mask);
        if (extra > (UmicomU64)ramEnd - end) return UMICOM_FALSE;
        end += extra;
    }
    if (plan->count == UMICOM_KERNEL_BOOT_RESERVATION_LIMIT) return UMICOM_FALSE;
    plan->ranges[plan->count++] =
        (UmicomKernelBootReservedRange){(UmicomAddress)address, (UmicomSize)(end - address)};
    return UMICOM_TRUE;
}
UmicomBoolean UmicomKernelBootReservationsPlan(UmicomAddress ramBase, UmicomSize ramBytes,
    UmicomAddress kernelEnd, UmicomAddress treeAddress,
    const UmicomKernelDeviceTreeLayout *tree, UmicomKernelBootReservationPlan *out)
{
    UmicomAddress ramEnd = 0, treeEnd = 0;
    const UmicomAddress mask = (UmicomAddress)UMICOM_KERNEL_PAGE_SIZE - 1U;
    if (!tree || !out || !ramBytes || (ramBase & mask) || (ramBytes & mask) ||
        !UmicomKernelAddressAddChecked(ramBase, ramBytes, &ramEnd) ||
        kernelEnd <= ramBase || kernelEnd > ramEnd || (kernelEnd & mask) ||
        treeAddress < kernelEnd || (treeAddress & 7U) ||
        tree->totalBytes < 40U || tree->totalBytes > UMICOM_KERNEL_FDT_LAYOUT_MAX_BYTES ||
        !UmicomKernelAddressAddChecked(treeAddress, tree->totalBytes, &treeEnd) ||
        treeEnd > ramEnd || tree->reservationCount > UMICOM_KERNEL_FDT_RESERVATION_LIMIT)
        return UMICOM_FALSE;
    UmicomKernelBootReservationPlan staged = {0};
    if (!Add(&staged, ramBase, ramEnd, ramBase, kernelEnd - ramBase) ||
        !Add(&staged, ramBase, ramEnd, treeAddress, tree->totalBytes))
        return UMICOM_FALSE;
    for (UmicomSize i = 0; i < tree->reservationCount; ++i) {
        const UmicomKernelDeviceTreeReservation *r = &tree->reservations[i];
        if (!Add(&staged, ramBase, ramEnd, r->address, r->bytes)) return UMICOM_FALSE;
    }
    /* Sort bounded local storage, then collapse touching pages. Byte-disjoint
     * firmware entries may share a physical page after outward rounding. */
    for (UmicomSize i = 1; i < staged.count; ++i) {
        UmicomKernelBootReservedRange value = staged.ranges[i];
        UmicomSize at = i;
        while (at && staged.ranges[at - 1U].base > value.base) {
            staged.ranges[at] = staged.ranges[at - 1U]; --at;
        }
        staged.ranges[at] = value;
    }
    UmicomSize count = 0;
    for (UmicomSize i = 0; i < staged.count; ++i) {
        UmicomKernelBootReservedRange next = staged.ranges[i];
        if (count) {
            UmicomKernelBootReservedRange *previous = &staged.ranges[count - 1U];
            UmicomAddress previousEnd = previous->base + previous->bytes;
            if (next.base <= previousEnd) {
                UmicomAddress nextEnd = next.base + next.bytes;
                if (nextEnd > previousEnd) previous->bytes = nextEnd - previous->base;
                continue;
            }
        }
        staged.ranges[count++] = next;
    }
    for (UmicomSize i = count; i < staged.count; ++i)
        staged.ranges[i] = (UmicomKernelBootReservedRange){0, 0};
    staged.count = count;
    *out = staged;
    return UMICOM_TRUE;
}
