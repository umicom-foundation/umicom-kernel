/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/device_tree.h
 *
 * PURPOSE:
 *   Publish the deliberately small the physical-memory foundation device-tree header inspection contract.
 *
 * EDUCATIONAL OVERVIEW:
 *   QEMU passes the address of a Flattened Device Tree (FDT/DTB) to the first
 *   RISC-V hart.  earlier boot and trap/timer only printed that pointer.  the physical-memory foundation needs to protect the
 *   DTB pages from the physical frame allocator, so it now reads only the
 *   standard header fields required to establish the DTB's byte range.
 *
 *   This is not yet a general device-tree parser.  Node/property traversal is
 *   deliberately deferred to a later hardware-discovery milestone.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_DEVICE_TREE_H
#define UMICOM_KERNEL_DEVICE_TREE_H

/* Import the full Umicom address/size/boolean types. */
#include "umicom/kernel/types.h"

/* Standard big-endian FDT header magic encoded by the device-tree format. */
#define UMICOM_KERNEL_FDT_MAGIC ((UmicomU32)0xd00dfeedU)

/* The v17 FDT header occupies ten 32-bit fields = 40 bytes.
 *
 * the physical-memory foundation only reads the first two fields, but requiring the complete minimum header
 * avoids accepting a truncated blob as a legitimate tree. */
#define UMICOM_KERNEL_FDT_MINIMUM_HEADER_BYTES ((UmicomSize)40U)

/* Describe only the physical range the physical-memory foundation needs to protect. */
typedef struct UmicomKernelDeviceTreeInfo {
    /* Physical address passed by QEMU in the RISC-V a1 register. */
    UmicomAddress address;

    /* Total DTB byte count read from the standard big-endian header. */
    UmicomSize totalBytes;
} UmicomKernelDeviceTreeInfo;

/* Validate the standard FDT magic/total-size fields and prove the complete DTB
 * remains inside the supplied physical RAM range.
 *
 * The function returns UMICOM_TRUE only after `outInfo` contains a range that
 * the physical-memory manager may safely reserve. */
UmicomBoolean UmicomKernelDeviceTreeInspect(
    UmicomAddress deviceTreeAddress,
    UmicomAddress ramBase,
    UmicomSize ramBytes,
    UmicomKernelDeviceTreeInfo *outInfo
);

#endif /* UMICOM_KERNEL_DEVICE_TREE_H */
