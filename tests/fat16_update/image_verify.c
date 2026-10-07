/*-----------------------------------------------------------------------------
 * Umicom Kernel — independent disposable FAT16 update-image verifier.
 *
 * Compare the original and guest-written regular files byte for byte. The
 * permitted transformation is deliberately expressed here independently of
 * the guest generator and updater: FRAG.BIN bytes 511..1210 contain the fixed
 * 700-byte acceptance pattern. Every other byte, including both FAT copies,
 * directories, unallocated space and cluster slack, must match the original.
 * This program opens both paths read-only and never formats or repairs media.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "../disk_inspection/fixture_layout.h"
#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: umicom-fat16-update-image-verify ORIGINAL-RAW WRITTEN-RAW\n");
        return 2;
    }
    FILE *original = fopen(argv[1], "rb");
    if (!original) { perror("original image"); return 3; }
    FILE *written = fopen(argv[2], "rb");
    if (!written) { perror("written image"); (void)fclose(original); return 3; }
    unsigned char before[512], after[512];
    uint64_t changed = 0U, patched = 0U;
    static const unsigned clusters[] = {4U, 9U, 6U};
    for (uint64_t sector = 0U; sector < UMICOM_DISK_FIXTURE_SECTORS; ++sector) {
        if (fread(before, 1U, sizeof(before), original) != sizeof(before) ||
            fread(after, 1U, sizeof(after), written) != sizeof(after)) {
            fprintf(stderr, "short image or read failure at sector %llu\n", (unsigned long long)sector);
            (void)fclose(original); (void)fclose(written); return 4;
        }
        for (unsigned byte = 0U; byte < 512U; ++byte) {
            unsigned char expected = before[byte];
            for (unsigned index = 0U; index < 3U; ++index) {
                const uint64_t dataSector = UMICOM_DISK_FIXTURE_DATA + clusters[index] - 2U;
                const unsigned fileOffset = index * 512U + byte;
                if (sector == dataSector && fileOffset >= 511U && fileOffset < 1211U) {
                    const unsigned patchIndex = fileOffset - 511U;
                    if (before[byte] != (unsigned char)((fileOffset * 37U + 11U) & 255U)) {
                        fprintf(stderr, "unexpected original FRAG.BIN byte at offset %u\n", fileOffset);
                        (void)fclose(original); (void)fclose(written); return 5;
                    }
                    expected = (unsigned char)((patchIndex * 73U + 0x5dU) & 255U);
                    ++patched;
                }
            }
            if (after[byte] != expected) {
                fprintf(stderr, "unexpected image byte at sector %llu offset %u: expected %02x, got %02x\n",
                    (unsigned long long)sector, byte, (unsigned)expected, (unsigned)after[byte]);
                (void)fclose(original); (void)fclose(written); return 6;
            }
            if (after[byte] != before[byte]) ++changed;
        }
    }
    if (fgetc(original) != EOF || ferror(original) || fgetc(written) != EOF || ferror(written)) {
        fprintf(stderr, "image length or final read status is invalid\n");
        (void)fclose(original); (void)fclose(written); return 7;
    }
    const int closedOriginal = fclose(original), closedWritten = fclose(written);
    if (closedOriginal || closedWritten || patched != 700U) return 8;
    printf("fat16-update.image=ok checked=%llu patch=%llu changed=%llu metadata-and-slack=unchanged\n",
        (unsigned long long)UMICOM_DISK_FIXTURE_SECTORS * 512U,
        (unsigned long long)patched, (unsigned long long)changed);
    return 0;
}
