/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/build.h
 *
 * PURPOSE:
 *   Keep K1's identity in normal source code so the first serial boot can state
 *   exactly what is running without any generated script or hosted runtime.
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

/* Human-readable product name printed during every K1 boot. */
#define UMICOM_KERNEL_NAME "Umicom Kernel"

/* First experimental native-kernel source version. */
#define UMICOM_KERNEL_VERSION "0.1.0"

/* Roadmap milestone proven by this image. */
#define UMICOM_KERNEL_MILESTONE "K1"

/* CPU architecture this particular K1 build targets. */
#define UMICOM_KERNEL_ARCHITECTURE "riscv64"

/* Machine adapter selected by the K1 source list. */
#define UMICOM_KERNEL_MACHINE "qemu-virt"

/* Stable teaching/debugging identity for this exact milestone design. */
#define UMICOM_KERNEL_BUILD_ID "k1-riscv64-freestanding"

#endif /* UMICOM_KERNEL_BUILD_H */
