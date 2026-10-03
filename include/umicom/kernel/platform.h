/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/platform.h
 *
 * PURPOSE:
 *   Define the small boundary between generic Kernel/architecture code and the
 *   QEMU RISC-V `virt` machine adapter used by the current freestanding Kernel configuration.
 *
 * EDUCATIONAL OVERVIEW:
 *   Platform adapters own machine addresses and device register layouts.
 *   Generic Kernel code therefore does not need to know where QEMU placed the
 *   UART, timer, test-finisher or physical RAM.
 *
 *   Canonical platform interfaces use the full `UmicomPlatform...` spelling.
 *   Earlier short-name mappings are retained at the end of this file as
 *   disabled historical material.  Current implementation and callers use
 *   only the full project name.
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
    /* First physical byte of usable RAM in the selected machine profile. */
    UmicomAddress base;

    /* Total physical RAM byte count configured for the selected machine profile. */
    UmicomSize bytes;
} UmicomPlatformPhysicalMemoryInfo;

/* Configure the earliest polling text-output device. */
void UmicomPlatformConsoleInitialize(void);

/* Send one byte to the earliest machine text-output device. */
void UmicomPlatformConsoleWriteByte(UmicomU8 value);

/* Read the machine timer's current 64-bit time value. */
UmicomU64 UmicomPlatformTimerRead(void);

/* Program one hart's machine-timer compare register with an absolute deadline. */
void UmicomPlatformTimerSetCompare(UmicomU64 hartId, UmicomU64 deadline);

/* Move one hart's compare value to the maximum so no near-term timer remains. */
void UmicomPlatformTimerDisable(UmicomU64 hartId);

/* Terminate the QEMU teaching machine with a successful test result. */
void UmicomPlatformFinishSuccess(void);

/* Terminate the QEMU teaching machine with a bounded nonzero failure code. */
void UmicomPlatformFinishFailure(UmicomU32 code);

/* Stop useful execution permanently if there is nowhere safe to continue. */
void UmicomPlatformHalt(void);

/* Publish the RAM geometry matched by the selected platform profile. */
void UmicomPlatformPhysicalMemoryDescribe(
    UmicomPlatformPhysicalMemoryInfo *outInfo
);

/*-----------------------------------------------------------------------------
 * HISTORICAL SHORT PLATFORM NAMES — RETAINED FOR REVIEW, NOT COMPILED
 *
 * Earlier machine adapters used `UmiPlatform...` spellings.  The real active
 * interface is the full `UmicomPlatform...` API declared above.  The old alias
 * statements are preserved below inside `#if 0` so they remain part of the
 * educational source record without changing how current code is compiled.
 *---------------------------------------------------------------------------*/
#if 0
/*-------------------------------------------------------------------------
 * LEGACY SOURCE-COMPATIBILITY ALIASES
 *
 * The aliases point from earlier short spellings to the canonical full Umicom
 * names.  They preserve old source without allowing new code to regress to the
 * abbreviated naming convention.
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
