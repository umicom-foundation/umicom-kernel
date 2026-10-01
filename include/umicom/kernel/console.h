/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/console.h
 *
 * PURPOSE:
 *   Publish a tiny machine-independent early diagnostic console.
 *
 * EDUCATIONAL NOTE:
 *   Generic kernel code calls these functions and therefore does not need to
 *   know the QEMU UART address or register layout.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_CONSOLE_H
#define UMICOM_KERNEL_CONSOLE_H

/* Import K1 fixed-width integer types used by formatting helpers. */
#include "umicom/kernel/types.h"

/* Prepare the platform's earliest available text output device. */
void UmiKernelConsoleInitialize(void);

/* Write a NUL-terminated string without automatically adding a new line. */
void UmiKernelConsoleWrite(const char *text);

/* Write a NUL-terminated string followed by CR+LF. */
void UmiKernelConsoleWriteLine(const char *text);

/* Render one 64-bit value as exactly sixteen hexadecimal digits with 0x. */
void UmiKernelConsoleWriteHex64(UmiU64 value);

/* Render one unsigned 64-bit value in base-10 without printf/libc. */
void UmiKernelConsoleWriteUnsigned(UmiU64 value);

#endif /* UMICOM_KERNEL_CONSOLE_H */
