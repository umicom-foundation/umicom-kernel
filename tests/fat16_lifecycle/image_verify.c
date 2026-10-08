/* Independently compare all 8 MiB of a lifecycle guest image, including free
 * data, allocation mirrors, metadata and neighbours. Never writes an image.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include <stdio.h>
#include <string.h>
#include "guest_fixture.h"
int main(int argc, char **argv)
{
    if (argc != 4 || strlen(argv[1]) != 1U || argv[1][0] < '0' || argv[1][0] > '6' ||
        (strcmp(argv[2], "clean") && strcmp(argv[2], "dirty"))) {
        fputs("usage: fat16-lifecycle-image-verify STATE_0_TO_6 clean|dirty IMAGE.raw\n", stderr); return 2;
    }
    const UmicomSize state = (UmicomSize)(argv[1][0] - '0');
    const UmicomBoolean dirty = !strcmp(argv[2], "dirty") ? UMICOM_TRUE : UMICOM_FALSE;
    if (dirty && (!state || state > 4U)) { fputs("dirty role requires state1..4\n", stderr); return 2; }
    FILE *const input = fopen(argv[3], "rb");
    if (!input) { perror("open image"); return 2; }
    UmicomU8 actual[512], expected[512], original[512];
    unsigned long long changed = 0U;
    for (UmicomU64 sector = 0U; sector < UMICOM_DISK_FIXTURE_SECTORS; ++sector) {
        if (fread(actual, 1U, 512U, input) != 512U) { fprintf(stderr,"short read sector%llu\n",(unsigned long long)sector); (void)fclose(input); return 1; }
        UmicomFat16LifecycleFixtureSector(sector, expected, state, dirty);
        UmicomFat16LifecycleFixtureSector(sector, original, 0U, UMICOM_FALSE);
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
    printf("fat16-lifecycle.image=ok phase=%s state=%u %s checked=8388608 changed=%llu other-bytes=unchanged\n",
        UmicomFat16LifecycleFixturePhase(state),(unsigned)state,argv[2],changed);
    return 0;
}
