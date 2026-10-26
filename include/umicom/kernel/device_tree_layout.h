/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/device_tree_layout.h
 *
 * PURPOSE:
 *   Validate bounded device-tree section geometry and firmware reservation records before consumers inspect them.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_DEVICE_TREE_LAYOUT_H
#define UMICOM_KERNEL_DEVICE_TREE_LAYOUT_H
#include "umicom/kernel/types.h"
#include "umicom/kernel/device_tree_reader.h"
/* Retain the initial draft bounds for review; the active projection follows
 * the established strict reader rather than inventing a larger second profile. */
#if 0
#define UMICOM_KERNEL_FDT_LAYOUT_MAX_BYTES ((UmicomSize)16U * 1024U * 1024U)
#define UMICOM_KERNEL_FDT_RESERVATION_LIMIT 64U
#endif
#define UMICOM_KERNEL_FDT_LAYOUT_MAX_BYTES ((UmicomSize)UMICOM_TREE_MAX_BYTES)
#define UMICOM_KERNEL_FDT_RESERVATION_LIMIT UMICOM_TREE_RESERVATION_LIMIT

typedef struct UmicomKernelDeviceTreeReservation {
    UmicomU64 address;
    UmicomU64 bytes;
} UmicomKernelDeviceTreeReservation;
typedef struct UmicomKernelDeviceTreeLayout {
    UmicomU32 version, lastCompatibleVersion, bootCpu;
    UmicomU32 totalBytes, structureOffset, structureBytes, stringsOffset, stringsBytes;
    UmicomU32 reservationOffset;
    UmicomSize reservationCount;
    UmicomKernelDeviceTreeReservation reservations[UMICOM_KERNEL_FDT_RESERVATION_LIMIT];
} UmicomKernelDeviceTreeLayout;
typedef enum UmicomKernelDeviceTreeLayoutStatus {
    UMICOM_FDT_LAYOUT_OK = 0,
    UMICOM_FDT_LAYOUT_ARGUMENT,
    UMICOM_FDT_LAYOUT_HEADER,
    UMICOM_FDT_LAYOUT_VERSION,
    UMICOM_FDT_LAYOUT_RANGE,
    UMICOM_FDT_LAYOUT_ALIGNMENT,
    UMICOM_FDT_LAYOUT_OVERLAP,
    UMICOM_FDT_LAYOUT_RESERVATION,
    UMICOM_FDT_LAYOUT_CAPACITY,
    UMICOM_FDT_LAYOUT_STRUCTURE
} UmicomKernelDeviceTreeLayoutStatus;

/* Accept an accessible caller-owned span; no physical address is dereferenced
 * from a reservation entry. The v17-compatible geometry and terminated memory
 * reservation list and tree grammar use the established strict reader.
 * Device-specific property semantics are outside this projection.
 * Success copies the result; failure leaves it unchanged. Input and output
 * storage must not overlap. This does not reserve physical allocator frames. */
UmicomKernelDeviceTreeLayoutStatus UmicomKernelDeviceTreeLayoutRead(
    const void *data, UmicomSize bytes, UmicomKernelDeviceTreeLayout *out);
/* Reader remains caller-owned and must retain its immutable backing bytes.
 * This projection also accepts a reader over the existing firmware copy. */
UmicomKernelDeviceTreeLayoutStatus UmicomKernelDeviceTreeLayoutFromReader(
    const UmicomKernelDeviceTreeReader *reader, UmicomKernelDeviceTreeLayout *out);
const char *UmicomKernelDeviceTreeLayoutStatusName(UmicomKernelDeviceTreeLayoutStatus status);
#endif
