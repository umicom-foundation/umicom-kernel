/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/kernel.h
 *
 * PURPOSE:
 *   Declare the transition point from architecture bootstrap Assembly into
 *   portable freestanding C23 Umicom Kernel logic.
 *
 * EDUCATIONAL NOTE:
 *   The architecture bootstrap enters this function only after it has created
 *   a valid stack and cleared the BSS region required by the C language.  The
 *   entry contract intentionally carries only the information already supplied
 *   by the RISC-V boot environment: the executing hart and the device-tree
 *   address.
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

/* Enter architecture-neutral Kernel C23 startup logic. */
void UmicomKernelMain(UmicomU64 hartId, UmicomAddress deviceTreeAddress);

/*-----------------------------------------------------------------------------
 * HISTORICAL ENTRY-POINT SPELLING — RETAINED FOR REVIEW, NOT COMPILED
 *
 * The first experimental entry point used the shorter name below.  The active
 * bootstrap and C source now call `UmicomKernelMain` directly.  Keep the old
 * mapping in a disabled block so the history remains visible without allowing
 * new source to resolve back to the abbreviated name.
 *---------------------------------------------------------------------------*/
#if 0
/* Legacy source compatibility for the earlier short spelling. */
#define UmiKernelMain UmicomKernelMain
#endif

#endif /* UMICOM_KERNEL_KERNEL_H */
