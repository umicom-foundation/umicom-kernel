/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/kernel.h
 *
 * PURPOSE:
 *   Declare the transition point from architecture bootstrap Assembly into
 *   portable freestanding C23 Umicom Kernel logic.
 *
 * EDUCATIONAL NOTE:
 *   The original K1 entry symbol was named `UmiKernelMain`.  K3 adopts the full
 *   source spelling `UmicomKernelMain` while preserving that existing symbol
 *   through a source alias.  This keeps historical K1/K2 binaries/source
 *   compatible while preventing new Kernel code from spreading the short name.
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

/* Historical K1/K2 C entry symbol called by boot.S. */
void UmiKernelMain(UmiU64 hartId, UmiAddress deviceTreeAddress);

/* Canonical full Umicom source spelling for all new Kernel implementation. */
#define UmicomKernelMain UmiKernelMain

#endif /* UMICOM_KERNEL_KERNEL_H */
