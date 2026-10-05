/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/virtual_memory.c
 *
 * PURPOSE:
 *   Construct, inspect and tear down a deliberately small RISC-V Sv39
 *   page-table hierarchy using physical frames supplied by the Kernel's
 *   physical-memory allocator.
 *
 * EDUCATIONAL OVERVIEW:
 *   Sv39 divides a canonical 64-bit virtual address into:
 *
 *     bits 38..30  -> level-2 index (VPN[2])
 *     bits 29..21  -> level-1 index (VPN[1])
 *     bits 20..12  -> level-0 index (VPN[0])
 *     bits 11..0   -> byte offset inside a 4 KiB page
 *
 *   Each page-table page contains 512 64-bit entries.  Non-leaf entries point
 *   to the next page-table level.  This first implementation accepts leaf
 *   entries only at level 0, so every mapping represents one 4 KiB page.
 *
 *   Address translation is NOT enabled in the CPU yet.  Machine-mode Kernel
 *   code still accesses RAM using physical addresses while it builds and
 *   verifies the structures that a later supervisor/user execution model will
 *   activate through the RISC-V `satp` CSR.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/address.h"
#include "umicom/kernel/virtual_memory.h"

/* RISC-V page-table entries are 64-bit values. */
typedef UmicomU64 UmicomRiscvPageTableEntry;

/* Sv39 uses nine index bits at each of its three translation levels. */
#define UMICOM_RISCV_SV39_INDEX_MASK ((UmicomU64)0x1ffU)

/* Page-table entry flag bits defined by the RISC-V privileged architecture. */
#define UMICOM_RISCV_PTE_VALID   ((UmicomRiscvPageTableEntry)1ULL << 0U)
#define UMICOM_RISCV_PTE_READ    ((UmicomRiscvPageTableEntry)1ULL << 1U)
#define UMICOM_RISCV_PTE_WRITE   ((UmicomRiscvPageTableEntry)1ULL << 2U)
#define UMICOM_RISCV_PTE_EXECUTE ((UmicomRiscvPageTableEntry)1ULL << 3U)
#define UMICOM_RISCV_PTE_USER    ((UmicomRiscvPageTableEntry)1ULL << 4U)
#define UMICOM_RISCV_PTE_GLOBAL  ((UmicomRiscvPageTableEntry)1ULL << 5U)
#define UMICOM_RISCV_PTE_ACCESSED ((UmicomRiscvPageTableEntry)1ULL << 6U)
#define UMICOM_RISCV_PTE_DIRTY    ((UmicomRiscvPageTableEntry)1ULL << 7U)

/* PTE bits 9:8 are reserved for software; this implementation leaves them zero. */
#define UMICOM_RISCV_PTE_PERMISSION_MASK \
    (UMICOM_RISCV_PTE_READ | UMICOM_RISCV_PTE_WRITE | \
     UMICOM_RISCV_PTE_EXECUTE | UMICOM_RISCV_PTE_USER | \
     UMICOM_RISCV_PTE_GLOBAL)

/* RISC-V stores the physical page number beginning at PTE bit 10. */
#define UMICOM_RISCV_PTE_PPN_SHIFT ((UmicomU32)10U)

/* A 4 KiB page offset occupies twelve low address bits. */
#define UMICOM_RISCV_PAGE_SHIFT ((UmicomU32)12U)

/* Sv39 canonical addresses contain 39 meaningful low bits. */
#define UMICOM_RISCV_SV39_SIGN_BIT ((UmicomU32)38U)
#define UMICOM_RISCV_SV39_UPPER_SHIFT ((UmicomU32)39U)

/* When virtual bit 38 is one, bits 63:39 must all also be one.  There are
 * twenty-five upper bits, so their all-ones value is 2^25 - 1. */
#define UMICOM_RISCV_SV39_UPPER_ONES ((UmicomU64)0x01ffffffU)

/* Return true when an entry is architecturally marked valid. */
static UmicomBoolean EntryIsValid(UmicomRiscvPageTableEntry entry)
{
    /* Test only the architectural V bit; every other interpretation begins
     * only after this check succeeds. */
    return (entry & UMICOM_RISCV_PTE_VALID) != 0U
        ? UMICOM_TRUE
        : UMICOM_FALSE;
}

/* Return true when an entry is a leaf rather than a pointer to another table. */
static UmicomBoolean EntryIsLeaf(UmicomRiscvPageTableEntry entry)
{
    /* RISC-V defines a leaf when any of R, W or X is set.  A pure V entry with
     * no access bits is therefore a next-level page-table pointer. */
    const UmicomRiscvPageTableEntry leafBits =
        entry & (UMICOM_RISCV_PTE_READ | UMICOM_RISCV_PTE_WRITE | UMICOM_RISCV_PTE_EXECUTE);

    /* Convert the non-zero mask into the Kernel's explicit boolean type. */
    return leafBits != 0U ? UMICOM_TRUE : UMICOM_FALSE;
}

/* Convert a page-aligned physical address into the PPN field stored in a PTE. */
static UmicomRiscvPageTableEntry PhysicalAddressToEntryPpn(
    UmicomAddress physicalAddress
)
{
    /* Removing the twelve page-offset bits produces the physical page number. */
    const UmicomU64 physicalPageNumber =
        (UmicomU64)physicalAddress >> UMICOM_RISCV_PAGE_SHIFT;

    /* Sv39 places that page number beginning at PTE bit ten. */
    return (UmicomRiscvPageTableEntry)(
        physicalPageNumber << UMICOM_RISCV_PTE_PPN_SHIFT
    );
}

