/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/boot_handoff/handoff_tests.c
 *
 * PURPOSE:
 *   Exercise the real boot reader with synthetic, bounded records and unchanged-output rejection checks.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_handoff.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void Put16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8U);
}
static void Put32(unsigned char *p, UmicomU32 value)
{
    for (unsigned int i = 0U; i < 4U; ++i) p[i] = (unsigned char)(value >> (8U * i));
}
static void Put64(unsigned char *p, UmicomU64 value)
{
    for (unsigned int i = 0U; i < 8U; ++i) p[i] = (unsigned char)(value >> (8U * i));
}
static void Span(unsigned char *p, UmicomU64 base, UmicomU64 bytes)
{
    Put64(p, base); Put64(p + 8U, bytes);
}
static size_t Fixture(unsigned char *p, unsigned int ram, unsigned int reserved, size_t command)
{
    memset(p, 0, UMICOM_BOOT_HANDOFF_MAX_BYTES);
    memcpy(p, "UMIBOOT", 7U);
    Put16(p + 8U, 1U); Put16(p + 10U, 64U);
    Put32(p + 16U, UMICOM_BOOT_RISCV64); Put64(p + 24U, 7U);
    Span(p + 32U, 0x80001000ULL, 4096U);
    Put16(p + 48U, ram); Put16(p + 50U, reserved);
    Put16(p + 56U, (unsigned int)command);
    size_t at = 64U;
    for (unsigned int i = 0U; i < ram; ++i, at += 16U)
        Span(p + at, 0x80000000ULL + (UmicomU64)i * 0x200000U, 0x100000U);
    for (unsigned int i = 0U; i < reserved; ++i, at += 16U)
        Span(p + at, i ? 0x80008000ULL + (UmicomU64)i * 4096U : 0x80000000ULL, i ? 4096U : 8192U);
    at += 48U; /* Absent firmware table and framebuffer are explicitly zero. */
    memset(p + at, 'x', command); at += command;
    Put32(p + 12U, (UmicomU32)at);
    return at;
}
static int Refused(const void *p, size_t bytes, UmicomBootHandoffStatus expected)
{
    UmicomKernelBootHandoff out;
    unsigned char before[sizeof out];
    memset(&out, 0x5a, sizeof out); memcpy(before, &out, sizeof out);
    CHECK(UmicomKernelBootHandoffRead(p, bytes, &out) == expected);
    CHECK(!memcmp(before, &out, sizeof out));
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    unsigned char bytes[UMICOM_BOOT_HANDOFF_MAX_BYTES + 1U];
    size_t length = Fixture(bytes, 1U, 1U, 3U);
    const size_t table = 96U, frame = 112U, command = 144U;
    UmicomKernelBootHandoff out;
    const char *name = argv[1];
    if (!strcmp(name, "valid")) {
        memset(&out, 0x5a, sizeof out);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        CHECK(out.bootstrapCpu == 7U && out.architecture == UMICOM_BOOT_RISCV64);
        CHECK(out.ramCount == 1U && out.reservedCount == 1U);
        CHECK(out.kernel.address == 0x80001000ULL && out.kernel.bytes == 4096U);
        CHECK(!strcmp(out.commandLine, "xxx"));
        for (size_t i = 1U; i < UMICOM_BOOT_HANDOFF_RAM_LIMIT; ++i)
            CHECK(!out.ram[i].address && !out.ram[i].bytes);
        for (size_t i = 1U; i < UMICOM_BOOT_HANDOFF_RESERVED_LIMIT; ++i)
            CHECK(!out.reserved[i].address && !out.reserved[i].bytes);
        for (size_t i = 3U; i < sizeof out.commandLine; ++i) CHECK(!out.commandLine[i]);
    } else if (!strcmp(name, "architectures")) {
        for (UmicomU32 arch = UMICOM_BOOT_RISCV64; arch <= UMICOM_BOOT_AARCH64; ++arch) {
            Put32(bytes + 16U, arch);
            CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
            CHECK((UmicomU32)out.architecture == arch);
        }
        Put32(bytes + 16U, 0U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_UNSUPPORTED));
        Put32(bytes + 16U, 4U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_UNSUPPORTED));
    } else if (!strcmp(name, "unaligned")) {
        memmove(bytes + 1U, bytes, length);
        CHECK(UmicomKernelBootHandoffRead(bytes + 1U, length, &out) == UMICOM_BOOT_HANDOFF_OK);
    } else if (!strcmp(name, "truncation")) {
        for (size_t n = 0U; n < length; ++n) CHECK(!Refused(bytes, n, UMICOM_BOOT_HANDOFF_LENGTH));
        CHECK(!Refused(bytes, length + 1U, UMICOM_BOOT_HANDOFF_LENGTH));
        Put32(bytes + 12U, (UmicomU32)length + 1U);
        CHECK(!Refused(bytes, length + 1U, UMICOM_BOOT_HANDOFF_LENGTH));
    } else if (!strcmp(name, "header")) {
        for (size_t i = 0U; i < 8U; ++i) {
            bytes[i] ^= 1U; CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_HEADER)); bytes[i] ^= 1U;
        }
        const size_t offsets[] = {8U, 10U, 20U, 60U};
        for (size_t i = 0U; i < sizeof offsets / sizeof offsets[0]; ++i) {
            bytes[offsets[i]] ^= 1U;
            CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_UNSUPPORTED)); bytes[offsets[i]] ^= 1U;
        }
    } else if (!strcmp(name, "counts")) {
        Put16(bytes + 48U, 0U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_LIMIT));
        Put16(bytes + 48U, 33U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_LIMIT));
        Put16(bytes + 48U, 1U); Put16(bytes + 50U, 65U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_LIMIT));
        CHECK(!Refused(bytes, UMICOM_BOOT_HANDOFF_MAX_BYTES + 1U, UMICOM_BOOT_HANDOFF_LIMIT));
    } else if (!strcmp(name, "maximum")) {
        length = Fixture(bytes, 32U, 64U, 255U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        CHECK(out.ramCount == 32U && out.reservedCount == 64U && strlen(out.commandLine) == 255U);
    } else if (!strcmp(name, "ram")) {
        const UmicomU64 bad[][2] = {{0x80000001ULL,4096U},{0x80000000ULL,4095U},
            {0x80000000ULL,0U},{~(UmicomU64)0U-4095U,8192U}};
        for (size_t i = 0U; i < sizeof bad / sizeof bad[0]; ++i) {
            Span(bytes + 64U, bad[i][0], bad[i][1]);
            CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_MEMORY));
        }
    } else if (!strcmp(name, "ram-overlap")) {
        length = Fixture(bytes, 2U, 1U, 0U);
        Span(bytes + 80U, 0x80001000ULL, 4096U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_MEMORY));
        Span(bytes + 80U, 0x80100000ULL, 4096U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
    } else if (!strcmp(name, "kernel")) {
        Span(bytes + 32U, ~(UmicomU64)0U, 1U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
        Span(bytes + 32U, 0x90000000ULL, 4096U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_MEMORY));
        Span(bytes + 32U, 0x800ff000ULL, 8192U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_MEMORY));
    } else if (!strcmp(name, "reservations")) {
        Span(bytes + 80U, 0U, 0U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
        Span(bytes + 80U, ~(UmicomU64)0U, 1U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
        Span(bytes + 80U, 0x80000000ULL, 4096U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_MEMORY));
        length = Fixture(bytes, 1U, 0U, 0U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_MEMORY));
    } else if (!strcmp(name, "firmware")) {
        Put32(bytes + 52U, UMICOM_BOOT_TABLE_FDT); Span(bytes + table, 0x80000100ULL, 40U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        Put32(bytes + 52U, UMICOM_BOOT_TABLE_ACPI_RSDP); Span(bytes + table, 0x1000U, 36U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        Put32(bytes + 52U, 3U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_UNSUPPORTED));
    } else if (!strcmp(name, "firmware-refusal")) {
        Span(bytes + table, 1U, 0U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
        Put32(bytes + 52U, UMICOM_BOOT_TABLE_FDT); Span(bytes + table, 0x80000101ULL, 40U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
        Span(bytes + table, 0x80000100ULL, 39U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
        Span(bytes + table, 0x80008000ULL, 40U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_RANGE));
    } else if (!strcmp(name, "framebuffer")) {
        Put16(bytes + 58U, UMICOM_BOOT_PIXELS_RGBX32); Span(bytes + frame, 0x10000000U, 4096U);
        Put32(bytes + frame + 16U, 16U); Put32(bytes + frame + 20U, 4U); Put32(bytes + frame + 24U, 64U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        CHECK(out.framebuffer.width == 16U && out.framebuffer.strideBytes == 64U);
        Put16(bytes + 58U, UMICOM_BOOT_PIXELS_BGRX32); Span(bytes + frame, 0x80000000ULL, 256U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        Span(bytes + frame, 0x80008000ULL, 256U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_FRAMEBUFFER));
    } else if (!strcmp(name, "framebuffer-refusal")) {
        Put32(bytes + frame + 16U, 1U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_FRAMEBUFFER));
        Put16(bytes + 58U, 1U); Span(bytes + frame, 0x10000000U, 255U);
        Put32(bytes + frame + 16U, 16U); Put32(bytes + frame + 20U, 4U); Put32(bytes + frame + 24U, 64U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_FRAMEBUFFER));
        Span(bytes + frame, 0x10000000U, 4096U); Put32(bytes + frame + 24U, 63U);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_FRAMEBUFFER));
        Put32(bytes + frame + 24U, 64U); Put32(bytes + frame + 16U, 0xffffffffU);
        CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_FRAMEBUFFER));
        Put32(bytes + frame + 28U, 1U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_UNSUPPORTED));
    } else if (!strcmp(name, "command")) {
        bytes[command] = 0U; CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_TEXT));
        bytes[command] = '\n'; CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_TEXT));
        bytes[command] = 0x80U; CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_TEXT));
        Put16(bytes + 56U, 256U); CHECK(!Refused(bytes, length, UMICOM_BOOT_HANDOFF_LIMIT));
        length = Fixture(bytes, 1U, 1U, 0U);
        CHECK(UmicomKernelBootHandoffRead(bytes, length, &out) == UMICOM_BOOT_HANDOFF_OK);
        CHECK(!out.commandLine[0]);
    } else if (!strcmp(name, "overlap")) {
        union { unsigned char bytes[4160]; UmicomKernelBootHandoff out; } shared;
        /* Initialise the whole overlap fixture, including its spare tail. */
        memset(&shared, 0, sizeof shared);
        length = Fixture(shared.bytes, 1U, 1U, 0U);
        unsigned char before[sizeof shared]; memcpy(before, &shared, sizeof shared);
        CHECK(UmicomKernelBootHandoffRead(shared.bytes, length, &shared.out) == UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(!memcmp(before, &shared, sizeof shared));
        length = Fixture(shared.bytes + 64U, 1U, 1U, 0U);
        memcpy(before, &shared, sizeof shared);
        CHECK(UmicomKernelBootHandoffRead(shared.bytes + 64U, length, &shared.out) == UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(!memcmp(before, &shared, sizeof shared));
    } else if (!strcmp(name, "arguments")) {
        CHECK(!Refused(NULL, length, UMICOM_BOOT_HANDOFF_ARGUMENT));
        CHECK(UmicomKernelBootHandoffRead(bytes, length, NULL) == UMICOM_BOOT_HANDOFF_ARGUMENT);
    } else return 2;
    return 0;
}
