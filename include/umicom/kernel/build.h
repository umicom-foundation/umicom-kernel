/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/build.h
 *
 * PURPOSE:
 *   Keep the first native-kernel milestone identity visible in the binary and
 *   serial boot evidence without requiring generated scripts or hosted tools.
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

#define UMICOM_KERNEL_NAME "Umicom Kernel"
#define UMICOM_KERNEL_VERSION "0.1.0"
#define UMICOM_KERNEL_MILESTONE "K1"
#define UMICOM_KERNEL_ARCHITECTURE "riscv64"
#define UMICOM_KERNEL_MACHINE "qemu-virt"
#define UMICOM_KERNEL_BUILD_ID "k1-riscv64-freestanding"

#endif
