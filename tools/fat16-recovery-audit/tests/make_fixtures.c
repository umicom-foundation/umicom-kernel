/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tools/fat16-recovery-audit/tests/make_fixtures.c
 *
 * PURPOSE:
 *   Generate three synthetic, disposable, sparse raw images for the host
 *   diagnostic CLI. Output filenames come from this standalone test project's
 *   private CMake build directory. No source fixture is modified.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_SECTOR_BYTES 512L
#define TEST_TOTAL_SECTORS 14048L
#define TEST_PARTITION_START 2048L
#define TEST_PARTITION_SECTORS 12000U
#define TEST_FAT_SECTORS 32L

static void UmicomWriteLe16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 255U);
    destination[1] = (uint8_t)(value >> 8U);
}

static void UmicomWriteLe32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 255U);
    destination[1] = (uint8_t)((value >> 8U) & 255U);
    destination[2] = (uint8_t)((value >> 16U) & 255U);
    destination[3] = (uint8_t)((value >> 24U) & 255U);
}

static int UmicomWriteSector(FILE *file, long sector, const uint8_t *bytes)
{
    if (fseek(file, sector * TEST_SECTOR_BYTES, SEEK_SET) != 0) return 1;
    return fwrite(bytes, 1U, (size_t)TEST_SECTOR_BYTES, file) == (size_t)TEST_SECTOR_BYTES ? 0 : 1;
}

static int UmicomMakeImage(const char *path, unsigned variation)
{
    uint8_t bytes[512];
    FILE *file = fopen(path, "wb");
    int failed = 0;
    if (file == NULL) return 1;
    /* Sparse allocation avoids writing millions of irrelevant zero bytes. */
    if (fseek(file, TEST_TOTAL_SECTORS * TEST_SECTOR_BYTES - 1L, SEEK_SET) != 0
        || fputc(0, file) == EOF) {
        failed = 1;
    }
    memset(bytes, 0, sizeof(bytes));
    bytes[510] = 0x55U;
    bytes[511] = 0xaaU;
    bytes[446 + 4] = 0x06U;
    UmicomWriteLe32(bytes + 446 + 8, (uint32_t)TEST_PARTITION_START);
    UmicomWriteLe32(bytes + 446 + 12, TEST_PARTITION_SECTORS);
    if (!failed) failed = UmicomWriteSector(file, 0L, bytes);

    memset(bytes, 0, sizeof(bytes));
    bytes[0] = 0xebU;
    bytes[2] = 0x90U;
    UmicomWriteLe16(bytes + 11, 512U);
    bytes[13] = 2U;
    UmicomWriteLe16(bytes + 14, 1U);
    bytes[16] = 2U;
    UmicomWriteLe16(bytes + 17, 512U);
    UmicomWriteLe16(bytes + 19, (uint16_t)TEST_PARTITION_SECTORS);
    bytes[21] = 0xf8U;
    UmicomWriteLe16(bytes + 22, (uint16_t)TEST_FAT_SECTORS);
    UmicomWriteLe32(bytes + 28, (uint32_t)TEST_PARTITION_START);
    bytes[510] = 0x55U;
    bytes[511] = 0xaaU;
    if (!failed) failed = UmicomWriteSector(file, TEST_PARTITION_START, bytes);

    memset(bytes, 0, sizeof(bytes));
    UmicomWriteLe16(bytes, 0xfff8U);
    UmicomWriteLe16(bytes + 2, variation == 1U ? 0x7fffU : 0xffffU);
    if (!failed) failed = UmicomWriteSector(file, TEST_PARTITION_START + 1L, bytes);
    if (!failed) failed = UmicomWriteSector(file, TEST_PARTITION_START + 1L + TEST_FAT_SECTORS, bytes);
    if (variation == 2U) {
        memset(bytes, 0, sizeof(bytes));
        bytes[500] = 0x01U;
        if (!failed) failed = UmicomWriteSector(file, TEST_PARTITION_START + 2L * TEST_FAT_SECTORS, bytes);
    }
    if (fclose(file) != 0) failed = 1;
    return failed;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fputs("Expected three disposable output-image paths\n", stderr);
        return 64;
    }
    if (UmicomMakeImage(argv[1], 0U) || UmicomMakeImage(argv[2], 1U)
        || UmicomMakeImage(argv[3], 2U)) {
        fputs("Unable to prepare one or more synthetic test disks\n", stderr);
        return 1;
    }
    puts("All three disposable synthetic FAT16 fixtures prepared");
    return 0;
}
