/* Independent complete-media expectations for one FAT16 short-name rename.
 * The published original sector generator supplies every original byte.
 * No production planner or writer output constructs the replacement alias.
 * FRAG.BIN becomes SAVED.BIN; size, attributes, all calendars, file data,
 * allocation and slack remain original. The source already has NT bits zero.
 * Stage-only media additionally retains the two persistent dirty clean bits.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_RENAME_GUEST_FIXTURE_H
#define UMICOM_FAT16_RENAME_GUEST_FIXTURE_H
#include "../fat16_update/guest_fixture.h"

typedef enum UmicomFat16RenameFixtureState {
    UMICOM_FAT16_RENAME_FIXTURE_ORIGINAL,
    UMICOM_FAT16_RENAME_FIXTURE_STAGED,
    UMICOM_FAT16_RENAME_FIXTURE_COMMITTED
} UmicomFat16RenameFixtureState;

static inline void UmicomFat16RenameFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomFat16RenameFixtureState state)
{
    UmicomFat16UpdateFixtureSector(sector, output, UMICOM_FALSE);
    if (sector == UMICOM_DISK_FIXTURE_ROOT) {
        UmicomU8 *const entry = output + 96U;
        entry[11] = 0U; /* Existing checked source has ARCHIVE initially clear. */
        if (state != UMICOM_FAT16_RENAME_FIXTURE_ORIGINAL) {
            const char alias[11] = {'S','A','V','E','D',' ',' ',' ','B','I','N'};
            for (UmicomSize i = 0U; i < 11U; ++i) entry[i] = (UmicomU8)alias[i];
            entry[12] &= (UmicomU8)~0x18U;
        }
    }
    if (state == UMICOM_FAT16_RENAME_FIXTURE_STAGED &&
        (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
         sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS))
        output[3] = 0x7fU;
}
#endif /* UMICOM_FAT16_RENAME_GUEST_FIXTURE_H */
