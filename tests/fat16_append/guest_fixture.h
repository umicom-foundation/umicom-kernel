/* Independent complete-media expectations for a bounded FAT16 append.
 * Reuse only the published original sector generator. No production planner,
 * encoder or writer output is used to construct the expected modified image.
 * FRAG.BIN grows from 1300 to 1497 bytes within cluster 6; every existing file
 * byte, allocation entry, neighbouring record and remaining slack byte stays
 * exact. The interrupted image additionally retains both dirty clean bits.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_APPEND_GUEST_FIXTURE_H
#define UMICOM_FAT16_APPEND_GUEST_FIXTURE_H
#include "../fat16_update/guest_fixture.h"

#define UMICOM_FAT16_APPEND_FIXTURE_OFFSET 1300U
#define UMICOM_FAT16_APPEND_FIXTURE_BYTES 197U
#define UMICOM_FAT16_APPEND_FIXTURE_SLACK 236U
#define UMICOM_FAT16_APPEND_FIXTURE_NEW_BYTES 1497U
#define UMICOM_FAT16_APPEND_FIXTURE_YEAR 2044U
#define UMICOM_FAT16_APPEND_FIXTURE_MONTH 2U
#define UMICOM_FAT16_APPEND_FIXTURE_DAY 29U
#define UMICOM_FAT16_APPEND_FIXTURE_HOUR 23U
#define UMICOM_FAT16_APPEND_FIXTURE_MINUTE 58U
#define UMICOM_FAT16_APPEND_FIXTURE_SECOND 57U
#define UMICOM_FAT16_APPEND_FIXTURE_TIME 0xbf5cU
#define UMICOM_FAT16_APPEND_FIXTURE_DATE 0x805dU

typedef enum UmicomFat16AppendFixtureState {
    UMICOM_FAT16_APPEND_FIXTURE_ORIGINAL,
    UMICOM_FAT16_APPEND_FIXTURE_STAGED,
    UMICOM_FAT16_APPEND_FIXTURE_COMMITTED
} UmicomFat16AppendFixtureState;

static inline UmicomU8 UmicomFat16AppendFixturePattern(UmicomU64 index)
{
    return (UmicomU8)((index * 37U + 0x53U) & 0x7fU);
}
static inline UmicomU8 UmicomFat16AppendFixtureFileByte(UmicomU64 index)
{
    return index < 1300U ? UmicomDiskFixturePattern(index) :
        UmicomFat16AppendFixturePattern(index - 1300U);
}
static inline void UmicomFat16AppendFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomFat16AppendFixtureState state)
{
    UmicomFat16UpdateFixtureSector(sector, output, UMICOM_FALSE);
    if (sector == UMICOM_DISK_FIXTURE_ROOT) {
        UmicomU8 *const entry = output + 96U;
        if (state == UMICOM_FAT16_APPEND_FIXTURE_ORIGINAL) {
            entry[11] = 0U;
        } else {
            entry[11] = 0x20U;
            entry[22] = 0x5cU; entry[23] = 0xbfU;
            entry[24] = 0x5dU; entry[25] = 0x80U;
            /* 1497=0x000005d9. All four size bytes are independently specified. */
            entry[28] = 0xd9U; entry[29] = 0x05U;
            entry[30] = 0U; entry[31] = 0U;
        }
    }
    if (state != UMICOM_FAT16_APPEND_FIXTURE_ORIGINAL &&
        sector == UMICOM_DISK_FIXTURE_DATA + 4U) {
        for (UmicomSize i = 0U; i < 197U; ++i)
            output[276U + i] = UmicomFat16AppendFixturePattern(i);
    }
    if (state == UMICOM_FAT16_APPEND_FIXTURE_STAGED &&
        (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
         sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS))
        output[3] = 0x7fU;
}
#endif /* UMICOM_FAT16_APPEND_GUEST_FIXTURE_H */
