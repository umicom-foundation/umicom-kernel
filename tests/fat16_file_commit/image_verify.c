/* Independently verify every byte of a real guest-written synthetic image.
 * Optional creation regenerates only the fixed original fixture and refuses
 * existing output paths. The normal cross-build needs no native helper.
 * This program does not mount, repair or write to an image being verified.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include <stdio.h>
#include <string.h>
#include "guest_fixture.h"

static int UmicomFatFileFixtureCreate(const char *path)
{
    FILE *const output = fopen(path, "wbx");
    if (!output) { perror("create new synthetic fixture"); return 2; }
    UmicomU8 expected[512];
    for (UmicomU64 sector = 0U; sector < UMICOM_DISK_FIXTURE_SECTORS; ++sector) {
        UmicomFat16FileCommitFixtureSector(sector, expected, UMICOM_FAT16_FILE_FIXTURE_ORIGINAL);
        if (fwrite(expected, 1U, sizeof(expected), output) != sizeof(expected)) {
            fputs("synthetic fixture write incomplete\n", stderr);
            (void)fclose(output); return 1;
        }
    }
    if (fclose(output)) { perror("close new synthetic fixture"); return 1; }
    puts("fat16-file-commit.fixture=created bytes=8388608 archive=clear");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "create") && strcmp(argv[1], "original") &&
        strcmp(argv[1], "committed") && strcmp(argv[1], "staged"))) {
        fputs("usage: fat16-file-commit-image-verify create|original|committed|staged IMAGE.raw\n", stderr);
        return 2;
    }
    if (!strcmp(argv[1], "create")) return UmicomFatFileFixtureCreate(argv[2]);
    const UmicomFat16FileCommitFixtureState state = !strcmp(argv[1], "committed") ?
        UMICOM_FAT16_FILE_FIXTURE_COMMITTED : (!strcmp(argv[1], "staged") ?
        UMICOM_FAT16_FILE_FIXTURE_STAGED : UMICOM_FAT16_FILE_FIXTURE_ORIGINAL);
    FILE *const input = fopen(argv[2], "rb");
    if (!input) { perror("open image"); return 2; }
    UmicomU8 actual[512], expected[512], original[512];
    unsigned long long changed = 0U;
    for (UmicomU64 sector = 0U; sector < UMICOM_DISK_FIXTURE_SECTORS; ++sector) {
        if (fread(actual, 1U, sizeof(actual), input) != sizeof(actual)) {
            fprintf(stderr, "short/error image read at sector %llu\n", (unsigned long long)sector);
            (void)fclose(input); return 1;
        }
        UmicomFat16FileCommitFixtureSector(sector, expected, state);
        UmicomFat16FileCommitFixtureSector(sector, original, UMICOM_FAT16_FILE_FIXTURE_ORIGINAL);
        for (UmicomSize i = 0U; i < sizeof(actual); ++i) {
            if (actual[i] != expected[i]) {
                fprintf(stderr, "unexpected image byte at %llu: expected %u actual %u\n",
                    (unsigned long long)(sector * 512U + i), (unsigned)expected[i], (unsigned)actual[i]);
                (void)fclose(input); return 1;
            }
            if (actual[i] != original[i]) ++changed;
        }
    }
    if (fgetc(input) != EOF || ferror(input)) {
        fputs("image has trailing data or a final read error\n", stderr);
        (void)fclose(input); return 1;
    }
    if (fclose(input)) { perror("close image"); return 1; }
    const unsigned long long required = state == UMICOM_FAT16_FILE_FIXTURE_ORIGINAL ? 0U :
        (state == UMICOM_FAT16_FILE_FIXTURE_STAGED ? 707U : 705U);
    if (changed != required) {
        fprintf(stderr, "unexpected total change count: expected %llu actual %llu\n", required, changed);
        return 1;
    }
    printf("fat16-file-commit.image=ok state=%s checked=8388608 patch=%u changed=%llu other-bytes=unchanged\n",
        argv[1], state == UMICOM_FAT16_FILE_FIXTURE_ORIGINAL ? 0U : 700U, changed);
    return 0;
}
