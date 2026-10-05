/*-----------------------------------------------------------------------------
 * Umicom Kernel native mapped-region tests
 * File: tests/mapped_regions/mapped_region_tests.c
 *
 * Use the actual mapper, destructor, physical allocator and region service.
 * The native arena represents RAM. Corruption cases intentionally alter private
 * records/PTEs to verify refusal BEFORE a foreign pointer is dereferenced.
 * Resets create isolated test fixtures; live production owners cannot be reset.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/mapped_regions.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define PAGES 512U
#define BASE ((UmicomAddress)0x200000U)
#define RW (UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE)
alignas(4096) static UmicomU8 umicomMappedArena[PAGES * 4096U];
static UmicomKernelMappedSpace umicomMappedOwner;
static UmicomKernelMappedSpace umicomMappedOther;
static UmicomU8 umicomMappedSeed[8192];
static UmicomU8 umicomMappedCopy[8192];
static UmicomBoolean umicomMappedAllowed = UMICOM_TRUE;
static unsigned umicomMappedAllocateCalls;
static unsigned umicomMappedFreeCalls;
static unsigned umicomMappedFailAllocation;
static unsigned umicomMappedFailFree;
static unsigned umicomMappedPublishes;
UmicomBoolean UmicomKernelMappedSpaceAccessAllowed(void) { return umicomMappedAllowed; }
void UmicomKernelMappedSpaceInstructionsPublish(void) { ++umicomMappedPublishes; }
UmicomKernelMemoryStatus UmicomMappedActualAllocate(UmicomAddress *frame);
UmicomKernelMemoryStatus UmicomMappedActualFree(UmicomAddress frame);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *frame)
{
    ++umicomMappedAllocateCalls;
    if (umicomMappedFailAllocation != 0U && umicomMappedAllocateCalls == umicomMappedFailAllocation)
        return UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY;
    return UmicomMappedActualAllocate(frame);
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress frame)
{
    ++umicomMappedFreeCalls;
    if (umicomMappedFailFree != 0U && umicomMappedFreeCalls == umicomMappedFailFree)
        return UMICOM_KERNEL_MEMORY_NOT_ALLOCATED;
    return UmicomMappedActualFree(frame);
}
static void UmicomMappedSetup(UmicomSize pages)
{
    memset(&umicomMappedOwner, 0, sizeof(umicomMappedOwner));
    memset(&umicomMappedOther, 0, sizeof(umicomMappedOther));
    memset(umicomMappedArena, 0xbb, sizeof(umicomMappedArena));
    for (UmicomSize index = 0U; index < sizeof(umicomMappedSeed); ++index) umicomMappedSeed[index] = (UmicomU8)(index % 251U);
    umicomMappedAllocateCalls = 0U; umicomMappedFreeCalls = 0U;
    umicomMappedFailAllocation = 0U; umicomMappedFailFree = 0U;
    umicomMappedAllowed = UMICOM_TRUE; umicomMappedPublishes = 0U;
    CHECK(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomMappedArena, pages * 4096U) == UMICOM_KERNEL_MEMORY_OK);
    CHECK(UmicomKernelMappedSpaceInitialize(&umicomMappedOwner) == UMICOM_MAPPED_OK);
}
static UmicomSize UmicomMappedCount(void)
{
    UmicomKernelPhysicalMemorySnapshot info = {0};
    CHECK(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&info) == UMICOM_KERNEL_MEMORY_OK);
    return info.allocatedFrames;
}
static UmicomKernelMappedRegionSpec UmicomMappedSpec(UmicomAddress base, UmicomSize pages)
{
    const UmicomKernelMappedRegionSpec spec = {base, pages, RW, UMICOM_TRUE, UMICOM_TRUE, NULL, 0U};
    return spec;
}
static UmicomKernelMappedRegionHandle UmicomMappedAdd(UmicomKernelMappedSpace *space, UmicomAddress base, UmicomSize pages)
{
    UmicomKernelMappedRegionHandle token = 0U;
    UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(base, pages);
    CHECK(UmicomKernelMappedRegionAdd(space, &spec, &token) == UMICOM_MAPPED_OK);
    return token;
}
static UmicomKernelMappedRegionHandle UmicomMappedBuild(void)
{
    UmicomKernelMappedRegionHandle handle = UmicomMappedAdd(&umicomMappedOwner, BASE, 2U);
    CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
    CHECK(UmicomKernelMappedSpaceValidate(&umicomMappedOwner) == UMICOM_MAPPED_OK);
    return handle;
}
static void UmicomMappedFinish(void)
{
    CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_OK);
    CHECK(UmicomKernelMappedSpaceValidate(&umicomMappedOwner) == UMICOM_MAPPED_OK);
    CHECK(UmicomMappedCount() == 0U);
}
static UmicomU64 *UmicomMappedRoot(void)
{
    return (UmicomU64 *)(UmicomUIntPtr)umicomMappedOwner.hierarchy.rootTablePhysicalAddress;
}
static UmicomU64 *UmicomMappedLevelOne(void)
{
    return (UmicomU64 *)(UmicomUIntPtr)((UmicomMappedRoot()[0] >> 10U) << 12U);
}
static UmicomU64 *UmicomMappedLeaf(void)
{
    return (UmicomU64 *)(UmicomUIntPtr)((UmicomMappedLevelOne()[1] >> 10U) << 12U);
}
static void UmicomMappedCorruptRefusal(UmicomSize allocated)
{
    CHECK(UmicomKernelMappedSpaceValidate(&umicomMappedOwner) == UMICOM_MAPPED_CORRUPT_STATE);
    CHECK(umicomMappedOwner.state == UMICOM_MAPPED_POISONED);
    CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_CORRUPT_STATE);
    CHECK(UmicomMappedCount() == allocated);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *const name = argv[1];
    UmicomMappedSetup(PAGES);
    if (strcmp(name, "initialisation-contract") == 0) {
        CHECK(UmicomKernelMappedSpaceInitialize(NULL) == UMICOM_MAPPED_INVALID_ARGUMENT);
        CHECK(UmicomKernelMappedSpaceInitialize(&umicomMappedOwner) == UMICOM_MAPPED_BAD_STATE);
        umicomMappedOther.regions[0].frames[0] = 1U;
        CHECK(UmicomKernelMappedSpaceInitialize(&umicomMappedOther) == UMICOM_MAPPED_BAD_STATE);
    } else if (strcmp(name, "planning-is-lazy") == 0) {
        (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 2U);
        CHECK(UmicomMappedCount() == 0U);
        UmicomMappedFinish();
    } else if (strcmp(name, "region-quota") == 0) {
        for (UmicomSize index = 0U; index < 8U; ++index) (void)UmicomMappedAdd(&umicomMappedOwner, BASE + index * 0x10000U, 1U);
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE + 0x100000U, 1U);
        UmicomU64 token = 99U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_CAPACITY && token == 99U);
        UmicomMappedFinish();
    } else if (strcmp(name, "page-quota") == 0) {
        for (UmicomSize index = 0U; index < 4U; ++index) (void)UmicomMappedAdd(&umicomMappedOwner, BASE + index * 0x100000U, 16U);
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE + 0x800000U, 1U);
        UmicomU64 token = 99U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_CAPACITY);
    } else if (strcmp(name, "null-arguments") == 0) {
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE, 1U);
        UmicomU64 token = 0U;
        CHECK(UmicomKernelMappedRegionAdd(NULL, &spec, &token) == UMICOM_MAPPED_INVALID_ARGUMENT);
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, NULL, &token) == UMICOM_MAPPED_INVALID_ARGUMENT);
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, NULL) == UMICOM_MAPPED_INVALID_ARGUMENT);
        CHECK(UmicomKernelMappedSpaceBuild(NULL) == UMICOM_MAPPED_INVALID_ARGUMENT);
        CHECK(UmicomKernelMappedSpaceClose(NULL) == UMICOM_MAPPED_INVALID_ARGUMENT);
        spec.initialBytes = 1U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_INVALID_ARGUMENT);
    } else if (strcmp(name, "noncanonical-range") == 0 || strcmp(name, "crossing-hole") == 0 ||
               strcmp(name, "arithmetic-overflow") == 0 || strcmp(name, "alignment-refusal") == 0 ||
               strcmp(name, "zero-page-refusal") == 0 || strcmp(name, "null-page-refusal") == 0) {
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE, 2U);
        if (strcmp(name, "noncanonical-range") == 0) spec.base = 0x8000000000ULL;
        if (strcmp(name, "crossing-hole") == 0) spec.base = 0x3ffffff000ULL;
        if (strcmp(name, "arithmetic-overflow") == 0) spec.base = 0xfffffffffffff000ULL;
        if (strcmp(name, "alignment-refusal") == 0) ++spec.base;
        if (strcmp(name, "zero-page-refusal") == 0) spec.pages = 0U;
        if (strcmp(name, "null-page-refusal") == 0) spec.base = 0U;
        UmicomU64 token = 88U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_INVALID_RANGE);
        CHECK(token == 88U && UmicomMappedCount() == 0U);
    } else if (strcmp(name, "permission-refusals") == 0) {
        const UmicomU32 invalid[] = {0U, 2U, 4U, 7U, 17U, 0x80000001U};
        for (UmicomSize index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE, 1U);
            spec.permissions = invalid[index];
            UmicomU64 token = 55U;
            CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_INVALID_PERMISSIONS);
        }
        CHECK(UmicomMappedCount() == 0U);
    } else if (strcmp(name, "guard-overlap") == 0 || strcmp(name, "below-and-above-reservation") == 0) {
        (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 2U);
        const UmicomAddress tries[] = {BASE - 4096U, BASE, BASE + 4096U, BASE + 8192U};
        for (UmicomSize index = 0U; index < 4U; ++index) {
            UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(tries[index], 1U);
            spec.guardBelow = UMICOM_FALSE; spec.guardAbove = UMICOM_FALSE;
            UmicomU64 token = 66U;
            CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_OVERLAP);
            CHECK(token == 66U);
        }
        UmicomMappedFinish();
    } else if (strcmp(name, "adjacent-regions") == 0) {
        (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 1U);
        (void)UmicomMappedAdd(&umicomMappedOwner, BASE + 12288U, 1U);
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        UmicomMappedFinish();
    } else if (strcmp(name, "high-canonical-region") == 0) {
        (void)UmicomMappedAdd(&umicomMappedOwner, 0xffffffc010000000ULL, 2U);
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedSpaceValidate(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        UmicomMappedFinish();
    } else if (strcmp(name, "seed-and-zero-padding") == 0 || strcmp(name, "source-release-after-build") == 0) {
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE, 2U);
        spec.initialData = umicomMappedSeed; spec.initialBytes = 4193U;
        UmicomU64 token = 0U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        memset(umicomMappedSeed, 0xcc, sizeof(umicomMappedSeed));
        CHECK(umicomMappedOwner.regions[0].spec.initialData == NULL);
        CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, 0U, umicomMappedCopy, 8192U) == UMICOM_MAPPED_OK);
        for (UmicomSize index = 0U; index < 8192U; ++index) CHECK(umicomMappedCopy[index] == (index < 4193U ? (UmicomU8)(index % 251U) : 0U));
        UmicomMappedFinish();
    } else if (strcmp(name, "fragmented-backing") == 0) {
        UmicomAddress held[12] = {0};
        for (unsigned index = 0U; index < 12U; ++index) CHECK(UmicomMappedActualAllocate(&held[index]) == UMICOM_KERNEL_MEMORY_OK);
        for (unsigned index = 0U; index < 12U; index += 2U) CHECK(UmicomMappedActualFree(held[index]) == UMICOM_KERNEL_MEMORY_OK);
        (void)UmicomMappedBuild();
        CHECK(umicomMappedOwner.regions[0].frames[1] - umicomMappedOwner.regions[0].frames[0] == 8192U);
        CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        for (unsigned index = 1U; index < 12U; index += 2U) CHECK(UmicomMappedActualFree(held[index]) == UMICOM_KERNEL_MEMORY_OK);
        CHECK(UmicomMappedCount() == 0U);
    } else if (strcmp(name, "copy-across-pages") == 0) {
        UmicomU64 token = UmicomMappedBuild();
        CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 4001U, umicomMappedSeed, 501U) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, 4001U, umicomMappedCopy, 501U) == UMICOM_MAPPED_OK);
        CHECK(memcmp(umicomMappedCopy, umicomMappedSeed, 501U) == 0);
        UmicomMappedFinish();
    } else if (strcmp(name, "copy-range-preflight") == 0 || strcmp(name, "copy-alias-refusal") == 0) {
        UmicomU64 token = UmicomMappedBuild();
        if (strcmp(name, "copy-range-preflight") == 0) {
            CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 8000U, umicomMappedSeed, 300U) == UMICOM_MAPPED_INVALID_RANGE);
            CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, ~(UmicomU64)0U, umicomMappedCopy, 2U) == UMICOM_MAPPED_INVALID_RANGE);
            CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, 8192U, NULL, 0U) == UMICOM_MAPPED_OK);
        } else {
            CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 0U, &umicomMappedOwner, 16U) == UMICOM_MAPPED_INVALID_ARGUMENT);
            CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, 0U, (void *)umicomMappedOwner.regions[0].frames[1], 16U) == UMICOM_MAPPED_INVALID_ARGUMENT);
            CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, 0U, (void *)umicomMappedOwner.hierarchy.rootTablePhysicalAddress, 16U) == UMICOM_MAPPED_INVALID_ARGUMENT);
        }
        CHECK(UmicomKernelMappedRegionRead(&umicomMappedOwner, token, 0U, umicomMappedCopy, 8192U) == UMICOM_MAPPED_OK);
        for (UmicomSize i = 0U; i < 8192U; ++i) CHECK(umicomMappedCopy[i] == 0U);
        UmicomMappedFinish();
    } else if (strcmp(name, "readonly-write-refusal") == 0) {
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE, 1U);
        spec.permissions = UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE;
        UmicomU64 token = 0U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 0U, umicomMappedSeed, 4U) == UMICOM_MAPPED_ACCESS_DENIED);
        UmicomMappedFinish();
    } else if (strcmp(name, "bad-region-handle") == 0) {
        (void)UmicomMappedBuild(); UmicomKernelMappedRegionInfo info = {0};
        CHECK(UmicomKernelMappedRegionQuery(&umicomMappedOwner, 0U, &info) == UMICOM_MAPPED_INVALID_HANDLE);
        CHECK(UmicomKernelMappedRegionQuery(&umicomMappedOwner, ~(UmicomU64)0U, &info) == UMICOM_MAPPED_INVALID_HANDLE);
        UmicomMappedFinish();
    } else if (strcmp(name, "build-only-once") == 0) {
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_BAD_STATE);
        (void)UmicomMappedBuild();
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_BAD_STATE);
        UmicomKernelMappedRegionSpec spec = UmicomMappedSpec(BASE + 0x100000U, 1U); UmicomU64 token = 77U;
        CHECK(UmicomKernelMappedRegionAdd(&umicomMappedOwner, &spec, &token) == UMICOM_MAPPED_BAD_STATE);
        UmicomMappedFinish();
    } else if (strcmp(name, "borrow-pins-storage") == 0 || strcmp(name, "bad-lease-return") == 0 ||
        strcmp(name, "double-return") == 0 || strcmp(name, "stale-lease") == 0 || strcmp(name, "lease-exhaustion") == 0) {
        UmicomU64 token = UmicomMappedBuild();
        UmicomKernelMappedLease lease = {0}, another = {0};
        if (strcmp(name, "lease-exhaustion") == 0) umicomMappedOwner.nextTicket = ~(UmicomU64)0U;
        CHECK(UmicomKernelMappedSpaceBorrow(&umicomMappedOwner, &lease) == UMICOM_MAPPED_OK);
        const UmicomSize count = UmicomMappedCount();
        CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_BAD_STATE);
        CHECK(UmicomKernelMappedSpaceBorrow(&umicomMappedOwner, &another) == UMICOM_MAPPED_BAD_STATE);
        CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 0U, umicomMappedSeed, 1U) == UMICOM_MAPPED_BAD_STATE);
        another = lease; ++another.ticket;
        CHECK(UmicomKernelMappedSpaceReturn(&umicomMappedOwner, &another) == UMICOM_MAPPED_INVALID_LEASE);
        CHECK(UmicomMappedCount() == count);
        CHECK(UmicomKernelMappedSpaceReturn(&umicomMappedOwner, &lease) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedSpaceReturn(&umicomMappedOwner, &lease) == UMICOM_MAPPED_INVALID_LEASE);
        if (strcmp(name, "lease-exhaustion") == 0) {
            CHECK(UmicomKernelMappedSpaceBorrow(&umicomMappedOwner, &another) == UMICOM_MAPPED_TICKET_EXHAUSTED);
        } else {
            CHECK(UmicomKernelMappedSpaceBorrow(&umicomMappedOwner, &another) == UMICOM_MAPPED_OK);
            CHECK(another.ticket != lease.ticket);
            CHECK(UmicomKernelMappedSpaceReturn(&umicomMappedOwner, &lease) == UMICOM_MAPPED_INVALID_LEASE);
            CHECK(UmicomKernelMappedSpaceReturn(&umicomMappedOwner, &another) == UMICOM_MAPPED_OK);
        }
        CHECK(umicomMappedPublishes >= 1U); UmicomMappedFinish();
    } else if (strcmp(name, "unsafe-operations") == 0) {
        UmicomU64 token = UmicomMappedBuild(); UmicomKernelMappedLease lease = {0};
        umicomMappedAllowed = UMICOM_FALSE;
        CHECK(UmicomKernelMappedSpaceBorrow(&umicomMappedOwner, &lease) == UMICOM_MAPPED_UNSAFE_CONTEXT);
        CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_UNSAFE_CONTEXT);
        CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 0U, umicomMappedSeed, 1U) == UMICOM_MAPPED_UNSAFE_CONTEXT);
        umicomMappedAllowed = UMICOM_TRUE; UmicomMappedFinish();
    } else if (strcmp(name, "close-scrubs") == 0 || strcmp(name, "closed-not-reinitialised") == 0) {
        UmicomU64 token = UmicomMappedBuild(); const UmicomAddress frame = umicomMappedOwner.regions[0].frames[0];
        CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, token, 0U, umicomMappedSeed, 4000U) == UMICOM_MAPPED_OK);
        UmicomMappedFinish();
        for (UmicomSize i = 0U; i < 4096U; ++i) CHECK(((UmicomU8 *)frame)[i] == 0U);
        CHECK(UmicomKernelMappedSpaceInitialize(&umicomMappedOwner) == UMICOM_MAPPED_BAD_STATE);
        CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_OK);
    } else if (strcmp(name, "root-substitution") == 0 || strcmp(name, "foreign-child-pointer") == 0 ||
        strcmp(name, "own-table-alias") == 0 || strcmp(name, "cycle") == 0 ||
        strcmp(name, "invalid-leaf-frame") == 0 || strcmp(name, "wrong-leaf-permissions") == 0 ||
        strcmp(name, "mapping-in-guard") == 0 || strcmp(name, "pte-reserved-bits") == 0 ||
        strcmp(name, "counters-disagree") == 0 || strcmp(name, "missing-owned-table") == 0 ||
        strcmp(name, "externally-freed-table") == 0 || strcmp(name, "externally-freed-data") == 0 ||
        strcmp(name, "duplicate-data-frame") == 0 || strcmp(name, "metadata-corruption") == 0) {
        (void)UmicomMappedBuild();
        if (strcmp(name, "root-substitution") == 0) umicomMappedOwner.hierarchy.rootTablePhysicalAddress = 0x10000000U;
        if (strcmp(name, "foreign-child-pointer") == 0) UmicomMappedRoot()[0] = ((UmicomU64)0x10000000U >> 2U) | 1U;
        if (strcmp(name, "own-table-alias") == 0) UmicomMappedRoot()[1] = UmicomMappedRoot()[0];
        if (strcmp(name, "cycle") == 0) UmicomMappedRoot()[0] = ((UmicomU64)umicomMappedOwner.hierarchy.rootTablePhysicalAddress >> 2U) | 1U;
        if (strcmp(name, "invalid-leaf-frame") == 0) UmicomMappedLeaf()[0] = 0x4000043U;
        if (strcmp(name, "wrong-leaf-permissions") == 0) UmicomMappedLeaf()[0] |= 8U;
        if (strcmp(name, "mapping-in-guard") == 0) { UmicomMappedLeaf()[2] = UmicomMappedLeaf()[1]; ++umicomMappedOwner.hierarchy.mappedPages; }
        if (strcmp(name, "pte-reserved-bits") == 0) UmicomMappedRoot()[0] |= 1ULL << 63U;
        if (strcmp(name, "counters-disagree") == 0) ++umicomMappedOwner.hierarchy.mappedPages;
        if (strcmp(name, "missing-owned-table") == 0) UmicomMappedRoot()[0] = 0U;
        if (strcmp(name, "externally-freed-table") == 0) CHECK(UmicomMappedActualFree(umicomMappedOwner.tables[1].frame) == UMICOM_KERNEL_MEMORY_OK);
        if (strcmp(name, "externally-freed-data") == 0) CHECK(UmicomMappedActualFree(umicomMappedOwner.regions[0].frames[1]) == UMICOM_KERNEL_MEMORY_OK);
        if (strcmp(name, "duplicate-data-frame") == 0) umicomMappedOwner.regions[0].frames[1] = umicomMappedOwner.regions[0].frames[0];
        if (strcmp(name, "metadata-corruption") == 0) umicomMappedOwner.regionCount = 999U;
        UmicomMappedCorruptRefusal(UmicomMappedCount());
    } else if (strcmp(name, "copied-owner") == 0) {
        (void)UmicomMappedBuild(); memcpy(&umicomMappedOther, &umicomMappedOwner, sizeof(umicomMappedOwner));
        CHECK(UmicomKernelMappedSpaceValidate(&umicomMappedOther) == UMICOM_MAPPED_BAD_STATE);
        UmicomMappedFinish();
    } else if (strcmp(name, "physical-exhaustion") == 0 || strcmp(name, "allocation-refusals") == 0) {
        for (unsigned budget = 1U; budget <= 17U; ++budget) {
            UmicomMappedSetup(strcmp(name, "physical-exhaustion") == 0 ? budget : PAGES);
            (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 2U);
            (void)UmicomMappedAdd(&umicomMappedOwner, 0x40000000U, 3U);
            if (strcmp(name, "allocation-refusals") == 0) umicomMappedFailAllocation = budget;
            UmicomKernelMappedStatus result = UmicomKernelMappedSpaceBuild(&umicomMappedOwner);
            CHECK(result == UMICOM_MAPPED_OK || result == UMICOM_MAPPED_OUT_OF_MEMORY);
            if (result != UMICOM_MAPPED_OK) { CHECK(UmicomMappedCount() == 0U); CHECK(umicomMappedOwner.state == UMICOM_MAPPED_PLANNING); }
            umicomMappedFailAllocation = 0U; UmicomMappedFinish();
        }
    } else if (strcmp(name, "cleanup-refusals") == 0) {
        for (unsigned fail = 1U; fail <= 10U; ++fail) {
            UmicomMappedSetup(PAGES);
            (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 2U);
            (void)UmicomMappedAdd(&umicomMappedOwner, 0x40000000U, 3U);
            CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
            umicomMappedFailFree = fail;
            CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOwner) == UMICOM_MAPPED_CLEANUP_REQUIRED);
            CHECK(umicomMappedOwner.state == UMICOM_MAPPED_CLOSING);
            CHECK(UmicomKernelMappedSpaceValidate(&umicomMappedOwner) == UMICOM_MAPPED_OK);
            umicomMappedFailFree = 0U; UmicomMappedFinish();
        }
    } else if (strcmp(name, "rollback-cleanup-refusal") == 0) {
        (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 3U);
        umicomMappedFailAllocation = 3U; umicomMappedFailFree = 1U;
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_CLEANUP_REQUIRED);
        CHECK(UmicomMappedCount() == 2U && umicomMappedOwner.state == UMICOM_MAPPED_CLOSING);
        umicomMappedFailFree = 0U; UmicomMappedFinish();
    } else if (strcmp(name, "combined-map-rollback-refusal") == 0) {
        (void)UmicomMappedAdd(&umicomMappedOwner, BASE, 1U);
        /* Refuse the second intermediate-table allocation AND the first
         * rollback release. The corrected mapper retains its linked empty
         * table, allowing the region owner to retry through normal destruction. */
        umicomMappedFailAllocation = 4U; umicomMappedFailFree = 1U;
        const UmicomKernelMappedStatus rollback = UmicomKernelMappedSpaceBuild(&umicomMappedOwner);
        printf("combined rollback: %s, remaining allocations=%llu\n",
            UmicomKernelMappedStatusName(rollback), (unsigned long long)UmicomMappedCount());
        CHECK(rollback == UMICOM_MAPPED_MAPPING_ERROR);
        CHECK(umicomMappedOwner.state == UMICOM_MAPPED_PLANNING && UmicomMappedCount() == 0U);
        umicomMappedFailAllocation = 0U; umicomMappedFailFree = 0U;
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        UmicomMappedFinish();
    } else if (strcmp(name, "interleaved-spaces") == 0) {
        UmicomU64 a = UmicomMappedBuild();
        CHECK(UmicomKernelMappedSpaceInitialize(&umicomMappedOther) == UMICOM_MAPPED_OK);
        UmicomU64 b = UmicomMappedAdd(&umicomMappedOther, BASE, 2U);
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOther) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedRegionWrite(&umicomMappedOwner, a, 0U, umicomMappedSeed, 200U) == UMICOM_MAPPED_OK);
        CHECK(UmicomKernelMappedRegionRead(&umicomMappedOther, b, 0U, umicomMappedCopy, 200U) == UMICOM_MAPPED_OK);
        for (UmicomSize i = 0U; i < 200U; ++i) CHECK(umicomMappedCopy[i] == 0U);
        CHECK(UmicomKernelMappedSpaceClose(&umicomMappedOther) == UMICOM_MAPPED_OK); UmicomMappedFinish();
    } else if (strcmp(name, "repeated-lifetimes") == 0) {
        for (unsigned lifetime = 0U; lifetime < 1000U; ++lifetime) {
            UmicomMappedSetup(PAGES); (void)UmicomMappedBuild(); UmicomMappedFinish();
        }
    } else if (strcmp(name, "maximum-layout") == 0) {
        for (UmicomSize region = 0U; region < 8U; ++region)
            (void)UmicomMappedAdd(&umicomMappedOwner, BASE + region * 0x40000000ULL, 8U);
        CHECK(UmicomKernelMappedSpaceBuild(&umicomMappedOwner) == UMICOM_MAPPED_OK);
        CHECK(umicomMappedOwner.hierarchy.mappedPages == 64U); UmicomMappedFinish();
    } else if (strcmp(name, "snapshot-accounting") == 0) {
        (void)UmicomMappedBuild(); UmicomKernelMappedSpaceInfo info = {0};
        CHECK(UmicomKernelMappedSpaceSnapshot(&umicomMappedOwner, &info) == UMICOM_MAPPED_OK);
        CHECK(info.dataFrames == 2U && info.tableFrames == 3U && info.mappedPages == 2U && info.regions == 1U);
        UmicomMappedFinish();
    } else if (strcmp(name, "output-unchanged") == 0) {
        (void)UmicomMappedBuild(); UmicomKernelMappedRegionInfo info, copy;
        memset(&info, 0xaa, sizeof(info)); memcpy(&copy, &info, sizeof(info));
        CHECK(UmicomKernelMappedRegionQuery(&umicomMappedOwner, 99U, &info) == UMICOM_MAPPED_INVALID_HANDLE);
        CHECK(memcmp(&info, &copy, sizeof(info)) == 0); UmicomMappedFinish();
    } else if (strcmp(name, "status-names") == 0) {
        for (int status = 0; status <= (int)UMICOM_MAPPED_TICKET_EXHAUSTED; ++status)
            CHECK(strcmp(UmicomKernelMappedStatusName((UmicomKernelMappedStatus)status), "unknown-mapped-status") != 0);
    } else {
        fprintf(stderr, "unknown native case: %s\n", name); return 2;
    }
    printf("mapped-region native check passed: %s\n", name);
    return 0;
}
