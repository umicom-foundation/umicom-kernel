/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/platform.h
 *
 * PURPOSE:
 *   Keep machine-specific early-boot operations behind a small internal-facing
 *   contract so generic kernel code does not embed QEMU MMIO addresses.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_PLATFORM_H
#define UMICOM_KERNEL_PLATFORM_H

#include "umicom/kernel/types.h"

void UmiPlatformConsoleInitialize(void);
void UmiPlatformConsoleWriteByte(UmiU8 value);
void UmiPlatformFinishSuccess(void);
void UmiPlatformHalt(void);

#endif
