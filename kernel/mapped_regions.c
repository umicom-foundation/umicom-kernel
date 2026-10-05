/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/mapped_regions.c
 *
 * PURPOSE:
 *   Join virtual reservations, physical backing and page-table ownership into
 *   one checked lifetime. Mapping and destruction still use the existing VM.
 *
 * EDUCATIONAL OVERVIEW:
 *   Planning is cheap and does not acquire RAM. Build creates an unpublished
 *   hierarchy, copies initial bytes into zeroed frames and then audits it.
 *   Once published the layout is immutable. A borrow pins the whole hierarchy;
 *   Close is allowed only after that borrow has been returned.
 *
 *   Table addresses are recorded only immediately after this owner's trusted
 *   mapper call, never by a public validation request. Ordinary audits require
 *   every child pointer to be in that ledger before touching the child. This
 *   catches a redirected PTE without walking unrelated memory. It does not make
 *   writable Kernel metadata safe against arbitrary machine-mode corruption.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/mapped_regions.h"
#include "umicom/kernel/address.h"

/* The owner deliberately accepts a stricter subset than the low-level mapper:
 * readable leaves, no global mappings, no large pages and never WRITE+EXECUTE. */
#define UMICOM_REGION_PAGE ((UmicomSize)4096U)
#define UMICOM_REGION_PTE_PPN ((UmicomU64)0x003ffffffffffc00ULL)
#define UMICOM_REGION_PTE_FLAGS ((UmicomU64)0xffU)

