/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console.c
 *
 * PURPOSE:
 *   Provide tiny formatting helpers for deterministic early-boot evidence.
 *
 * EDUCATIONAL NOTE:
 *   There is no printf(), malloc(), terminal driver or operating system below
 *   us.  These functions deliberately show how a kernel can build a minimal
 *   formatting layer from a single "write one byte" machine primitive.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import this module's public console declarations. */
#include "umicom/kernel/console.h"

/* Import the machine adapter used to emit one physical serial byte. */
#include "umicom/kernel/platform.h"

void UmiKernelConsoleInitialize(void)
{
    /* Delegate hardware-specific setup to the platform adapter. */
    UmiPlatformConsoleInitialize();
}

void UmiKernelConsoleWrite(const char *text)
{
    /* Reject a null pointer instead of dereferencing address zero.
     *
     * K1 does not yet have a panic/error subsystem, so "nothing to write" is
     * the safest behaviour for this tiny diagnostic helper. */
    if (text == (const char *)0) {
        return;
    }

    /* Continue until the standard C NUL terminator marks the end of the text. */
    while (*text != '\0') {

        /* Convert the current character to an explicit unsigned byte and pass
         * only that byte through the platform boundary. */
        UmiPlatformConsoleWriteByte((UmiU8)*text);

        /* Advance the pointer so the next loop reads the next character. */
        ++text;
    }
}

void UmiKernelConsoleWriteLine(const char *text)
{
    /* First write the caller's text exactly as supplied. */
    UmiKernelConsoleWrite(text);

    /* Emit carriage return for terminals expecting CR before line feed. */
    UmiPlatformConsoleWriteByte((UmiU8)'\r');

    /* Emit line feed to move to the next terminal row. */
    UmiPlatformConsoleWriteByte((UmiU8)'\n');
}

void UmiKernelConsoleWriteHex64(UmiU64 value)
{
    /* Store hexadecimal digit characters in index order 0 through 15.
     *
     * "static const" keeps one read-only copy rather than constructing the
     * table on the stack for every call. */
    static const char digits[] = "0123456789abcdef";

    /* Start at bit 60 because a 64-bit value contains sixteen 4-bit nibbles.
     * The signed loop variable can safely count downward past zero. */
    int shift = 60;

    /* Prefix the number so readers know the following digits are hexadecimal. */
    UmiKernelConsoleWrite("0x");

    /* Render from the most-significant nibble down to the least-significant. */
    for (shift = 60; shift >= 0; shift -= 4) {

        /* Move the selected nibble to the low four bits, then mask away every
         * other bit.  The result is guaranteed to be between 0 and 15. */
        const UmiU64 nibble =
            (value >> (UmiU32)shift) & (UmiU64)0xFULL;

        /* Use the nibble value as an index into the digit table and emit the
         * resulting ASCII character directly through the platform adapter. */
        UmiPlatformConsoleWriteByte((UmiU8)digits[nibble]);
    }
}

void UmiKernelConsoleWriteUnsigned(UmiU64 value)
{
    /* A 64-bit unsigned decimal integer can contain at most 20 digits.
     * Reserve one extra byte so the capacity is obvious, even though this
     * reverse-digit helper does not need a NUL terminator. */
    char buffer[21];

    /* Count how many decimal digits have been stored in the temporary buffer. */
    UmiUsize used = 0U;

    /* Zero is a special case because the digit-extraction loop below only runs
     * while the value is non-zero. */
    if (value == 0U) {

        /* Emit the one correct decimal digit for zero. */
        UmiPlatformConsoleWriteByte((UmiU8)'0');

        /* No further formatting work is required. */
        return;
    }

    /* Repeatedly take the final decimal digit.
     *
     * Digits arrive least-significant first, so they are temporarily stored in
     * reverse order and emitted backwards in the second loop. */
    while (value != 0U && used < (UmiUsize)sizeof(buffer)) {

        /* value % 10 gives a number from 0 to 9.  Adding it to ASCII '0'
         * converts that numeric digit to its printable character. */
        buffer[used] = (char)('0' + (char)(value % 10U));

        /* Integer division by ten removes the digit we just stored. */
        value /= 10U;

        /* Advance the count to the next unused buffer element. */
        ++used;
    }

    /* Emit the stored digits in reverse storage order so humans read the
     * normal most-significant-to-least-significant decimal representation. */
    while (used != 0U) {

        /* Move the index back to the last digit that has not yet been emitted. */
        --used;

        /* Write that digit to the early console. */
        UmiPlatformConsoleWriteByte((UmiU8)buffer[used]);
    }
}
