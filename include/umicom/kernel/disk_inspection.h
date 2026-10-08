/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/disk_inspection.h
 *
 * Read-only disk interpretation starts above a sector reader, not above a raw
 * physical address. This contract has no write operation. The reader owns its
 * transport; these parsers neither reset that transport nor free its DMA pages.
 *
 * Callers supply trusted, separate Kernel buffers. The medium must remain
 * immutable during a call. Read-only guest access does not prevent a host from
 * editing its backing file, so this is not a snapshot or authenticity service.
 * All calls are serial; callbacks must not re-enter an active owner.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_INSPECTION_H
#define UMICOM_KERNEL_DISK_INSPECTION_H
#include "umicom/kernel/types.h"

#define UMICOM_DISK_SECTOR_BYTES 512U
#define UMICOM_DISK_PRIMARY_PARTITIONS 4U

typedef enum UmicomKernelDiskStatus {
    UMICOM_DISK_OK,
    UMICOM_DISK_INVALID_ARGUMENT,
    UMICOM_DISK_BAD_STATE,
    UMICOM_DISK_IO_ERROR,
    UMICOM_DISK_SIGNATURE,
    UMICOM_DISK_UNSUPPORTED_TABLE,
    UMICOM_DISK_UNSUPPORTED_FILESYSTEM,
    UMICOM_DISK_CORRUPT,
    UMICOM_DISK_RANGE,
    UMICOM_DISK_OVERLAP,
    UMICOM_DISK_NOT_FOUND,
    UMICOM_DISK_NOT_DIRECTORY,
    UMICOM_DISK_IS_DIRECTORY,
    UMICOM_DISK_LIMIT,
    UMICOM_DISK_FAT_MISMATCH,
    UMICOM_DISK_CHAIN_CYCLE,
    UMICOM_DISK_DIRTY,
    /* Preserve the original final spelling while appending an explicit
     * update-admission refusal without renumbering existing status values. */
#if 0
    UMICOM_DISK_BUSY
#endif
    UMICOM_DISK_BUSY,
    UMICOM_DISK_READ_ONLY
    /* Keep every established status value while adding rename collisions. */
    , UMICOM_DISK_EXISTS
    /* Allocation and fixed-directory capacity exhaustion are explicit. */
    , UMICOM_DISK_NO_SPACE
    /* Live children prevent directory removal without recursive deletion. */
    , UMICOM_DISK_NOT_EMPTY
} UmicomKernelDiskStatus;

/* A successful callback fills exactly one sector. Its capacity is fixed by the
 * type contract, and every parser checks the LBA before calling it. A failure
 * may damage the supplied scratch sector: no scratch bytes are then published.
 * The adapter must impose its own time bound on device I/O. */
typedef struct UmicomKernelDiskReader {
    UmicomU64 sectors;
    UmicomBoolean (*read)(void *context, UmicomU64 sector, UmicomU8 *output);
    void *context;
} UmicomKernelDiskReader;

typedef struct UmicomKernelDiskPartition {
    UmicomU64 firstSector;
    UmicomU64 sectors;
    UmicomU8 type;
    UmicomBoolean bootable;
    UmicomBoolean present;
} UmicomKernelDiskPartition;
typedef struct UmicomKernelPartitionTable {
    UmicomSize count;
    /* Indices preserve the four physical MBR slots, including empty slots. */
    UmicomKernelDiskPartition entries[UMICOM_DISK_PRIMARY_PARTITIONS];
} UmicomKernelPartitionTable;

/* Inspect LBA zero. Publish only a complete, non-overlapping primary MBR table.
 * GPT protective/hybrid tables and extended chains are refused, not treated as
 * empty disks. Boot code and CHS values are never executed or used as addresses.
 * A failure leaves the caller's table unchanged. */
UmicomKernelDiskStatus UmicomKernelDiskPartitionsInspect(
    const UmicomKernelDiskReader *reader, UmicomKernelPartitionTable *outTable);
const char *UmicomKernelDiskStatusName(UmicomKernelDiskStatus status);
#endif /* UMICOM_KERNEL_DISK_INSPECTION_H */
