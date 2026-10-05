/*-----------------------------------------------------------------------------
 * Umicom Kernel object-cache native tests
 * File: tests/object_cache/object_cache_tests.c
 *
 * PURPOSE:
 *   Exercise the actual cache and physical allocator in a page-aligned host
 *   arena. Only execution admission and explicit frame-release failures are
 *   modelled; object storage, tickets, guards and bitmap accounting are real.
 *
 * EDUCATIONAL NOTE:
 *   Corruption tests deliberately edit otherwise-private metadata or guards.
 *   Production callers must never perform those edits. A poisoned case checks
 *   that frames remain allocated rather than pretending they were reclaimed.
 *   The host process then ends; this is not a Kernel recovery mechanism.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/object_cache.h"

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)
#define ARENA_PAGES 96U
alignas(4096) static UmicomU8 umicomObjectArena[ARENA_PAGES * 4096U];
static UmicomKernelObjectCache umicomCache;
static UmicomKernelObjectCache umicomSecondCache;
static UmicomBoolean umicomAllowed = UMICOM_TRUE;
static unsigned umicomFreeCalls;
static unsigned umicomRefuseFree;

/* Only the architecture gate is a model. Native C cannot read RV64 machine
 * CSRs; the normal guest links arch/riscv64/object_cache.c instead. */
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void) { return umicomAllowed; }
UmicomKernelMemoryStatus UmicomObjectActualFrameFree(UmicomAddress frame);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress frame)
{
    ++umicomFreeCalls;
    if (umicomRefuseFree != 0U && umicomFreeCalls == umicomRefuseFree)
        return UMICOM_KERNEL_MEMORY_NOT_ALLOCATED;
    return UmicomObjectActualFrameFree(frame);
}