/* Recover the page-aligned physical address encoded by a valid PTE. */
static UmicomAddress EntryToPhysicalAddress(UmicomRiscvPageTableEntry entry)
{
    /* Remove low flag/software bits by shifting the stored page number down. */
    const UmicomU64 physicalPageNumber =
        (UmicomU64)(entry >> UMICOM_RISCV_PTE_PPN_SHIFT);

    /* Restore the twelve zero page-offset bits to obtain the byte address. */
    return (UmicomAddress)(physicalPageNumber << UMICOM_RISCV_PAGE_SHIFT);
}

/* Interpret a physical page-table frame as its 512-entry array.
 *
 * The processor still runs with translation disabled, so physical RAM is
 * directly addressable by the current machine-mode code. */
static UmicomRiscvPageTableEntry *PageTableFromPhysicalAddress(
    UmicomAddress physicalAddress
)
{
    /* Convert the integer physical address to a native pointer explicitly. */
    return (UmicomRiscvPageTableEntry *)(UmicomUIntPtr)physicalAddress;
}

/* Zero all 512 entries in one newly allocated page-table frame. */
static void ClearPageTable(UmicomAddress physicalAddress)
{
    /* Obtain direct machine-mode access to the page-table frame. */
    UmicomRiscvPageTableEntry *const table =
        PageTableFromPhysicalAddress(physicalAddress);

    /* Visit each fixed-size entry exactly once. */
    for (
        UmicomSize index = (UmicomSize)0U;
        index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
        ++index
    ) {
        /* A zero entry is invalid and therefore represents no mapping/table. */
        table[index] = (UmicomRiscvPageTableEntry)0U;
    }
}

/* Return one of the three nine-bit Sv39 VPN indexes. */
static UmicomSize VirtualPageIndex(UmicomAddress virtualAddress, UmicomU32 level)
{
    /* Level zero begins at bit 12, level one at 21 and level two at 30. */
    const UmicomU32 shift =
        UMICOM_RISCV_PAGE_SHIFT + (level * (UmicomU32)9U);

    /* Shift the selected index into the low bits and mask it to 0..511. */
    return (UmicomSize)(
        ((UmicomU64)virtualAddress >> shift) & UMICOM_RISCV_SV39_INDEX_MASK
    );
}

/* Validate the public permission combination before it becomes a hardware PTE. */
static UmicomBoolean PermissionsAreValid(
    UmicomKernelVirtualMemoryPermissions permissions
)
{
    /* Reject bits not defined by the public permission contract. */
    const UmicomU32 knownMask =
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ |
        UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE |
        UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE |
        UMICOM_KERNEL_VIRTUAL_MEMORY_USER |
        UMICOM_KERNEL_VIRTUAL_MEMORY_GLOBAL;

    /* Unknown permission bits usually indicate a caller/ABI mistake. */
    if ((permissions & ~knownMask) != 0U) {
        return UMICOM_FALSE;
    }

    /* RISC-V reserves the encoding W=1,R=0, so writable pages must also be
     * readable in this implementation. */
    if (
        (permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != 0U &&
        (permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == 0U
    ) {
        return UMICOM_FALSE;
    }

    /* At least one of read or execute must be set for a supported leaf. */
    if (
        (permissions & (
            UMICOM_KERNEL_VIRTUAL_MEMORY_READ |
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE
        )) == 0U
    ) {
        return UMICOM_FALSE;
    }

    /* Every supported permission combination passed the explicit checks. */
    return UMICOM_TRUE;
}

/* Convert public permission flags into RISC-V leaf PTE bits. */
static UmicomRiscvPageTableEntry PermissionsToEntryBits(
    UmicomKernelVirtualMemoryPermissions permissions
)
{
    /* Start with a valid leaf and set accessed so later hardware activation
     * does not depend on an implementation-specific first-access update. */
    UmicomRiscvPageTableEntry bits =
        UMICOM_RISCV_PTE_VALID | UMICOM_RISCV_PTE_ACCESSED;

    /* Set each architectural bit only when the caller requested it. */
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_READ) != 0U) {
        bits |= UMICOM_RISCV_PTE_READ;
    }
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != 0U) {
        bits |= UMICOM_RISCV_PTE_WRITE | UMICOM_RISCV_PTE_DIRTY;
    }
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != 0U) {
        bits |= UMICOM_RISCV_PTE_EXECUTE;
    }
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_USER) != 0U) {
        bits |= UMICOM_RISCV_PTE_USER;
    }
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_GLOBAL) != 0U) {
        bits |= UMICOM_RISCV_PTE_GLOBAL;
    }

    /* Return the complete supported leaf flag set. */
    return bits;
}

