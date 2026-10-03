/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/address.h
 *
 * PURPOSE:
 *   Publish the checked physical-address arithmetic helpers introduced by the physical-memory foundation.
 *
 * EDUCATIONAL OVERVIEW:
 *   Kernel address calculations must never rely on unsigned wrap-around being
 *   harmless.  A wrapped range could make a huge or out-of-range region look
 *   small and valid, which is particularly dangerous in a memory allocator.
 *
 *   the physical-memory foundation therefore performs additions and alignment through explicit checked
 *   helpers before the physical-memory manager trusts a range.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_ADDRESS_H
#define UMICOM_KERNEL_ADDRESS_H

/* Import the full Umicom address, size and boolean types. */
#include "umicom/kernel/types.h"

/* Return true only when `value` is a non-zero power of two.
 *
 * Page alignment uses power-of-two arithmetic, so rejecting an invalid
 * alignment is safer than producing a plausible-looking wrong address. */
UmicomBoolean UmicomKernelIsPowerOfTwo(UmicomU64 value);

/* Add `amount` bytes to `base` without allowing unsigned-address wrap.
 *
 * On success, write the exact result to caller-owned `outResult` and return
 * UMICOM_TRUE.  On invalid output storage or overflow, leave no result and
 * return UMICOM_FALSE. */
UmicomBoolean UmicomKernelAddressAddChecked(
    UmicomAddress base,
    UmicomSize amount,
    UmicomAddress *outResult
);

/* Round an address downward to the requested power-of-two alignment.
 *
 * Invalid alignment or a null output pointer is reported as UMICOM_FALSE. */
UmicomBoolean UmicomKernelAddressAlignDown(
    UmicomAddress value,
    UmicomU64 alignment,
    UmicomAddress *outResult
);

/* Round an address upward to the requested power-of-two alignment while
 * explicitly refusing the addition if it would overflow the address space. */
UmicomBoolean UmicomKernelAddressAlignUpChecked(
    UmicomAddress value,
    UmicomU64 alignment,
    UmicomAddress *outResult
);

#endif /* UMICOM_KERNEL_ADDRESS_H */
