/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console.c
 *
 * PURPOSE:
 *   Provide tiny formatting helpers for deterministic early-boot evidence.
 *   This code is deliberately freestanding and uses no libc allocation,
 *   printf implementation or operating-system service.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

void UmiKernelConsoleInitialize(void)
{
    UmiPlatformConsoleInitialize();
}

void UmiKernelConsoleWrite(const char *text)
{
    if (text == (const char *)0) {
        return;
    }

    while (*text != '\0') {
        UmiPlatformConsoleWriteByte((UmiU8)*text);
        ++text;
    }
}

void UmiKernelConsoleWriteLine(const char *text)
{
    UmiKernelConsoleWrite(text);
    UmiPlatformConsoleWriteByte((UmiU8)'\r');
    UmiPlatformConsoleWriteByte((UmiU8)'\n');
}

void UmiKernelConsoleWriteHex64(UmiU64 value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    UmiKernelConsoleWrite("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        const UmiU64 nibble = (value >> (UmiU32)shift) & 0xFULL;
        UmiPlatformConsoleWriteByte((UmiU8)digits[nibble]);
    }
}

void UmiKernelConsoleWriteUnsigned(UmiU64 value)
{
    char buffer[21];
    UmiUsize used = 0U;

    if (value == 0U) {
        UmiPlatformConsoleWriteByte((UmiU8)'0');
        return;
    }

    while (value != 0U && used < (UmiUsize)sizeof(buffer)) {
        buffer[used] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        ++used;
    }

    while (used != 0U) {
        --used;
        UmiPlatformConsoleWriteByte((UmiU8)buffer[used]);
    }
}
