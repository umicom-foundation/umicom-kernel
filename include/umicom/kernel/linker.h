/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/linker.h
 *
 * PURPOSE:
 *   Publish the small set of linker-defined image-boundary symbols that the physical-memory foundation
 *   needs to protect Kernel-owned physical memory.
 *
 * EDUCATIONAL OVERVIEW:
 *   These are not ordinary C variables.  `arch/riscv64/linker.ld` assigns the
 *   symbols to addresses.  C takes each symbol's address to discover where the
 *   linked image starts and where the complete BSS/bootstrap-stack image ends.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_LINKER_H
#define UMICOM_KERNEL_LINKER_H

/* Import the byte/address-sized types used to expose linker locations. */
#include "umicom/kernel/types.h"

/* Linker symbol at the first byte owned by the linked Umicom Kernel image. */
extern UmicomU8 __kernel_start[];

/* Linker symbol at the first byte after all linked/BSS/bootstrap-stack memory. */
extern UmicomU8 __kernel_end[];

/* Convert a linker symbol pointer to the target's pointer-sized address type.
 *
 * The explicit intermediate `UmicomUIntPtr` documents that this is a deliberate
 * address representation conversion, not ordinary application pointer maths. */
#define UMICOM_KERNEL_LINKER_ADDRESS(symbol) \
    ((UmicomAddress)(UmicomUIntPtr)(symbol))

#endif /* UMICOM_KERNEL_LINKER_H */
