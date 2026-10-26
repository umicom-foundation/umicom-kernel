/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/console_numbers/test_numbers.c
 *
 * PURPOSE:
 *   Verify bounded decimal fields and preserve the existing console numeric contract.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/console_shell.h"
#include <stdio.h>
#include <string.h>
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check %d: %s\n", __LINE__, #value); return 1; } } while (0)

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    UmicomU64 value = 73U;
    if (!strcmp(argv[1], "unterminated")) {
        const char digits[3] = {'1', '2', '3'};
        CHECK(UmicomKernelShellUnsignedSpan(digits, sizeof digits, &value));
        CHECK(value == 123U);
    } else if (!strcmp(argv[1], "prefix")) {
        CHECK(UmicomKernelShellUnsignedSpan("42suffix", 2U, &value));
        CHECK(value == 42U);
        CHECK(!UmicomKernelShellUnsignedSpan("42suffix", 8U, &value));
        CHECK(value == 42U);
    } else if (!strcmp(argv[1], "boundary")) {
        CHECK(UmicomKernelShellUnsignedSpan("18446744073709551615", 20U, &value));
        CHECK(value == ~(UmicomU64)0U);
        CHECK(!UmicomKernelShellUnsignedSpan("18446744073709551616", 20U, &value));
        CHECK(value == ~(UmicomU64)0U);
    } else if (!strcmp(argv[1], "invalid")) {
        const char *bad[] = {"", "-1", "+1", "1 ", " 1", "1x", "0x1", "1.0"};
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
            CHECK(!UmicomKernelShellUnsignedSpan(bad[i], strlen(bad[i]), &value));
            CHECK(value == 73U);
        }
        CHECK(!UmicomKernelShellUnsignedSpan(NULL, 1U, &value));
        CHECK(!UmicomKernelShellUnsignedSpan("1", 1U, NULL));
        CHECK(!UmicomKernelShellUnsignedSpan("1", UMICOM_SHELL_LINE_BYTES, &value));
        CHECK(value == 73U);
    } else if (!strcmp(argv[1], "embedded-nul")) {
        const char bytes[3] = {'1', 0, '2'};
        CHECK(!UmicomKernelShellUnsignedSpan(bytes, sizeof bytes, &value));
        CHECK(value == 73U);
    } else if (!strcmp(argv[1], "legacy")) {
        CHECK(UmicomKernelShellUnsigned("0007", &value) && value == 7U);
        CHECK(!UmicomKernelShellUnsigned("", &value) && value == 7U);
        CHECK(!UmicomKernelShellUnsigned("18446744073709551616", &value) && value == 7U);
        char text[UMICOM_SHELL_LINE_BYTES];
        memset(text, '0', sizeof text);
        CHECK(!UmicomKernelShellUnsigned(text, &value) && value == 7U);
        text[sizeof text - 1U] = 0;
        CHECK(UmicomKernelShellUnsigned(text, &value) && value == 0U);
    } else return 2;
    return 0;
}
