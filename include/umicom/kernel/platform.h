/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/platform.h
 *
 * PURPOSE:
 *   Define the very small K1 boundary between generic kernel code and the
 *   machine-specific QEMU RISC-V adapter.
 *
 * EDUCATIONAL NOTE:
 *   Later milestones will grow proper HAL/device contracts.  K1 keeps this
 *   interface deliberately tiny so generic code never embeds QEMU MMIO values.
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

/* Import the byte type used by the early serial writer. */
#include "umicom/kernel/types.h"

/* Configure the earliest machine text-output device for polling output. */
void UmiPlatformConsoleInitialize(void);

/* Send one byte to the machine text-output device. */
void UmiPlatformConsoleWriteByte(UmiU8 value);

/* Tell the K1 test machine that the boot milestone completed successfully. */
void UmiPlatformFinishSuccess(void);

/* Stop useful execution permanently if there is nowhere safe to continue. */
void UmiPlatformHalt(void);

#endif /* UMICOM_KERNEL_PLATFORM_H */
