/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/kernel.h
 *
 * PURPOSE:
 *   Declare the transition point from architecture bootstrap Assembly into
 *   portable freestanding C23 kernel logic.
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

/* Import the explicit RV64 value/address types used by the entry contract. */
#include "umicom/kernel/types.h"

/* Enter portable K1 C code.
 *
 * hartId:
 *   RISC-V hardware-thread identifier selected by boot.S.
 *
 * deviceTreeAddress:
 *   QEMU-provided device-tree blob address.  K1 reports but does not parse it.
 */
void UmiKernelMain(UmiU64 hartId, UmiAddress deviceTreeAddress);

#endif /* UMICOM_KERNEL_KERNEL_H */
