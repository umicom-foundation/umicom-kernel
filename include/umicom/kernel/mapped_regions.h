/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/mapped_regions.h
 *
 * PURPOSE:
 *   Own an immutable set of mapped RAM regions, their backing frames and one
 *   private Sv39 hierarchy, including deliberately unmapped stack margins.
 *
 * EDUCATIONAL OVERVIEW:
 *   A mapping is not an allocation. The existing page-table service owns its
 *   tables, while a caller owns the data pages named by its leaves. This owner
 *   brings those lifetimes together without introducing another page mapper.
 *   Planning reserves virtual intervals, Build allocates and maps, Borrow pins
 *   the result for execution, and Close removes tables before releasing data.
 *
 *   Keep this object in stable, zero-filled Kernel storage. Its fields are
 *   visible for static allocation and tests, not permission to edit them. Calls
 *   are serial on hart zero with the same allocation boundary as object caches.
 *   A lease is Kernel bookkeeping, not a capability against hostile M-mode code.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_MAPPED_REGIONS_H
#define UMICOM_KERNEL_MAPPED_REGIONS_H
#include "umicom/kernel/virtual_memory.h"

/* Explicit quotas bound both physical consumption and independent audit work. */
#define UMICOM_MAPPED_REGION_LIMIT 8U
#define UMICOM_MAPPED_REGION_PAGE_LIMIT 16U
#define UMICOM_MAPPED_SPACE_PAGE_LIMIT 64U
/* In the worst scattered layout every data page needs two private ancestors. */
#define UMICOM_MAPPED_SPACE_TABLE_LIMIT (1U + 2U * UMICOM_MAPPED_SPACE_PAGE_LIMIT)

typedef UmicomU64 UmicomKernelMappedRegionHandle;
typedef enum UmicomKernelMappedSpaceState {
    UMICOM_MAPPED_EMPTY,
    UMICOM_MAPPED_PLANNING,
    UMICOM_MAPPED_BUILDING,
    UMICOM_MAPPED_READY,
    UMICOM_MAPPED_BORROWED,
    UMICOM_MAPPED_CLOSING,
    UMICOM_MAPPED_CLOSED,
    UMICOM_MAPPED_POISONED
} UmicomKernelMappedSpaceState;
typedef enum UmicomKernelMappedStatus {
    UMICOM_MAPPED_OK,
    UMICOM_MAPPED_INVALID_ARGUMENT,
    UMICOM_MAPPED_BAD_STATE,
    UMICOM_MAPPED_UNSAFE_CONTEXT,
    UMICOM_MAPPED_CAPACITY,
    UMICOM_MAPPED_INVALID_RANGE,
    UMICOM_MAPPED_INVALID_PERMISSIONS,
    UMICOM_MAPPED_OVERLAP,
    UMICOM_MAPPED_INVALID_HANDLE,
    UMICOM_MAPPED_OUT_OF_MEMORY,
    UMICOM_MAPPED_MAPPING_ERROR,
    UMICOM_MAPPED_CLEANUP_REQUIRED,
    UMICOM_MAPPED_CORRUPT_STATE,
    UMICOM_MAPPED_ACCESS_DENIED,
    UMICOM_MAPPED_INVALID_LEASE,
    UMICOM_MAPPED_TICKET_EXHAUSTED
} UmicomKernelMappedStatus;

/* base names the first DATA page, not its optional lower guard. Both guards
 * reserve a whole page. They have no backing frame and are never mapped.
 * Initial bytes are copied at offset zero and all remaining page bytes are zero.
 * The immutable source is borrowed until Build succeeds or the plan is closed. */
typedef struct UmicomKernelMappedRegionSpec {
    UmicomAddress base;
    UmicomSize pages;
    UmicomKernelVirtualMemoryPermissions permissions;
    UmicomBoolean guardBelow;
    UmicomBoolean guardAbove;
    const UmicomU8 *initialData;
    UmicomSize initialBytes;
} UmicomKernelMappedRegionSpec;

typedef struct UmicomKernelMappedRegionRecord {
    UmicomKernelMappedRegionSpec spec;
    UmicomAddress reservedBegin;
    UmicomAddress reservedEnd; /* Exclusive, checked not to wrap. */
    UmicomAddress frames[UMICOM_MAPPED_REGION_PAGE_LIMIT];
} UmicomKernelMappedRegionRecord;
typedef struct UmicomKernelMappedTableRecord {
    UmicomAddress frame;
    UmicomU64 prefix; /* Low Sv39 virtual prefix at this node. */
    UmicomU32 level;
    UmicomBoolean live;
} UmicomKernelMappedTableRecord;

