/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/user_memory.c
 *
 * PURPOSE:
 *   Turn user virtual buffers into checked, bounded copies without exposing
 *   arbitrary physical memory to an environment call.
 *
 * EDUCATIONAL OVERVIEW:
 *   No existing allocator or page walker is replaced here. They remain the
 *   source of the address-space model. This layer adds the permission and
 *   backing-ownership checks that machine-mode access would otherwise bypass.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/address.h"
#include "umicom/kernel/user_memory.h"

/* Locate a single byte without trusting a user's claimed physical address.
 * The page record also catches accidental changes to a Kernel-created PTE. */
static UmicomU64 UmicomKernelUserMemoryLocate(
    const UmicomKernelUserMemory *memory,
    UmicomAddress address,
    UmicomKernelVirtualMemoryPermissions access,
    UmicomAddress *physical
)
{
    /* Only internal, live address spaces may be supplied to this routine. */
    if (memory == (const UmicomKernelUserMemory *)0 ||
        memory->space == (const UmicomKernelVirtualAddressSpace *)0 ||
        memory->pages == (const UmicomKernelUserPage *)0 ||
        memory->pageCount == 0U || memory->pageCount > UMICOM_USER_MEMORY_MAX_PAGES) {
        return UMICOM_USER_RESULT_BAD_ADDRESS;
    }

    /* The existing walker handles canonicality, missing leaves and offsets. */
    UmicomKernelVirtualMemoryPermissions observed = 0U;
    const UmicomKernelVirtualMemoryStatus status =
        UmicomKernelVirtualMemoryTranslate(memory->space, address, physical, &observed);
    if (status != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return UMICOM_USER_RESULT_BAD_ADDRESS;
    }

    /* USER is mandatory even when machine mode could read the frame directly. */
    const UmicomKernelVirtualMemoryPermissions required =
        access | UMICOM_KERNEL_VIRTUAL_MEMORY_USER;
    if ((observed & required) != required) {
        return UMICOM_USER_RESULT_DENIED;
    }

    /* A writable executable page is outside this user environment's policy.
     * Global leaves are also disallowed: these roots are independent owners. */
    if ((observed & UMICOM_KERNEL_VIRTUAL_MEMORY_GLOBAL) != 0U ||
        ((observed & UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != 0U &&
         (observed & UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != 0U)) {
        return UMICOM_USER_RESULT_DENIED;
    }

    /* Reduce both views to whole frames for the ownership comparison. */
    const UmicomAddress mask = (UmicomAddress)UMICOM_KERNEL_PAGE_SIZE - 1U;
    const UmicomAddress virtualBase = address & ~mask;
    const UmicomAddress physicalBase = *physical & ~mask;
    for (UmicomSize index = 0U; index < memory->pageCount; ++index) {
        const UmicomKernelUserPage *const page = &memory->pages[index];
        if (page->virtualBase == virtualBase) {
            /* Reject an aliased or widened leaf even if its hardware U bit is set. */
            if (page->physicalBase != physicalBase ||
                page->permissions != observed ||
                (page->permissions & required) != required) {
                return UMICOM_USER_RESULT_DENIED;
            }
            return UMICOM_USER_RESULT_OK;
        }
    }
    /* A translation without a backing record is not this user's memory. */
    return UMICOM_USER_RESULT_DENIED;
}

UmicomU64 UmicomKernelUserMemoryCheck(
    const UmicomKernelUserMemory *memory,
    UmicomAddress address,
    UmicomSize bytes,
    UmicomKernelVirtualMemoryPermissions access
)
{
    /* Empty operations do not read or write a byte; even a null address is safe. */
    if (bytes == 0U) {
        return UMICOM_USER_RESULT_OK;
    }
    /* A request may ask only for actual memory access permissions. */
    const UmicomKernelVirtualMemoryPermissions allowed =
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE |
        UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE;
    if (access == 0U || (access & ~allowed) != 0U) {
        return UMICOM_USER_RESULT_DENIED;
    }

    /* Check the last byte, not address + bytes: an inclusive final byte at the
     * greatest address is representable, although it still needs a valid PTE. */
    UmicomAddress last = 0U;
    if (UmicomKernelAddressAddChecked(address, bytes - 1U, &last) == UMICOM_FALSE ||
        UmicomKernelVirtualMemoryIsCanonical(address) == UMICOM_FALSE ||
        UmicomKernelVirtualMemoryIsCanonical(last) == UMICOM_FALSE) {
        return UMICOM_USER_RESULT_BAD_ADDRESS;
    }

    /* Walk by page boundaries. Endpoints alone cannot prove that the pages
     * between them exist or share the same user permissions. */
    UmicomAddress cursor = address;
    for (;;) {
        UmicomAddress physical = 0U;
        const UmicomU64 status =
            UmicomKernelUserMemoryLocate(memory, cursor, access, &physical);
        if (status != UMICOM_USER_RESULT_OK) {
            return status;
        }
        const UmicomAddress pageLast = cursor |
            ((UmicomAddress)UMICOM_KERNEL_PAGE_SIZE - 1U);
        if (last <= pageLast) {
            return UMICOM_USER_RESULT_OK;
        }
        /* last > pageLast proves pageLast is not the largest address. */
        cursor = pageLast + 1U;
    }
}

UmicomU64 UmicomKernelUserMemoryRead(
    const UmicomKernelUserMemory *memory,
    UmicomAddress source,
    UmicomU8 *destination,
    UmicomSize bytes
)
{
    /* Keep work and scratch-buffer demands bounded before following pointers. */
    if (bytes > UMICOM_USER_COPY_LIMIT) {
        return UMICOM_USER_RESULT_TOO_LARGE;
    }
    if (bytes == 0U) {
        return UMICOM_USER_RESULT_OK;
    }
    if (destination == (UmicomU8 *)0) {
        return UMICOM_USER_RESULT_BAD_ADDRESS;
    }
    const UmicomU64 status = UmicomKernelUserMemoryCheck(
        memory, source, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    if (status != UMICOM_USER_RESULT_OK) {
        return status;
    }

    /* Preflight has proved the entire span. The monitor is single-hart and
     * non-reentrant, with no mapper running concurrently with this copy. */
    for (UmicomSize index = 0U; index < bytes; ++index) {
        UmicomAddress physical = 0U;
        const UmicomU64 located = UmicomKernelUserMemoryLocate(
            memory, source + (UmicomAddress)index,
            UMICOM_KERNEL_VIRTUAL_MEMORY_READ, &physical);
        if (located != UMICOM_USER_RESULT_OK) {
            return located; /* Would mean the trusted ownership contract was broken. */
        }
        destination[index] = *(const volatile UmicomU8 *)physical;
    }
    return UMICOM_USER_RESULT_OK;
}

UmicomU64 UmicomKernelUserMemoryWrite(
    const UmicomKernelUserMemory *memory,
    UmicomAddress destination,
    const UmicomU8 *source,
    UmicomSize bytes
)
{
    if (bytes > UMICOM_USER_COPY_LIMIT) {
        return UMICOM_USER_RESULT_TOO_LARGE;
    }
    if (bytes == 0U) {
        return UMICOM_USER_RESULT_OK;
    }
    if (source == (const UmicomU8 *)0) {
        return UMICOM_USER_RESULT_BAD_ADDRESS;
    }
    /* Validate the whole destination before the first store. A read-only or
     * absent second page must not leave a partly written first page behind. */
    const UmicomU64 status = UmicomKernelUserMemoryCheck(
        memory, destination, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE);
    if (status != UMICOM_USER_RESULT_OK) {
        return status;
    }
    for (UmicomSize index = 0U; index < bytes; ++index) {
        UmicomAddress physical = 0U;
        const UmicomU64 located = UmicomKernelUserMemoryLocate(
            memory, destination + (UmicomAddress)index,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE, &physical);
        if (located != UMICOM_USER_RESULT_OK) {
            return located;
        }
        *(volatile UmicomU8 *)physical = source[index];
    }
    return UMICOM_USER_RESULT_OK;
}
