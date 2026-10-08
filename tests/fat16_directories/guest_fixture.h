/* Umicom Kernel independent directory lifecycle media oracle.
 * Literal fixture clusters describe a disposable disk only. Production code
 * never uses these identifiers or chooses allocations from this test oracle.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_DIRECTORY_GUEST_FIXTURE_H
#define UMICOM_FAT16_DIRECTORY_GUEST_FIXTURE_H
#include "../fat16_lifecycle/guest_fixture.h"

/* Retain the original directory-only bound while extending real reboot checks. */
#if 0
#define UMICOM_FAT16_DIRECTORY_FIXTURE_STATES 10U
#endif
#define UMICOM_FAT16_DIRECTORY_FIXTURE_STATES 14U
static inline const char *UmicomFat16DirectoryFixturePhase(UmicomSize state)
{
    /* Static read-only storage avoids a freestanding runtime copy at startup. */
    static const char *const names[] = {"original", "mkdir", "nested", "create", "append",
        "delete", "rmdir-nested", "rmdir", "reuse", "growth",
        "move-tree", "move-root", "rename-tree", "move-file"};
    return state < UMICOM_FAT16_DIRECTORY_FIXTURE_STATES ? names[state] : "invalid";
}
/* Encode the supplied leap-day calendar independently from the production
 * encoder. Both dot entries and their parent entry must carry these values. */
static inline void UmicomFat16DirectoryFixtureEntry(UmicomU8 *entry,
    const char *name, UmicomU8 attributes, UmicomU16 cluster, UmicomU32 bytes)
{
    for (UmicomSize i = 0U; i < 32U; ++i) entry[i] = 0U;
    UmicomFat16UpdateFixtureEntry(entry, name, attributes, cluster, bytes);
    UmicomFat16UpdateFixturePut16(entry + 14U, 0xbf5cU);
    UmicomFat16UpdateFixturePut16(entry + 16U, 0x805dU);
    UmicomFat16UpdateFixturePut16(entry + 18U, 0x805dU);
    UmicomFat16UpdateFixturePut16(entry + 22U, 0xbf5cU);
    UmicomFat16UpdateFixturePut16(entry + 24U, 0x805dU);
}
static inline void UmicomFat16DirectoryFixtureDots(UmicomU8 *output, UmicomU16 self, UmicomU16 parent)
{
    for (UmicomSize i = 0U; i < 512U; ++i) output[i] = 0U;
    UmicomFat16DirectoryFixtureEntry(output, ".          ", 0x10U, self, 0U);
    UmicomFat16DirectoryFixtureEntry(output + 32U, "..         ", 0x10U, parent, 0U);
}
/* The original checkpoint generator remains the foundation for its ten states.
 * Later move checkpoints overlay only independently specified metadata bytes. */
