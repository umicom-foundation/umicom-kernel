/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/boot_reservations.h
 *
 * PURPOSE:
 *   Plan the union of Kernel, device-tree and firmware-reserved physical pages before allocator initialization.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_BOOT_RESERVATIONS_H
#define UMICOM_KERNEL_BOOT_RESERVATIONS_H
#include "umicom/kernel/device_tree_layout.h"
#define UMICOM_KERNEL_BOOT_RESERVATION_LIMIT (UMICOM_KERNEL_FDT_RESERVATION_LIMIT + 2U)
typedef struct UmicomKernelBootReservedRange {
    UmicomAddress base;
    UmicomSize bytes;
} UmicomKernelBootReservedRange;
typedef struct UmicomKernelBootReservationPlan {
    UmicomSize count;
    UmicomKernelBootReservedRange ranges[UMICOM_KERNEL_BOOT_RESERVATION_LIMIT];
} UmicomKernelBootReservationPlan;
/* Single-hart boot preflight. Clip firmware reservations to declared RAM,
 * round outward to pages and merge overlaps before asking the allocator to
 * reserve anything. Failure preserves out; no allocator or hardware is used. */
UmicomBoolean UmicomKernelBootReservationsPlan(UmicomAddress ramBase, UmicomSize ramBytes,
    UmicomAddress kernelEnd, UmicomAddress treeAddress,
    const UmicomKernelDeviceTreeLayout *tree, UmicomKernelBootReservationPlan *out);
#endif
