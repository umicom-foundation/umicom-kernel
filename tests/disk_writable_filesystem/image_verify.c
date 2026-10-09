/* Independently compare every byte of the writable-VFS guest's disposable
 * 8 MiB image. This program never writes a disk and has no production backend.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include <stdio.h>
#include <string.h>
#include "guest_fixture.h"
int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "original") && strcmp(argv[1], "committed"))) {
        fputs("usage: disk-writable-image-verify original|committed IMAGE.raw\n", stderr);
        return 2;
    }
    const UmicomBoolean committed = !strcmp(argv[1], "committed") ? UMICOM_TRUE : UMICOM_FALSE;
    FILE *const input = fopen(argv[2], "rb");
    if (!input) { perror("open image"); return 2; }
    UmicomU8 actual[512], expected[512], original[512];
    UmicomU64 changed = 0U;
    for (UmicomU64 sector = 0U; sector < UMICOM_DISK_FIXTURE_SECTORS; ++sector) {
        if (fread(actual, 1U, sizeof(actual), input) != sizeof(actual)) {
            fprintf(stderr, "short image at sector %llu\n", (unsigned long long)sector);
            (void)fclose(input); return 1;
        }
        UmicomKernelDiskWritableFixtureSector(sector, expected, committed);
        UmicomKernelDiskWritableFixtureSector(sector, original, UMICOM_FALSE);
        for (UmicomSize i = 0U; i < sizeof(actual); ++i) {
            if (actual[i] != expected[i]) {
                fprintf(stderr, "unexpected byte %llu: expected %u, actual %u\n",
                    (unsigned long long)(sector * 512U + i), (unsigned)expected[i], (unsigned)actual[i]);
                (void)fclose(input); return 1;
            }
            if (actual[i] != original[i]) ++changed;
        }
    }
    if (fgetc(input) != EOF || ferror(input)) {
        fputs("trailing image bytes or read failure\n", stderr); (void)fclose(input); return 1;
    }
    if (fclose(input)) { perror("close image"); return 1; }
    printf("disk-writable.image=verified state=%s checked=8388608 changed=%llu other-bytes=unchanged\n",
        argv[1], (unsigned long long)changed);
    return 0;
}
