/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/console.h
 *
 * PURPOSE:
 *   Define the first architecture-neutral diagnostic console contract used by
 *   early kernel code before a filesystem, allocator or full driver model
 *   exists.
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

#include "umicom/kernel/types.h"

void UmiKernelConsoleInitialize(void);
void UmiKernelConsoleWrite(const char *text);
void UmiKernelConsoleWriteLine(const char *text);
void UmiKernelConsoleWriteHex64(UmiU64 value);
void UmiKernelConsoleWriteUnsigned(UmiU64 value);

#endif
