/* Umicom Kernel polling-input adapter checks on modelled MMIO storage.
 * Compile the actual serial.c and map ordinary host RAM at its device address.
 * This checks branching/output publication, not FIFO timing or real UART I/O.
 * No host device is opened. Unsupported fixed mappings report SKIP, not PASS.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#if defined(__linux__)
#define _GNU_SOURCE
#include <sys/mman.h>
#include <string.h>
#include "umicom/kernel/console_input.h"
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    void *const page = mmap((void *)(UmicomUIntPtr)0x10000000U, 4096U,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page == MAP_FAILED) return 77;
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
    if (munmap(page, 4096U) != 0) result = 1;
    return result;
}
#else
/* The normal Windows cross-build does not depend on this Linux-only MMIO model. */
int main(void) { return 77; }
#endif
