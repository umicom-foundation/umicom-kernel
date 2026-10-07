/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/device_tree_reader.h
 *
 * PURPOSE:
 *   Read a bounded flattened device tree without following a firmware-supplied
 *   pointer, allocating memory, or touching a device register.
 *
 * The caller owns both the byte buffer and this index. Keep the bytes immutable
 * and accessible until the index is no longer used. The index contains offsets
 * into that buffer; it is not a relocated tree and must not be copied while open.
 * Once separate, valid storage is supplied, a parsing failure leaves an unusable,
 * cleared index rather than a partial tree. Invalid pointer/overlap arguments
 * are refused before writing the output owner.
 * Input and output storage must not overlap. These are trusted Kernel pointers;
 * a byte count cannot establish that an arbitrary physical pointer is readable.
 * The boot adapter establishes that boundary before it calls this parser.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DEVICE_TREE_READER_H
#define UMICOM_KERNEL_DEVICE_TREE_READER_H
#include "umicom/kernel/types.h"

/* These limits bound work as well as storage. An oversized tree is refused,
 * never silently truncated into an apparently complete hardware description. */
#define UMICOM_TREE_MAX_BYTES 131072U
#define UMICOM_TREE_NODE_LIMIT 128U
#define UMICOM_TREE_PROPERTY_LIMIT 512U
#define UMICOM_TREE_DEPTH_LIMIT 32U
#define UMICOM_TREE_RESERVATION_LIMIT 32U
#define UMICOM_TREE_NAME_LIMIT 95U
#define UMICOM_TREE_PROPERTY_NAME_LIMIT 63U
#define UMICOM_TREE_PATH_BYTES 256U
#define UMICOM_TREE_NO_NODE ((UmicomU32)0xffffffffU)

typedef enum UmicomKernelTreeStatus {
    UMICOM_TREE_OK, UMICOM_TREE_INVALID_ARGUMENT, UMICOM_TREE_BAD_HEADER,
    UMICOM_TREE_UNSUPPORTED_FORMAT, UMICOM_TREE_OUTSIDE_BUFFER, UMICOM_TREE_LIMIT,
    UMICOM_TREE_BAD_STRUCTURE, UMICOM_TREE_DUPLICATE, UMICOM_TREE_BAD_VALUE,
    UMICOM_TREE_NOT_FOUND, UMICOM_TREE_UNTRANSLATED, UMICOM_TREE_UNSUPPORTED_CELLS
} UmicomKernelTreeStatus;

typedef struct UmicomKernelTreeSpan {
    const UmicomU8 *data; /* Borrowed immutable bytes, never a device address. */
    UmicomSize bytes;
} UmicomKernelTreeSpan;
typedef struct UmicomKernelTreeReservation {
    UmicomU64 address;
    UmicomU64 bytes;
} UmicomKernelTreeReservation;
typedef struct UmicomKernelTreeNode {
    UmicomU32 parent; /* Root alone uses UMICOM_TREE_NO_NODE. */
    UmicomU32 nameOffset;
    UmicomU32 nameBytes;
    UmicomU32 firstProperty;
    UmicomU32 propertyCount;
} UmicomKernelTreeNode;
typedef struct UmicomKernelTreeProperty {
    UmicomU32 nameOffset; /* Absolute blob offset, not a native pointer. */
    UmicomU32 nameBytes;
    UmicomU32 valueOffset;
    UmicomU32 bytes;
} UmicomKernelTreeProperty;
typedef struct UmicomKernelDeviceTreeReader {
    const struct UmicomKernelDeviceTreeReader *self;
    const UmicomU8 *blob;
    UmicomSize bytes;
    UmicomU32 nodeCount;
    UmicomU32 propertyCount;
    UmicomU32 reservationCount;
    UmicomBoolean opened;
    UmicomKernelTreeNode nodes[UMICOM_TREE_NODE_LIMIT];
    UmicomKernelTreeProperty properties[UMICOM_TREE_PROPERTY_LIMIT];
    UmicomKernelTreeReservation reservations[UMICOM_TREE_RESERVATION_LIMIT];
} UmicomKernelDeviceTreeReader;

/* Open supports the complete-header encoding compatible with DTB format 17.
 * Format numbers here describe the external binary specification, not an Umicom
 * release. Fields are read bytewise, so the caller's buffer may be unaligned. */
UmicomKernelTreeStatus UmicomKernelDeviceTreeOpen(const void *blob, UmicomSize accessibleBytes,
    UmicomKernelDeviceTreeReader *reader);
UmicomKernelTreeStatus UmicomKernelDeviceTreeProperty(const UmicomKernelDeviceTreeReader *reader,
    UmicomU32 node, const char *name, UmicomKernelTreeSpan *outSpan);
UmicomKernelTreeStatus UmicomKernelDeviceTreePath(const UmicomKernelDeviceTreeReader *reader,
    UmicomU32 node, char *outPath, UmicomSize capacity);
UmicomKernelTreeStatus UmicomKernelDeviceTreeFind(const UmicomKernelDeviceTreeReader *reader,
    const char *absolutePath, UmicomU32 *outNode);
/* Conversion helpers validate exact lengths and complete string lists. A
 * matching prefix is not a matching property or compatible string. */
UmicomKernelTreeStatus UmicomKernelTreeU32(UmicomKernelTreeSpan span, UmicomU32 *outValue);
UmicomKernelTreeStatus UmicomKernelTreeString(UmicomKernelTreeSpan span,
    char *outText, UmicomSize capacity);
UmicomKernelTreeStatus UmicomKernelTreeStringListContains(UmicomKernelTreeSpan span,
    const char *text, UmicomBoolean *outFound);
const char *UmicomKernelTreeStatusName(UmicomKernelTreeStatus status);
#endif /* UMICOM_KERNEL_DEVICE_TREE_READER_H */
