/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/device_tree_layout.c
 *
 * PURPOSE:
 *   Read device-tree layout with byte-order conversion, bounded section arithmetic and nonoverlapping reservations.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/device_tree_layout.h"
#include "umicom/kernel/device_tree.h"
#include "boot_memory.h"

static UmicomU32 Big32(const UmicomU8 *p)
{
    return ((UmicomU32)p[0] << 24U) | ((UmicomU32)p[1] << 16U) |
        ((UmicomU32)p[2] << 8U) | (UmicomU32)p[3];
}
/* The draft layout-only parser is retained for review. Full grammar and
 * reservation admission now come from DeviceTreeOpen, avoiding divergent
 * bounds and keeping hardware discovery and boot on the same validation. */
#if 0
static UmicomU64 Big64(const UmicomU8 *p)
{
    return ((UmicomU64)Big32(p) << 32U) | (UmicomU64)Big32(p + 4U);
}
static UmicomBoolean Section(UmicomU32 offset, UmicomU32 count, UmicomU32 total)
{
    /* Subtraction is only evaluated after proving the offset is in range. */
    return offset >= 40U && offset <= total && count <= total - offset
        ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelDeviceTreeLayoutStatus UmicomKernelDeviceTreeLayoutRead(
    const void *data, UmicomSize bytes, UmicomKernelDeviceTreeLayout *out)
{
    if (!data || !out) return UMICOM_FDT_LAYOUT_ARGUMENT;
    if (bytes < 40U) return UMICOM_FDT_LAYOUT_HEADER;
    const UmicomU8 *p = (const UmicomU8 *)data;
    if (Big32(p) != UMICOM_KERNEL_FDT_MAGIC) return UMICOM_FDT_LAYOUT_HEADER;
    UmicomKernelDeviceTreeLayout staged = {0};
    staged.totalBytes = Big32(p + 4U);
    if (staged.totalBytes < 40U || staged.totalBytes > bytes)
        return UMICOM_FDT_LAYOUT_RANGE;
    if (staged.totalBytes > UMICOM_KERNEL_FDT_LAYOUT_MAX_BYTES)
        return UMICOM_FDT_LAYOUT_CAPACITY;
    staged.structureOffset = Big32(p + 8U);
    staged.stringsOffset = Big32(p + 12U);
    staged.reservationOffset = Big32(p + 16U);
    staged.version = Big32(p + 20U);
    staged.lastCompatibleVersion = Big32(p + 24U);
    staged.bootCpu = Big32(p + 28U);
    staged.stringsBytes = Big32(p + 32U);
    staged.structureBytes = Big32(p + 36U);
    if (staged.version < 17U || staged.lastCompatibleVersion < 16U ||
        staged.lastCompatibleVersion > 17U ||
        staged.lastCompatibleVersion > staged.version)
        return UMICOM_FDT_LAYOUT_VERSION;
    if (!Section(staged.structureOffset, staged.structureBytes, staged.totalBytes) ||
        !Section(staged.stringsOffset, staged.stringsBytes, staged.totalBytes) ||
        !Section(staged.reservationOffset, 16U, staged.totalBytes) ||
        staged.structureBytes < 4U)
        return UMICOM_FDT_LAYOUT_RANGE;
    if (staged.structureOffset % 4U || staged.structureBytes % 4U ||
        staged.reservationOffset % 8U)
        return UMICOM_FDT_LAYOUT_ALIGNMENT;
    /* The supported flat layout orders reservation, structure and strings.
     * Free-space gaps are permitted; overlapping interpretations are refused. */
    if (staged.reservationOffset >= staged.structureOffset ||
        staged.structureOffset + staged.structureBytes > staged.stringsOffset)
        return UMICOM_FDT_LAYOUT_OVERLAP;
    UmicomU32 at = staged.reservationOffset;
    for (;;) {
        if (at > staged.structureOffset || staged.structureOffset - at < 16U)
            return UMICOM_FDT_LAYOUT_RESERVATION;
        const UmicomU64 address = Big64(p + at);
        const UmicomU64 count = Big64(p + at + 8U);
        at += 16U; /* The preceding difference check proves this cannot wrap. */
        if (address == 0U && count == 0U) break;
        if (count == 0U || address > ~(UmicomU64)0U - count)
            return UMICOM_FDT_LAYOUT_RESERVATION;
        if (staged.reservationCount == UMICOM_KERNEL_FDT_RESERVATION_LIMIT)
            return UMICOM_FDT_LAYOUT_CAPACITY;
        for (UmicomSize i = 0U; i < staged.reservationCount; ++i) {
            const UmicomKernelDeviceTreeReservation *prior = &staged.reservations[i];
            if (address < prior->address + prior->bytes && prior->address < address + count)
                return UMICOM_FDT_LAYOUT_OVERLAP;
        }
        staged.reservations[staged.reservationCount++] =
            (UmicomKernelDeviceTreeReservation){address, count};
    }
    *out = staged;
    return UMICOM_FDT_LAYOUT_OK;
}
#endif

/* DeviceTreeOpen is the existing grammar authority used by hardware discovery.
 * This adapter copies only the boot-layout projection; it does not maintain a
 * second header/reservation parser with different admission rules. */
static UmicomKernelDeviceTreeLayoutStatus Translate(UmicomKernelTreeStatus status)
{
    switch (status) {
    case UMICOM_TREE_OK: return UMICOM_FDT_LAYOUT_OK;
    case UMICOM_TREE_INVALID_ARGUMENT: return UMICOM_FDT_LAYOUT_ARGUMENT;
    case UMICOM_TREE_UNSUPPORTED_FORMAT: return UMICOM_FDT_LAYOUT_VERSION;
    case UMICOM_TREE_OUTSIDE_BUFFER: return UMICOM_FDT_LAYOUT_RANGE;
    case UMICOM_TREE_LIMIT: return UMICOM_FDT_LAYOUT_CAPACITY;
    case UMICOM_TREE_BAD_VALUE: return UMICOM_FDT_LAYOUT_RESERVATION;
    case UMICOM_TREE_BAD_STRUCTURE:
    case UMICOM_TREE_DUPLICATE: return UMICOM_FDT_LAYOUT_STRUCTURE;
    default: return UMICOM_FDT_LAYOUT_HEADER;
    }
}
static UmicomBoolean Separate(const void *a, UmicomSize an, const void *b, UmicomSize bn)
{
    UmicomUIntPtr first=(UmicomUIntPtr)a,second=(UmicomUIntPtr)b;
    if(an>~(UmicomUIntPtr)0U-first||bn>~(UmicomUIntPtr)0U-second)return UMICOM_FALSE;
    return first<second+bn&&second<first+an?UMICOM_FALSE:UMICOM_TRUE;
}
UmicomKernelDeviceTreeLayoutStatus UmicomKernelDeviceTreeLayoutFromReader(
    const UmicomKernelDeviceTreeReader *reader, UmicomKernelDeviceTreeLayout *out)
{
    if(!reader||!out||reader->self!=reader||!reader->opened||!reader->blob||
        reader->bytes<40U||reader->bytes>UMICOM_TREE_MAX_BYTES||
        reader->reservationCount>UMICOM_TREE_RESERVATION_LIMIT||
        !Separate(reader,sizeof *reader,out,sizeof *out)||
        !Separate(reader->blob,reader->bytes,out,sizeof *out))
        return UMICOM_FDT_LAYOUT_ARGUMENT;
    const UmicomU8 *p=reader->blob;
    /* Staging stays private until success; explicit bytes avoid hosted memset.
     * The old form remains available for review of the original contract. */
#if 0
    UmicomKernelDeviceTreeLayout staged={0};
#endif
    UmicomKernelDeviceTreeLayout staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    staged.totalBytes=(UmicomU32)reader->bytes;
    staged.version=Big32(p+20U);staged.lastCompatibleVersion=Big32(p+24U);
    staged.bootCpu=Big32(p+28U);staged.structureOffset=Big32(p+8U);
    staged.structureBytes=Big32(p+36U);staged.stringsOffset=Big32(p+12U);
    staged.stringsBytes=Big32(p+32U);staged.reservationOffset=Big32(p+16U);
    staged.reservationCount=reader->reservationCount;
    /* Scalar fields and the bounded final copy replace implicit libc calls.
     * Preserve the equivalent aggregate form for comparison. */
#if 0
    for(UmicomU32 i=0;i<reader->reservationCount;++i)
        staged.reservations[i]=(UmicomKernelDeviceTreeReservation){
            reader->reservations[i].address,reader->reservations[i].bytes};
    *out=staged;
#endif
    for (UmicomU32 i = 0U; i < reader->reservationCount; ++i) {
        staged.reservations[i].address = reader->reservations[i].address;
        staged.reservations[i].bytes = reader->reservations[i].bytes;
    }
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_FDT_LAYOUT_OK;
}
UmicomKernelDeviceTreeLayoutStatus UmicomKernelDeviceTreeLayoutRead(
    const void *data, UmicomSize bytes, UmicomKernelDeviceTreeLayout *out)
{
    if(!data||!out||!Separate(data,bytes,out,sizeof *out))return UMICOM_FDT_LAYOUT_ARGUMENT;
    UmicomKernelDeviceTreeReader reader;
    UmicomKernelTreeStatus status=UmicomKernelDeviceTreeOpen(data,bytes,&reader);
    if(status!=UMICOM_TREE_OK)return Translate(status);
    return UmicomKernelDeviceTreeLayoutFromReader(&reader,out);
}

const char *UmicomKernelDeviceTreeLayoutStatusName(UmicomKernelDeviceTreeLayoutStatus status)
{
    switch (status) {
    case UMICOM_FDT_LAYOUT_OK: return "layout-valid";
    case UMICOM_FDT_LAYOUT_ARGUMENT: return "invalid-argument";
    case UMICOM_FDT_LAYOUT_HEADER: return "invalid-header";
    case UMICOM_FDT_LAYOUT_VERSION: return "unsupported-version";
    case UMICOM_FDT_LAYOUT_RANGE: return "section-outside-buffer";
    case UMICOM_FDT_LAYOUT_ALIGNMENT: return "misaligned-section";
    case UMICOM_FDT_LAYOUT_OVERLAP: return "overlapping-sections-or-reservations";
    case UMICOM_FDT_LAYOUT_RESERVATION: return "invalid-tree-value";
    case UMICOM_FDT_LAYOUT_STRUCTURE: return "invalid-tree-structure";
    case UMICOM_FDT_LAYOUT_CAPACITY: return "layout-capacity-exceeded";
    default: return "unknown-layout-status";
    }
}
