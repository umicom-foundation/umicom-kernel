/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/platform.h
 *
 * PURPOSE:
 *   Define the small boundary between generic kernel/architecture code and the
 *   QEMU RISC-V "virt" machine adapter used by K2.
 *
 * EDUCATIONAL NOTE:
 *   A platform adapter owns machine addresses and device register layouts.
 *   Generic kernel code therefore never needs to know where QEMU placed the
 *   UART, timer or test-finisher devices.
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

/* Import the fixed-width types used by device-facing function contracts. */
#include "umicom/kernel/types.h"

/* Configure the earliest polling text-output device. */
void UmiPlatformConsoleInitialize(void);

/* Send one byte to the earliest machine text-output device. */
void UmiPlatformConsoleWriteByte(UmiU8 value);

/* Read the machine timer's current 64-bit time value. */
UmiU64 UmiPlatformTimerRead(void);

/* Program one hart's machine-timer compare register with an absolute deadline. */
void UmiPlatformTimerSetCompare(UmiU64 hartId, UmiU64 deadline);

/* Move one hart's compare value to the maximum so no near-term timer remains. */
void UmiPlatformTimerDisable(UmiU64 hartId);

/* Terminate the QEMU teaching machine with a successful test result. */
void UmiPlatformFinishSuccess(void);

/* Terminate the QEMU teaching machine with a bounded nonzero failure code. */
void UmiPlatformFinishFailure(UmiU32 code);

/* Stop useful execution permanently if there is nowhere safe to continue. */
void UmiPlatformHalt(void);

#endif /* UMICOM_KERNEL_PLATFORM_H */
