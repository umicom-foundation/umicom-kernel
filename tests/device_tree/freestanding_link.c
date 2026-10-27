/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/device_tree/freestanding_link.c
 *
 * PURPOSE:
 *   Keep boot reservation and device-tree operations reachable in a no-runtime link regression.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_reservations.h"
#include "umicom/kernel/device_tree_layout.h"

/* Link-only image: never launch this file as firmware. External arguments keep
 * the real APIs reachable at both optimisation levels, including error paths.
 * The target deliberately links without libc, startup objects or section GC. */
UmicomBoolean UmicomBootMemoryLinkCheck(const void *blob, UmicomSize bytes,
    UmicomAddress ram, UmicomSize ramBytes, UmicomAddress kernelEnd,
    UmicomAddress treeAddress, UmicomKernelDeviceTreeLayout *layout,
    UmicomKernelBootReservationPlan *plan);
UmicomBoolean UmicomBootMemoryLinkCheck(const void *blob, UmicomSize bytes,
    UmicomAddress ram, UmicomSize ramBytes, UmicomAddress kernelEnd,
    UmicomAddress treeAddress, UmicomKernelDeviceTreeLayout *layout,
    UmicomKernelBootReservationPlan *plan)
{
    if (UmicomKernelDeviceTreeLayoutRead(blob, bytes, layout) != UMICOM_FDT_LAYOUT_OK)
        return UMICOM_FALSE;
    return UmicomKernelBootReservationsPlan(ram, ramBytes, kernelEnd, treeAddress, layout, plan);
}
