/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/identity.h
 *
 * PURPOSE:
 *   Publish stable descriptive identity strings for early diagnostic output
 *   without embedding temporary development batch numbers or release labels.
 *
 * EDUCATIONAL OVERVIEW:
 *   Early boot code cannot depend on a package manager, operating-system
 *   metadata service or generated release manifest.  A small set of static
 *   strings therefore identifies the product, CPU architecture and machine
 *   adapter that produced the serial output.
 *
 *   These strings describe what the running image is.  Git history and release
 *   metadata record when a capability was introduced; source code should not
 *   depend on batch numbers to explain its purpose.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_IDENTITY_H
#define UMICOM_KERNEL_IDENTITY_H

/* Human-readable product name printed by the early diagnostic console. */
#define UMICOM_KERNEL_NAME "Umicom Kernel"

/* CPU architecture selected by the active cross-compilation preset. */
#define UMICOM_KERNEL_ARCHITECTURE "riscv64"

/* Machine adapter compiled into the current experimental image. */
#define UMICOM_KERNEL_MACHINE "qemu-virt"

#endif /* UMICOM_KERNEL_IDENTITY_H */