/* Convert one leaf PTE's hardware permission bits back to the public model. */
static UmicomKernelVirtualMemoryPermissions EntryBitsToPermissions(
    UmicomRiscvPageTableEntry entry
)
{
    /* Begin with no public rights and add each independently observed bit. */
    UmicomKernelVirtualMemoryPermissions permissions = 0U;

    if ((entry & UMICOM_RISCV_PTE_READ) != 0U) {
        permissions |= UMICOM_KERNEL_VIRTUAL_MEMORY_READ;
    }
    if ((entry & UMICOM_RISCV_PTE_WRITE) != 0U) {
        permissions |= UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE;
    }
    if ((entry & UMICOM_RISCV_PTE_EXECUTE) != 0U) {
        permissions |= UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE;
    }
    if ((entry & UMICOM_RISCV_PTE_USER) != 0U) {
        permissions |= UMICOM_KERNEL_VIRTUAL_MEMORY_USER;
    }
    if ((entry & UMICOM_RISCV_PTE_GLOBAL) != 0U) {
        permissions |= UMICOM_KERNEL_VIRTUAL_MEMORY_GLOBAL;
    }

    /* Publish only the abstraction's supported permission vocabulary. */
    return permissions;
}

/* Allocate one page-table frame through the physical-memory authority. */
static UmicomKernelVirtualMemoryStatus AllocatePageTable(
    UmicomAddress *outPhysicalAddress
)
{
    /* Caller-owned result storage is mandatory. */
    if (outPhysicalAddress == (UmicomAddress *)0) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Ask the physical allocator for one ordinary 4 KiB frame. */
    const UmicomKernelMemoryStatus memoryStatus =
        UmicomKernelPhysicalMemoryAllocateFrame(outPhysicalAddress);

    /* Preserve a meaningful out-of-memory distinction for callers/tests. */
    if (memoryStatus == UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY;
    }

    /* Any other physical-memory failure means the underlying authority could
     * not satisfy a valid page-table request. */
    if (memoryStatus != UMICOM_KERNEL_MEMORY_OK) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR;
    }

    /* New page tables must begin invalid; stale RAM contents could otherwise
     * be interpreted as arbitrary mappings or child-table pointers. */
    ClearPageTable(*outPhysicalAddress);

    /* The caller now owns one clean page-table frame. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

/* Return true only when every entry in one table is invalid/zero. */
static UmicomBoolean PageTableIsEmpty(UmicomAddress physicalAddress)
{
    /* Access the page directly while translation remains disabled. */
    const UmicomRiscvPageTableEntry *const table =
        PageTableFromPhysicalAddress(physicalAddress);

    /* Stop at the first live entry because one mapping/table is enough to keep
     * this intermediate page allocated. */
    for (
        UmicomSize index = (UmicomSize)0U;
        index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
        ++index
    ) {
        if (table[index] != (UmicomRiscvPageTableEntry)0U) {
            return UMICOM_FALSE;
        }
    }

    /* No entry remained live. */
    return UMICOM_TRUE;
}

