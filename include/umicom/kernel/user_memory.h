/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/user_memory.h
 *
 * PURPOSE:
 *   Check a caller's complete virtual buffer before reading or writing it from
 *   machine mode. Kernel code must not mistake an address for authority.
 *
 * EDUCATIONAL OVERVIEW:
 *   The existing software page walker returns a physical address and leaf
 *   permissions. Machine mode can dereference that address even when user mode
 *   could not. We therefore require both a user-accessible PTE and a matching
 *   Kernel-owned record of the backing frame before touching any bytes.
 *
 *   A buffer can straddle pages. Every page is checked first, so a bad second
 *   page cannot leave the first page partly modified. This guarantee relies on
 *   our present single-hart monitor: it does not allow the user or another hart
 *   to modify the page tables between validation and copying. It is not an SMP
 *   pinning or lock implementation.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_MEMORY_H
#define UMICOM_KERNEL_USER_MEMORY_H
#include "umicom/kernel/virtual_memory.h"
#include "umicom/kernel/user_abi.h"

/* Each entry describes one explicitly owned page, including its allowed view. */
#define UMICOM_USER_MEMORY_MAX_PAGES 16U

typedef struct UmicomKernelUserPage {
    UmicomAddress virtualBase; /* Aligned address in this user's page table. */
    UmicomAddress physicalBase; /* Backing frame supplied by the Kernel. */
    UmicomKernelVirtualMemoryPermissions permissions; /* Maximum permitted view. */
} UmicomKernelUserPage;

typedef struct UmicomKernelUserMemory {
    const UmicomKernelVirtualAddressSpace *space; /* Borrowed, not freed here. */
    const UmicomKernelUserPage *pages; /* Kernel-owned, never writable by U-mode. */
    UmicomSize pageCount; /* Bounds the backing-frame records we may inspect. */
} UmicomKernelUserMemory;

/* Validate a complete byte span with USER plus the requested R/W/X permissions.
 * Zero length is a no-op and does not dereference the address. Nonzero spans
 * must not wrap, cross the canonical hole, or touch an unowned/forbidden page. */
UmicomU64 UmicomKernelUserMemoryCheck(
    const UmicomKernelUserMemory *memory,
    UmicomAddress address,
    UmicomSize bytes,
    UmicomKernelVirtualMemoryPermissions access
);

/* Kernel destinations/sources are trusted caller-owned buffers, never another
 * unchecked user pointer. These bounded helpers leave the destination unchanged
 * when the source or destination span is refused during preflight. */
UmicomU64 UmicomKernelUserMemoryRead(
    const UmicomKernelUserMemory *memory,
    UmicomAddress source,
    UmicomU8 *destination,
    UmicomSize bytes
);
UmicomU64 UmicomKernelUserMemoryWrite(
    const UmicomKernelUserMemory *memory,
    UmicomAddress destination,
    const UmicomU8 *source,
    UmicomSize bytes
);
#endif /* UMICOM_KERNEL_USER_MEMORY_H */
