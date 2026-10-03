/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/platform.h
 *
 * PURPOSE:
 *   Define the small boundary between generic Kernel/architecture code and the
 *   QEMU RISC-V `virt` machine adapter used by K3.
 *
 * EDUCATIONAL OVERVIEW:
 *   Platform adapters own machine addresses and device register layouts.
 *   Generic Kernel code therefore does not need to know where QEMU placed the
 *   UART, timer, test-finisher or physical RAM.
 *
 *   K1/K2 introduced `UmiPlatform...` function symbols.  K3 preserves those
 *   already-committed symbols and publishes full `UmicomPlatform...` source
 *   aliases for new Kernel code.  New K3 APIs use full Umicom names directly.
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

/* Import fixed-width/address types used by device-facing contracts. */
#include "umicom/kernel/types.h"

/* Describe one contiguous physical RAM range for the selected platform profile. */
typedef struct UmicomPlatformPhysicalMemoryInfo {
    /* First physical byte of usable RAM in the K3 machine profile. */
    UmicomAddress base;

    /* Total physical RAM byte count configured for the K3 machine profile. */
    UmicomSize bytes;
} UmicomPlatformPhysicalMemoryInfo;

/*-------------------------------------------------------------------------
 * K1/K2 HISTORICAL SYMBOLS
 *
 * These declarations remain because the symbols were already committed and
 * tested.  Canonical full-name aliases follow immediately afterwards.
 *-------------------------------------------------------------------------*/

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

/*-------------------------------------------------------------------------
 * CANONICAL FULL UMICOM NAMES FOR NEW KERNEL SOURCE
 *
 * These preprocessor aliases preserve the original K1/K2 binary symbols while
 * allowing all new K3 source to use the full project name.  A future stable
 * native ABI can introduce full-name exported symbols with compatibility
 * wrappers after the ABI itself has been designed deliberately.
 *-------------------------------------------------------------------------*/

/* Full-name source alias for early console setup. */
#define UmicomPlatformConsoleInitialize UmiPlatformConsoleInitialize

/* Full-name source alias for one-byte console output. */
#define UmicomPlatformConsoleWriteByte UmiPlatformConsoleWriteByte

/* Full-name source alias for reading the machine timer. */
#define UmicomPlatformTimerRead UmiPlatformTimerRead

/* Full-name source alias for programming a machine timer deadline. */
#define UmicomPlatformTimerSetCompare UmiPlatformTimerSetCompare

/* Full-name source alias for disabling the machine timer deadline. */
#define UmicomPlatformTimerDisable UmiPlatformTimerDisable

/* Full-name source alias for successful QEMU test completion. */
#define UmicomPlatformFinishSuccess UmiPlatformFinishSuccess

/* Full-name source alias for failed QEMU test completion. */
#define UmicomPlatformFinishFailure UmiPlatformFinishFailure

/* Full-name source alias for permanent machine halt. */
#define UmicomPlatformHalt UmiPlatformHalt

/* New K3 platform API: publish the RAM geometry matched by the selected
 * QEMU profile.  This function uses the full Umicom name as its real symbol. */
void UmicomPlatformPhysicalMemoryDescribe(
    UmicomPlatformPhysicalMemoryInfo *outInfo
);

#endif /* UMICOM_KERNEL_PLATFORM_H */
