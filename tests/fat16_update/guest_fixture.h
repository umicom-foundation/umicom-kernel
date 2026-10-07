/* Synthetic FAT16 update fixture expectations for the two dedicated guests.
 * The original sector generator follows the published fixture builder. The
 * changed generator replaces only 700 caller bytes in the fragmented file;
 * metadata, directory records, slack and every other sector remain original.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_UPDATE_GUEST_FIXTURE_H
#define UMICOM_FAT16_UPDATE_GUEST_FIXTURE_H
#include "umicom/kernel/types.h"
#include "../disk_inspection/fixture_layout.h"

#define UMICOM_FAT16_UPDATE_FIXTURE_OFFSET 511U
#define UMICOM_FAT16_UPDATE_FIXTURE_BYTES 700U

static inline UmicomU8 UmicomFat16UpdateFixturePattern(UmicomU64 index)
{
    return (UmicomU8)((index * 73U + 0x5dU) & 255U);
}
static inline UmicomU8 UmicomFat16UpdateFixtureFileByte(UmicomU64 index, UmicomBoolean changed)
{
    if (changed && index >= UMICOM_FAT16_UPDATE_FIXTURE_OFFSET &&
        index - UMICOM_FAT16_UPDATE_FIXTURE_OFFSET < UMICOM_FAT16_UPDATE_FIXTURE_BYTES)
        return UmicomFat16UpdateFixturePattern(index - UMICOM_FAT16_UPDATE_FIXTURE_OFFSET);
    return UmicomDiskFixturePattern(index);
}
static inline void UmicomFat16UpdateFixturePut16(UmicomU8 *bytes, UmicomU16 value)
{
    bytes[0] = (UmicomU8)value; bytes[1] = (UmicomU8)(value >> 8U);
}
static inline void UmicomFat16UpdateFixturePut32(UmicomU8 *bytes, UmicomU32 value)
{
    for (UmicomSize i = 0U; i < 4U; ++i) bytes[i] = (UmicomU8)(value >> (i * 8U));
}
static inline void UmicomFat16UpdateFixtureCopy(UmicomU8 *output, const char *text, UmicomSize bytes)
{
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = (UmicomU8)text[i];
}
static inline void UmicomFat16UpdateFixtureEntry(UmicomU8 *output, const char *name,
    UmicomU8 attributes, UmicomU16 cluster, UmicomU32 bytes)
{
    UmicomFat16UpdateFixtureCopy(output, name, 11U);
    output[11] = attributes;
    UmicomFat16UpdateFixturePut16(output + 26U, cluster);
    UmicomFat16UpdateFixturePut32(output + 28U, bytes);
}
static inline void UmicomFat16UpdateFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomBoolean changed)
{
    /* Volatile stores keep this freestanding helper independent of memset. */
    volatile UmicomU8 *const cleared = (volatile UmicomU8 *)output;
    for (UmicomSize i = 0U; i < 512U; ++i) cleared[i] = 0U;
    if (sector == 0U) {
        UmicomU8 *const part = output + 446U;
        part[1] = 0xfeU; part[2] = 0xffU; part[3] = 0xffU; part[4] = 0x06U;
        part[5] = 0xfeU; part[6] = 0xffU; part[7] = 0xffU;
        UmicomFat16UpdateFixturePut32(part + 8U, UMICOM_DISK_FIXTURE_FIRST);
        UmicomFat16UpdateFixturePut32(part + 12U, UMICOM_DISK_FIXTURE_LENGTH);
        output[510] = 0x55U; output[511] = 0xaaU;
    } else if (sector == UMICOM_DISK_FIXTURE_FIRST) {
        output[0] = 0xebU; output[1] = 0x3cU; output[2] = 0x90U;
        UmicomFat16UpdateFixtureCopy(output + 3U, "UMICOM  ", 8U);
        UmicomFat16UpdateFixturePut16(output + 11U, 512U); output[13] = 1U;
        UmicomFat16UpdateFixturePut16(output + 14U, 1U); output[16] = 2U;
        UmicomFat16UpdateFixturePut16(output + 17U, 512U);
        UmicomFat16UpdateFixturePut16(output + 19U, UMICOM_DISK_FIXTURE_LENGTH);
        output[21] = 0xf8U;
        UmicomFat16UpdateFixturePut16(output + 22U, UMICOM_DISK_FIXTURE_FAT_SECTORS);
        UmicomFat16UpdateFixturePut16(output + 24U, 63U);
        UmicomFat16UpdateFixturePut16(output + 26U, 255U);
        UmicomFat16UpdateFixturePut32(output + 28U, UMICOM_DISK_FIXTURE_FIRST);
        output[36] = 0x80U; output[38] = 0x29U;
        UmicomFat16UpdateFixturePut32(output + 39U, 0x554d4346U);
        UmicomFat16UpdateFixtureCopy(output + 43U, "UMICOMDISK ", 11U);
        UmicomFat16UpdateFixtureCopy(output + 54U, "FAT16   ", 8U);
        output[510] = 0x55U; output[511] = 0xaaU;
    } else if (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
        sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS) {
        UmicomFat16UpdateFixturePut16(output, 0xfff8U);
        UmicomFat16UpdateFixturePut16(output + 2U, 0xffffU);
        UmicomFat16UpdateFixturePut16(output + 4U, 0xffffU);
        UmicomFat16UpdateFixturePut16(output + 6U, 0xffffU);
        UmicomFat16UpdateFixturePut16(output + 8U, 9U);
        UmicomFat16UpdateFixturePut16(output + 18U, 6U);
        UmicomFat16UpdateFixturePut16(output + 12U, 0xffffU);
        UmicomFat16UpdateFixturePut16(output + 14U, 0xffffU);
    } else if (sector == UMICOM_DISK_FIXTURE_ROOT) {
        UmicomFat16UpdateFixtureEntry(output, "UMICOMDISK ", 0x08U, 0U, 0U);
        UmicomFat16UpdateFixtureEntry(output + 32U, "README  TXT", 0x21U, 2U,
            (UmicomU32)(sizeof(UMICOM_DISK_FIXTURE_README) - 1U));
        UmicomFat16UpdateFixtureEntry(output + 64U, "DOCS       ", 0x10U, 3U, 0U);
        UmicomFat16UpdateFixtureEntry(output + 96U, "FRAG    BIN", 0x20U, 4U,
            UMICOM_DISK_FIXTURE_FRAGMENT_BYTES);
        UmicomFat16UpdateFixtureEntry(output + 128U, "EMPTY   TXT", 0x20U, 0U, 0U);
    } else if (sector == UMICOM_DISK_FIXTURE_DATA) {
        UmicomFat16UpdateFixtureCopy(output, UMICOM_DISK_FIXTURE_README,
            sizeof(UMICOM_DISK_FIXTURE_README) - 1U);
    } else if (sector == UMICOM_DISK_FIXTURE_DATA + 1U) {
        UmicomFat16UpdateFixtureEntry(output, ".          ", 0x10U, 3U, 0U);
        UmicomFat16UpdateFixtureEntry(output + 32U, "..         ", 0x10U, 0U, 0U);
        UmicomFat16UpdateFixtureEntry(output + 64U, "GUIDE   TXT", 0x21U, 7U,
            (UmicomU32)(sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U));
    } else if (sector == UMICOM_DISK_FIXTURE_DATA + 5U) {
        UmicomFat16UpdateFixtureCopy(output, UMICOM_DISK_FIXTURE_GUIDE,
            sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U);
    } else if (sector == UMICOM_DISK_FIXTURE_DATA + 2U ||
        sector == UMICOM_DISK_FIXTURE_DATA + 7U || sector == UMICOM_DISK_FIXTURE_DATA + 4U) {
        const UmicomSize clusterIndex = sector == UMICOM_DISK_FIXTURE_DATA + 2U ? 0U :
            (sector == UMICOM_DISK_FIXTURE_DATA + 7U ? 1U : 2U);
        for (UmicomSize i = 0U; i < 512U; ++i) {
            const UmicomSize fileOffset = clusterIndex * 512U + i;
            output[i] = fileOffset < UMICOM_DISK_FIXTURE_FRAGMENT_BYTES ?
                UmicomFat16UpdateFixtureFileByte(fileOffset, changed) : 0xa6U;
        }
    }
}
#endif /* UMICOM_FAT16_UPDATE_GUEST_FIXTURE_H */
