/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/object_cache.c
 *
 * PURPOSE:
 *   Own small Kernel allocations without an intrusive free list or a heap.
 *
 * EDUCATIONAL OVERVIEW:
 *   Each frame is obtained independently from the physical allocator. A cache
 *   never assumes the next frame is adjacent to the previous one. The caller
 *   chooses one object size and alignment when creating a cache; allocation
 *   then selects an empty slot with that geometry.
 *
 *   The object's bytes are not our bookkeeping. Addresses and allocation
 *   tickets live in the stable cache owner. Free checks the reference before
 *   clearing data, and a new allocation gets a new ticket even at the same
 *   address. Tail guards catch some overwrites, but cannot stop a raw C pointer
 *   from writing stale memory. This service is for trusted Kernel components.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/object_cache.h"

static UmicomBoolean UmicomObjectGeometry(UmicomSize bytes, UmicomSize alignment,
    UmicomSize *outStride, UmicomSize *outSlots)
{
    /* Bound values before adding them. With these bounds the rounded stride
     * fits one page, or is explicitly refused; no wrapping size becomes small. */
    if (bytes == 0U || bytes > UMICOM_OBJECT_CACHE_MAX_BYTES || alignment == 0U ||
        alignment > UMICOM_KERNEL_PAGE_SIZE || (alignment & (alignment - 1U)) != 0U) {
        return UMICOM_FALSE;
    }
    const UmicomSize effective = alignment < 16U ? 16U : alignment;
    const UmicomSize stride = (bytes + UMICOM_OBJECT_CACHE_GUARD_BYTES + effective - 1U) & ~(effective - 1U);
    if (stride > UMICOM_KERNEL_PAGE_SIZE) return UMICOM_FALSE;
    *outStride = stride;
    *outSlots = UMICOM_KERNEL_PAGE_SIZE / stride;
    return UMICOM_TRUE;
}

static void UmicomObjectClear(UmicomAddress address, UmicomSize bytes)
{
    /* Volatile stores prevent a dead-store optimisation from removing the
     * scrub when an object/frame is about to leave this owner's lifetime. */
    volatile UmicomU8 *const target = (volatile UmicomU8 *)address;
    for (UmicomSize index = 0U; index < bytes; ++index) target[index] = 0U;
}

static UmicomU8 UmicomObjectGuardByte(UmicomU64 ticket, UmicomSize index)
{
    /* This is a diagnostic pattern, not a cryptographic authenticator. Varying
     * it with the ticket makes an old slot image less likely to look intact. */
    return (UmicomU8)((ticket ^ (UmicomU64)index ^ 0xa7U) & 0xffU);
}