static void UmicomObjectTestSetup(UmicomSize pages)
{
    /* Each reset represents a new isolated test environment, not a supported
     * way to reinitialise a live cache or resurrect allocation references. */
    memset(&umicomCache, 0, sizeof(umicomCache));
    memset(&umicomSecondCache, 0, sizeof(umicomSecondCache));
    memset(umicomObjectArena, 0xdd, sizeof(umicomObjectArena));
    umicomAllowed = UMICOM_TRUE;
    umicomFreeCalls = 0U;
    umicomRefuseFree = 0U;
    REQUIRE(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomObjectArena, pages * 4096U) == UMICOM_KERNEL_MEMORY_OK);
}
static void UmicomObjectTestInit(UmicomSize bytes, UmicomSize alignment, UmicomSize limit)
{
    REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, bytes, alignment, limit) == UMICOM_OBJECT_OK);
}
static UmicomKernelObjectReference UmicomObjectTestAllocate(void)
{
    UmicomKernelObjectReference reference = {0};
    REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &reference) == UMICOM_OBJECT_OK);
    REQUIRE(reference.ticket != 0U && reference.cache == &umicomCache && reference.address != NULL);
    return reference;
}
static UmicomU64 UmicomObjectAllocatedFrames(void)
{
    UmicomKernelPhysicalMemorySnapshot snapshot;
    REQUIRE(UmicomKernelPhysicalMemorySnapshotRead(&snapshot) == UMICOM_KERNEL_MEMORY_OK);
    REQUIRE(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    return snapshot.allocatedFrames;
}
static void UmicomObjectCheckZero(const void *memory, UmicomSize bytes)
{
    const UmicomU8 *const data = memory;
    for (UmicomSize index = 0U; index < bytes; ++index) REQUIRE(data[index] == 0U);
}
static void UmicomObjectFinish(void)
{
    REQUIRE(UmicomKernelObjectCacheClose(&umicomCache) == UMICOM_OBJECT_OK);
    REQUIRE(UmicomKernelObjectCacheValidate(&umicomCache) == UMICOM_OBJECT_OK);
    REQUIRE(UmicomObjectAllocatedFrames() == 0U);
}

static const char *const umicomObjectCases[] = {
    "null-init", "size-zero", "size-too-large", "alignment-zero", "alignment-non-power",
    "alignment-too-large", "quota-zero", "quota-too-large", "dirty-storage", "repeated-init",
    "copied-cache", "unsafe-init", "backend-uninitialised", "lazy-init", "geometry-grid",
    "small-objects-share-frame", "page-alignment", "noncontiguous-frames", "initial-zeroing",
    "free-scrubs-padding", "close-live-refused", "trim-live-preserved", "trim-empty",
    "reuse-cached-slot", "trim-reuse-stale-ticket", "wrong-cache", "forged-ticket",
    "interior-pointer", "foreign-pointer", "null-reference", "zero-ticket", "double-free",
    "cache-capacity", "physical-exhaustion", "exhaustion-recovery", "ticket-exhaustion",
    "guard-poison-retains", "externally-freed-frame", "reserved-backing-frame", "duplicate-frame",
    "counter-corruption", "unused-slot-corruption", "out-of-range-frame", "invalid-state",
    "trim-partial-retry", "close-partial-retry", "release-refusal-retains", "outputs-unchanged",
    "repeated-lifetimes", "allocation-budgets", "interleaved-lifetimes", "frame-query-uninitialised",
    "frame-query-null", "frame-query-alignment", "frame-query-range", "frame-query-classes",
    "frame-query-nonmutating", "resolve-live", "unsafe-operations", "closed-not-reinitialised",
    "status-names", "null-outputs", "trim-retry-every-position", "object-data-not-metadata"
};

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    UmicomSize selected = sizeof(umicomObjectCases) / sizeof(umicomObjectCases[0]);
    for (UmicomSize index = 0U; index < sizeof(umicomObjectCases) / sizeof(umicomObjectCases[0]); ++index)
        if (strcmp(argv[1], umicomObjectCases[index]) == 0) selected = index;
    if (selected == sizeof(umicomObjectCases) / sizeof(umicomObjectCases[0])) return 2;
    /* These two cases must see the allocator's genuine initial static state. */
    if (selected != 12U && selected != 51U) UmicomObjectTestSetup(ARENA_PAGES);
    switch (selected) {
        case 0:
            REQUIRE(UmicomKernelObjectCacheInitialize(NULL, 32U, 16U, 1U) == UMICOM_OBJECT_INVALID_ARGUMENT); break;
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: {
            const UmicomSize sizes[] = {0U, 4081U, 32U, 32U, 32U, 32U, 32U};
            const UmicomSize aligns[] = {16U, 16U, 0U, 3U, 8192U, 16U, 16U};
            const UmicomSize limits[] = {1U, 1U, 1U, 1U, 1U, 0U, 17U};
            const UmicomSize index = selected - 1U;
            REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, sizes[index], aligns[index], limits[index]) == UMICOM_OBJECT_INVALID_ARGUMENT);
            UmicomObjectCheckZero(&umicomCache, sizeof(umicomCache));
            REQUIRE(UmicomObjectAllocatedFrames() == 0U); break;
        }
        case 8:
            umicomCache.issued = 7U;
            REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, 32U, 16U, 1U) == UMICOM_OBJECT_BAD_STATE);
            REQUIRE(umicomCache.issued == 7U); break;
        case 9:
            UmicomObjectTestInit(32U, 16U, 1U);
            REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, 32U, 16U, 1U) == UMICOM_OBJECT_BAD_STATE);
            UmicomObjectFinish(); break;
        case 10:
            UmicomObjectTestInit(32U, 16U, 1U);
            umicomSecondCache = umicomCache;
            REQUIRE(UmicomKernelObjectCacheValidate(&umicomSecondCache) == UMICOM_OBJECT_BAD_STATE);
            REQUIRE(umicomCache.state == UMICOM_OBJECT_CACHE_OPEN); UmicomObjectFinish(); break;
        case 11:
            umicomAllowed = UMICOM_FALSE;
            REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, 32U, 16U, 1U) == UMICOM_OBJECT_UNSAFE_CONTEXT);
            UmicomObjectCheckZero(&umicomCache, sizeof(umicomCache)); break;
        case 12:
            REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, 32U, 16U, 1U) == UMICOM_OBJECT_BACKEND_ERROR);
            UmicomObjectCheckZero(&umicomCache, sizeof(umicomCache)); break;
        case 13: {
            UmicomObjectTestInit(33U, 16U, 3U);
            UmicomKernelObjectCacheInfo info;
            REQUIRE(UmicomKernelObjectCacheSnapshot(&umicomCache, &info) == UMICOM_OBJECT_OK);
            REQUIRE(info.frames == 0U && info.liveBytes == 0U && info.stride == 64U && info.slotsPerFrame == 64U);
            UmicomObjectFinish(); break;
        }
        case 14: case 16: {
            const UmicomSize sizes[] = {1U, 15U, 16U, 17U, 33U, 64U, 65U, 127U, 513U, 2048U, 4080U};
            for (UmicomSize s = 0U; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
                for (UmicomSize alignment = 1U; alignment <= 4096U; alignment *= 2U) {
                    UmicomObjectTestSetup(ARENA_PAGES);
                    UmicomObjectTestInit(sizes[s], alignment, 2U);
                    const UmicomKernelObjectReference ref = UmicomObjectTestAllocate();
                    REQUIRE(((UmicomAddress)ref.address % alignment) == 0U);
                    REQUIRE(umicomCache.stride >= sizes[s] + 16U && umicomCache.slotsPerFrame >= 1U);
                    UmicomObjectCheckZero(ref.address, sizes[s]);
                    REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, ref) == UMICOM_OBJECT_OK);
                    UmicomObjectFinish();
                }
            } break;
        }
        case 15: case 17: {
            UmicomObjectTestInit(64U, 64U, 3U);
            UmicomKernelObjectReference refs[40];
            UmicomAddress blocker = 0U;
            const UmicomSize count = selected == 15U ? 32U : 40U;
            for (UmicomSize i = 0U; i < count; ++i) {
                if (i == 32U) REQUIRE(UmicomKernelPhysicalMemoryAllocateFrame(&blocker) == UMICOM_KERNEL_MEMORY_OK);
                refs[i] = UmicomObjectTestAllocate();
                memset(refs[i].address, (int)i, 64U);
            }
            if (selected == 17U) {
                REQUIRE((UmicomAddress)refs[32].address != (UmicomAddress)refs[0].address + 4096U);
                REQUIRE(UmicomKernelPhysicalMemoryFreeFrame(blocker) == UMICOM_KERNEL_MEMORY_OK);
            }
            REQUIRE(umicomCache.frames == (selected == 15U ? 1U : 2U));
            for (UmicomSize i = 0U; i < count; ++i) {
                const UmicomU8 *const data = refs[i].address;
                for (UmicomSize b = 0U; b < 64U; ++b) REQUIRE(data[b] == (UmicomU8)i);
                REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, refs[i]) == UMICOM_OBJECT_OK);
            }
            UmicomObjectFinish(); break;
        }
        case 18: case 19: case 20: case 21: case 22: case 23: case 24: {
            UmicomObjectTestInit(61U, 64U, 2U);
            const UmicomKernelObjectReference old = UmicomObjectTestAllocate();
            UmicomObjectCheckZero(old.address, 61U);
            memset(old.address, 0x5a, 61U);
            if (selected == 20U) REQUIRE(UmicomKernelObjectCacheClose(&umicomCache) == UMICOM_OBJECT_BUSY);
            if (selected == 21U) {
                UmicomSize trimmed = 99U;
                REQUIRE(UmicomKernelObjectCacheTrim(&umicomCache, &trimmed) == UMICOM_OBJECT_OK && trimmed == 0U);
                REQUIRE(*(UmicomU8 *)old.address == 0x5aU);
            }
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, old) == UMICOM_OBJECT_OK);
            /* This test observes storage through the fixture, not as a supported
             * use of a freed production pointer. The frame is still allocated. */
            if (selected == 19U) UmicomObjectCheckZero(old.address, umicomCache.stride);
            if (selected == 22U || selected == 24U) {
                UmicomSize trimmed = 99U;
                REQUIRE(UmicomKernelObjectCacheTrim(&umicomCache, &trimmed) == UMICOM_OBJECT_OK && trimmed == 1U);
            }
            const UmicomKernelObjectReference fresh = UmicomObjectTestAllocate();
            REQUIRE(fresh.address == old.address && fresh.ticket != old.ticket);
            UmicomObjectCheckZero(fresh.address, 61U);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, old) == UMICOM_OBJECT_INVALID_REFERENCE);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, fresh) == UMICOM_OBJECT_OK);
            UmicomObjectFinish(); break;
        }
        case 25: case 26: case 27: case 28: case 29: case 30: case 31: {
            UmicomObjectTestInit(64U, 16U, 2U);
            const UmicomKernelObjectReference original = UmicomObjectTestAllocate();
            UmicomKernelObjectReference invalid = original;
            if (selected == 25U) invalid.cache = &umicomSecondCache;
            if (selected == 26U) invalid.ticket += 17U;
            if (selected == 27U) invalid.address = (void *)((UmicomAddress)original.address + 1U);
            if (selected == 28U) invalid.address = (void *)(UmicomUIntPtr)(~(UmicomUIntPtr)0U);
            if (selected == 29U) invalid.address = NULL;
            if (selected == 30U) invalid.ticket = 0U;
            if (selected == 31U) REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, original) == UMICOM_OBJECT_OK);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, invalid) == UMICOM_OBJECT_INVALID_REFERENCE);
            void *output = (void *)umicomObjectArena;
            REQUIRE(UmicomKernelObjectCacheResolve(&umicomCache, invalid, &output) == UMICOM_OBJECT_INVALID_REFERENCE);
            REQUIRE(output == (void *)umicomObjectArena);
            if (selected != 31U) REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, original) == UMICOM_OBJECT_OK);
            UmicomObjectFinish(); break;
        }
        case 32: case 33: case 34: {
            if (selected != 32U) UmicomObjectTestSetup(1U);
            UmicomObjectTestInit(4080U, 4096U, selected == 32U ? 1U : 2U);
            const UmicomKernelObjectReference one = UmicomObjectTestAllocate();
            UmicomKernelObjectReference refused = {&umicomSecondCache, umicomObjectArena, 999U};
            REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &refused) ==
                (selected == 32U ? UMICOM_OBJECT_CAPACITY : UMICOM_OBJECT_OUT_OF_MEMORY));
            REQUIRE(refused.ticket == 999U && refused.cache == &umicomSecondCache);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, one) == UMICOM_OBJECT_OK);
            if (selected == 34U) {
                const UmicomKernelObjectReference retry = UmicomObjectTestAllocate();
                REQUIRE(retry.ticket == 2U);
                REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, retry) == UMICOM_OBJECT_OK);
            }
            UmicomObjectFinish(); break;
        }
        case 35: {
            UmicomObjectTestInit(32U, 16U, 2U);
            umicomCache.issued = ~(UmicomU64)0U - 1U; /* Explicit exhaustion injection. */
            const UmicomKernelObjectReference last = UmicomObjectTestAllocate();
            REQUIRE(last.ticket == ~(UmicomU64)0U);
            UmicomKernelObjectReference refused = {0};
            REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &refused) == UMICOM_OBJECT_TICKET_EXHAUSTED);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, last) == UMICOM_OBJECT_OK);
            REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &refused) == UMICOM_OBJECT_TICKET_EXHAUSTED);
            UmicomObjectFinish(); break;
        }
        case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43: {
            UmicomObjectTestInit(4080U, 4096U, 3U);
            const UmicomKernelObjectReference one = UmicomObjectTestAllocate();
            if (selected == 36U) ((UmicomU8 *)one.address)[4080] ^= 1U;
            if (selected == 37U || selected == 38U) {
                REQUIRE(UmicomKernelPhysicalMemoryFreeFrame((UmicomAddress)one.address) == UMICOM_KERNEL_MEMORY_OK);
                if (selected == 38U) REQUIRE(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)one.address, 4096U) == UMICOM_KERNEL_MEMORY_OK);
            }
            if (selected == 39U) {
                (void)UmicomObjectTestAllocate();
                umicomCache.pages[1].frame = umicomCache.pages[0].frame;
            }
            if (selected == 40U) ++umicomCache.liveObjects;
            if (selected == 41U) umicomCache.pages[0].tickets[2] = one.ticket;
            if (selected == 42U) umicomCache.pages[0].frame = (UmicomAddress)umicomObjectArena + sizeof(umicomObjectArena);
            if (selected == 43U) umicomCache.state = (UmicomKernelObjectCacheState)99;
            REQUIRE(UmicomKernelObjectCacheValidate(&umicomCache) == UMICOM_OBJECT_CORRUPT_STATE);
            REQUIRE(umicomCache.state == UMICOM_OBJECT_CACHE_POISONED);
            const UmicomU64 retained = UmicomObjectAllocatedFrames();
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, one) == UMICOM_OBJECT_CORRUPT_STATE);
            REQUIRE(UmicomKernelObjectCacheClose(&umicomCache) == UMICOM_OBJECT_CORRUPT_STATE);
            REQUIRE(UmicomObjectAllocatedFrames() == retained); break;
        }
        case 44: case 45: case 46: case 62: {
            const unsigned positions = selected == 62U ? 4U : 1U;
            for (unsigned position = 1U; position <= positions; ++position) {
                UmicomObjectTestSetup(ARENA_PAGES);
                UmicomObjectTestInit(4080U, 4096U, 4U);
                UmicomKernelObjectReference refs[4];
                for (UmicomSize i = 0U; i < 4U; ++i) refs[i] = UmicomObjectTestAllocate();
                for (UmicomSize i = 0U; i < 4U; ++i) REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, refs[i]) == UMICOM_OBJECT_OK);
                umicomRefuseFree = selected == 62U ? position : 2U;
                UmicomSize released = 99U;
                const UmicomKernelObjectStatus status = selected == 45U ? UmicomKernelObjectCacheClose(&umicomCache) :
                    UmicomKernelObjectCacheTrim(&umicomCache, &released);
                REQUIRE(status == UMICOM_OBJECT_RELEASE_FAILED);
                REQUIRE(umicomCache.frames == 5U - umicomRefuseFree);
                REQUIRE(UmicomObjectAllocatedFrames() == umicomCache.frames);
                if (selected == 45U) {
                    UmicomKernelObjectReference no = {0};
                    REQUIRE(umicomCache.state == UMICOM_OBJECT_CACHE_CLOSING);
                    REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &no) == UMICOM_OBJECT_BAD_STATE);
                } else {
                    REQUIRE(released == umicomRefuseFree - 1U && umicomCache.state == UMICOM_OBJECT_CACHE_OPEN);
                }
                REQUIRE(UmicomKernelObjectCacheValidate(&umicomCache) == UMICOM_OBJECT_OK);
                umicomRefuseFree = 0U;
                UmicomObjectFinish();
                REQUIRE(umicomFreeCalls == 5U); /* Four transfers and one refusal, no repeated free. */
            } break;
        }
        case 47: {
            UmicomKernelObjectReference out = {&umicomSecondCache, umicomObjectArena, 41U};
            REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &out) == UMICOM_OBJECT_BAD_STATE);
            REQUIRE(out.ticket == 41U && out.address == umicomObjectArena && out.cache == &umicomSecondCache);
            UmicomObjectTestInit(32U, 16U, 1U); UmicomObjectFinish(); break;
        }
        case 48: {
            UmicomObjectTestInit(113U, 16U, 2U);
            UmicomKernelObjectReference old = {0};
            for (UmicomU64 lifetime = 1U; lifetime <= 2000U; ++lifetime) {
                const UmicomKernelObjectReference fresh = UmicomObjectTestAllocate();
                REQUIRE(fresh.ticket == lifetime);
                if (old.ticket != 0U) REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, old) == UMICOM_OBJECT_INVALID_REFERENCE);
                UmicomObjectCheckZero(fresh.address, 113U);
                memset(fresh.address, 0xac, 113U);
                REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, fresh) == UMICOM_OBJECT_OK);
                old = fresh;
                if ((lifetime % 7U) == 0U) {
                    UmicomSize released = 0U;
                    REQUIRE(UmicomKernelObjectCacheTrim(&umicomCache, &released) == UMICOM_OBJECT_OK);
                }
            }
            UmicomObjectFinish(); break;
        }
        case 49: {
            for (UmicomSize budget = 1U; budget <= 17U; ++budget) {
                UmicomObjectTestSetup(budget);
                UmicomObjectTestInit(4080U, 4096U, 16U);
                UmicomKernelObjectReference refs[16];
                const UmicomSize admitted = budget < 16U ? budget : 16U;
                for (UmicomSize i = 0U; i < admitted; ++i) refs[i] = UmicomObjectTestAllocate();
                UmicomKernelObjectReference no = {0};
                REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &no) ==
                    (budget < 16U ? UMICOM_OBJECT_OUT_OF_MEMORY : UMICOM_OBJECT_CAPACITY));
                REQUIRE(umicomCache.liveObjects == admitted);
                for (UmicomSize i = 0U; i < admitted; ++i) REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, refs[i]) == UMICOM_OBJECT_OK);
                UmicomObjectFinish();
            } break;
        }
        case 50: {
            UmicomObjectTestInit(33U, 16U, 4U);
            UmicomKernelObjectReference refs[64] = {0};
            UmicomU64 random = 17U;
            UmicomSize live = 0U;
            for (UmicomSize step = 0U; step < 4000U; ++step) {
                random = random * 6364136223846793005ULL + 1U;
                const UmicomSize index = (random >> 32U) % 64U;
                if (refs[index].ticket == 0U) {
                    refs[index] = UmicomObjectTestAllocate();
                    memset(refs[index].address, (int)index, 33U);
                    ++live;
                } else {
                    const UmicomU8 *const bytes = refs[index].address;
                    for (UmicomSize b = 0U; b < 33U; ++b) REQUIRE(bytes[b] == (UmicomU8)index);
                    REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, refs[index]) == UMICOM_OBJECT_OK);
                    refs[index].ticket = 0U;
                    --live;
                }
                REQUIRE(umicomCache.liveObjects == live);
                REQUIRE(UmicomKernelObjectCacheValidate(&umicomCache) == UMICOM_OBJECT_OK);
            }
            for (UmicomSize index = 0U; index < 64U; ++index)
                if (refs[index].ticket != 0U) REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, refs[index]) == UMICOM_OBJECT_OK);
            UmicomObjectFinish(); break;
        }
        case 51: case 52: case 53: case 54: case 55: case 56: {
            UmicomKernelPhysicalFrameState state = (UmicomKernelPhysicalFrameState)99;
            const UmicomAddress base = (UmicomAddress)umicomObjectArena;
            if (selected == 51U) REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(base, &state) == UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
            if (selected == 52U) REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(base, NULL) == UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT);
            if (selected == 53U) REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(base + 1U, &state) == UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT);
            if (selected == 54U) REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(base + sizeof(umicomObjectArena), &state) == UMICOM_KERNEL_MEMORY_OUTSIDE_RAM);
            if (selected < 55U) REQUIRE(state == (UmicomKernelPhysicalFrameState)99);
            if (selected >= 55U) {
                REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(base, &state) == UMICOM_KERNEL_MEMORY_OK && state == UMICOM_PHYSICAL_FRAME_FREE);
                REQUIRE(UmicomKernelPhysicalMemoryReserveRange(base, 4096U) == UMICOM_KERNEL_MEMORY_OK);
                REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(base, &state) == UMICOM_KERNEL_MEMORY_OK && state == UMICOM_PHYSICAL_FRAME_RESERVED);
                UmicomAddress frame = 0U;
                REQUIRE(UmicomKernelPhysicalMemoryAllocateFrame(&frame) == UMICOM_KERNEL_MEMORY_OK);
                for (UmicomSize repeat = 0U; repeat < 100U; ++repeat)
                    REQUIRE(UmicomKernelPhysicalMemoryFrameQuery(frame, &state) == UMICOM_KERNEL_MEMORY_OK && state == UMICOM_PHYSICAL_FRAME_ALLOCATED);
                REQUIRE(UmicomObjectAllocatedFrames() == 1U);
                REQUIRE(UmicomKernelPhysicalMemoryFreeFrame(frame) == UMICOM_KERNEL_MEMORY_OK);
                REQUIRE(UmicomKernelPhysicalMemoryReleaseReservedRange(base, 4096U) == UMICOM_KERNEL_MEMORY_OK);
                REQUIRE(UmicomObjectAllocatedFrames() == 0U);
            } break;
        }
        case 57: {
            UmicomObjectTestInit(32U, 16U, 1U);
            const UmicomKernelObjectReference ref = UmicomObjectTestAllocate();
            void *out = NULL;
            REQUIRE(UmicomKernelObjectCacheResolve(&umicomCache, ref, &out) == UMICOM_OBJECT_OK && out == ref.address);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, ref) == UMICOM_OBJECT_OK);
            UmicomObjectFinish(); break;
        }
        case 58: {
            UmicomObjectTestInit(32U, 16U, 1U);
            const UmicomKernelObjectReference ref = UmicomObjectTestAllocate();
            umicomAllowed = UMICOM_FALSE;
            UmicomKernelObjectReference out = {0};
            UmicomKernelObjectCacheInfo info;
            UmicomSize released = 71U;
            void *object = NULL;
            REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, &out) == UMICOM_OBJECT_UNSAFE_CONTEXT);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, ref) == UMICOM_OBJECT_UNSAFE_CONTEXT);
            REQUIRE(UmicomKernelObjectCacheResolve(&umicomCache, ref, &object) == UMICOM_OBJECT_UNSAFE_CONTEXT);
            REQUIRE(UmicomKernelObjectCacheTrim(&umicomCache, &released) == UMICOM_OBJECT_UNSAFE_CONTEXT && released == 71U);
            REQUIRE(UmicomKernelObjectCacheSnapshot(&umicomCache, &info) == UMICOM_OBJECT_UNSAFE_CONTEXT);
            REQUIRE(UmicomKernelObjectCacheClose(&umicomCache) == UMICOM_OBJECT_UNSAFE_CONTEXT);
            REQUIRE(umicomCache.liveObjects == 1U);
            umicomAllowed = UMICOM_TRUE;
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, ref) == UMICOM_OBJECT_OK);
            UmicomObjectFinish(); break;
        }
        case 59:
            UmicomObjectTestInit(32U, 16U, 1U); UmicomObjectFinish();
            REQUIRE(UmicomKernelObjectCacheInitialize(&umicomCache, 32U, 16U, 1U) == UMICOM_OBJECT_BAD_STATE);
            REQUIRE(UmicomKernelObjectCacheClose(&umicomCache) == UMICOM_OBJECT_OK); break;
        case 60:
            for (int status = UMICOM_OBJECT_OK; status <= UMICOM_OBJECT_CORRUPT_STATE; ++status)
                REQUIRE(strcmp(UmicomKernelObjectStatusName((UmicomKernelObjectStatus)status), "unknown-object-status") != 0);
            REQUIRE(strcmp(UmicomKernelObjectStatusName((UmicomKernelObjectStatus)99), "unknown-object-status") == 0); break;
        case 61: {
            UmicomObjectTestInit(32U, 16U, 1U);
            const UmicomKernelObjectReference zero = {0};
            REQUIRE(UmicomKernelObjectCacheAllocate(&umicomCache, NULL) == UMICOM_OBJECT_INVALID_ARGUMENT);
            REQUIRE(UmicomKernelObjectCacheResolve(&umicomCache, zero, NULL) == UMICOM_OBJECT_INVALID_ARGUMENT);
            REQUIRE(UmicomKernelObjectCacheTrim(&umicomCache, NULL) == UMICOM_OBJECT_INVALID_ARGUMENT);
            REQUIRE(UmicomKernelObjectCacheSnapshot(&umicomCache, NULL) == UMICOM_OBJECT_INVALID_ARGUMENT);
            REQUIRE(UmicomKernelObjectCacheValidate(NULL) == UMICOM_OBJECT_INVALID_ARGUMENT);
            REQUIRE(UmicomKernelObjectCacheClose(NULL) == UMICOM_OBJECT_INVALID_ARGUMENT);
            UmicomObjectFinish(); break;
        }
        case 63: {
            UmicomObjectTestInit(64U, 16U, 1U);
            const UmicomKernelObjectReference ref = UmicomObjectTestAllocate();
            /* Deliberately write values resembling pointers throughout payload;
             * no allocator operation is allowed to follow them as free-list links. */
            memset(ref.address, 0xff, 64U);
            REQUIRE(UmicomKernelObjectCacheValidate(&umicomCache) == UMICOM_OBJECT_OK);
            REQUIRE(UmicomKernelObjectCacheFree(&umicomCache, ref) == UMICOM_OBJECT_OK);
            UmicomObjectFinish(); break;
        }
        default: return 2;
    }
    printf("PASS %s\n", argv[1]);
    return 0;
}