static UmicomBoolean UmicomMappedBoolean(UmicomBoolean value)
{
    return value == UMICOM_FALSE || value == UMICOM_TRUE ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomMappedOutside(const void *pointer, UmicomSize bytes,
    UmicomAddress begin, UmicomSize extent)
{
    /* An empty span performs no access. For nonempty spans, both endpoints must
     * be representable before interval comparisons are meaningful. */
    if (bytes == 0U) return UMICOM_TRUE;
    const UmicomAddress address = (UmicomAddress)(UmicomUIntPtr)pointer;
    if (address == 0U || bytes > ~(UmicomAddress)0U - address ||
        extent > ~(UmicomAddress)0U - begin) return UMICOM_FALSE;
    return address + bytes <= begin || address >= begin + extent ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomMappedExternal(const UmicomKernelMappedSpace *space,
    const void *pointer, UmicomSize bytes)
{
    /* A caller must not overwrite metadata by using it as an output buffer. */
    return UmicomMappedOutside(pointer, bytes, (UmicomAddress)(UmicomUIntPtr)space, sizeof(*space));
}
static UmicomKernelMappedStatus UmicomMappedGate(UmicomKernelMappedSpace *space)
{
    if (space == (UmicomKernelMappedSpace *)0) return UMICOM_MAPPED_INVALID_ARGUMENT;
    if (UmicomKernelMappedSpaceAccessAllowed() == UMICOM_FALSE) return UMICOM_MAPPED_UNSAFE_CONTEXT;
    if (space->self != space || space->state <= UMICOM_MAPPED_EMPTY ||
        space->state > UMICOM_MAPPED_POISONED) return UMICOM_MAPPED_BAD_STATE;
    if (space->state == UMICOM_MAPPED_POISONED) return UMICOM_MAPPED_CORRUPT_STATE;
    return UMICOM_MAPPED_OK;
}
static UmicomKernelMappedStatus UmicomMappedPoison(UmicomKernelMappedSpace *space)
{
    /* Do not interpret suspect addresses as permission to free unrelated RAM. */
    space->state = UMICOM_MAPPED_POISONED;
    return UMICOM_MAPPED_CORRUPT_STATE;
}
static UmicomBoolean UmicomMappedAllocated(UmicomAddress frame)
{
    UmicomKernelPhysicalFrameState state = UMICOM_PHYSICAL_FRAME_FREE;
    return (frame & (UMICOM_REGION_PAGE - 1U)) == 0U && frame != 0U &&
        UmicomKernelPhysicalMemoryFrameQuery(frame, &state) == UMICOM_KERNEL_MEMORY_OK &&
        state == UMICOM_PHYSICAL_FRAME_ALLOCATED ? UMICOM_TRUE : UMICOM_FALSE;
}
static void UmicomMappedClear(UmicomAddress frame)
{
    /* Volatile byte stores keep this freestanding scrub independent of libc. */
    volatile UmicomU8 *const data = (volatile UmicomU8 *)(UmicomUIntPtr)frame;
    for (UmicomSize index = 0U; index < UMICOM_REGION_PAGE; ++index) data[index] = 0U;
}
static UmicomBoolean UmicomMappedGeometry(const UmicomKernelMappedRegionSpec *spec,
    UmicomAddress *begin, UmicomAddress *end)
{
    if (spec->pages == 0U || spec->pages > UMICOM_MAPPED_REGION_PAGE_LIMIT ||
        (spec->base & (UMICOM_REGION_PAGE - 1U)) != 0U || spec->base == 0U ||
        UmicomMappedBoolean(spec->guardBelow) == UMICOM_FALSE ||
        UmicomMappedBoolean(spec->guardAbove) == UMICOM_FALSE) return UMICOM_FALSE;
    const UmicomSize below = spec->guardBelow != UMICOM_FALSE ? UMICOM_REGION_PAGE : 0U;
    const UmicomSize above = spec->guardAbove != UMICOM_FALSE ? UMICOM_REGION_PAGE : 0U;
    const UmicomSize span = spec->pages * UMICOM_REGION_PAGE + above;
    if (spec->base < below || span > ~(UmicomAddress)0U - spec->base) return UMICOM_FALSE;
    *begin = spec->base - below;
    *end = spec->base + span;
    /* Endpoint canonicality alone must not permit an interval across the Sv39
     * hole. The entire reservation must remain in the same canonical half. */
    return UmicomKernelVirtualMemoryIsCanonical(*begin) != UMICOM_FALSE &&
        UmicomKernelVirtualMemoryIsCanonical(*end - 1U) != UMICOM_FALSE &&
        ((*begin >> 38U) & 1U) == (((*end - 1U) >> 38U) & 1U)
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomMappedPermissions(UmicomKernelVirtualMemoryPermissions value)
{
    const UmicomU32 known = UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE |
        UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE | UMICOM_KERNEL_VIRTUAL_MEMORY_USER;
    return (value & ~known) == 0U && (value & UMICOM_KERNEL_VIRTUAL_MEMORY_READ) != 0U &&
        (value & (UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE | UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE)) !=
        (UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE | UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE)
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomU64 UmicomMappedLeafBits(UmicomKernelVirtualMemoryPermissions permissions)
{
    /* V and A are set in every leaf. Writable leaves also start dirty, matching
     * the established mapper and avoiding a first-access update requirement. */
    UmicomU64 bits = 0x43U;
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != 0U) bits |= 0x84U;
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != 0U) bits |= 0x08U;
    if ((permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_USER) != 0U) bits |= 0x10U;
    return bits;
}
static UmicomBoolean UmicomMappedIsData(const UmicomKernelMappedSpace *space, UmicomAddress frame)
{
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        for (UmicomSize page = 0U; page < space->regions[region].spec.pages; ++page) {
            if (space->regions[region].frames[page] == frame) return UMICOM_TRUE;
        }
    }
    return UMICOM_FALSE;
}
static UmicomBoolean UmicomMappedBuffer(const UmicomKernelMappedSpace *space,
    const void *buffer, UmicomSize bytes)
{
    /* Copying to an alias of a backing page could change later source bytes or
     * tables mid-operation. Reject aliases rather than invent memmove policy. */
    if (UmicomMappedExternal(space, buffer, bytes) == UMICOM_FALSE) return UMICOM_FALSE;
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        for (UmicomSize page = 0U; page < space->regions[region].spec.pages; ++page) {
            const UmicomAddress frame = space->regions[region].frames[page];
            if (frame != 0U && UmicomMappedOutside(buffer, bytes, frame, UMICOM_REGION_PAGE) == UMICOM_FALSE)
                return UMICOM_FALSE;
        }
    }
    for (UmicomSize table = 0U; table < space->tableCount; ++table) {
        if (space->tables[table].live != UMICOM_FALSE &&
            UmicomMappedOutside(buffer, bytes, space->tables[table].frame, UMICOM_REGION_PAGE) == UMICOM_FALSE)
            return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}

/* A separate, bounded walker audits provenance and exact leaves. It does NOT
 * create, translate or destroy mappings; those operations use virtual_memory.c. */
typedef struct UmicomMappedWalk {
    UmicomKernelMappedSpace *space;
    UmicomBoolean capture;
    UmicomU8 visited[UMICOM_MAPPED_SPACE_TABLE_LIMIT];
    UmicomSize tables;
    UmicomSize leaves;
} UmicomMappedWalk;
static UmicomBoolean UmicomMappedWalkTable(UmicomMappedWalk *walk, UmicomAddress address,
    UmicomU32 level, UmicomU64 prefix)
{
    UmicomKernelMappedSpace *const space = walk->space;
    UmicomSize slot = 0U;
    for (; slot < space->tableCount; ++slot) {
        if (space->tables[slot].frame == address) break;
    }
    if (slot == space->tableCount) {
        /* Only Build can capture, immediately after a private, trusted mapper
         * call. Public Validate never "learns" an address from a suspect PTE. */
        if (walk->capture == UMICOM_FALSE || slot == UMICOM_MAPPED_SPACE_TABLE_LIMIT ||
            UmicomMappedIsData(space, address) != UMICOM_FALSE ||
            UmicomMappedAllocated(address) == UMICOM_FALSE ||
            UmicomMappedExternal(space, (const void *)(UmicomUIntPtr)address, UMICOM_REGION_PAGE) == UMICOM_FALSE)
            return UMICOM_FALSE;
        space->tables[slot].frame = address;
        space->tables[slot].prefix = prefix;
        space->tables[slot].level = level;
        space->tables[slot].live = UMICOM_TRUE;
        ++space->tableCount;
    }
    const UmicomKernelMappedTableRecord *const record = &space->tables[slot];
    /* Ledger membership is tested before dereferencing. Prefix and level make
     * an alias to another OWN table just as invalid as an unrelated pointer. */
    if (record->live != UMICOM_TRUE || record->level != level || record->prefix != prefix ||
        walk->visited[slot] != 0U || UmicomMappedAllocated(address) == UMICOM_FALSE) return UMICOM_FALSE;
    walk->visited[slot] = 1U;
    ++walk->tables;
    const UmicomU64 *const table = (const UmicomU64 *)(UmicomUIntPtr)address;
    for (UmicomSize index = 0U; index < 512U; ++index) {
        const UmicomU64 entry = table[index];
        if (entry == 0U) continue;
        if ((entry & 1U) == 0U || (entry & ~(UMICOM_REGION_PTE_PPN | UMICOM_REGION_PTE_FLAGS)) != 0U)
            return UMICOM_FALSE;
        const UmicomAddress target = (UmicomAddress)((entry & UMICOM_REGION_PTE_PPN) << 2U);
        const UmicomU64 nextPrefix = prefix | ((UmicomU64)index << (12U + level * 9U));
        if (level != 0U) {
            /* Intermediate entries contain V only. This rejects large-page
             * leaves, reserved permission bits and accidental user table flags. */
            if ((entry & 0x3ffU) != 1U ||
                UmicomMappedWalkTable(walk, target, level - 1U, nextPrefix) == UMICOM_FALSE) return UMICOM_FALSE;
        } else {
            const UmicomAddress virtualAddress = (UmicomAddress)((nextPrefix & (1ULL << 38U)) != 0U
                ? nextPrefix | 0xffffff8000000000ULL : nextPrefix);
            UmicomBoolean found = UMICOM_FALSE;
            for (UmicomSize region = 0U; region < space->regionCount; ++region) {
                const UmicomKernelMappedRegionRecord *const data = &space->regions[region];
                const UmicomSize bytes = data->spec.pages * UMICOM_REGION_PAGE;
                if (virtualAddress >= data->spec.base && virtualAddress - data->spec.base < bytes) {
                    const UmicomSize page = (virtualAddress - data->spec.base) / UMICOM_REGION_PAGE;
                    if (data->frames[page] != target ||
                        (entry & 0x3ffU) != UmicomMappedLeafBits(data->spec.permissions)) return UMICOM_FALSE;
                    found = UMICOM_TRUE;
                    break;
                }
            }
            /* Guards and holes have no data record. Any leaf in them is wrong. */
            if (found == UMICOM_FALSE) return UMICOM_FALSE;
            ++walk->leaves;
        }
    }
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomMappedAudit(UmicomKernelMappedSpace *space,
    UmicomBoolean capture, UmicomBoolean sparse)
{
    if (space->regionCount > UMICOM_MAPPED_REGION_LIMIT || space->tableCount > UMICOM_MAPPED_SPACE_TABLE_LIMIT ||
        space->plannedPages > UMICOM_MAPPED_SPACE_PAGE_LIMIT) return UMICOM_FALSE;
    if (UmicomMappedBoolean(space->hierarchy.initialised) == UMICOM_FALSE ||
        ((space->state == UMICOM_MAPPED_BORROWED) != (space->activeTicket != 0U))) return UMICOM_FALSE;
    if ((space->state == UMICOM_MAPPED_PLANNING || space->state == UMICOM_MAPPED_CLOSED) &&
        (space->hierarchy.initialised != UMICOM_FALSE || space->tableCount != 0U)) return UMICOM_FALSE;
    if ((space->state == UMICOM_MAPPED_READY || space->state == UMICOM_MAPPED_BORROWED) &&
        space->hierarchy.initialised != UMICOM_TRUE) return UMICOM_FALSE;
    UmicomSize planned = 0U;
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        const UmicomKernelMappedRegionRecord *const record = &space->regions[region];
        UmicomAddress begin = 0U;
        UmicomAddress end = 0U;
        if (UmicomMappedGeometry(&record->spec, &begin, &end) == UMICOM_FALSE ||
            UmicomMappedPermissions(record->spec.permissions) == UMICOM_FALSE ||
            record->reservedBegin != begin || record->reservedEnd != end ||
            record->spec.initialBytes > record->spec.pages * UMICOM_REGION_PAGE ||
            UmicomMappedExternal(space, record->spec.initialData, record->spec.initialBytes) == UMICOM_FALSE)
            return UMICOM_FALSE;
        planned += record->spec.pages;
        for (UmicomSize older = 0U; older < region; ++older) {
            if (begin < space->regions[older].reservedEnd && end > space->regions[older].reservedBegin)
                return UMICOM_FALSE;
        }
        for (UmicomSize page = 0U; page < record->spec.pages; ++page) {
            const UmicomAddress frame = record->frames[page];
            if (frame == 0U) {
                if (sparse == UMICOM_FALSE) return UMICOM_FALSE;
                continue;
            }
            if (space->state == UMICOM_MAPPED_PLANNING || space->state == UMICOM_MAPPED_CLOSED)
                return UMICOM_FALSE;
            if (UmicomMappedAllocated(frame) == UMICOM_FALSE ||
                UmicomMappedExternal(space, (const void *)(UmicomUIntPtr)frame, UMICOM_REGION_PAGE) == UMICOM_FALSE)
                return UMICOM_FALSE;
            for (UmicomSize table = 0U; table < space->tableCount; ++table) {
                if (space->tables[table].live != UMICOM_FALSE && space->tables[table].frame == frame)
                    return UMICOM_FALSE;
            }
            for (UmicomSize older = 0U; older <= region; ++older) {
                const UmicomSize limit = older == region ? page : space->regions[older].spec.pages;
                for (UmicomSize item = 0U; item < limit; ++item) {
                    if (space->regions[older].frames[item] == frame) return UMICOM_FALSE;
                }
            }
        }
    }
    if (planned != space->plannedPages) return UMICOM_FALSE;
    UmicomMappedWalk walk;
    walk.space = space;
    walk.capture = capture;
    walk.tables = 0U;
    walk.leaves = 0U;
    for (UmicomSize index = 0U; index < UMICOM_MAPPED_SPACE_TABLE_LIMIT; ++index) walk.visited[index] = 0U;
    if (space->hierarchy.initialised != UMICOM_FALSE) {
        if (space->tableCount == 0U && capture == UMICOM_FALSE) return UMICOM_FALSE;
        if (space->tableCount != 0U && space->tables[0].frame != space->hierarchy.rootTablePhysicalAddress)
            return UMICOM_FALSE;
        if (UmicomMappedWalkTable(&walk, space->hierarchy.rootTablePhysicalAddress, 2U, 0U) == UMICOM_FALSE)
            return UMICOM_FALSE;
    } else if (space->hierarchy.rootTablePhysicalAddress != 0U) return UMICOM_FALSE;
    for (UmicomSize index = 0U; index < space->tableCount; ++index) {
        if ((space->tables[index].live != UMICOM_FALSE) != (walk.visited[index] != 0U)) return UMICOM_FALSE;
    }
    return walk.tables == space->hierarchy.pageTableFrames && walk.leaves == space->hierarchy.mappedPages &&
        (sparse != UMICOM_FALSE || walk.leaves == planned) ? UMICOM_TRUE : UMICOM_FALSE;
}

UmicomKernelMappedStatus UmicomKernelMappedSpaceInitialize(UmicomKernelMappedSpace *space)
{
    if (space == (UmicomKernelMappedSpace *)0) return UMICOM_MAPPED_INVALID_ARGUMENT;
    if (UmicomKernelMappedSpaceAccessAllowed() == UMICOM_FALSE) return UMICOM_MAPPED_UNSAFE_CONTEXT;
    /* Require genuinely zero-filled metadata, not merely an overwritten flag. */
    const UmicomU8 *const bytes = (const UmicomU8 *)space;
    for (UmicomSize index = 0U; index < sizeof(*space); ++index) {
        if (bytes[index] != 0U) return UMICOM_MAPPED_BAD_STATE;
    }
    space->self = space;
    space->state = UMICOM_MAPPED_PLANNING;
    space->nextTicket = 1U;
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedRegionAdd(UmicomKernelMappedSpace *space,
    const UmicomKernelMappedRegionSpec *spec, UmicomKernelMappedRegionHandle *outHandle)
{
    UmicomKernelMappedStatus status = UmicomMappedGate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (spec == (const UmicomKernelMappedRegionSpec *)0 || outHandle == (UmicomKernelMappedRegionHandle *)0 ||
        UmicomMappedExternal(space, spec, sizeof(*spec)) == UMICOM_FALSE ||
        UmicomMappedExternal(space, outHandle, sizeof(*outHandle)) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    if (space->state != UMICOM_MAPPED_PLANNING) return UMICOM_MAPPED_BAD_STATE;
    if (UmicomMappedAudit(space, UMICOM_FALSE, UMICOM_TRUE) == UMICOM_FALSE) return UmicomMappedPoison(space);
    UmicomAddress begin = 0U;
    UmicomAddress end = 0U;
    if (UmicomMappedGeometry(spec, &begin, &end) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_RANGE;
    if (UmicomMappedPermissions(spec->permissions) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_PERMISSIONS;
    if (spec->initialBytes > spec->pages * UMICOM_REGION_PAGE ||
        (spec->initialBytes != 0U && spec->initialData == (const UmicomU8 *)0) ||
        UmicomMappedExternal(space, spec->initialData, spec->initialBytes) == UMICOM_FALSE)
        return UMICOM_MAPPED_INVALID_ARGUMENT;
    if (space->regionCount == UMICOM_MAPPED_REGION_LIMIT ||
        spec->pages > UMICOM_MAPPED_SPACE_PAGE_LIMIT - space->plannedPages) return UMICOM_MAPPED_CAPACITY;
    for (UmicomSize index = 0U; index < space->regionCount; ++index) {
        if (begin < space->regions[index].reservedEnd && end > space->regions[index].reservedBegin)
            return UMICOM_MAPPED_OVERLAP;
    }
    UmicomKernelMappedRegionRecord *const record = &space->regions[space->regionCount];
    record->spec = *spec;
    record->reservedBegin = begin;
    record->reservedEnd = end;
    ++space->regionCount;
    space->plannedPages += spec->pages;
    /* Plans are append-only. A handle's slot is never recycled in this domain. */
    *outHandle = (UmicomU64)space->regionCount;
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedStackAdd(UmicomKernelMappedSpace *space,
    UmicomAddress base, UmicomSize pages, UmicomBoolean user, UmicomKernelMappedRegionHandle *outHandle)
{
    if (UmicomMappedBoolean(user) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    const UmicomKernelMappedRegionSpec spec = {base, pages,
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE |
            (user != UMICOM_FALSE ? UMICOM_KERNEL_VIRTUAL_MEMORY_USER : 0U),
        UMICOM_TRUE, UMICOM_TRUE, (const UmicomU8 *)0, 0U};
    return UmicomKernelMappedRegionAdd(space, &spec, outHandle);
}

static UmicomKernelMappedStatus UmicomMappedRelease(UmicomKernelMappedSpace *space)
{
    if (UmicomMappedAudit(space, UMICOM_FALSE, UMICOM_TRUE) == UMICOM_FALSE) return UmicomMappedPoison(space);
    if (space->hierarchy.initialised != UMICOM_FALSE) {
        const UmicomKernelVirtualMemoryStatus result = UmicomKernelVirtualAddressSpaceDestroy(&space->hierarchy);
        /* The existing destructor disconnects only successfully freed tables.
         * Refresh LIVE flags after that controlled call, never by adopting any
         * new address. A free failure retains the remaining graph for retry. */
        for (UmicomSize index = 0U; index < space->tableCount; ++index) {
            if (space->tables[index].live == UMICOM_FALSE) continue;
            UmicomKernelPhysicalFrameState state = UMICOM_PHYSICAL_FRAME_RESERVED;
            if (UmicomKernelPhysicalMemoryFrameQuery(space->tables[index].frame, &state) != UMICOM_KERNEL_MEMORY_OK ||
                state == UMICOM_PHYSICAL_FRAME_RESERVED) return UmicomMappedPoison(space);
            if (state == UMICOM_PHYSICAL_FRAME_FREE) space->tables[index].live = UMICOM_FALSE;
        }
        if (UmicomMappedAudit(space, UMICOM_FALSE, UMICOM_TRUE) == UMICOM_FALSE) return UmicomMappedPoison(space);
        if (result != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) return UMICOM_MAPPED_CLEANUP_REQUIRED;
    }
    /* No remaining CPU-visible table may name a data frame when we scrub it. */
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        for (UmicomSize page = 0U; page < space->regions[region].spec.pages; ++page) {
            UmicomAddress *const frame = &space->regions[region].frames[page];
            if (*frame == 0U) continue;
            if (UmicomMappedAllocated(*frame) == UMICOM_FALSE) return UmicomMappedPoison(space);
            UmicomMappedClear(*frame);
            if (UmicomKernelPhysicalMemoryFreeFrame(*frame) != UMICOM_KERNEL_MEMORY_OK)
                return UMICOM_MAPPED_CLEANUP_REQUIRED;
            *frame = 0U; /* Publish only the successful release; retry skips it. */
        }
    }
    space->tableCount = 0U;
    return UMICOM_MAPPED_OK;
}
static UmicomKernelMappedStatus UmicomMappedRollback(UmicomKernelMappedSpace *space,
    UmicomKernelMappedStatus reason, UmicomSize previousAllocations)
{
    space->state = UMICOM_MAPPED_CLOSING;
    const UmicomKernelMappedStatus release = UmicomMappedRelease(space);
    if (release != UMICOM_MAPPED_OK) return release;
    UmicomKernelPhysicalMemorySnapshot memory;
    if (UmicomKernelPhysicalMemorySnapshotRead(&memory) != UMICOM_KERNEL_MEMORY_OK ||
        memory.allocatedFrames != previousAllocations) return UmicomMappedPoison(space);
    space->state = UMICOM_MAPPED_PLANNING;
    return reason;
}
UmicomKernelMappedStatus UmicomKernelMappedSpaceBuild(UmicomKernelMappedSpace *space)
{
    UmicomKernelMappedStatus status = UmicomMappedGate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (space->state != UMICOM_MAPPED_PLANNING || space->regionCount == 0U) return UMICOM_MAPPED_BAD_STATE;
    if (UmicomMappedAudit(space, UMICOM_FALSE, UMICOM_TRUE) == UMICOM_FALSE) return UmicomMappedPoison(space);
    UmicomKernelPhysicalMemorySnapshot before;
    if (UmicomKernelPhysicalMemorySnapshotRead(&before) != UMICOM_KERNEL_MEMORY_OK) return UmicomMappedPoison(space);
    space->state = UMICOM_MAPPED_BUILDING;
    /* Acquire each page independently. Consecutive virtual pages need not have
     * consecutive physical backing. Record each allocation before doing more. */
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        for (UmicomSize page = 0U; page < space->regions[region].spec.pages; ++page) {
            UmicomAddress *const frame = &space->regions[region].frames[page];
            if (UmicomKernelPhysicalMemoryAllocateFrame(frame) != UMICOM_KERNEL_MEMORY_OK)
                return UmicomMappedRollback(space, UMICOM_MAPPED_OUT_OF_MEMORY, before.allocatedFrames);
            UmicomMappedClear(*frame);
        }
    }
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        UmicomKernelMappedRegionRecord *const record = &space->regions[region];
        if (UmicomMappedBuffer(space, record->spec.initialData, record->spec.initialBytes) == UMICOM_FALSE)
            return UmicomMappedRollback(space, UMICOM_MAPPED_INVALID_ARGUMENT, before.allocatedFrames);
        for (UmicomSize index = 0U; index < record->spec.initialBytes; ++index) {
            volatile UmicomU8 *const page = (volatile UmicomU8 *)(UmicomUIntPtr)record->frames[index / UMICOM_REGION_PAGE];
            page[index % UMICOM_REGION_PAGE] = record->spec.initialData[index];
        }
    }
    if (UmicomKernelVirtualAddressSpaceCreate(&space->hierarchy) != UMICOM_KERNEL_VIRTUAL_MEMORY_OK)
        return UmicomMappedRollback(space, UMICOM_MAPPED_OUT_OF_MEMORY, before.allocatedFrames);
    if (UmicomMappedAudit(space, UMICOM_TRUE, UMICOM_TRUE) == UMICOM_FALSE) return UmicomMappedPoison(space);
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        const UmicomKernelMappedRegionRecord *const record = &space->regions[region];
        for (UmicomSize page = 0U; page < record->spec.pages; ++page) {
            const UmicomKernelVirtualMemoryStatus mapped = UmicomKernelVirtualMemoryMapPage(&space->hierarchy,
                record->spec.base + page * UMICOM_REGION_PAGE, record->frames[page], record->spec.permissions);
            /* Capture only the graph just produced by this owner's mapper.
             * Subsequent public operations may verify it but may not expand it. */
            if (UmicomMappedAudit(space, UMICOM_TRUE, UMICOM_TRUE) == UMICOM_FALSE) return UmicomMappedPoison(space);
            if (mapped != UMICOM_KERNEL_VIRTUAL_MEMORY_OK)
                return UmicomMappedRollback(space, mapped == UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY
                    ? UMICOM_MAPPED_OUT_OF_MEMORY : UMICOM_MAPPED_MAPPING_ERROR, before.allocatedFrames);
        }
    }
    if (UmicomMappedAudit(space, UMICOM_FALSE, UMICOM_FALSE) == UMICOM_FALSE) return UmicomMappedPoison(space);
    UmicomKernelPhysicalMemorySnapshot after;
    if (UmicomKernelPhysicalMemorySnapshotRead(&after) != UMICOM_KERNEL_MEMORY_OK ||
        after.allocatedFrames - before.allocatedFrames != space->plannedPages + space->tableCount)
        return UmicomMappedPoison(space);
    /* Publication releases every borrowed initial source. Runtime memory now
     * contains private copies; no future read follows those initial pointers. */
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        space->regions[region].spec.initialData = (const UmicomU8 *)0;
        space->regions[region].spec.initialBytes = 0U;
    }
    space->state = UMICOM_MAPPED_READY;
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedSpaceValidate(UmicomKernelMappedSpace *space)
{
    UmicomKernelMappedStatus status = UmicomMappedGate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (space->state == UMICOM_MAPPED_BUILDING || space->state == UMICOM_MAPPED_EMPTY)
        return UMICOM_MAPPED_BAD_STATE;
    const UmicomBoolean sparse = space->state == UMICOM_MAPPED_READY || space->state == UMICOM_MAPPED_BORROWED
        ? UMICOM_FALSE : UMICOM_TRUE;
    return UmicomMappedAudit(space, UMICOM_FALSE, sparse) != UMICOM_FALSE ? UMICOM_MAPPED_OK : UmicomMappedPoison(space);
}
UmicomKernelMappedStatus UmicomKernelMappedRegionQuery(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomKernelMappedRegionInfo *outInfo)
{
    UmicomKernelMappedStatus status = UmicomKernelMappedSpaceValidate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (space->state != UMICOM_MAPPED_READY && space->state != UMICOM_MAPPED_PLANNING &&
        space->state != UMICOM_MAPPED_BORROWED) return UMICOM_MAPPED_BAD_STATE;
    if (handle == 0U || handle > space->regionCount) return UMICOM_MAPPED_INVALID_HANDLE;
    if (outInfo == (UmicomKernelMappedRegionInfo *)0 ||
        UmicomMappedBuffer(space, outInfo, sizeof(*outInfo)) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    const UmicomKernelMappedRegionRecord *const record = &space->regions[handle - 1U];
    outInfo->base = record->spec.base;
    outInfo->end = record->spec.base + record->spec.pages * UMICOM_REGION_PAGE;
    outInfo->pages = record->spec.pages;
    outInfo->permissions = record->spec.permissions;
    outInfo->reservedBegin = record->reservedBegin;
    outInfo->reservedEnd = record->reservedEnd;
    return UMICOM_MAPPED_OK;
}
static UmicomKernelMappedStatus UmicomMappedCopy(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomSize offset, void *buffer, UmicomSize bytes, UmicomBoolean writing)
{
    UmicomKernelMappedStatus status = UmicomKernelMappedSpaceValidate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (space->state != UMICOM_MAPPED_READY) return UMICOM_MAPPED_BAD_STATE;
    if (handle == 0U || handle > space->regionCount) return UMICOM_MAPPED_INVALID_HANDLE;
    const UmicomKernelMappedRegionRecord *const region = &space->regions[handle - 1U];
    const UmicomSize capacity = region->spec.pages * UMICOM_REGION_PAGE;
    if (offset > capacity || bytes > capacity - offset) return UMICOM_MAPPED_INVALID_RANGE;
    if (writing != UMICOM_FALSE && (region->spec.permissions & UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) == 0U)
        return UMICOM_MAPPED_ACCESS_DENIED;
    if (UmicomMappedBuffer(space, buffer, bytes) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    /* The full span and all backing frames have passed before the first write.
     * Use owner records for the copy; public users never receive a raw frame. */
    UmicomU8 *const external = (UmicomU8 *)buffer;
    for (UmicomSize index = 0U; index < bytes; ++index) {
        const UmicomSize position = offset + index;
        volatile UmicomU8 *const data = (volatile UmicomU8 *)(UmicomUIntPtr)region->frames[position / UMICOM_REGION_PAGE];
        if (writing != UMICOM_FALSE) data[position % UMICOM_REGION_PAGE] = external[index];
        else external[index] = data[position % UMICOM_REGION_PAGE];
    }
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedRegionRead(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomSize offset, void *destination, UmicomSize bytes)
{
    return UmicomMappedCopy(space, handle, offset, destination, bytes, UMICOM_FALSE);
}
UmicomKernelMappedStatus UmicomKernelMappedRegionWrite(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomSize offset, const void *source, UmicomSize bytes)
{
    /* The common loop reads, rather than writes, the external buffer in this branch. */
    return UmicomMappedCopy(space, handle, offset, (void *)source, bytes, UMICOM_TRUE);
}
UmicomKernelMappedStatus UmicomKernelMappedSpaceBorrow(UmicomKernelMappedSpace *space,
    UmicomKernelMappedLease *outLease)
{
    UmicomKernelMappedStatus status = UmicomKernelMappedSpaceValidate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (space->state != UMICOM_MAPPED_READY) return UMICOM_MAPPED_BAD_STATE;
    if (outLease == (UmicomKernelMappedLease *)0 ||
        UmicomMappedBuffer(space, outLease, sizeof(*outLease)) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    if (space->nextTicket == 0U) return UMICOM_MAPPED_TICKET_EXHAUSTED;
    space->activeTicket = space->nextTicket;
    space->nextTicket = space->nextTicket == ~(UmicomU64)0U ? 0U : space->nextTicket + 1U;
    UmicomKernelMappedSpaceInstructionsPublish();
    space->state = UMICOM_MAPPED_BORROWED;
    outLease->owner = space;
    outLease->ticket = space->activeTicket;
    outLease->root = space->hierarchy.rootTablePhysicalAddress;
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedSpaceReturn(UmicomKernelMappedSpace *space,
    const UmicomKernelMappedLease *lease)
{
    UmicomKernelMappedStatus status = UmicomKernelMappedSpaceValidate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (lease == (const UmicomKernelMappedLease *)0 ||
        UmicomMappedBuffer(space, lease, sizeof(*lease)) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    if (space->state != UMICOM_MAPPED_BORROWED || lease->owner != space ||
        lease->ticket != space->activeTicket || lease->root != space->hierarchy.rootTablePhysicalAddress)
        return UMICOM_MAPPED_INVALID_LEASE;
    space->activeTicket = 0U;
    space->state = UMICOM_MAPPED_READY;
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedSpaceSnapshot(UmicomKernelMappedSpace *space,
    UmicomKernelMappedSpaceInfo *outInfo)
{
    UmicomKernelMappedStatus status = UmicomKernelMappedSpaceValidate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (outInfo == (UmicomKernelMappedSpaceInfo *)0 ||
        UmicomMappedBuffer(space, outInfo, sizeof(*outInfo)) == UMICOM_FALSE) return UMICOM_MAPPED_INVALID_ARGUMENT;
    UmicomSize data = 0U;
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        for (UmicomSize page = 0U; page < space->regions[region].spec.pages; ++page)
            if (space->regions[region].frames[page] != 0U) ++data;
    }
    outInfo->state = space->state;
    outInfo->regions = space->regionCount;
    outInfo->dataFrames = data;
    outInfo->tableFrames = space->hierarchy.pageTableFrames;
    outInfo->mappedPages = space->hierarchy.mappedPages;
    return UMICOM_MAPPED_OK;
}
UmicomKernelMappedStatus UmicomKernelMappedSpaceClose(UmicomKernelMappedSpace *space)
{
    UmicomKernelMappedStatus status = UmicomMappedGate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    if (space->state == UMICOM_MAPPED_CLOSED) return UMICOM_MAPPED_OK;
    if (space->state != UMICOM_MAPPED_READY && space->state != UMICOM_MAPPED_PLANNING &&
        space->state != UMICOM_MAPPED_CLOSING) return UMICOM_MAPPED_BAD_STATE;
    status = UmicomKernelMappedSpaceValidate(space);
    if (status != UMICOM_MAPPED_OK) return status;
    space->state = UMICOM_MAPPED_CLOSING;
    status = UmicomMappedRelease(space);
    if (status != UMICOM_MAPPED_OK) return status;
    for (UmicomSize region = 0U; region < space->regionCount; ++region) {
        space->regions[region].spec.initialData = (const UmicomU8 *)0;
        space->regions[region].spec.initialBytes = 0U;
    }
    space->state = UMICOM_MAPPED_CLOSED;
    return UMICOM_MAPPED_OK;
}
const char *UmicomKernelMappedStatusName(UmicomKernelMappedStatus status)
{
    switch (status) {
        case UMICOM_MAPPED_OK: return "ok";
        case UMICOM_MAPPED_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_MAPPED_BAD_STATE: return "bad-state";
        case UMICOM_MAPPED_UNSAFE_CONTEXT: return "unsafe-context";
        case UMICOM_MAPPED_CAPACITY: return "capacity";
        case UMICOM_MAPPED_INVALID_RANGE: return "invalid-range";
        case UMICOM_MAPPED_INVALID_PERMISSIONS: return "invalid-permissions";
        case UMICOM_MAPPED_OVERLAP: return "overlap";
        case UMICOM_MAPPED_INVALID_HANDLE: return "invalid-handle";
        case UMICOM_MAPPED_OUT_OF_MEMORY: return "out-of-memory";
        case UMICOM_MAPPED_MAPPING_ERROR: return "mapping-error";
        case UMICOM_MAPPED_CLEANUP_REQUIRED: return "cleanup-required";
        case UMICOM_MAPPED_CORRUPT_STATE: return "corrupt-state";
        case UMICOM_MAPPED_ACCESS_DENIED: return "access-denied";
        case UMICOM_MAPPED_INVALID_LEASE: return "invalid-lease";
        case UMICOM_MAPPED_TICKET_EXHAUSTED: return "ticket-exhausted";
        default: return "unknown-mapped-status";
    }
}
