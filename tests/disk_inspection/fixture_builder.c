/*-----------------------------------------------------------------------------
 * Umicom Kernel synthetic disk-image builder (optional native C tool)
 *
 * The normal cross-build copies the packaged fixture; contributors do not need
 * to run this tool. It exists so every image byte can be regenerated from free
 * source without a formatter, Python or shell script. It creates only a new
 * regular output file and refuses an existing path. Never pass a device path.
 *
 * This is not a general filesystem formatter. Its single fixed geometry is
 * deliberately small, with a fragmented file to exercise FAT-chain traversal.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "fixture_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void Put16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8U);
}
static void Put32(unsigned char *p, uint32_t value)
{
    for (unsigned i = 0U; i < 4U; ++i) p[i] = (unsigned char)(value >> (i * 8U));
}
static void Entry(unsigned char *p, const char *name, unsigned char attributes, uint16_t cluster, uint32_t bytes)
{
    memcpy(p, name, 11U); /* Fixture names are explicit eleven-byte short aliases. */
    p[11] = attributes; Put16(p + 26U, cluster); Put32(p + 28U, bytes);
}
int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: umicom-disk-fixture NEW-OUTPUT-FILE\n"); return 2; }
    const size_t bytes = (size_t)UMICOM_DISK_FIXTURE_SECTORS * 512U;
    unsigned char *image = calloc(1U, bytes);
    if (!image) return 3;
    /* Only the MBR partition record and signature are set. There is no MBR
     * program to execute, and no copy of somebody else's boot-sector code. */
    unsigned char *part = image + 446U;
    part[1] = 0xfeU; part[2] = 0xffU; part[3] = 0xffU; part[4] = 0x06U;
    part[5] = 0xfeU; part[6] = 0xffU; part[7] = 0xffU;
    Put32(part + 8U, UMICOM_DISK_FIXTURE_FIRST); Put32(part + 12U, UMICOM_DISK_FIXTURE_LENGTH);
    image[510] = 0x55U; image[511] = 0xaaU;
    unsigned char *boot = image + UMICOM_DISK_FIXTURE_FIRST * 512U;
    boot[0] = 0xebU; boot[1] = 0x3cU; boot[2] = 0x90U;
    memcpy(boot + 3U, "UMICOM  ", 8U);
    Put16(boot + 11U, 512U); boot[13] = 1U; Put16(boot + 14U, 1U); boot[16] = 2U;
    Put16(boot + 17U, 512U); Put16(boot + 19U, UMICOM_DISK_FIXTURE_LENGTH); boot[21] = 0xf8U;
    Put16(boot + 22U, UMICOM_DISK_FIXTURE_FAT_SECTORS); Put16(boot + 24U, 63U); Put16(boot + 26U, 255U);
    Put32(boot + 28U, UMICOM_DISK_FIXTURE_FIRST); boot[36] = 0x80U; boot[38] = 0x29U;
    Put32(boot + 39U, 0x554d4346U); memcpy(boot + 43U, "UMICOMDISK ", 11U); memcpy(boot + 54U, "FAT16   ", 8U);
    boot[510] = 0x55U; boot[511] = 0xaaU;
    unsigned char *fat = boot + 512U;
    Put16(fat, 0xfff8U); Put16(fat + 2U, 0xffffU);
    Put16(fat + 2U * 2U, 0xffffU); /* README.TXT */
    Put16(fat + 3U * 2U, 0xffffU); /* DOCS directory */
    Put16(fat + 4U * 2U, 9U); Put16(fat + 9U * 2U, 6U); Put16(fat + 6U * 2U, 0xffffU);
    Put16(fat + 7U * 2U, 0xffffU); /* DOCS/GUIDE.TXT */
    memcpy(fat + UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U, fat, UMICOM_DISK_FIXTURE_FAT_SECTORS * 512U);
    unsigned char *root = image + UMICOM_DISK_FIXTURE_ROOT * 512U;
    Entry(root, "UMICOMDISK ", 0x08U, 0U, 0U);
    Entry(root + 32U, "README  TXT", 0x21U, 2U, (uint32_t)(sizeof(UMICOM_DISK_FIXTURE_README) - 1U));
    Entry(root + 64U, "DOCS       ", 0x10U, 3U, 0U);
    Entry(root + 96U, "FRAG    BIN", 0x20U, 4U, UMICOM_DISK_FIXTURE_FRAGMENT_BYTES);
    Entry(root + 128U, "EMPTY   TXT", 0x20U, 0U, 0U);
    unsigned char *data = image + UMICOM_DISK_FIXTURE_DATA * 512U;
    memcpy(data, UMICOM_DISK_FIXTURE_README, sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    unsigned char *directory = data + 512U;
    Entry(directory, ".          ", 0x10U, 3U, 0U);
    Entry(directory + 32U, "..         ", 0x10U, 0U, 0U);
    Entry(directory + 64U, "GUIDE   TXT", 0x21U, 7U, (uint32_t)(sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U));
    memcpy(data + (7U - 2U) * 512U, UMICOM_DISK_FIXTURE_GUIDE, sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    const unsigned clusters[] = {4U, 9U, 6U};
    for (unsigned i = 0U; i < 3U; ++i) {
        unsigned char *page = data + (clusters[i] - 2U) * 512U;
        memset(page, 0xa6, 512U); /* Slack must not be returned as file contents. */
        for (unsigned j = 0U; j < 512U && i * 512U + j < UMICOM_DISK_FIXTURE_FRAGMENT_BYTES; ++j)
            page[j] = UmicomDiskFixturePattern(i * 512U + j);
    }
    /* Exclusive creation avoids replacing a useful file during regeneration. */
    FILE *file = fopen(argv[1], "wbx");
    if (!file) { perror("new fixture"); free(image); return 4; }
    const size_t written = fwrite(image, 1U, bytes, file);
    const int closed = fclose(file);
    free(image);
    if (written != bytes || closed != 0) { fprintf(stderr, "fixture write incomplete\n"); return 5; }
    return 0;
}
