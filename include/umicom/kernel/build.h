/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/build.h
 *
 * PURPOSE:
 *   Keep stable human-readable identity for the running freestanding Kernel
 *   without embedding development-batch numbers or temporary release versions.
 *
 * EDUCATIONAL NOTE:
 *   Git history and release metadata record chronology.  Runtime source should
 *   describe what the software is and which architecture/machine it targets,
 *   rather than carrying a development-batch label that becomes meaningless
 *   after the next capability is added.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_BUILD_H
#define UMICOM_KERNEL_BUILD_H

/* Human-readable product name printed during early diagnostic startup. */
#define UMICOM_KERNEL_NAME "Umicom Kernel"

/* CPU architecture selected by this build configuration. */
#define UMICOM_KERNEL_ARCHITECTURE "riscv64"

/* Machine adapter selected by the current source and QEMU test configuration. */
#define UMICOM_KERNEL_MACHINE "qemu-virt"

#endif /* UMICOM_KERNEL_BUILD_H */
