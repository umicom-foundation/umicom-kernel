/* Independently compare all 8 MiB of a lifecycle guest image, including free
 * data, allocation mirrors, metadata and neighbours. Never writes an image.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include <stdio.h>
#include <string.h>
#include "guest_fixture.h"
int main(int argc, char **argv)
{
    /* Move checkpoints extend the original single-digit parser retained here. */
#if 0
    if (argc != 4 || strlen(argv[1]) != 1U || argv[1][0] < '0' || argv[1][0] > '9' ||
        (strcmp(argv[2], "clean") && strcmp(argv[2], "dirty"))) {
        fputs("usage: fat16-directory-image-verify STATE_0_TO_9 clean|dirty IMAGE.raw\n", stderr); return 2;
    }
    const UmicomSize state = (UmicomSize)(argv[1][0] - '0');
    const UmicomBoolean dirty = !strcmp(argv[2], "dirty") ? UMICOM_TRUE : UMICOM_FALSE;
    if (dirty && (!state || state > 9U)) { fputs("dirty role requires state1..9\n", stderr); return 2; }
#endif
    /* Parse the finite checkpoint range without accepting signs, trailing
     * characters or an unbounded integer from the command line. */
    if (argc != 4 || !argv[1][0] || strlen(argv[1]) > 2U ||
        (strcmp(argv[2], "clean") && strcmp(argv[2], "dirty"))) {
        fputs("usage: fat16-directory-image-verify STATE clean|dirty IMAGE.raw\n", stderr); return 2;
    }
    UmicomSize state = 0U;
    for (UmicomSize i = 0U; argv[1][i]; ++i) {
        if (argv[1][i] < '0' || argv[1][i] > '9') return 2;
        state = state * 10U + (UmicomSize)(argv[1][i] - '0');
    }
    if (state >= UMICOM_FAT16_DIRECTORY_FIXTURE_STATES) return 2;
    const UmicomBoolean dirty = !strcmp(argv[2], "dirty") ? UMICOM_TRUE : UMICOM_FALSE;
    if (dirty && !state) { fputs("the original checkpoint has no dirty role\n", stderr); return 2; }
    FILE *const input = fopen(argv[3], "rb");
    if (!input) { perror("open image"); return 2; }
    UmicomU8 actual[512], expected[512], original[512];
    unsigned long long changed = 0U;
    for (UmicomU64 sector = 0U; sector < UMICOM_DISK_FIXTURE_SECTORS; ++sector) {
        if (fread(actual, 1U, 512U, input) != 512U) { fprintf(stderr,"short read sector%llu\n",(unsigned long long)sector); (void)fclose(input); return 1; }
        UmicomFat16DirectoryFixtureSector(sector, expected, state, dirty);
        UmicomFat16DirectoryFixtureSector(sector, original, 0U, UMICOM_FALSE);
        for (UmicomSize i = 0U; i < 512U; ++i) {
            if (actual[i] != expected[i]) {
                fprintf(stderr,"unexpected byte%llu expected%u actual%u\n",(unsigned long long)(sector*512U+i),(unsigned)expected[i],(unsigned)actual[i]);
                (void)fclose(input); return 1;
            }
            if (actual[i] != original[i]) ++changed;
        }
    }
    if (fgetc(input) != EOF || ferror(input)) { fputs("trailing bytes or read error\n",stderr); (void)fclose(input); return 1; }
    if (fclose(input)) { perror("close image"); return 1; }
    printf("fat16-directory.image=ok phase=%s state=%u %s checked=8388608 changed=%llu other-bytes=unchanged\n",
        UmicomFat16DirectoryFixturePhase(state),(unsigned)state,argv[2],changed);
    return 0;
}
