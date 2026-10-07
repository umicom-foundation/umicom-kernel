/* Synthetic MBR/FAT16 teaching image. No bytes are copied from a host disk.
 * Shared constants describe the fixture, not the production parser's policy.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_DISK_FIXTURE_LAYOUT_H
#define UMICOM_DISK_FIXTURE_LAYOUT_H
#define UMICOM_DISK_FIXTURE_SECTORS 16384U
#define UMICOM_DISK_FIXTURE_FIRST 2048U
#define UMICOM_DISK_FIXTURE_LENGTH 12288U
#define UMICOM_DISK_FIXTURE_FAT_SECTORS 48U
#define UMICOM_DISK_FIXTURE_ROOT (UMICOM_DISK_FIXTURE_FIRST + 97U)
#define UMICOM_DISK_FIXTURE_DATA (UMICOM_DISK_FIXTURE_FIRST + 129U)
#define UMICOM_DISK_FIXTURE_FRAGMENT_BYTES 1300U
#define UMICOM_DISK_FIXTURE_README \
    "Umicom Kernel read-only FAT16 fixture.\n" \
    "These files are synthetic test data, not a host disk.\n" \
    "No disk writes or repairs are enabled.\n"
#define UMICOM_DISK_FIXTURE_GUIDE \
    "Partition offsets are checked before filesystem reads.\n" \
    "FRAG.BIN deliberately uses non-contiguous clusters.\n"
static inline unsigned char UmicomDiskFixturePattern(unsigned long long index)
{
    return (unsigned char)((index * 37U + 11U) & 255U);
}
#endif
