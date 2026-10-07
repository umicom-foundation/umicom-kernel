/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/device_tree_firmware.h
 *
 * PURPOSE:
 *   Make an explicit owned firmware copy for producers that leave residue in
 *   property alignment bytes. The ordinary DeviceTreeOpen API remains strict.
 *
 * DTSpec describes zero-valued alignment padding. Some deployed QEMU/libfdt
 * combinations retain previous buffer contents in those bytes. This adapter
 * changes only the one to three alignment bytes after an FDT_PROP value, then
 * requires the original strict reader to validate the complete copied tree.
 * Node-name padding, tokens, lengths, names, offsets, values and reservations
 * retain every existing strict check. No compatible string authenticates the
 * producer or authorises a device operation.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DEVICE_TREE_FIRMWARE_H
#define UMICOM_KERNEL_DEVICE_TREE_FIRMWARE_H
#include "umicom/kernel/device_tree_reader.h"

/* All pointers name caller-owned, accessible storage. The input's declared
 * accessible span, the destination's full capacity, reader, and count output
 * must be pairwise separate and must not wrap the address space. Reader and
 * count must have their normal C alignment; byte buffers may be unaligned.
 * Invalid ownership arguments leave all outputs untouched.
 *
 * Once those ownership arguments are valid, a failure clears reader and sets
 * the count to zero. Any bytes already copied into copyBuffer are wiped; bytes
 * outside that copied extent are untouched. The original input is never written.
 *
 * Success publishes a reader borrowing copyBuffer, not the original firmware.
 * Keep the copy immutable and accessible for that reader's lifetime. reader's
 * bytes field describes the copied header totalsize. The count records only
 * non-zero property-padding bytes actually changed to zero; zero padding is
 * not counted. The shared UMICOM_TREE_MAX_BYTES limit bounds copying and work.
 * This function allocates nothing and performs no MMIO or driver admission. */
UmicomKernelTreeStatus UmicomKernelDeviceTreeOpenFirmwareCopy(
    const void *blob, UmicomSize accessibleBytes,
    void *copyBuffer, UmicomSize copyCapacity,
    UmicomKernelDeviceTreeReader *reader,
    UmicomSize *outCanonicalisedPropertyPaddingBytes);

#endif /* UMICOM_KERNEL_DEVICE_TREE_FIRMWARE_H */
