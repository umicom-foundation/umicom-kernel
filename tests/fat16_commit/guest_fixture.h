/* Synthetic FAT16 clean-commit and deliberate-interruption expectations.
 * Reuse the established independent fixture generator, adding only the two
 * documented clean-bit changes while an update remains staged. No production
 * planner or header image is used to construct the expected disk contents.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_COMMIT_GUEST_FIXTURE_H
#define UMICOM_FAT16_COMMIT_GUEST_FIXTURE_H
#include "../fat16_update/guest_fixture.h"

typedef enum UmicomFat16CommitFixtureState {
    UMICOM_FAT16_FIXTURE_ORIGINAL,
    UMICOM_FAT16_FIXTURE_STAGED,
    UMICOM_FAT16_FIXTURE_COMMITTED
} UmicomFat16CommitFixtureState;

static inline void UmicomFat16CommitFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomFat16CommitFixtureState state)
{
    UmicomFat16UpdateFixtureSector(sector, output,
        state == UMICOM_FAT16_FIXTURE_ORIGINAL ? UMICOM_FALSE : UMICOM_TRUE);
    if (state == UMICOM_FAT16_FIXTURE_STAGED &&
        (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
         sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS)) {
        /* FAT[1] is the little-endian word at byte two. Only its clean bit
         * changes: 0xffff becomes 0x7fff; the no-error bit remains set. */
        output[3] = 0x7fU;
    }
}
#endif /* UMICOM_FAT16_COMMIT_GUEST_FIXTURE_H */