static UmicomKernelObjectStatus UmicomObjectInspect(const UmicomKernelObjectCache *cache)
{
    /* Check metadata geometry before it can control an address calculation.
     * self detects a copied owner; it is not protection from malicious C code. */
    if (cache->self != cache || cache->state < UMICOM_OBJECT_CACHE_OPEN ||
        cache->state > UMICOM_OBJECT_CACHE_CLOSED) return UMICOM_OBJECT_BAD_STATE;
    UmicomSize stride = 0U;
    UmicomSize slots = 0U;
    if (UmicomObjectGeometry(cache->objectBytes, cache->alignment, &stride, &slots) == UMICOM_FALSE ||
        cache->stride != stride || cache->slotsPerFrame != slots ||
        cache->frameLimit == 0U || cache->frameLimit > UMICOM_OBJECT_CACHE_FRAME_LIMIT) {
        return UMICOM_OBJECT_CORRUPT_STATE;
    }
    UmicomSize frames = 0U;
    UmicomSize live = 0U;
    for (UmicomSize pageIndex = 0U; pageIndex < UMICOM_OBJECT_CACHE_FRAME_LIMIT; ++pageIndex) {
        const UmicomKernelObjectPage *const page = &cache->pages[pageIndex];
        if (page->owned != UMICOM_FALSE && page->owned != UMICOM_TRUE) return UMICOM_OBJECT_CORRUPT_STATE;
        if (page->owned == UMICOM_FALSE) {
            /* Empty metadata may not hide a frame or a surviving reference. */
            if (page->frame != 0U || page->live != 0U) return UMICOM_OBJECT_CORRUPT_STATE;
        } else {
            if (pageIndex >= cache->frameLimit || page->frame == 0U) return UMICOM_OBJECT_CORRUPT_STATE;
            UmicomKernelPhysicalFrameState state;
            /* Query uses the allocator's actual bitmap. Refuse before reading
             * guard bytes if the frame is reserved, missing or out of range. */
            if (UmicomKernelPhysicalMemoryFrameQuery(page->frame, &state) != UMICOM_KERNEL_MEMORY_OK ||
                state != UMICOM_PHYSICAL_FRAME_ALLOCATED) return UMICOM_OBJECT_CORRUPT_STATE;
            for (UmicomSize earlier = 0U; earlier < pageIndex; ++earlier) {
                if (cache->pages[earlier].owned != UMICOM_FALSE &&
                    cache->pages[earlier].frame == page->frame) return UMICOM_OBJECT_CORRUPT_STATE;
            }
            ++frames;
        }
        UmicomSize pageLive = 0U;
        for (UmicomSize slot = 0U; slot < UMICOM_OBJECT_CACHE_SLOT_LIMIT; ++slot) {
            const UmicomU64 ticket = page->tickets[slot];
            if (ticket == 0U) continue;
            if (page->owned == UMICOM_FALSE || slot >= slots || ticket > cache->issued) {
                return UMICOM_OBJECT_CORRUPT_STATE;
            }
            ++pageLive;
            const volatile UmicomU8 *const object = (const volatile UmicomU8 *)(page->frame + slot * stride);
            /* No allocator decision reads a link from user-writable object
             * data. Inspect only the diagnostic tail beyond its declared size. */
            for (UmicomSize byte = cache->objectBytes; byte < stride; ++byte) {
                if (object[byte] != UmicomObjectGuardByte(ticket, byte)) return UMICOM_OBJECT_CORRUPT_STATE;
            }
        }
        if (page->live != pageLive) return UMICOM_OBJECT_CORRUPT_STATE;
        live += pageLive;
    }
    /* Recount rather than trusting the stored totals used by admission. */
    if (frames != cache->frames || live != cache->liveObjects || live > cache->issued ||
        cache->peakObjects < live || cache->peakObjects > cache->frameLimit * slots) {
        return UMICOM_OBJECT_CORRUPT_STATE;
    }
    if (cache->state == UMICOM_OBJECT_CACHE_CLOSING && live != 0U) return UMICOM_OBJECT_CORRUPT_STATE;
    if (cache->state == UMICOM_OBJECT_CACHE_CLOSED && (frames != 0U || live != 0U)) return UMICOM_OBJECT_CORRUPT_STATE;
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheValidate(UmicomKernelObjectCache *cache)
{
    if (cache == (UmicomKernelObjectCache *)0) return UMICOM_OBJECT_INVALID_ARGUMENT;
    if (UmicomKernelObjectCacheAccessAllowed() == UMICOM_FALSE) return UMICOM_OBJECT_UNSAFE_CONTEXT;
    if (cache->self != cache || cache->state == UMICOM_OBJECT_CACHE_UNINITIALISED) return UMICOM_OBJECT_BAD_STATE;
    if (cache->state == UMICOM_OBJECT_CACHE_POISONED) return UMICOM_OBJECT_CORRUPT_STATE;
    const UmicomKernelObjectStatus status = UmicomObjectInspect(cache);
    if (status != UMICOM_OBJECT_OK) {
        /* Do not scrub or free through suspect ownership records. Retain the
         * original addresses, tickets and counts for a debugger to inspect. */
        cache->state = UMICOM_OBJECT_CACHE_POISONED;
        return UMICOM_OBJECT_CORRUPT_STATE;
    }
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheInitialize(UmicomKernelObjectCache *cache,
    UmicomSize objectBytes, UmicomSize alignment, UmicomSize frameLimit)
{
    if (cache == (UmicomKernelObjectCache *)0) return UMICOM_OBJECT_INVALID_ARGUMENT;
    if (UmicomKernelObjectCacheAccessAllowed() == UMICOM_FALSE) return UMICOM_OBJECT_UNSAFE_CONTEXT;
    UmicomSize stride = 0U;
    UmicomSize slots = 0U;
    if (frameLimit == 0U || frameLimit > UMICOM_OBJECT_CACHE_FRAME_LIMIT ||
        UmicomObjectGeometry(objectBytes, alignment, &stride, &slots) == UMICOM_FALSE) {
        return UMICOM_OBJECT_INVALID_ARGUMENT;
    }
    const unsigned char *const bytes = (const unsigned char *)cache;
    for (UmicomSize index = 0U; index < sizeof(*cache); ++index) {
        /* Reject dirty or previously used storage. Resetting ticket history
         * would make an old reference capable of naming a future allocation. */
        if (bytes[index] != 0U) return UMICOM_OBJECT_BAD_STATE;
    }
    UmicomKernelPhysicalMemorySnapshot physical;
    if (UmicomKernelPhysicalMemorySnapshotRead(&physical) != UMICOM_KERNEL_MEMORY_OK ||
        physical.ramBase == 0U || physical.pageBytes != UMICOM_KERNEL_PAGE_SIZE ||
        UmicomKernelPhysicalMemoryValidate() != UMICOM_KERNEL_MEMORY_OK) return UMICOM_OBJECT_BACKEND_ERROR;
    /* Publish only after every prerequisite succeeds. There is no frame or
     * hidden allocation to roll back during creation of the empty cache. */
    cache->self = cache;
    cache->objectBytes = objectBytes;
    cache->alignment = alignment;
    cache->stride = stride;
    cache->slotsPerFrame = slots;
    cache->frameLimit = frameLimit;
    cache->state = UMICOM_OBJECT_CACHE_OPEN;
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheAllocate(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference *outReference)
{
    if (outReference == (UmicomKernelObjectReference *)0) return UMICOM_OBJECT_INVALID_ARGUMENT;
    UmicomKernelObjectStatus status = UmicomKernelObjectCacheValidate(cache);
    if (status != UMICOM_OBJECT_OK) return status;
    if (cache->state != UMICOM_OBJECT_CACHE_OPEN) return UMICOM_OBJECT_BAD_STATE;
    if (cache->issued == ~(UmicomU64)0U) return UMICOM_OBJECT_TICKET_EXHAUSTED;
    UmicomSize selected = cache->frameLimit;
    UmicomSize empty = cache->frameLimit;
    for (UmicomSize index = 0U; index < cache->frameLimit; ++index) {
        if (cache->pages[index].owned == UMICOM_FALSE) {
            if (empty == cache->frameLimit) empty = index;
        } else if (cache->pages[index].live < cache->slotsPerFrame) {
            selected = index;
            break;
        }
    }
    if (selected == cache->frameLimit) {
        if (empty == cache->frameLimit) return UMICOM_OBJECT_CAPACITY;
        UmicomAddress frame = 0U;
        const UmicomKernelMemoryStatus memory = UmicomKernelPhysicalMemoryAllocateFrame(&frame);
        if (memory == UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY) return UMICOM_OBJECT_OUT_OF_MEMORY;
        if (memory != UMICOM_KERNEL_MEMORY_OK) return UMICOM_OBJECT_BACKEND_ERROR;
        /* All later steps are bounded writes with no remaining fallible resource
         * acquisition. Keep the new frame in the owner before touching its data. */
        selected = empty;
        cache->pages[selected].frame = frame;
        cache->pages[selected].owned = UMICOM_TRUE;
        ++cache->frames;
        if (frame == 0U || (frame % UMICOM_KERNEL_PAGE_SIZE) != 0U) {
            cache->state = UMICOM_OBJECT_CACHE_POISONED;
            return UMICOM_OBJECT_CORRUPT_STATE;
        }
        UmicomKernelPhysicalFrameState frameState;
        if (UmicomKernelPhysicalMemoryFrameQuery(frame, &frameState) != UMICOM_KERNEL_MEMORY_OK ||
            frameState != UMICOM_PHYSICAL_FRAME_ALLOCATED) {
            cache->state = UMICOM_OBJECT_CACHE_POISONED;
            return UMICOM_OBJECT_CORRUPT_STATE;
        }
        for (UmicomSize other = 0U; other < cache->frameLimit; ++other) {
            if (other != selected && cache->pages[other].owned != UMICOM_FALSE &&
                cache->pages[other].frame == frame) {
                cache->state = UMICOM_OBJECT_CACHE_POISONED;
                return UMICOM_OBJECT_CORRUPT_STATE;
            }
        }
        UmicomObjectClear(frame, UMICOM_KERNEL_PAGE_SIZE);
    }
    UmicomKernelObjectPage *const page = &cache->pages[selected];
    UmicomSize slot = 0U;
    while (slot < cache->slotsPerFrame && page->tickets[slot] != 0U) ++slot;
    /* Validate already proved that this page contains a free slot. */
    if (slot == cache->slotsPerFrame) {
        cache->state = UMICOM_OBJECT_CACHE_POISONED;
        return UMICOM_OBJECT_CORRUPT_STATE;
    }
    const UmicomAddress address = page->frame + slot * cache->stride;
    const UmicomU64 ticket = cache->issued + 1U;
    UmicomObjectClear(address, cache->stride);
    volatile UmicomU8 *const object = (volatile UmicomU8 *)address;
    for (UmicomSize byte = cache->objectBytes; byte < cache->stride; ++byte) {
        object[byte] = UmicomObjectGuardByte(ticket, byte);
    }
    /* The complete zeroed object and guard exist before its reference becomes
     * visible. A refusal above has not published a half-initialised object. */
    page->tickets[slot] = ticket;
    ++page->live;
    ++cache->liveObjects;
    cache->issued = ticket;
    if (cache->liveObjects > cache->peakObjects) cache->peakObjects = cache->liveObjects;
    outReference->cache = cache;
    outReference->address = (void *)address;
    outReference->ticket = ticket;
    return UMICOM_OBJECT_OK;
}

static UmicomKernelObjectStatus UmicomObjectLocate(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference reference, UmicomSize *outPage, UmicomSize *outSlot)
{
    if (reference.cache != cache || reference.address == (void *)0 || reference.ticket == 0U) {
        return UMICOM_OBJECT_INVALID_REFERENCE;
    }
    const UmicomAddress address = (UmicomAddress)reference.address;
    for (UmicomSize index = 0U; index < cache->frameLimit; ++index) {
        const UmicomKernelObjectPage *const page = &cache->pages[index];
        /* Subtract only after ordering the addresses. A forged high address
         * then becomes a large offset, not a wrapping frame-end comparison. */
        if (page->owned == UMICOM_FALSE || address < page->frame) continue;
        const UmicomAddress offset = address - page->frame;
        if (offset >= UMICOM_KERNEL_PAGE_SIZE || (offset % cache->stride) != 0U) continue;
        const UmicomSize slot = offset / cache->stride;
        if (slot >= cache->slotsPerFrame || page->tickets[slot] != reference.ticket) continue;
        *outPage = index;
        *outSlot = slot;
        return UMICOM_OBJECT_OK;
    }
    return UMICOM_OBJECT_INVALID_REFERENCE;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheResolve(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference reference, void **outObject)
{
    if (outObject == (void **)0) return UMICOM_OBJECT_INVALID_ARGUMENT;
    const UmicomKernelObjectStatus status = UmicomKernelObjectCacheValidate(cache);
    if (status != UMICOM_OBJECT_OK) return status;
    if (cache->state != UMICOM_OBJECT_CACHE_OPEN) return UMICOM_OBJECT_BAD_STATE;
    UmicomSize page = 0U;
    UmicomSize slot = 0U;
    const UmicomKernelObjectStatus found = UmicomObjectLocate(cache, reference, &page, &slot);
    if (found != UMICOM_OBJECT_OK) return found;
    /* No payload bytes are interpreted while checking the reference. */
    *outObject = reference.address;
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheFree(UmicomKernelObjectCache *cache,
    UmicomKernelObjectReference reference)
{
    const UmicomKernelObjectStatus status = UmicomKernelObjectCacheValidate(cache);
    if (status != UMICOM_OBJECT_OK) return status;
    if (cache->state != UMICOM_OBJECT_CACHE_OPEN) return UMICOM_OBJECT_BAD_STATE;
    UmicomSize page = 0U;
    UmicomSize slot = 0U;
    const UmicomKernelObjectStatus found = UmicomObjectLocate(cache, reference, &page, &slot);
    if (found != UMICOM_OBJECT_OK) return found;
    /* Scrub while the ticket still owns the slot. No successful Free can leave
     * old object data for the next allocation. Retain the frame until Trim. */
    UmicomObjectClear((UmicomAddress)reference.address, cache->stride);
    cache->pages[page].tickets[slot] = 0U;
    --cache->pages[page].live;
    --cache->liveObjects;
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheTrim(UmicomKernelObjectCache *cache,
    UmicomSize *outReleased)
{
    if (outReleased == (UmicomSize *)0) return UMICOM_OBJECT_INVALID_ARGUMENT;
    const UmicomKernelObjectStatus status = UmicomKernelObjectCacheValidate(cache);
    if (status != UMICOM_OBJECT_OK) return status;
    *outReleased = 0U;
    for (UmicomSize index = 0U; index < cache->frameLimit; ++index) {
        UmicomKernelObjectPage *const page = &cache->pages[index];
        if (page->owned == UMICOM_FALSE || page->live != 0U) continue;
        UmicomObjectClear(page->frame, UMICOM_KERNEL_PAGE_SIZE);
        if (UmicomKernelPhysicalMemoryFreeFrame(page->frame) != UMICOM_KERNEL_MEMORY_OK) {
            /* The allocator refused ownership transfer. Keep this frame and
             * its empty slot catalogue; a retry must not free earlier pages twice. */
            return UMICOM_OBJECT_RELEASE_FAILED;
        }
        page->owned = UMICOM_FALSE;
        page->frame = 0U;
        --cache->frames;
        ++*outReleased;
    }
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheClose(UmicomKernelObjectCache *cache)
{
    const UmicomKernelObjectStatus status = UmicomKernelObjectCacheValidate(cache);
    if (status != UMICOM_OBJECT_OK) return status;
    if (cache->state == UMICOM_OBJECT_CACHE_CLOSED) return UMICOM_OBJECT_OK;
    if (cache->liveObjects != 0U) return UMICOM_OBJECT_BUSY;
    /* Freeze admission before starting possibly partial frame release. The
     * object's identity and ticket high-water mark are never reset on retry. */
    cache->state = UMICOM_OBJECT_CACHE_CLOSING;
    UmicomSize released = 0U;
    const UmicomKernelObjectStatus trimmed = UmicomKernelObjectCacheTrim(cache, &released);
    if (trimmed != UMICOM_OBJECT_OK) return trimmed;
    cache->state = UMICOM_OBJECT_CACHE_CLOSED;
    return UMICOM_OBJECT_OK;
}

UmicomKernelObjectStatus UmicomKernelObjectCacheSnapshot(UmicomKernelObjectCache *cache,
    UmicomKernelObjectCacheInfo *outInfo)
{
    if (outInfo == (UmicomKernelObjectCacheInfo *)0) return UMICOM_OBJECT_INVALID_ARGUMENT;
    const UmicomKernelObjectStatus status = UmicomKernelObjectCacheValidate(cache);
    if (status != UMICOM_OBJECT_OK) return status;
    UmicomSize empty = 0U;
    for (UmicomSize index = 0U; index < cache->frameLimit; ++index) {
        if (cache->pages[index].owned != UMICOM_FALSE && cache->pages[index].live == 0U) ++empty;
    }
    /* Return values rather than exposing additional ownership pointers. The
     * backing bytes include diagnostic padding and empty cached slots. */
    /* Explicit stores avoid a compiler-generated memcpy dependency in a
     * freestanding image which does not link a hosted C runtime. */
    outInfo->state = cache->state;
    outInfo->objectBytes = cache->objectBytes;
    outInfo->alignment = cache->alignment;
    outInfo->stride = cache->stride;
    outInfo->slotsPerFrame = cache->slotsPerFrame;
    outInfo->frames = cache->frames;
    outInfo->liveObjects = cache->liveObjects;
    outInfo->emptyFrames = empty;
    outInfo->liveBytes = cache->liveObjects * cache->objectBytes;
    outInfo->reservedBytes = cache->frames * UMICOM_KERNEL_PAGE_SIZE;
    outInfo->peakObjects = cache->peakObjects;
    outInfo->issued = cache->issued;
    return UMICOM_OBJECT_OK;
}

const char *UmicomKernelObjectStatusName(UmicomKernelObjectStatus status)
{
    switch (status) {
        case UMICOM_OBJECT_OK: return "ok";
        case UMICOM_OBJECT_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_OBJECT_BAD_STATE: return "bad-state";
        case UMICOM_OBJECT_UNSAFE_CONTEXT: return "unsafe-context";
        case UMICOM_OBJECT_INVALID_REFERENCE: return "invalid-reference";
        case UMICOM_OBJECT_CAPACITY: return "capacity";
        case UMICOM_OBJECT_OUT_OF_MEMORY: return "out-of-memory";
        case UMICOM_OBJECT_TICKET_EXHAUSTED: return "ticket-exhausted";
        case UMICOM_OBJECT_BUSY: return "live-objects";
        case UMICOM_OBJECT_BACKEND_ERROR: return "backend-error";
        case UMICOM_OBJECT_RELEASE_FAILED: return "release-failed";
        case UMICOM_OBJECT_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-object-status";
    }
}
