/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/kernel.h
 *
 * PURPOSE:
 *   Publish the architecture entry into portable C23 kernel code. The first
 *   milestone receives the boot hart and firmware-provided device-tree address
 *   but does not parse the device tree yet.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_KERNEL_H
#define UMICOM_KERNEL_KERNEL_H

#include "umicom/kernel/types.h"

void UmiKernelMain(UmiU64 hartId, UmiAddress deviceTreeAddress);

#endif
