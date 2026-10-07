/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/device_tree_firmware.c
 *
 * A bounded compatibility copy precedes, rather than replaces, strict DTB
 * validation. The small token-envelope walk identifies property padding only;
 * it cannot publish an index or a hardware catalogue. DeviceTreeOpen remains
 * the sole authority for complete grammar, ownership, and typed byte indexing.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/device_tree_firmware.h"
#include "umicom/kernel/device_tree.h"

static void UmicomFirmwareClear(void *memory, UmicomSize bytes)
{
    /* Volatile byte stores avoid a hidden hosted memset dependency at any
     * optimisation level in the freestanding Kernel. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)memory;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static void UmicomFirmwareCopy(void *destination, const void *source, UmicomSize bytes)
{
    /* The complete source/destination ownership check precedes every copy. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)destination;
    const UmicomU8 *in = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
static UmicomU32 UmicomFirmwareRead32(const UmicomU8 *bytes)
{
    return ((UmicomU32)bytes[0] << 24U) | ((UmicomU32)bytes[1] << 16U) |
        ((UmicomU32)bytes[2] << 8U) | (UmicomU32)bytes[3];
}
static UmicomBoolean UmicomFirmwareFits(UmicomSize start, UmicomSize bytes, UmicomSize limit)
{
    return start <= limit && bytes <= limit - start ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomFirmwareOwnerFits(const void *memory, UmicomSize bytes)
{
    const UmicomUIntPtr start = (UmicomUIntPtr)memory;
    return bytes <= (UmicomSize)(~(UmicomUIntPtr)0U - start) ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomFirmwareSeparate(const void *first, UmicomSize firstBytes,
    const void *second, UmicomSize secondBytes)
{
    /* Call only after both address-space extent checks have succeeded. Empty
     * spans contain no bytes, while every later read still needs a size check. */
    if (!firstBytes || !secondBytes) return UMICOM_TRUE;
    const UmicomUIntPtr a = (UmicomUIntPtr)first, b = (UmicomUIntPtr)second;
    return a < b + secondBytes && b < a + firstBytes ? UMICOM_FALSE : UMICOM_TRUE;
}

static UmicomKernelTreeStatus UmicomFirmwareCanonicaliseProperties(UmicomU8 *copy,
    UmicomSize total, UmicomSize *changedBytes)
{
    /* These are the minimum envelope checks needed to walk an owned copy.
     * The original reader subsequently checks every header block, reservation,
     * duplicate, parent/child rule, name, and terminal token without relaxation. */
    const UmicomU32 structure = UmicomFirmwareRead32(copy + 8U);
    const UmicomU32 structureBytes = UmicomFirmwareRead32(copy + 36U);
    if (structure < 40U || (structure & 3U) || (structureBytes & 3U) || structureBytes < 12U)
        return UMICOM_TREE_BAD_HEADER;
    if (!UmicomFirmwareFits(structure, structureBytes, total)) return UMICOM_TREE_OUTSIDE_BUFFER;
    UmicomSize position = structure;
    const UmicomSize end = (UmicomSize)structure + structureBytes;
    while (position < end) {
        if (!UmicomFirmwareFits(position, 4U, end)) return UMICOM_TREE_BAD_STRUCTURE;
        const UmicomU32 token = UmicomFirmwareRead32(copy + position);
        position += 4U;
        if (token == 4U || token == 2U) continue; /* NOP and END_NODE have no payload. */
        if (token == 1U) {
            /* Find the bounded node-name terminator, then skip its alignment
             * bytes without changing them. Non-zero node padding must continue
             * to fail the strict reader, just as it did before this adapter. */
            while (position < end && copy[position] != 0U) ++position;
            if (position == end) return UMICOM_TREE_BAD_STRUCTURE;
            ++position;
            while (position & 3U) {
                if (position >= end) return UMICOM_TREE_BAD_STRUCTURE;
                ++position;
            }
        } else if (token == 3U) {
            if (!UmicomFirmwareFits(position, 8U, end)) return UMICOM_TREE_BAD_STRUCTURE;
            const UmicomU32 valueBytes = UmicomFirmwareRead32(copy + position);
            position += 8U;
            if (!UmicomFirmwareFits(position, valueBytes, end)) return UMICOM_TREE_OUTSIDE_BUFFER;
            position += valueBytes;
            /* The declared value is already behind us. Only its one to three
             * alignment bytes can change; a token or value byte is never used
             * as a repair candidate. The original input remains untouched. */
            while (position & 3U) {
                if (position >= end) return UMICOM_TREE_BAD_STRUCTURE;
                if (copy[position] != 0U) {
                    copy[position] = 0U;
                    ++*changedBytes;
                }
                ++position;
            }
        } else if (token == 9U) {
            return position == end ? UMICOM_TREE_OK : UMICOM_TREE_BAD_STRUCTURE;
        } else return UMICOM_TREE_BAD_STRUCTURE;
    }
    return UMICOM_TREE_BAD_STRUCTURE;
}

