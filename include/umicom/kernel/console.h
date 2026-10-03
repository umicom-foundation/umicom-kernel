/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/console.h
 *
 * PURPOSE:
 *   Publish the tiny machine-independent early diagnostic console used by the
 *   early Umicom Kernel startup and diagnostic paths.
 *
 * EDUCATIONAL NOTE:
 *   Generic Kernel code calls these helpers and therefore does not know the
 *   QEMU UART address or register layout.
 *
 *   Canonical Kernel interfaces use the full `UmicomKernel...` project name.
 *   The older short-name mappings are retained at the end of this header in a
 *   disabled historical block.  They remain readable, but they no longer take
 *   part in compilation.
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

/* Prepare the platform's earliest available text output device. */
void UmicomKernelConsoleInitialize(void);

/* Write a NUL-terminated string without automatically adding a new line. */
void UmicomKernelConsoleWrite(const char *text);

/* Write a NUL-terminated string followed by CR+LF. */
void UmicomKernelConsoleWriteLine(const char *text);

/* Render one 64-bit value as exactly sixteen hexadecimal digits with 0x. */
void UmicomKernelConsoleWriteHex64(UmicomU64 value);

/* Render one unsigned 64-bit value in base-10 without printf/libc. */
void UmicomKernelConsoleWriteUnsigned(UmicomU64 value);

/*-----------------------------------------------------------------------------
 * HISTORICAL SHORT CONSOLE NAMES — RETAINED FOR REVIEW, NOT COMPILED
 *
 * The earlier console interface used the abbreviated `UmiKernel...` spelling.
 * The active declarations above are the real `UmicomKernel...` functions.
 * The old preprocessor aliases are kept exactly as historical reference, but
 * they are disabled so the compiler can never make the short spelling the
 * working interface by accident.
 *---------------------------------------------------------------------------*/
#if 0
/*-------------------------------------------------------------------------
 * LEGACY SOURCE-COMPATIBILITY ALIASES
 *
 * These aliases intentionally point from the earlier short spelling to the
 * canonical full Umicom names.  New Kernel source must use the full names.
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

#endif

#endif /* UMICOM_KERNEL_CONSOLE_H */
