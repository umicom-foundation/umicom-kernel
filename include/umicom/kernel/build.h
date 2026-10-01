/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/build.h
 *
 * PURPOSE:
 *   Keep the experimental kernel identity in ordinary source so early serial
 *   evidence can identify the exact milestone without a generated script.
 *
 * EDUCATIONAL NOTE:
 *   A mature release pipeline may generate stronger source/build provenance.
 *   K2 deliberately keeps identity simple and inspectable while the kernel is
 *   still teaching the first privileged execution mechanisms.
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

/* Human-readable product name printed during every educational boot. */
#define UMICOM_KERNEL_NAME "Umicom Kernel"

/* Experimental native-kernel source version for the K2 milestone. */
#define UMICOM_KERNEL_VERSION "0.2.0"

/* Roadmap milestone this image is expected to prove. */
#define UMICOM_KERNEL_MILESTONE "K2"

/* CPU architecture selected by this build. */
#define UMICOM_KERNEL_ARCHITECTURE "riscv64"

/* Machine adapter selected by the current source list. */
#define UMICOM_KERNEL_MACHINE "qemu-virt"

/* Stable teaching/debugging identity for this milestone design. */
#define UMICOM_KERNEL_BUILD_ID "k2-riscv64-traps-timer"

#endif /* UMICOM_KERNEL_BUILD_H */
