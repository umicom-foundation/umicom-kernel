/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/console.h
 *
 * PURPOSE:
 *   Publish the tiny machine-independent early diagnostic console used by the
 *   first Umicom Kernel milestones.
 *
 * EDUCATIONAL NOTE:
 *   Generic Kernel code calls these helpers and therefore does not know the
 *   QEMU UART address or register layout.
 *
 *   K1/K2 committed `UmiKernel...` symbols.  K3 preserves those symbols and
 *   introduces full `UmicomKernel...` source aliases so new code follows the
 *   agreed full-name convention without silently breaking earlier milestones.
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

/* Import the fixed-width integer types used by formatting helpers. */
#include "umicom/kernel/types.h"

/*-------------------------------------------------------------------------
 * K1/K2 HISTORICAL SYMBOL DECLARATIONS
 *-------------------------------------------------------------------------*/

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

/*-------------------------------------------------------------------------
 * CANONICAL FULL UMICOM SOURCE NAMES
 *-------------------------------------------------------------------------*/

/* Full-name source alias for early console initialization. */
#define UmicomKernelConsoleInitialize UmiKernelConsoleInitialize

/* Full-name source alias for raw text output. */
#define UmicomKernelConsoleWrite UmiKernelConsoleWrite

/* Full-name source alias for one line of output. */
#define UmicomKernelConsoleWriteLine UmiKernelConsoleWriteLine

/* Full-name source alias for hexadecimal rendering. */
#define UmicomKernelConsoleWriteHex64 UmiKernelConsoleWriteHex64

/* Full-name source alias for unsigned decimal rendering. */
#define UmicomKernelConsoleWriteUnsigned UmiKernelConsoleWriteUnsigned

#endif /* UMICOM_KERNEL_CONSOLE_H */