typedef struct UmicomKernelMappedSpace {
    const struct UmicomKernelMappedSpace *self;
    UmicomKernelMappedSpaceState state;
    UmicomKernelVirtualAddressSpace hierarchy; /* Mutated only through the existing VM API. */
    UmicomSize regionCount;
    UmicomSize plannedPages;
    UmicomSize tableCount;
    UmicomU64 nextTicket;
    UmicomU64 activeTicket;
    UmicomKernelMappedRegionRecord regions[UMICOM_MAPPED_REGION_LIMIT];
    UmicomKernelMappedTableRecord tables[UMICOM_MAPPED_SPACE_TABLE_LIMIT];
} UmicomKernelMappedSpace;

typedef struct UmicomKernelMappedRegionInfo {
    UmicomAddress base;
    UmicomAddress end;
    UmicomAddress reservedBegin;
    UmicomAddress reservedEnd;
    UmicomSize pages;
    UmicomKernelVirtualMemoryPermissions permissions;
} UmicomKernelMappedRegionInfo;
typedef struct UmicomKernelMappedSpaceInfo {
    UmicomKernelMappedSpaceState state;
    UmicomSize regions;
    UmicomSize dataFrames;
    UmicomSize tableFrames;
    UmicomSize mappedPages;
} UmicomKernelMappedSpaceInfo;
typedef struct UmicomKernelMappedLease {
    const UmicomKernelMappedSpace *owner;
    UmicomU64 ticket;
    UmicomAddress root;
} UmicomKernelMappedLease;

/* Initialise once. Closing does not authorise reinitialisation at the same
 * address: doing so could make a forgotten reference meaningful again. */
UmicomKernelMappedStatus UmicomKernelMappedSpaceInitialize(UmicomKernelMappedSpace *space);
/* Add a plan without allocating. The complete guarded interval is reserved;
 * later plans cannot occupy even a guard page. Outputs are unchanged on error. */
UmicomKernelMappedStatus UmicomKernelMappedRegionAdd(UmicomKernelMappedSpace *space,
    const UmicomKernelMappedRegionSpec *spec, UmicomKernelMappedRegionHandle *outHandle);
/* Convenience policy: readable/writable, never executable, guarded on both
 * sides. user selects USER permission; it does not make the API user-callable. */
UmicomKernelMappedStatus UmicomKernelMappedStackAdd(UmicomKernelMappedSpace *space,
    UmicomAddress base, UmicomSize pages, UmicomBoolean user,
    UmicomKernelMappedRegionHandle *outHandle);
/* Build once. An ordinary failure releases partial ownership and returns to
 * PLANNING; a teardown failure stays CLOSING for explicit Close retry. */
UmicomKernelMappedStatus UmicomKernelMappedSpaceBuild(UmicomKernelMappedSpace *space);
UmicomKernelMappedStatus UmicomKernelMappedRegionQuery(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomKernelMappedRegionInfo *outInfo);
/* Checked Kernel copying, not a user-copy syscall. Buffers must not overlap
 * this owner or its backing/table frames. Writes require WRITE, even in M-mode. */
UmicomKernelMappedStatus UmicomKernelMappedRegionRead(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomSize offset, void *destination, UmicomSize bytes);
UmicomKernelMappedStatus UmicomKernelMappedRegionWrite(UmicomKernelMappedSpace *space,
    UmicomKernelMappedRegionHandle handle, UmicomSize offset, const void *source, UmicomSize bytes);
/* Only one execution lease is issued at a time. While borrowed, no mutation,
 * another borrow or teardown is allowed. Return asserts the synchronous caller
 * has stopped using the root; it also requires the ordinary machine context. */
UmicomKernelMappedStatus UmicomKernelMappedSpaceBorrow(UmicomKernelMappedSpace *space,
    UmicomKernelMappedLease *outLease);
UmicomKernelMappedStatus UmicomKernelMappedSpaceReturn(UmicomKernelMappedSpace *space,
    const UmicomKernelMappedLease *lease);
/* Independent audit refuses unregistered table pointers BEFORE following them.
 * It is not a general audit for roots created outside this ownership service. */
UmicomKernelMappedStatus UmicomKernelMappedSpaceValidate(UmicomKernelMappedSpace *space);
UmicomKernelMappedStatus UmicomKernelMappedSpaceSnapshot(UmicomKernelMappedSpace *space,
    UmicomKernelMappedSpaceInfo *outInfo);
/* Destroy tables first, then scrub/free backing frames. Partial progress is
 * retained. A poisoned owner is deliberately not automatically reclaimed. */
UmicomKernelMappedStatus UmicomKernelMappedSpaceClose(UmicomKernelMappedSpace *space);
const char *UmicomKernelMappedStatusName(UmicomKernelMappedStatus status);
void UmicomKernelMappedRegionsValidateExecution(void);

/* Architecture gate and cache synchronisation; modelled only in native tests. */
UmicomBoolean UmicomKernelMappedSpaceAccessAllowed(void);
void UmicomKernelMappedSpaceInstructionsPublish(void);
#endif /* UMICOM_KERNEL_MAPPED_REGIONS_H */
