/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/address.c
 *
 * PURPOSE:
 *   Implement the physical-memory foundation's small checked-address arithmetic layer.
 *
 * EDUCATIONAL OVERVIEW:
 *   The functions in this file deliberately avoid compiler builtins or hosted
 *   libraries so the arithmetic rules are visible to a learner.  Later Kernel
 *   code can reuse the same checks rather than open-coding overflow logic.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import this module's public contracts. */
#include "umicom/kernel/address.h"

UmicomBoolean UmicomKernelIsPowerOfTwo(UmicomU64 value)
{
    /* Zero is not a valid alignment even though many bit tricks would treat it
     * as a special case. */
    if (value == (UmicomU64)0U) {
        /* Report the invalid value explicitly. */
        return UMICOM_FALSE;
    }

    /* A non-zero power of two contains exactly one set bit.
     *
     * Subtracting one clears that bit and sets every lower bit.  Therefore a
     * bitwise AND between `value` and `value - 1` is zero only for powers of
     * two. */
    return (
        (value & (value - (UmicomU64)1U)) == (UmicomU64)0U
    ) ? UMICOM_TRUE : UMICOM_FALSE;
}

UmicomBoolean UmicomKernelAddressAddChecked(
    UmicomAddress base,
    UmicomSize amount,
    UmicomAddress *outResult
)
{
    /* A checked helper cannot publish a result without caller-owned storage. */
    if (outResult == (UmicomAddress *)0) {
        /* Reject the invalid call before any arithmetic result is exposed. */
        return UMICOM_FALSE;
    }

    /* Construct the largest value representable by the address type.
     *
     * Unsigned all-ones is portable for our freestanding fixed-width integer. */
    const UmicomAddress maximumAddress = ~(UmicomAddress)0U;

    /* If `amount` is greater than the space remaining above `base`, addition
     * would wrap through zero and describe the wrong physical range. */
    if ((UmicomAddress)amount > maximumAddress - base) {
        /* Refuse the overflowing calculation. */
        return UMICOM_FALSE;
    }

    /* The prior comparison proves this addition cannot wrap. */
    *outResult = base + (UmicomAddress)amount;

    /* Tell the caller that `outResult` now contains a trustworthy sum. */
    return UMICOM_TRUE;
}

UmicomBoolean UmicomKernelAddressAlignDown(
    UmicomAddress value,
    UmicomU64 alignment,
    UmicomAddress *outResult
)
{
    /* Caller-owned output storage is mandatory. */
    if (outResult == (UmicomAddress *)0) {
        /* No result can be published safely. */
        return UMICOM_FALSE;
    }

    /* The simple mask formula below is valid only for power-of-two alignment. */
    if (UmicomKernelIsPowerOfTwo(alignment) == UMICOM_FALSE) {
        /* Reject values such as 0, 3 or 6000 instead of rounding incorrectly. */
        return UMICOM_FALSE;
    }

    /* For a power of two, alignment - 1 identifies the low address bits that
     * represent an offset inside one alignment unit. */
    const UmicomAddress mask = (UmicomAddress)(alignment - (UmicomU64)1U);

    /* Clearing those low bits moves the address to the start of its containing
     * aligned block. */
    *outResult = value & ~mask;

    /* The aligned-down value is always less than or equal to the input and
     * therefore cannot overflow. */
    return UMICOM_TRUE;
}

UmicomBoolean UmicomKernelAddressAlignUpChecked(
    UmicomAddress value,
    UmicomU64 alignment,
    UmicomAddress *outResult
)
{
    /* Caller-owned output storage is mandatory. */
    if (outResult == (UmicomAddress *)0) {
        /* Reject the invalid request immediately. */
        return UMICOM_FALSE;
    }

    /* The remainder/mask calculation requires a power-of-two alignment. */
    if (UmicomKernelIsPowerOfTwo(alignment) == UMICOM_FALSE) {
        /* Do not silently reinterpret an unsupported alignment. */
        return UMICOM_FALSE;
    }

    /* Alignment minus one selects the low offset bits. */
    const UmicomAddress mask =
        (UmicomAddress)(alignment - (UmicomU64)1U);

    /* Read the current offset inside the requested alignment block. */
    const UmicomAddress remainder = value & mask;

    /* An already aligned value must not be advanced to the next block. */
    if (remainder == (UmicomAddress)0U) {
        /* Preserve the exact original value. */
        *outResult = value;

        /* Report success because no arithmetic beyond the identity was needed. */
        return UMICOM_TRUE;
    }

    /* Determine how many bytes are required to reach the next aligned address. */
    const UmicomSize adjustment =
        (UmicomSize)((UmicomAddress)alignment - remainder);

    /* Reuse the checked-add primitive so an address near the top of the address
     * space cannot wrap while being aligned upward. */
    return UmicomKernelAddressAddChecked(value, adjustment, outResult);
}
