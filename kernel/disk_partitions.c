/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_partitions.c
 *
 * Decode a primary MBR without trusting packed C layouts, native alignment or
 * legacy cylinder/head/sector geometry. Each nonempty record must fit the
 * reader's whole-device extent before any later volume read is authorised.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_inspection.h"

static UmicomU32 UmicomPartitionWord(const UmicomU8 *bytes)
{
    /* Cast before shifting: integer promotion must not turn bit 31 into a
     * signed shift. Byte decoding also permits unaligned firmware records. */
    return (UmicomU32)bytes[0] | ((UmicomU32)bytes[1] << 8U) |
        ((UmicomU32)bytes[2] << 16U) | ((UmicomU32)bytes[3] << 24U);
}
UmicomKernelDiskStatus UmicomKernelDiskPartitionsInspect(
    const UmicomKernelDiskReader *reader, UmicomKernelPartitionTable *outTable)
{
    if (!reader || !reader->read || !reader->sectors || !outTable)
        return UMICOM_DISK_INVALID_ARGUMENT;
    /* The table is not changed until all four slots pass. In particular, a bad
     * last record cannot leave three apparently admitted partitions behind. */
    UmicomU8 sector[UMICOM_DISK_SECTOR_BYTES];
    UmicomKernelPartitionTable result;
    volatile UmicomU8 *scratch = sector;
    for (UmicomSize i = 0U; i < sizeof(sector); ++i) scratch[i] = 0U;
    scratch = (volatile UmicomU8 *)&result;
    for (UmicomSize i = 0U; i < sizeof(result); ++i) scratch[i] = 0U;
    if (!reader->read(reader->context, 0U, sector)) return UMICOM_DISK_IO_ERROR;
    if (sector[510] != 0x55U || sector[511] != 0xaaU) return UMICOM_DISK_SIGNATURE;
    for (UmicomSize i = 0U; i < UMICOM_DISK_PRIMARY_PARTITIONS; ++i) {
        const UmicomU8 *entry = sector + 446U + i * 16U;
        UmicomKernelDiskPartition *part = &result.entries[i];
        /* Flags are defined as inactive or active. Multiple active records do
         * not matter to a reader: this code never chooses boot code to run. */
        if (entry[0] != 0U && entry[0] != 0x80U) return UMICOM_DISK_CORRUPT;
        part->type = entry[4];
        part->firstSector = UmicomPartitionWord(entry + 8U);
        part->sectors = UmicomPartitionWord(entry + 12U);
        part->bootable = entry[0] == 0x80U ? UMICOM_TRUE : UMICOM_FALSE;
        /* Refuse container formats before interpreting their protective range
         * as an ordinary filesystem partition. No EBR following is attempted. */
        if (part->type == 0xeeU || part->type == 0x05U ||
            part->type == 0x0fU || part->type == 0x85U)
            return UMICOM_DISK_UNSUPPORTED_TABLE;
        if (part->type == 0U) {
            if (part->firstSector || part->sectors || part->bootable)
                return UMICOM_DISK_CORRUPT;
            continue;
        }
        /* Subtraction avoids overflow and protects the partition-table sector
         * itself. An unused record is not permission to expose the whole disk. */
        if (!part->firstSector || !part->sectors || part->firstSector >= reader->sectors ||
            part->sectors > reader->sectors - part->firstSector) return UMICOM_DISK_RANGE;
        part->present = UMICOM_TRUE;
        for (UmicomSize j = 0U; j < i; ++j) {
            const UmicomKernelDiskPartition *other = &result.entries[j];
            if (other->present && part->firstSector < other->firstSector + other->sectors &&
                other->firstSector < part->firstSector + part->sectors) return UMICOM_DISK_OVERLAP;
        }
        ++result.count;
    }
    volatile UmicomU8 *published = (volatile UmicomU8 *)outTable;
    const UmicomU8 *checked = (const UmicomU8 *)&result;
    for (UmicomSize i = 0U; i < sizeof(result); ++i) published[i] = checked[i];
    return UMICOM_DISK_OK;
}
const char *UmicomKernelDiskStatusName(UmicomKernelDiskStatus status)
{
    switch (status) {
    case UMICOM_DISK_OK: return "ok";
    case UMICOM_DISK_INVALID_ARGUMENT: return "invalid-argument";
    case UMICOM_DISK_BAD_STATE: return "bad-state";
    case UMICOM_DISK_IO_ERROR: return "io-error";
    case UMICOM_DISK_SIGNATURE: return "bad-signature";
    case UMICOM_DISK_UNSUPPORTED_TABLE: return "unsupported-partition-table";
    case UMICOM_DISK_UNSUPPORTED_FILESYSTEM: return "unsupported-filesystem";
    case UMICOM_DISK_CORRUPT: return "corrupt-metadata";
    case UMICOM_DISK_RANGE: return "range";
    case UMICOM_DISK_OVERLAP: return "overlapping-partitions";
    case UMICOM_DISK_NOT_FOUND: return "not-found";
    case UMICOM_DISK_NOT_DIRECTORY: return "not-directory";
    case UMICOM_DISK_IS_DIRECTORY: return "is-directory";
    case UMICOM_DISK_LIMIT: return "inspection-limit";
    case UMICOM_DISK_FAT_MISMATCH: return "fat-copies-differ";
    case UMICOM_DISK_CHAIN_CYCLE: return "cluster-cycle";
    case UMICOM_DISK_DIRTY: return "unclean-volume";
    case UMICOM_DISK_BUSY: return "busy";
    case UMICOM_DISK_READ_ONLY: return "read-only";
    case UMICOM_DISK_EXISTS: return "exists";
    }
    return "unknown-status";
}
