/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/virtual_memory.h
 *
 * PURPOSE:
 *   Publish the first RISC-V Sv39 page-table construction and inspection
 *   contracts built on top of the physical page-frame allocator.
 *
 * EDUCATIONAL OVERVIEW:
 *   Physical memory answers the question "which RAM frame exists and who owns
 *   it?".  Virtual memory answers a different question: "which physical frame
 *   should a virtual address refer to, with which access permissions?"
 *
 *   This subsystem deliberately constructs and validates Sv39 page tables
 *   without enabling address translation in the CPU yet.  The Kernel still
 *   executes in machine mode with physical addressing while it learns how to
 *   create trustworthy page-table state.  A later privilege/address-space
 *   stage can activate these tables after supervisor/user execution exists.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_VIRTUAL_MEMORY_H
#define UMICOM_KERNEL_VIRTUAL_MEMORY_H

#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/types.h"

/* Sv39 uses three page-table levels with 512 eight-byte entries per page. */
#define UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES ((UmicomSize)512U)

/* Virtual-memory operation results are explicit so callers can distinguish
 * malformed addresses, duplicate mappings, absent mappings and allocator
 * failures instead of collapsing every problem into one boolean. */
typedef enum UmicomKernelVirtualMemoryStatus {
    UMICOM_KERNEL_VIRTUAL_MEMORY_OK = 0,
    UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT = 1,
    UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ALIGNMENT = 2,
    UMICOM_KERNEL_VIRTUAL_MEMORY_NON_CANONICAL_ADDRESS = 3,
    UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PERMISSIONS = 4,
    UMICOM_KERNEL_VIRTUAL_MEMORY_ALREADY_MAPPED = 5,
    UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED = 6,
    UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY = 7,
    UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE = 8,
    UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE = 9,
    UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR = 10
} UmicomKernelVirtualMemoryStatus;

/* Leaf permissions are deliberately small and architecture-neutral enough for
 * the teaching API.  The implementation converts them into RISC-V PTE bits. */
typedef UmicomU32 UmicomKernelVirtualMemoryPermissions;

enum {
    UMICOM_KERNEL_VIRTUAL_MEMORY_READ = 1U << 0U,
    UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE = 1U << 1U,
    UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE = 1U << 2U,
    UMICOM_KERNEL_VIRTUAL_MEMORY_USER = 1U << 3U,
    UMICOM_KERNEL_VIRTUAL_MEMORY_GLOBAL = 1U << 4U
};

/* One page-table hierarchy owned by the Kernel.
 *
 * The hierarchy owns only page-table frames.  Physical frames mapped as leaf
 * pages remain owned by the caller that allocated/reserved them. */
typedef struct UmicomKernelVirtualAddressSpace {
    UmicomAddress rootTablePhysicalAddress;
    UmicomSize pageTableFrames;
    UmicomSize mappedPages;
    UmicomBoolean initialised;
} UmicomKernelVirtualAddressSpace;

/* Snapshot public accounting without exposing raw page-table arrays. */
typedef struct UmicomKernelVirtualMemorySnapshot {
    UmicomAddress rootTablePhysicalAddress;
    UmicomSize pageTableFrames;
    UmicomSize mappedPages;
} UmicomKernelVirtualMemorySnapshot;

/* Return true only for addresses with a legal Sv39 sign-extension pattern. */
UmicomBoolean UmicomKernelVirtualMemoryIsCanonical(UmicomAddress virtualAddress);

/* Allocate and clear one root page-table frame. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualAddressSpaceCreate(
    UmicomKernelVirtualAddressSpace *space
);

/* Validate the hierarchy, free all page-table frames and reset the structure.
 * Leaf physical frames are intentionally not freed. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualAddressSpaceDestroy(
    UmicomKernelVirtualAddressSpace *space
);

/* Map one 4 KiB virtual page to one page-aligned physical address.
 * Large-page leaves are deliberately outside this first implementation. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryMapPage(
    UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress,
    UmicomAddress physicalAddress,
    UmicomKernelVirtualMemoryPermissions permissions
);

/* Remove one 4 KiB mapping and reclaim now-empty intermediate page tables. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryUnmapPage(
    UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress
);

/* Walk the software page tables and translate one virtual address, including
 * its byte offset inside the mapped 4 KiB page. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryTranslate(
    const UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress,
    UmicomAddress *outPhysicalAddress,
    UmicomKernelVirtualMemoryPermissions *outPermissions
);

/* Prove every reachable page-table entry obeys the supported Sv39 subset and
 * that public table/mapping counters match an independent tree walk. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryValidate(
    const UmicomKernelVirtualAddressSpace *space
);

/* Copy page-table/mapping accounting to caller-owned storage. */
UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemorySnapshotRead(
    const UmicomKernelVirtualAddressSpace *space,
    UmicomKernelVirtualMemorySnapshot *outSnapshot
);

/* Convert a status enum into stable diagnostic text for serial evidence. */
const char *UmicomKernelVirtualMemoryStatusName(
    UmicomKernelVirtualMemoryStatus status
);

#endif /* UMICOM_KERNEL_VIRTUAL_MEMORY_H */