UmicomKernelTreeStatus UmicomKernelDeviceTreeOpenFirmwareCopy(
    const void *blob, UmicomSize accessibleBytes,
    void *copyBuffer, UmicomSize copyCapacity,
    UmicomKernelDeviceTreeReader *reader,
    UmicomSize *outCanonicalisedPropertyPaddingBytes)
{
    /* Establish every owner before writing even a failure status. In
     * particular, an output counter must not hide inside firmware bytes or
     * inside the reader that would otherwise overwrite it while initialising. */
    if (!blob || !copyBuffer || !reader || !outCanonicalisedPropertyPaddingBytes ||
        ((UmicomUIntPtr)reader % _Alignof(UmicomKernelDeviceTreeReader)) != 0U ||
        ((UmicomUIntPtr)outCanonicalisedPropertyPaddingBytes % _Alignof(UmicomSize)) != 0U ||
        !UmicomFirmwareOwnerFits(blob, accessibleBytes) ||
        !UmicomFirmwareOwnerFits(copyBuffer, copyCapacity) ||
        !UmicomFirmwareOwnerFits(reader, sizeof(*reader)) ||
        !UmicomFirmwareOwnerFits(outCanonicalisedPropertyPaddingBytes, sizeof(*outCanonicalisedPropertyPaddingBytes)) ||
        !UmicomFirmwareSeparate(blob, accessibleBytes, copyBuffer, copyCapacity) ||
        !UmicomFirmwareSeparate(blob, accessibleBytes, reader, sizeof(*reader)) ||
        !UmicomFirmwareSeparate(blob, accessibleBytes, outCanonicalisedPropertyPaddingBytes,
            sizeof(*outCanonicalisedPropertyPaddingBytes)) ||
        !UmicomFirmwareSeparate(copyBuffer, copyCapacity, reader, sizeof(*reader)) ||
        !UmicomFirmwareSeparate(copyBuffer, copyCapacity, outCanonicalisedPropertyPaddingBytes,
            sizeof(*outCanonicalisedPropertyPaddingBytes)) ||
        !UmicomFirmwareSeparate(reader, sizeof(*reader), outCanonicalisedPropertyPaddingBytes,
            sizeof(*outCanonicalisedPropertyPaddingBytes)))
        return UMICOM_TREE_INVALID_ARGUMENT;

    UmicomFirmwareClear(reader, sizeof(*reader));
    *outCanonicalisedPropertyPaddingBytes = 0U;
    if (accessibleBytes < UMICOM_KERNEL_FDT_MINIMUM_HEADER_BYTES) return UMICOM_TREE_OUTSIDE_BUFFER;
    const UmicomU8 *source = (const UmicomU8 *)blob;
    if (UmicomFirmwareRead32(source) != UMICOM_KERNEL_FDT_MAGIC) return UMICOM_TREE_BAD_HEADER;
    const UmicomU32 total = UmicomFirmwareRead32(source + 4U);
    if (total < 40U || total > accessibleBytes) return UMICOM_TREE_OUTSIDE_BUFFER;
    if (total > UMICOM_TREE_MAX_BYTES) return UMICOM_TREE_LIMIT;
    if (total > copyCapacity) return UMICOM_TREE_OUTSIDE_BUFFER;

    UmicomFirmwareCopy(copyBuffer, blob, total);
    UmicomSize changedBytes = 0U;
    UmicomKernelTreeStatus status = UmicomFirmwareCanonicaliseProperties(
        (UmicomU8 *)copyBuffer, total, &changedBytes);
    if (status == UMICOM_TREE_OK)
        status = UmicomKernelDeviceTreeOpen(copyBuffer, total, reader);
    if (status != UMICOM_TREE_OK) {
        UmicomFirmwareClear(reader, sizeof(*reader));
        UmicomFirmwareClear(copyBuffer, total);
        return status;
    }
    /* Publishing the count is the final step: no partially repaired tree can
     * escape merely because one padding byte happened to be canonicalised. */
    *outCanonicalisedPropertyPaddingBytes = changedBytes;
    return UMICOM_TREE_OK;
}
