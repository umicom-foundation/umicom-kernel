/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/boot_handoff.c
 *
 * PURPOSE:
 *   Validate boot metadata as bounded bytes before publishing an owned, architecture-neutral observation.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_handoff.h"
#include "umicom/kernel/address.h"
#include "boot_memory.h"

static UmicomU16 Read16(const UmicomU8 *p)
{
    return (UmicomU16)((UmicomU16)p[0] | (UmicomU16)((UmicomU16)p[1] << 8U));
}
static UmicomU32 Read32(const UmicomU8 *p)
{
    return (UmicomU32)p[0] | ((UmicomU32)p[1] << 8U) |
        ((UmicomU32)p[2] << 16U) | ((UmicomU32)p[3] << 24U);
}
static UmicomU64 Read64(const UmicomU8 *p)
{
    return (UmicomU64)Read32(p) | ((UmicomU64)Read32(p + 4U) << 32U);
}
static void ReadSpan(const UmicomU8 *p, UmicomBootSpan *out)
{
    out->address = Read64(p);
    out->bytes = Read64(p + 8U);
}
static UmicomBoolean ValidSpan(const UmicomBootSpan *span)
{
    return span->bytes && span->address <= ~(UmicomU64)0U - span->bytes
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean Contains(const UmicomBootSpan *outer, const UmicomBootSpan *inner)
{
    /* All spans have already passed ValidSpan. Subtract before comparing the
     * remaining length so containment does not introduce another overflow. */
    return inner->address >= outer->address &&
        inner->address - outer->address <= outer->bytes &&
        inner->bytes <= outer->bytes - (inner->address - outer->address)
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean Intersects(const UmicomBootSpan *a, const UmicomBootSpan *b)
{
    return a->address < b->address + b->bytes && b->address < a->address + a->bytes
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean Covered(const UmicomBootSpan *ranges, UmicomSize count,
    const UmicomBootSpan *span)
{
    for (UmicomSize i = 0U; i < count; ++i)
        if (Contains(&ranges[i], span)) return UMICOM_TRUE;
    return UMICOM_FALSE;
}
static UmicomBoolean ReservedIfRam(const UmicomKernelBootHandoff *h, const UmicomBootSpan *span)
{
    for (UmicomSize i = 0U; i < h->ramCount; ++i)
        if (Intersects(&h->ram[i], span))
            return Covered(h->reserved, h->reservedCount, span);
    return UMICOM_TRUE;
}
UmicomBootHandoffStatus UmicomKernelBootHandoffRead(const void *bytes,
    UmicomSize accessibleBytes, UmicomKernelBootHandoff *out)
{
    if (!bytes || !out) return UMICOM_BOOT_HANDOFF_ARGUMENT;
    if (accessibleBytes < UMICOM_BOOT_HANDOFF_HEADER_BYTES) return UMICOM_BOOT_HANDOFF_LENGTH;
    if (accessibleBytes > UMICOM_BOOT_HANDOFF_MAX_BYTES) return UMICOM_BOOT_HANDOFF_LIMIT;
    UmicomAddress inputEnd = 0U, outputEnd = 0U;
    const UmicomAddress inputStart = (UmicomAddress)bytes;
    const UmicomAddress outputStart = (UmicomAddress)out;
    if (!UmicomKernelAddressAddChecked(inputStart, accessibleBytes, &inputEnd) ||
        !UmicomKernelAddressAddChecked(outputStart, sizeof *out, &outputEnd) ||
        (inputStart < outputEnd && outputStart < inputEnd))
        return UMICOM_BOOT_HANDOFF_ARGUMENT;
    const UmicomU8 *p = (const UmicomU8 *)bytes;
    static const UmicomU8 magic[8] = {'U','M','I','B','O','O','T',0};
    for (UmicomSize i = 0U; i < sizeof magic; ++i)
        if (p[i] != magic[i]) return UMICOM_BOOT_HANDOFF_HEADER;
    if (Read16(p + 8U) != 1U || Read16(p + 10U) != UMICOM_BOOT_HANDOFF_HEADER_BYTES ||
        Read32(p + 20U) || Read32(p + 60U)) return UMICOM_BOOT_HANDOFF_UNSUPPORTED;
    const UmicomU32 architecture = Read32(p + 16U);
    const UmicomU32 tableKind = Read32(p + 52U);
    const UmicomU16 pixelFormat = Read16(p + 58U);
    if (architecture < UMICOM_BOOT_RISCV64 || architecture > UMICOM_BOOT_AARCH64 ||
        tableKind > UMICOM_BOOT_TABLE_ACPI_RSDP || pixelFormat > UMICOM_BOOT_PIXELS_BGRX32)
        return UMICOM_BOOT_HANDOFF_UNSUPPORTED;
    const UmicomSize ramCount = Read16(p + 48U);
    const UmicomSize reservedCount = Read16(p + 50U);
    const UmicomSize commandBytes = Read16(p + 56U);
    if (!ramCount || ramCount > UMICOM_BOOT_HANDOFF_RAM_LIMIT ||
        reservedCount > UMICOM_BOOT_HANDOFF_RESERVED_LIMIT ||
        commandBytes >= UMICOM_BOOT_HANDOFF_COMMAND_BYTES) return UMICOM_BOOT_HANDOFF_LIMIT;
    /* Counts are bounded before multiplication or indexing. Version 1 uses a
     * single canonical section order, with no attacker-chosen section offsets. */
    const UmicomSize expected = 64U + 16U * (ramCount + reservedCount) + 16U + 32U + commandBytes;
    if (Read32(p + 12U) != accessibleBytes || expected != accessibleBytes)
        return UMICOM_BOOT_HANDOFF_LENGTH;
    UmicomKernelBootHandoff staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    staged.architecture = (UmicomBootArchitecture)architecture;
    staged.bootstrapCpu = Read64(p + 24U);
    ReadSpan(p + 32U, &staged.kernel);
    if (!ValidSpan(&staged.kernel)) return UMICOM_BOOT_HANDOFF_RANGE;
    staged.ramCount = ramCount;
    staged.reservedCount = reservedCount;
    staged.tableKind = (UmicomBootFirmwareTable)tableKind;
    staged.framebuffer.format = (UmicomBootPixelFormat)pixelFormat;
    UmicomSize offset = 64U;
    for (UmicomSize i = 0U; i < ramCount; ++i, offset += 16U) {
        ReadSpan(p + offset, &staged.ram[i]);
        if (!ValidSpan(&staged.ram[i]) || (staged.ram[i].address & 4095U) ||
            (staged.ram[i].bytes & 4095U)) return UMICOM_BOOT_HANDOFF_MEMORY;
        for (UmicomSize previous = 0U; previous < i; ++previous)
            if (Intersects(&staged.ram[previous], &staged.ram[i])) return UMICOM_BOOT_HANDOFF_MEMORY;
    }
    for (UmicomSize i = 0U; i < reservedCount; ++i, offset += 16U) {
        ReadSpan(p + offset, &staged.reserved[i]);
        if (!ValidSpan(&staged.reserved[i])) return UMICOM_BOOT_HANDOFF_RANGE;
    }
    if (!Covered(staged.ram, ramCount, &staged.kernel) ||
        !Covered(staged.reserved, reservedCount, &staged.kernel)) return UMICOM_BOOT_HANDOFF_MEMORY;
    ReadSpan(p + offset, &staged.table);
    offset += 16U;
    if (tableKind == UMICOM_BOOT_TABLE_NONE) {
        if (staged.table.address || staged.table.bytes) return UMICOM_BOOT_HANDOFF_RANGE;
    } else {
        if (!ValidSpan(&staged.table) ||
            (tableKind == UMICOM_BOOT_TABLE_FDT && ((staged.table.address & 7U) || staged.table.bytes < 40U)) ||
            (tableKind == UMICOM_BOOT_TABLE_ACPI_RSDP && ((staged.table.address & 15U) || staged.table.bytes < 20U)) ||
            !ReservedIfRam(&staged, &staged.table)) return UMICOM_BOOT_HANDOFF_RANGE;
    }
    UmicomBootFramebuffer *frame = &staged.framebuffer;
    ReadSpan(p + offset, &frame->storage);
    frame->width = Read32(p + offset + 16U);
    frame->height = Read32(p + offset + 20U);
    frame->strideBytes = Read32(p + offset + 24U);
    if (Read32(p + offset + 28U)) return UMICOM_BOOT_HANDOFF_UNSUPPORTED;
    offset += 32U;
    if (!pixelFormat) {
        if (frame->storage.address || frame->storage.bytes || frame->width || frame->height || frame->strideBytes)
            return UMICOM_BOOT_HANDOFF_FRAMEBUFFER;
    } else {
        /* Widen before multiplication. Two 32-bit dimensions fit a 64-bit
         * product; width*4 must still fit the advertised 32-bit row stride. */
        const UmicomU64 rowBytes = (UmicomU64)frame->width * 4U;
        const UmicomU64 imageBytes = (UmicomU64)frame->strideBytes * frame->height;
        if (!ValidSpan(&frame->storage) || !frame->width || !frame->height ||
            rowBytes > frame->strideBytes || (frame->strideBytes & 3U) ||
            (frame->storage.address & 3U) || imageBytes > frame->storage.bytes ||
            !ReservedIfRam(&staged, &frame->storage)) return UMICOM_BOOT_HANDOFF_FRAMEBUFFER;
    }
    for (UmicomSize i = 0U; i < commandBytes; ++i) {
        if (p[offset + i] < 0x20U || p[offset + i] > 0x7eU) return UMICOM_BOOT_HANDOFF_TEXT;
        staged.commandLine[i] = (char)p[offset + i];
    }
    /* Publishing is the only write to caller state. Addresses remain integer
     * observations until a platform adapter proves mapping and ownership. */
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_BOOT_HANDOFF_OK;
}