UmicomBoolean UmicomKernelVirtualMemoryIsCanonical(
    UmicomAddress virtualAddress
)
{
    /* Read virtual bit 38, which acts as the Sv39 sign bit. */
    const UmicomU64 signBit =
        ((UmicomU64)virtualAddress >> UMICOM_RISCV_SV39_SIGN_BIT) & 1U;

    /* Read the twenty-five bits above the Sv39 address payload. */
    const UmicomU64 upperBits =
        (UmicomU64)virtualAddress >> UMICOM_RISCV_SV39_UPPER_SHIFT;

    /* Low canonical addresses require all upper bits to be zero. */
    if (signBit == 0U) {
        return upperBits == 0U ? UMICOM_TRUE : UMICOM_FALSE;
    }

    /* High canonical addresses require those upper bits to be all ones. */
    return upperBits == UMICOM_RISCV_SV39_UPPER_ONES
        ? UMICOM_TRUE
        : UMICOM_FALSE;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualAddressSpaceCreate(
    UmicomKernelVirtualAddressSpace *space
)
{
    /* The caller must provide the structure whose lifetime owns the tables. */
    if (space == (UmicomKernelVirtualAddressSpace *)0) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Refuse to overwrite an already-live hierarchy because that would leak
     * the currently owned root/intermediate page-table frames. */
    if (space->initialised != UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Start from a known empty public state before requesting physical RAM. */
    space->rootTablePhysicalAddress = (UmicomAddress)0U;
    space->pageTableFrames = (UmicomSize)0U;
    space->mappedPages = (UmicomSize)0U;

    /* Allocate and zero the level-2 root table. */
    const UmicomKernelVirtualMemoryStatus status =
        AllocatePageTable(&space->rootTablePhysicalAddress);

    /* Leave the structure visibly inactive if allocation failed. */
    if (status != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return status;
    }

    /* Account for the one root page-table frame now owned by this space. */
    space->pageTableFrames = (UmicomSize)1U;

    /* Publish initialisation only after every required field is valid. */
    space->initialised = UMICOM_TRUE;

    /* The address space now owns a valid but completely empty Sv39 root. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

/* Ensure one non-leaf entry points to a valid child table, allocating the
 * table transactionally when the entry is currently invalid. */
static UmicomKernelVirtualMemoryStatus EnsureChildTable(
    UmicomRiscvPageTableEntry *entry,
    UmicomAddress *outChildPhysicalAddress,
    UmicomBoolean *outCreated
)
{
    /* Every output is required because the mapper needs both the address and
     * rollback ownership information. */
    if (
        entry == (UmicomRiscvPageTableEntry *)0 ||
        outChildPhysicalAddress == (UmicomAddress *)0 ||
        outCreated == (UmicomBoolean *)0
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Default to reusing existing state until a new frame is actually created. */
    *outCreated = UMICOM_FALSE;

    /* An invalid entry has no child table yet. */
    if (EntryIsValid(*entry) == UMICOM_FALSE) {
        /* Request one clean table frame from the physical allocator. */
        UmicomAddress childPhysicalAddress = (UmicomAddress)0U;
        const UmicomKernelVirtualMemoryStatus status =
            AllocatePageTable(&childPhysicalAddress);

        /* Propagate allocation failure without publishing a half-created PTE. */
        if (status != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
            return status;
        }

        /* Encode the child physical page number and mark only V.  R/W/X remain
         * zero, which is the Sv39 signature for a next-level table pointer. */
        *entry =
            PhysicalAddressToEntryPpn(childPhysicalAddress) |
            UMICOM_RISCV_PTE_VALID;

        /* Publish the exact child address to the caller. */
        *outChildPhysicalAddress = childPhysicalAddress;

        /* Tell the caller it owns rollback responsibility for this new frame. */
        *outCreated = UMICOM_TRUE;

        /* The non-leaf child is now ready for the next walk level. */
        return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
    }

    /* A leaf at an intermediate level would be a large page.  This first page
     * table implementation deliberately supports only 4 KiB leaves. */
    if (EntryIsLeaf(*entry) != UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE;
    }

    /* Existing non-leaf entries must contain no access permission bits. */
    if ((*entry & UMICOM_RISCV_PTE_PERMISSION_MASK) != 0U) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
    }

    /* Decode the existing child page-table frame. */
    const UmicomAddress childPhysicalAddress =
        EntryToPhysicalAddress(*entry);

    /* Every page table must itself begin on a 4 KiB frame boundary. */
    if (
        (childPhysicalAddress &
         (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - (UmicomSize)1U)) != 0U
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
    }

    /* Publish the reused child address without taking rollback ownership. */
    *outChildPhysicalAddress = childPhysicalAddress;

    /* Existing state is ready for continued page-table traversal. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryMapPage(
    UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress,
    UmicomAddress physicalAddress,
    UmicomKernelVirtualMemoryPermissions permissions
)
{
    /* Mapping requires one valid, live address-space owner. */
    if (
        space == (UmicomKernelVirtualAddressSpace *)0 ||
        space->initialised == UMICOM_FALSE
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Sv39 cannot represent arbitrary 64-bit virtual bit patterns. */
    if (UmicomKernelVirtualMemoryIsCanonical(virtualAddress) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_NON_CANONICAL_ADDRESS;
    }

    /* Mapping granularity is exactly one 4 KiB page; both endpoints must be
     * page aligned so no caller accidentally loses low address bits. */
    const UmicomAddress pageMask =
        (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - (UmicomSize)1U);

    if (
        (virtualAddress & pageMask) != 0U ||
        (physicalAddress & pageMask) != 0U
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ALIGNMENT;
    }

    /* Reject unsupported/architecturally reserved access-right combinations. */
    if (PermissionsAreValid(permissions) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PERMISSIONS;
    }

    /* Obtain the level-2 root page that belongs to this address space. */
    UmicomRiscvPageTableEntry *const rootTable =
        PageTableFromPhysicalAddress(space->rootTablePhysicalAddress);

    /* Select the level-2 entry using virtual bits 38..30. */
    const UmicomSize level2Index = VirtualPageIndex(virtualAddress, (UmicomU32)2U);

    /* Track whether this operation created a level-1 table so failures later in
     * the walk can return all temporary ownership to the physical allocator. */
    UmicomBoolean createdLevel1 = UMICOM_FALSE;
    UmicomAddress level1PhysicalAddress = (UmicomAddress)0U;

    UmicomKernelVirtualMemoryStatus status = EnsureChildTable(
        &rootTable[level2Index],
        &level1PhysicalAddress,
        &createdLevel1
    );

    /* Stop immediately if the root entry could not provide a legal child. */
    if (status != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return status;
    }

    /* Count the newly allocated intermediate table only after it is linked. */
    if (createdLevel1 != UMICOM_FALSE) {
        ++space->pageTableFrames;
    }

    /* Walk the new/existing level-1 table. */
    UmicomRiscvPageTableEntry *const level1Table =
        PageTableFromPhysicalAddress(level1PhysicalAddress);

    /* Select virtual bits 29..21. */
    const UmicomSize level1Index = VirtualPageIndex(virtualAddress, (UmicomU32)1U);

    /* Track possible creation of the level-0 table independently. */
    UmicomBoolean createdLevel0 = UMICOM_FALSE;
    UmicomAddress level0PhysicalAddress = (UmicomAddress)0U;

    status = EnsureChildTable(
        &level1Table[level1Index],
        &level0PhysicalAddress,
        &createdLevel0
    );

    /* If level-0 setup fails, remove a level-1 table created exclusively by
     * this mapping attempt so the failed operation does not leak a frame. */
    if (status != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        if (createdLevel1 != UMICOM_FALSE) {
            rootTable[level2Index] = (UmicomRiscvPageTableEntry)0U;
            (void)UmicomKernelPhysicalMemoryFreeFrame(level1PhysicalAddress);
            --space->pageTableFrames;
        }
        return status;
    }

    /* Account for a newly allocated level-0 table after successful linkage. */
    if (createdLevel0 != UMICOM_FALSE) {
        ++space->pageTableFrames;
    }

    /* Select the final 4 KiB leaf slot using virtual bits 20..12. */
    UmicomRiscvPageTableEntry *const level0Table =
        PageTableFromPhysicalAddress(level0PhysicalAddress);
    const UmicomSize level0Index = VirtualPageIndex(virtualAddress, (UmicomU32)0U);

    /* Overwriting a live leaf/table silently would destroy an existing mapping
     * or corrupt the hierarchy, so duplicate mapping is refused. */
    if (EntryIsValid(level0Table[level0Index]) != UMICOM_FALSE) {
        /* Reclaim only intermediate tables that this failed operation created. */
        if (createdLevel0 != UMICOM_FALSE) {
            level1Table[level1Index] = (UmicomRiscvPageTableEntry)0U;
            (void)UmicomKernelPhysicalMemoryFreeFrame(level0PhysicalAddress);
            --space->pageTableFrames;
        }
        if (
            createdLevel1 != UMICOM_FALSE &&
            PageTableIsEmpty(level1PhysicalAddress) != UMICOM_FALSE
        ) {
            rootTable[level2Index] = (UmicomRiscvPageTableEntry)0U;
            (void)UmicomKernelPhysicalMemoryFreeFrame(level1PhysicalAddress);
            --space->pageTableFrames;
        }
        return UMICOM_KERNEL_VIRTUAL_MEMORY_ALREADY_MAPPED;
    }

    /* Construct the supported 4 KiB leaf from physical page number + rights. */
    level0Table[level0Index] =
        PhysicalAddressToEntryPpn(physicalAddress) |
        PermissionsToEntryBits(permissions);

    /* One additional virtual page is now represented by the hierarchy. */
    ++space->mappedPages;

    /* The mapping is complete and all newly allocated tables are owned by space. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

/* Walk from root to level-0 while returning each table/entry needed by unmap. */
static UmicomKernelVirtualMemoryStatus LocateLeaf(
    const UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress,
    UmicomRiscvPageTableEntry **outRootTable,
    UmicomSize *outLevel2Index,
    UmicomAddress *outLevel1PhysicalAddress,
    UmicomRiscvPageTableEntry **outLevel1Table,
    UmicomSize *outLevel1Index,
    UmicomAddress *outLevel0PhysicalAddress,
    UmicomRiscvPageTableEntry **outLevel0Table,
    UmicomSize *outLevel0Index
)
{
    /* The helper is internal, so every output is required to keep unmap logic
     * explicit rather than hiding ownership in globals. */
    if (
        space == (const UmicomKernelVirtualAddressSpace *)0 ||
        space->initialised == UMICOM_FALSE ||
        outRootTable == (UmicomRiscvPageTableEntry **)0 ||
        outLevel2Index == (UmicomSize *)0 ||
        outLevel1PhysicalAddress == (UmicomAddress *)0 ||
        outLevel1Table == (UmicomRiscvPageTableEntry **)0 ||
        outLevel1Index == (UmicomSize *)0 ||
        outLevel0PhysicalAddress == (UmicomAddress *)0 ||
        outLevel0Table == (UmicomRiscvPageTableEntry **)0 ||
        outLevel0Index == (UmicomSize *)0
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Reject a bit pattern Sv39 cannot legally translate. */
    if (UmicomKernelVirtualMemoryIsCanonical(virtualAddress) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_NON_CANONICAL_ADDRESS;
    }

    /* Root table and index identify the first walk step. */
    UmicomRiscvPageTableEntry *const rootTable =
        PageTableFromPhysicalAddress(space->rootTablePhysicalAddress);
    const UmicomSize level2Index = VirtualPageIndex(virtualAddress, (UmicomU32)2U);
    const UmicomRiscvPageTableEntry level2Entry = rootTable[level2Index];

    /* Missing root entry means the virtual address has no mapping. */
    if (EntryIsValid(level2Entry) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED;
    }

    /* A root leaf would be a 1 GiB mapping, outside this supported subset. */
    if (EntryIsLeaf(level2Entry) != UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE;
    }

    /* Decode and access the level-1 table. */
    const UmicomAddress level1PhysicalAddress = EntryToPhysicalAddress(level2Entry);
    UmicomRiscvPageTableEntry *const level1Table =
        PageTableFromPhysicalAddress(level1PhysicalAddress);
    const UmicomSize level1Index = VirtualPageIndex(virtualAddress, (UmicomU32)1U);
    const UmicomRiscvPageTableEntry level1Entry = level1Table[level1Index];

    /* Missing level-1 entry also means no mapping exists. */
    if (EntryIsValid(level1Entry) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED;
    }

    /* A level-1 leaf would be a 2 MiB mapping, intentionally unsupported here. */
    if (EntryIsLeaf(level1Entry) != UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE;
    }

    /* Decode and access the final level-0 table. */
    const UmicomAddress level0PhysicalAddress = EntryToPhysicalAddress(level1Entry);
    UmicomRiscvPageTableEntry *const level0Table =
        PageTableFromPhysicalAddress(level0PhysicalAddress);
    const UmicomSize level0Index = VirtualPageIndex(virtualAddress, (UmicomU32)0U);

    /* Publish the complete path to the caller. */
    *outRootTable = rootTable;
    *outLevel2Index = level2Index;
    *outLevel1PhysicalAddress = level1PhysicalAddress;
    *outLevel1Table = level1Table;
    *outLevel1Index = level1Index;
    *outLevel0PhysicalAddress = level0PhysicalAddress;
    *outLevel0Table = level0Table;
    *outLevel0Index = level0Index;

    /* The caller can now inspect the final entry itself. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryTranslate(
    const UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress,
    UmicomAddress *outPhysicalAddress,
    UmicomKernelVirtualMemoryPermissions *outPermissions
)
{
    /* Translation requires caller-owned output for the resulting address. */
    if (outPhysicalAddress == (UmicomAddress *)0) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Reuse the same walk logic as unmap so both operations interpret tables
     * identically. */
    UmicomRiscvPageTableEntry *rootTable;
    UmicomSize level2Index;
    UmicomAddress level1PhysicalAddress;
    UmicomRiscvPageTableEntry *level1Table;
    UmicomSize level1Index;
    UmicomAddress level0PhysicalAddress;
    UmicomRiscvPageTableEntry *level0Table;
    UmicomSize level0Index;

    const UmicomKernelVirtualMemoryStatus locateStatus = LocateLeaf(
        space,
        virtualAddress,
        &rootTable,
        &level2Index,
        &level1PhysicalAddress,
        &level1Table,
        &level1Index,
        &level0PhysicalAddress,
        &level0Table,
        &level0Index
    );

    /* Silence intentionally unused path components after the common walk. */
    (void)rootTable;
    (void)level2Index;
    (void)level1PhysicalAddress;
    (void)level1Table;
    (void)level1Index;
    (void)level0PhysicalAddress;

    /* Propagate absence/non-canonical/large-page results exactly. */
    if (locateStatus != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return locateStatus;
    }

    /* The final slot must contain a valid leaf, not another child table. */
    const UmicomRiscvPageTableEntry leafEntry = level0Table[level0Index];
    if (EntryIsValid(leafEntry) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED;
    }
    if (EntryIsLeaf(leafEntry) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
    }

    /* Recover the mapped physical page base. */
    const UmicomAddress physicalPageBase = EntryToPhysicalAddress(leafEntry);

    /* Preserve the original byte offset inside the 4 KiB virtual page. */
    const UmicomAddress pageOffset =
        virtualAddress & (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - (UmicomSize)1U);

    /* The page base is aligned, so this addition cannot cross more than one
     * frame; use checked arithmetic anyway to keep the general safety rule. */
    if (
        UmicomKernelAddressAddChecked(
            physicalPageBase,
            (UmicomSize)pageOffset,
            outPhysicalAddress
        ) == UMICOM_FALSE
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
    }

    /* Permission output is optional because many callers need only translation. */
    if (outPermissions != (UmicomKernelVirtualMemoryPermissions *)0) {
        *outPermissions = EntryBitsToPermissions(leafEntry);
    }

    /* A complete 4 KiB software translation succeeded. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryUnmapPage(
    UmicomKernelVirtualAddressSpace *space,
    UmicomAddress virtualAddress
)
{
    /* Unmap operates on complete pages, so byte-offset virtual addresses are
     * rejected instead of silently rounded down. */
    const UmicomAddress pageMask =
        (UmicomAddress)(UMICOM_KERNEL_PAGE_SIZE - (UmicomSize)1U);

    if ((virtualAddress & pageMask) != 0U) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ALIGNMENT;
    }

    /* Locate the exact leaf and its two parent tables. */
    UmicomRiscvPageTableEntry *rootTable;
    UmicomSize level2Index;
    UmicomAddress level1PhysicalAddress;
    UmicomRiscvPageTableEntry *level1Table;
    UmicomSize level1Index;
    UmicomAddress level0PhysicalAddress;
    UmicomRiscvPageTableEntry *level0Table;
    UmicomSize level0Index;

    const UmicomKernelVirtualMemoryStatus locateStatus = LocateLeaf(
        space,
        virtualAddress,
        &rootTable,
        &level2Index,
        &level1PhysicalAddress,
        &level1Table,
        &level1Index,
        &level0PhysicalAddress,
        &level0Table,
        &level0Index
    );

    /* Propagate missing or malformed path results. */
    if (locateStatus != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return locateStatus;
    }

    /* The level-0 slot must contain one supported 4 KiB leaf. */
    if (EntryIsValid(level0Table[level0Index]) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED;
    }
    if (EntryIsLeaf(level0Table[level0Index]) == UMICOM_FALSE) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
    }

    /* Remove only the mapping.  The mapped data frame remains caller-owned. */
    level0Table[level0Index] = (UmicomRiscvPageTableEntry)0U;

    /* One fewer virtual page is represented by the hierarchy. */
    --space->mappedPages;

    /* Reclaim an empty level-0 table because no mapping still needs it. */
    if (PageTableIsEmpty(level0PhysicalAddress) != UMICOM_FALSE) {
        /* Disconnect the table before returning its frame to the allocator. */
        level1Table[level1Index] = (UmicomRiscvPageTableEntry)0U;

        /* Free the now-unreachable page-table frame. */
        const UmicomKernelMemoryStatus freeStatus =
            UmicomKernelPhysicalMemoryFreeFrame(level0PhysicalAddress);

        /* A failure means page-table ownership and allocator ownership disagree. */
        if (freeStatus != UMICOM_KERNEL_MEMORY_OK) {
            return UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR;
        }

        /* Public table-frame accounting follows the successful free. */
        --space->pageTableFrames;
    }

    /* If the level-1 table is now empty too, reclaim it from the root. */
    if (PageTableIsEmpty(level1PhysicalAddress) != UMICOM_FALSE) {
        /* Disconnect the empty level-1 table. */
        rootTable[level2Index] = (UmicomRiscvPageTableEntry)0U;

        /* Return its frame to the physical allocator. */
        const UmicomKernelMemoryStatus freeStatus =
            UmicomKernelPhysicalMemoryFreeFrame(level1PhysicalAddress);

        if (freeStatus != UMICOM_KERNEL_MEMORY_OK) {
            return UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR;
        }

        --space->pageTableFrames;
    }

    /* The target mapping no longer exists and unused intermediate tables were
     * reclaimed without touching the mapped data frame itself. */
    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

/* Recursively validate the supported three-level subset while counting owned
 * page-table frames and 4 KiB leaf mappings. */
static UmicomKernelVirtualMemoryStatus ValidateHierarchy(
    const UmicomKernelVirtualAddressSpace *space,
    UmicomSize *outTableFrames,
    UmicomSize *outMappedPages
)
{
    /* Internal counting outputs are mandatory. */
    if (
        space == (const UmicomKernelVirtualAddressSpace *)0 ||
        outTableFrames == (UmicomSize *)0 ||
        outMappedPages == (UmicomSize *)0
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Root ownership always contributes one page-table frame. */
    UmicomSize observedTableFrames = (UmicomSize)1U;
    UmicomSize observedMappedPages = (UmicomSize)0U;

    const UmicomRiscvPageTableEntry *const rootTable =
        PageTableFromPhysicalAddress(space->rootTablePhysicalAddress);

    /* Examine every possible level-2 root entry. */
    for (
        UmicomSize level2Index = (UmicomSize)0U;
        level2Index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
        ++level2Index
    ) {
        const UmicomRiscvPageTableEntry level2Entry = rootTable[level2Index];

        if (EntryIsValid(level2Entry) == UMICOM_FALSE) {
            continue;
        }
        if (EntryIsLeaf(level2Entry) != UMICOM_FALSE) {
            return UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE;
        }

        const UmicomAddress level1PhysicalAddress = EntryToPhysicalAddress(level2Entry);
        const UmicomRiscvPageTableEntry *const level1Table =
            PageTableFromPhysicalAddress(level1PhysicalAddress);
        ++observedTableFrames;

        /* Examine each second-level pointer. */
        for (
            UmicomSize level1Index = (UmicomSize)0U;
            level1Index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
            ++level1Index
        ) {
            const UmicomRiscvPageTableEntry level1Entry = level1Table[level1Index];

            if (EntryIsValid(level1Entry) == UMICOM_FALSE) {
                continue;
            }
            if (EntryIsLeaf(level1Entry) != UMICOM_FALSE) {
                return UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE;
            }

            const UmicomAddress level0PhysicalAddress = EntryToPhysicalAddress(level1Entry);
            const UmicomRiscvPageTableEntry *const level0Table =
                PageTableFromPhysicalAddress(level0PhysicalAddress);
            ++observedTableFrames;

            /* Level zero may contain only invalid entries or supported leaves. */
            for (
                UmicomSize level0Index = (UmicomSize)0U;
                level0Index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
                ++level0Index
            ) {
                const UmicomRiscvPageTableEntry leafEntry = level0Table[level0Index];

                if (EntryIsValid(leafEntry) == UMICOM_FALSE) {
                    continue;
                }
                if (EntryIsLeaf(leafEntry) == UMICOM_FALSE) {
                    return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
                }

                /* The architecture reserves W=1,R=0; reject corrupted leaves. */
                if (
                    (leafEntry & UMICOM_RISCV_PTE_WRITE) != 0U &&
                    (leafEntry & UMICOM_RISCV_PTE_READ) == 0U
                ) {
                    return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
                }

                ++observedMappedPages;
            }
        }
    }

    /* Publish independent recounts only after the complete tree is valid. */
    *outTableFrames = observedTableFrames;
    *outMappedPages = observedMappedPages;

    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemoryValidate(
    const UmicomKernelVirtualAddressSpace *space
)
{
    /* A live root is required before any tree validation can be meaningful. */
    if (
        space == (const UmicomKernelVirtualAddressSpace *)0 ||
        space->initialised == UMICOM_FALSE
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    UmicomSize observedTableFrames = (UmicomSize)0U;
    UmicomSize observedMappedPages = (UmicomSize)0U;

    const UmicomKernelVirtualMemoryStatus status = ValidateHierarchy(
        space,
        &observedTableFrames,
        &observedMappedPages
    );

    if (status != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return status;
    }

    /* Public counters must match an independent walk of the actual entries. */
    if (
        observedTableFrames != space->pageTableFrames ||
        observedMappedPages != space->mappedPages
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE;
    }

    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualMemorySnapshotRead(
    const UmicomKernelVirtualAddressSpace *space,
    UmicomKernelVirtualMemorySnapshot *outSnapshot
)
{
    /* Both the owner and output structure must be valid. */
    if (
        space == (const UmicomKernelVirtualAddressSpace *)0 ||
        space->initialised == UMICOM_FALSE ||
        outSnapshot == (UmicomKernelVirtualMemorySnapshot *)0
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Publish the root frame and the two public accounting counters. */
    outSnapshot->rootTablePhysicalAddress = space->rootTablePhysicalAddress;
    outSnapshot->pageTableFrames = space->pageTableFrames;
    outSnapshot->mappedPages = space->mappedPages;

    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

UmicomKernelVirtualMemoryStatus UmicomKernelVirtualAddressSpaceDestroy(
    UmicomKernelVirtualAddressSpace *space
)
{
    /* A caller can destroy only one live hierarchy. */
    if (
        space == (UmicomKernelVirtualAddressSpace *)0 ||
        space->initialised == UMICOM_FALSE
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT;
    }

    /* Refuse partial teardown when the tree is already inconsistent. */
    const UmicomKernelVirtualMemoryStatus validationStatus =
        UmicomKernelVirtualMemoryValidate(space);

    if (validationStatus != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return validationStatus;
    }

    UmicomRiscvPageTableEntry *const rootTable =
        PageTableFromPhysicalAddress(space->rootTablePhysicalAddress);

    /* Free only page-table frames.  Leaf data frames remain caller-owned. */
    for (
        UmicomSize level2Index = (UmicomSize)0U;
        level2Index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
        ++level2Index
    ) {
        const UmicomRiscvPageTableEntry level2Entry = rootTable[level2Index];
        if (EntryIsValid(level2Entry) == UMICOM_FALSE) {
            continue;
        }

        const UmicomAddress level1PhysicalAddress = EntryToPhysicalAddress(level2Entry);
        UmicomRiscvPageTableEntry *const level1Table =
            PageTableFromPhysicalAddress(level1PhysicalAddress);

        for (
            UmicomSize level1Index = (UmicomSize)0U;
            level1Index < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
            ++level1Index
        ) {
            const UmicomRiscvPageTableEntry level1Entry = level1Table[level1Index];
            if (EntryIsValid(level1Entry) == UMICOM_FALSE) {
                continue;
            }

            /* Validation proved every level-1 entry is a non-leaf child table. */
            const UmicomAddress level0PhysicalAddress =
                EntryToPhysicalAddress(level1Entry);

            /* Count this child's mappings while we still own its memory.
             * Destruction can stop at any failed frame release. Updating the
             * public counts after each successful detach keeps the remaining
             * hierarchy valid for a later retry, rather than describing pages
             * that an earlier partial attempt already returned to the allocator.
             * These are mapping counts only; leaf data remains caller-owned. */
            const UmicomRiscvPageTableEntry *const retiringTable =
                PageTableFromPhysicalAddress(level0PhysicalAddress);
            UmicomSize retiringMappings = (UmicomSize)0U;
            for (UmicomSize entryIndex = (UmicomSize)0U;
                 entryIndex < UMICOM_KERNEL_SV39_PAGE_TABLE_ENTRIES;
                 ++entryIndex) {
                if (EntryIsValid(retiringTable[entryIndex]) != UMICOM_FALSE) {
                    ++retiringMappings;
                }
            }

            /* Return the level-0 page-table frame to physical memory. */
            if (
                UmicomKernelPhysicalMemoryFreeFrame(level0PhysicalAddress) !=
                UMICOM_KERNEL_MEMORY_OK
            ) {
                return UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR;
            }

            /* Disconnect the released child so no stale table pointer remains. */
            level1Table[level1Index] = (UmicomRiscvPageTableEntry)0U;
            /* Publish accounting only after the successful free and detach.
             * A failed free above leaves both the edge and counters untouched. */
            --space->pageTableFrames;
            space->mappedPages -= retiringMappings;
        }

        /* Return the now-empty level-1 page table. */
        if (
            UmicomKernelPhysicalMemoryFreeFrame(level1PhysicalAddress) !=
            UMICOM_KERNEL_MEMORY_OK
        ) {
            return UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR;
        }

        /* Disconnect it from the root after successful release. */
        rootTable[level2Index] = (UmicomRiscvPageTableEntry)0U;
        /* A root-free refusal must leave an empty, one-frame hierarchy whose
         * counters still match an independent walk on the next attempt. */
        --space->pageTableFrames;
    }

    /* Finally return the root frame itself. */
    if (
        UmicomKernelPhysicalMemoryFreeFrame(space->rootTablePhysicalAddress) !=
        UMICOM_KERNEL_MEMORY_OK
    ) {
        return UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR;
    }

    /* Reset every public field so accidental reuse is visibly uninitialised. */
    space->rootTablePhysicalAddress = (UmicomAddress)0U;
    space->pageTableFrames = (UmicomSize)0U;
    space->mappedPages = (UmicomSize)0U;
    space->initialised = UMICOM_FALSE;

    return UMICOM_KERNEL_VIRTUAL_MEMORY_OK;
}

const char *UmicomKernelVirtualMemoryStatusName(
    UmicomKernelVirtualMemoryStatus status
)
{
    /* Stable names keep serial tests readable without printf or enum reflection. */
    switch (status) {
        case UMICOM_KERNEL_VIRTUAL_MEMORY_OK:
            return "ok";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ARGUMENT:
            return "invalid-argument";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ALIGNMENT:
            return "invalid-alignment";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_NON_CANONICAL_ADDRESS:
            return "non-canonical-address";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PERMISSIONS:
            return "invalid-permissions";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_ALREADY_MAPPED:
            return "already-mapped";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED:
            return "not-mapped";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY:
            return "out-of-memory";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE:
            return "corrupt-page-table";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE:
            return "unsupported-large-page";
        case UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR:
            return "physical-memory-error";
        default:
            return "unknown-status";
    }
}
