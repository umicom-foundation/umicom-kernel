/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/console_shell/uart_input_tests.c
 *
 * PURPOSE:
 *   Exercise the real polling-input adapter using ordinary, non-executable host memory.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
/* Compile actual serial.c against modelled MMIO storage. These checks cover
 * branching and output publication, not FIFO timing or physical UART I/O.
 * No host device is opened. A refused exact-address reservation reports SKIP.
 * Windows uses a new reservation; it never replaces somebody else's mapping. */

#if defined(__linux__) || defined(_WIN32)
#if defined(__linux__)
#define _GNU_SOURCE
#include <sys/mman.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <stdio.h>
#include <string.h>
#include "umicom/kernel/console_input.h"
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
#if defined(_WIN32)
    void *const page = VirtualAlloc((void *)(UmicomUIntPtr)0x10000000U, 4096U,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (page == NULL) {
        fprintf(stderr, "UART RAM model reservation unavailable (Windows error %lu).\n", GetLastError());
        return 77;
    }
#else
    void *const page = mmap((void *)(UmicomUIntPtr)0x10000000U, 4096U,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page == MAP_FAILED) { fprintf(stderr, "UART RAM model mapping unavailable.\n"); return 77; }
#endif
    /* The production driver still reads its compiled device address. A host
     * mapping at any other address cannot qualify that code safely. */
    if (page != (void *)(UmicomUIntPtr)0x10000000U) {
#if defined(_WIN32)
        if (!VirtualFree(page, 0U, MEM_RELEASE)) return 1;
#else
        if (munmap(page, 4096U) != 0) return 1;
#endif
        fprintf(stderr, "UART RAM model did not receive the required address.\n");
        return 77;
    }
    volatile UmicomU8 *const registers = (volatile UmicomU8 *)page;
    UmicomU8 byte = 0xa5U;
    int result = 0;
    if (!strcmp(argv[1], "null-output")) {
        result = UmicomPlatformConsoleTryReadByte((UmicomU8 *)0) == UMICOM_CONSOLE_INPUT_INVALID_ARGUMENT ? 0 : 1;
    } else if (!strcmp(argv[1], "idle-output-unchanged")) {
        registers[5] = 0x60U; /* Transmitter readiness is not receive readiness. */
        result = UmicomPlatformConsoleTryReadByte(&byte) == UMICOM_CONSOLE_INPUT_IDLE && byte == 0xa5U ? 0 : 1;
    } else if (!strcmp(argv[1], "all-byte-values")) {
        for (unsigned i = 0U; i < 256U; ++i) {
            registers[0] = (UmicomU8)i; registers[5] = 0x61U;
            if (UmicomPlatformConsoleTryReadByte(&byte) != UMICOM_CONSOLE_INPUT_BYTE || byte != (UmicomU8)i) result = 1;
        }
    } else if (!strcmp(argv[1], "line-errors")) {
        for (unsigned bit = 1U; bit <= 4U; ++bit) {
            registers[5] = (UmicomU8)(1U << bit);
            if (UmicomPlatformConsoleTryReadByte(&byte) != UMICOM_CONSOLE_INPUT_ERROR || byte != 0xa5U) result = 1;
        }
    } else if (!strcmp(argv[1], "error-with-data")) {
        for (unsigned bit = 1U; bit <= 4U; ++bit) {
            registers[0] = 'x'; registers[5] = (UmicomU8)((1U << bit) | 1U);
            if (UmicomPlatformConsoleTryReadByte(&byte) != UMICOM_CONSOLE_INPUT_ERROR || byte != 0xa5U) result = 1;
        }
    } else if (!strcmp(argv[1], "no-register-writes")) {
        for (unsigned i = 0U; i < 8U; ++i) registers[i] = (UmicomU8)(i + 40U);
        registers[5] = 0x61U;
        UmicomU8 before[8]; for (unsigned i = 0U; i < 8U; ++i) before[i] = registers[i];
        if (UmicomPlatformConsoleTryReadByte(&byte) != UMICOM_CONSOLE_INPUT_BYTE) result = 1;
        for (unsigned i = 0U; i < 8U; ++i) if (registers[i] != before[i]) result = 1;
    } else result = 1;
#if defined(_WIN32)
    if (!VirtualFree(page, 0U, MEM_RELEASE)) result = 1;
#else
    if (munmap(page, 4096U) != 0) result = 1;
#endif
    return result;
}
#else
/* Other hosts retain the original unsupported-model outcome. */
int main(void) { return 77; }
#endif
