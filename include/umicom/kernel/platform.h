/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/platform.h
 *
 * PURPOSE:
 *   Define the small boundary between architecture-neutral Kernel code and the
 *   machine adapter that knows the selected platform's device addresses and
 *   low-level hardware behaviour.
 *
 * EDUCATIONAL OVERVIEW:
 *   A Kernel service should not need to know that QEMU places its UART at one
 *   physical address, its test-finisher at another address, or that the current
 *   virtual machine has exactly 128 MiB of RAM.  Those facts belong to the
 *   platform adapter.
 *
 *   Keeping this contract small also prepares the project for additional
 *   machines.  A future physical RISC-V board can provide these same operations
 *   without changing the architecture-neutral console, memory allocator or
 *   higher Kernel services.
 *
 *   The public interface uses the full `UmicomPlatform...` spelling.  No short
 *   compatibility aliases are active or retained in this header; earlier
 *   naming experiments remain available through Git history instead of being
 *   allowed to complicate today's compiled interface.
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

/* Describe one contiguous physical RAM range for the selected platform profile.
 *
 * This deliberately models only the single QEMU RAM region used by the current
 * machine adapter.  The contract can grow into a region catalogue when device
 * tree driven hardware discovery requires multiple usable/reserved ranges. */
typedef struct UmicomPlatformPhysicalMemoryInfo {
    /* First physical byte of usable RAM in the selected machine profile. */
    UmicomAddress base;

    /* Total physical RAM byte count configured for the selected machine. */
    UmicomSize bytes;
} UmicomPlatformPhysicalMemoryInfo;

/* Configure the earliest polling text-output device.
 *
 * This operation is deliberately usable before interrupts, scheduling,
 * allocation or virtual memory exist. */
void UmicomPlatformConsoleInitialize(void);

/* Send exactly one byte to the earliest machine text-output device. */
void UmicomPlatformConsoleWriteByte(UmicomU8 value);

/* Read the machine timer's current monotonically increasing 64-bit time value. */
UmicomU64 UmicomPlatformTimerRead(void);

/* Program one hart's machine-timer compare register with an absolute deadline. */
void UmicomPlatformTimerSetCompare(
    UmicomU64 hartId,
    UmicomU64 deadline
);

/* Move one hart's compare value far enough into the future that the current
 * one-shot timer validation cannot remain pending. */
void UmicomPlatformTimerDisable(UmicomU64 hartId);

/* Terminate the QEMU validation machine with a successful guest result.
 *
 * This is a test-machine convenience, not the future physical-machine power
 * interface. */
void UmicomPlatformFinishSuccess(void);

/* Terminate the QEMU validation machine with a bounded nonzero failure code. */
void UmicomPlatformFinishFailure(UmicomU32 code);

/* Stop useful execution permanently when there is no safe continuation path. */
void UmicomPlatformHalt(void);

/* Publish the physical RAM geometry matched by the selected platform profile. */
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

/*
 * IMPORTANT PRESERVATION NOTE:
 *
 * Everything above in this disabled block is retained because it is already
 * part of the committed source history.  It is deliberately not compiled and
 * it must not be copied as the pattern for new interfaces.  The active API is
 * the `UmicomPlatform...` interface declared before this block.
 *
 * This `#endif` closes only the historical `#if 0`.  The header guard remains
 * open until the final `#endif` below.  Keeping those two conditionals separate
 * fixes the build failure where the historical block accidentally consumed the
 * header guard's closing directive.
 */
#endif /* HISTORICAL SHORT PLATFORM NAMES */

#endif /* UMICOM_KERNEL_PLATFORM_H */
