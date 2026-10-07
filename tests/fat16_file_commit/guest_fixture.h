/* Synthetic FAT16 file-data and directory-entry commit expectations.
 * The initial image differs from the established fixture only by clearing
 * FRAG.BIN's ARCHIVE bit. Its timestamped update changes caller bytes, ARCHIVE
 * and WrtTime/WrtDate; the staged image additionally retains both dirty flags.
 * No production planner, timestamp encoder or commit image generates these
 * expectations. Every other directory byte and neighbouring sector is exact.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_FILE_COMMIT_GUEST_FIXTURE_H
#define UMICOM_FAT16_FILE_COMMIT_GUEST_FIXTURE_H
#include "../fat16_update/guest_fixture.h"

#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_YEAR 2037U
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_MONTH 11U
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_DAY 23U
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_HOUR 14U
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_MINUTE 35U
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_SECOND 59U
/* Independently written constants for 2037-11-23 14:35:58. The odd caller
 * second is rounded down to FAT's two-second representation. */
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_TIME 0x747dU
#define UMICOM_FAT16_FILE_COMMIT_FIXTURE_DATE 0x7377U

typedef enum UmicomFat16FileCommitFixtureState {
    UMICOM_FAT16_FILE_FIXTURE_ORIGINAL,
    UMICOM_FAT16_FILE_FIXTURE_STAGED,
    UMICOM_FAT16_FILE_FIXTURE_COMMITTED
} UmicomFat16FileCommitFixtureState;

static inline void UmicomFat16FileCommitFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomFat16FileCommitFixtureState state)
{
    UmicomFat16UpdateFixtureSector(sector, output,
        state == UMICOM_FAT16_FILE_FIXTURE_ORIGINAL ? UMICOM_FALSE : UMICOM_TRUE);
    if (sector == UMICOM_DISK_FIXTURE_ROOT) {
        UmicomU8 *const entry = output + 96U;
        if (state == UMICOM_FAT16_FILE_FIXTURE_ORIGINAL) {
            entry[11] = 0U;
        } else {
            entry[11] = 0x20U;
            /* These exact bytes are deliberately independent of the public
             * calendar encoder and the production directory-sector plan. */
            entry[22] = 0x7dU; entry[23] = 0x74U;
            entry[24] = 0x77U; entry[25] = 0x73U;
        }
    }
    if (state == UMICOM_FAT16_FILE_FIXTURE_STAGED &&
        (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
         sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS)) {
        /* Only FAT[1]'s clean bit changes; its no-error bit stays set. */
        output[3] = 0x7fU;
    }
}
#endif /* UMICOM_FAT16_FILE_COMMIT_GUEST_FIXTURE_H */
