/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/disk_space_console.h
 *
 * PURPOSE:
 *   Expose read-only FAT16 capacity through the existing console transport.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_SPACE_CONSOLE_H
#define UMICOM_KERNEL_DISK_SPACE_CONSOLE_H
#include "umicom/kernel/disk_console.h"
#include "umicom/kernel/fat16_space.h"

/* Emit counts only after the complete scan and successful device release.
 * On error emit diagnostic status, with no partial capacity result. */
UmicomKernelDiskStatus UmicomKernelDiskSpaceInspect(UmicomSize slot,
    UmicomSize partition, void *context, UmicomKernelBlockOutput output);
#endif