static inline void UmicomFat16DirectoryFixtureBaseSector(UmicomU64 sector, UmicomU8 *output,
    UmicomSize state, UmicomBoolean dirty)
{
    UmicomFat16LifecycleFixtureSector(sector, output, 0U, UMICOM_FALSE);
    if (state && sector == UMICOM_DISK_FIXTURE_ROOT) {
        UmicomFat16DirectoryFixtureEntry(output + 160U, state >= 8U ? "AGAIN      " : "WORK       ", 0x10U, 5U, 0U);
        if (state == 7U) output[160U] = 0xe5U;
    }
    if (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
        sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS) {
        if (state) UmicomFat16UpdateFixturePut16(output + 10U, state == 7U ? 0U : state == 9U ? 10U : 0xffffU);
        if (state >= 2U) UmicomFat16UpdateFixturePut16(output + 16U, state < 6U || state == 9U ? 0xffffU : 0U);
        if (state >= 3U) {
            UmicomFat16UpdateFixturePut16(output + 20U, state < 5U ? 11U : state == 9U ? 0xffffU : 0U);
            UmicomFat16UpdateFixturePut16(output + 22U, state == 3U ? 0xffffU : state == 4U ? 12U : 0U);
        }
        if (state >= 4U) {
            UmicomFat16UpdateFixturePut16(output + 24U, state == 4U ? 13U : 0U);
            UmicomFat16UpdateFixturePut16(output + 26U, state == 4U ? 0xffffU : 0U);
        }
        if (dirty) output[3U] = 0x7fU;
    }
    if (state && sector == UMICOM_DISK_FIXTURE_DATA + 3U) {
        UmicomFat16DirectoryFixtureDots(output, 5U, 0U);
        if (state >= 2U && state < 8U) {
            UmicomFat16DirectoryFixtureEntry(output + 64U, "SUB        ", 0x10U, 8U, 0U);
            if (state >= 6U) output[64U] = 0xe5U;
        }
        if (state == 9U) {
            /* Fourteen empty files exhaust the first cluster after dot records. */
            for (UmicomSize i = 0U; i < 14U; ++i) {
                char alias[] = "F0000000TXT";
                alias[6] = (char)('0' + i / 10U);
                alias[7] = (char)('0' + i % 10U);
                UmicomFat16DirectoryFixtureEntry(output + (i + 2U) * 32U, alias, 0x20U, 0U, 0U);
            }
        }
    }
    if (state >= 2U && sector == UMICOM_DISK_FIXTURE_DATA + 6U) {
        UmicomFat16DirectoryFixtureDots(output, 8U, 5U);
        if (state >= 3U && state < 9U) {
            UmicomFat16DirectoryFixtureEntry(output + 64U, "NOTE    TXT", 0x20U, 10U, state == 3U ? 700U : 1600U);
            if (state >= 5U) output[64U] = 0xe5U;
        }
    }
    if (state >= 3U && sector >= UMICOM_DISK_FIXTURE_DATA + 8U &&
        sector <= UMICOM_DISK_FIXTURE_DATA + 11U) {
        const UmicomU64 first = (sector - (UMICOM_DISK_FIXTURE_DATA + 8U)) * 512U;
        if (first < 1024U || state >= 4U)
            for (UmicomSize i = 0U; i < 512U; ++i) {
                const UmicomU64 index = first + i;
                output[i] = index < 700U ? UmicomFat16LifecycleFixturePattern(1U, index) :
                    state >= 4U && index < 1600U ? UmicomFat16LifecycleFixturePattern(2U, index - 700U) : 0U;
            }
        if (state == 9U && sector == UMICOM_DISK_FIXTURE_DATA + 8U) {
            for (UmicomSize i = 0U; i < 512U; ++i) output[i] = 0U;
            UmicomFat16DirectoryFixtureEntry(output, "CHILD      ", 0x10U, 8U, 0U);
        }
    }
}

/* The move oracle uses known synthetic records and constants, never a production
 * plan or the resulting disk. Every unmentioned byte remains checkpoint nine. */
static inline void UmicomFat16DirectoryFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomSize state, UmicomBoolean dirty)
{
    UmicomFat16DirectoryFixtureBaseSector(sector, output, state < 10U ? state : 9U, dirty);
    if (state < 10U) return;
    if (sector == UMICOM_DISK_FIXTURE_ROOT) {
        output[160U] = 0xe5U;
        if (state >= 11U)
            UmicomFat16DirectoryFixtureEntry(output + 160U, "CHILD      ", 0x10U, 8U, 0U);
        if (state >= 13U)
            UmicomFat16DirectoryFixtureEntry(output + 192U, "MOVED   TXT", 0x20U, 0U, 0U);
    }
    if (sector == UMICOM_DISK_FIXTURE_DATA + 1U)
        UmicomFat16DirectoryFixtureEntry(output + 96U, state >= 12U ? "RENAMED    " : "LIBRARY    ", 0x10U, 5U, 0U);
    if (sector == UMICOM_DISK_FIXTURE_DATA + 3U) {
        UmicomFat16UpdateFixturePut16(output + 58U, 3U);
        if (state >= 13U) output[64U] = 0xe5U;
    }
    if (state >= 11U && sector == UMICOM_DISK_FIXTURE_DATA + 6U)
        UmicomFat16UpdateFixturePut16(output + 58U, 0U);
    if (state >= 11U && sector == UMICOM_DISK_FIXTURE_DATA + 8U) output[0U] = 0xe5U;
}

#endif /* UMICOM_FAT16_DIRECTORY_GUEST_FIXTURE_H */
