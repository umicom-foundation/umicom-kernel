/* Independent complete-media oracle for six persisted FAT16 lifecycle steps.
 * The published synthetic source supplies all original bytes. This generator
 * applies literal allocation, directory and payload expectations, without any
 * production lifecycle planner or transport result. Freed data remains intact;
 * newly allocated clusters, including their unused tails, begin fully zeroed.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_FAT16_LIFECYCLE_GUEST_FIXTURE_H
#define UMICOM_FAT16_LIFECYCLE_GUEST_FIXTURE_H
#include "../fat16_update/guest_fixture.h"
#include "umicom/kernel/fat16_file_plan.h"

#define UMICOM_FAT16_LIFECYCLE_FIXTURE_STATES 7U
#define UMICOM_FAT16_LIFECYCLE_CREATE_BYTES 700U
#define UMICOM_FAT16_LIFECYCLE_APPEND_BYTES 900U
#define UMICOM_FAT16_LIFECYCLE_TRUNCATE_BYTES 513U
#define UMICOM_FAT16_LIFECYCLE_EMPTY_BYTES 600U

/* State zero is the checked source; states one through six follow this order. */
static inline const char *UmicomFat16LifecycleFixturePhase(UmicomSize state)
{
    const char *const phases[7] = {"original", "create", "append", "truncate", "delete", "reuse", "empty-append"};
    return state < 7U ? phases[state] : "invalid";
}
static inline UmicomKernelFat16FileTime UmicomFat16LifecycleFixtureTime(UmicomSize state)
{
    /* Scalar assignments avoid an implicit freestanding memcpy at -O0. */
    UmicomKernelFat16FileTime time;
    time.year = state >= 1U && state <= 3U ? 2048U : (state == 5U ? 2049U : (state == 6U ? 2050U : 0U));
    time.month = state == 1U ? 2U : (state == 2U || state == 3U ? 3U : (state == 5U ? 1U : (state == 6U ? 6U : 0U)));
    time.day = state == 1U ? 29U : (state == 2U ? 1U : (state == 3U || state == 5U ? 2U : (state == 6U ? 7U : 0U)));
    time.hour = state == 1U ? 12U : (state == 2U ? 1U : (state == 3U ? 2U : (state == 5U ? 3U : (state == 6U ? 8U : 0U))));
    time.minute = state == 1U ? 34U : (state == 2U ? 2U : (state == 3U ? 3U : (state == 5U ? 4U : (state == 6U ? 9U : 0U))));
    time.second = state == 1U ? 57U : (state == 2U ? 3U : (state == 3U ? 5U : (state == 5U ? 7U : (state == 6U ? 11U : 0U))));
    return time;
}
static inline UmicomU16 UmicomFat16LifecycleFixtureDate(UmicomSize state)
{
    const UmicomKernelFat16FileTime time = UmicomFat16LifecycleFixtureTime(state);
    return time.year ? (UmicomU16)((((UmicomU32)time.year - 1980U) << 9U) | ((UmicomU32)time.month << 5U) | (UmicomU32)time.day) : 0U;
}
static inline UmicomU16 UmicomFat16LifecycleFixtureClock(UmicomSize state)
{
    const UmicomKernelFat16FileTime time = UmicomFat16LifecycleFixtureTime(state);
    return (UmicomU16)(((UmicomU32)time.hour << 11U) | ((UmicomU32)time.minute << 5U) | ((UmicomU32)time.second / 2U));
}
static inline UmicomU8 UmicomFat16LifecycleFixturePattern(UmicomSize phase, UmicomU64 index)
{
    if (phase == 2U) return (UmicomU8)((index * 53U + 0xb7U) & 255U);
    if (phase == 5U) return (UmicomU8)((index * 73U + 0x5dU) & 255U);
    if (phase == 6U) return (UmicomU8)((index * 97U + 0xc3U) & 255U);
    return (UmicomU8)((index * 29U + 0x31U) & 255U);
}
static inline UmicomU8 UmicomFat16LifecycleFixtureFileByte(UmicomSize state, UmicomU64 index)
{
    if (state >= 5U) return UmicomFat16LifecycleFixturePattern(5U, index);
    return index < 700U ? UmicomFat16LifecycleFixturePattern(1U, index) :
        UmicomFat16LifecycleFixturePattern(2U, index - 700U);
}
static inline void UmicomFat16LifecycleFixtureWriteTime(UmicomU8 *entry, UmicomSize phase)
{
    UmicomFat16UpdateFixturePut16(entry + 22U, UmicomFat16LifecycleFixtureClock(phase));
    UmicomFat16UpdateFixturePut16(entry + 24U, UmicomFat16LifecycleFixtureDate(phase));
}
static inline void UmicomFat16LifecycleFixtureNewEntry(UmicomU8 *entry, UmicomSize phase)
{
    for (UmicomSize i = 0U; i < 32U; ++i) entry[i] = 0U;
    UmicomFat16UpdateFixtureEntry(entry, phase == 5U ? "REUSE   BIN" : "LIFE    BIN", 0x20U, 5U, 700U);
    UmicomFat16UpdateFixturePut16(entry + 14U, UmicomFat16LifecycleFixtureClock(phase));
    UmicomFat16UpdateFixturePut16(entry + 16U, UmicomFat16LifecycleFixtureDate(phase));
    UmicomFat16UpdateFixturePut16(entry + 18U, UmicomFat16LifecycleFixtureDate(phase));
    UmicomFat16LifecycleFixtureWriteTime(entry, phase);
}
static inline void UmicomFat16LifecycleFixtureSector(UmicomU64 sector, UmicomU8 *output,
    UmicomSize state, UmicomBoolean dirty)
{
    UmicomFat16UpdateFixtureSector(sector, output, UMICOM_FALSE);
    if (sector == UMICOM_DISK_FIXTURE_ROOT) {
        output[96U + 11U] = 0U; /* Checked file-commit source, not update source. */
        if (state) {
            UmicomU8 *const entry = output + 160U;
            UmicomFat16LifecycleFixtureNewEntry(entry, state >= 5U ? 5U : 1U);
            if (state >= 2U && state <= 4U) {
                UmicomFat16UpdateFixturePut32(entry + 28U, state == 2U ? 1600U : 513U);
                UmicomFat16LifecycleFixtureWriteTime(entry, state == 2U ? 2U : 3U);
            }
            if (state == 4U) entry[0] = 0xe5U;
        }
        if (state == 6U) {
            UmicomFat16UpdateFixturePut16(output + 128U + 26U, 10U);
            UmicomFat16UpdateFixturePut32(output + 128U + 28U, 600U);
            UmicomFat16LifecycleFixtureWriteTime(output + 128U, 6U);
        }
    }
    if (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
        sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS) {
        if (state && state != 4U) {
            UmicomFat16UpdateFixturePut16(output + 10U, 8U);
            UmicomFat16UpdateFixturePut16(output + 16U, state == 2U ? 10U : 0xffffU);
        }
        if (state == 2U || state == 6U) {
            UmicomFat16UpdateFixturePut16(output + 20U, 11U);
            UmicomFat16UpdateFixturePut16(output + 22U, 0xffffU);
        }
        if (dirty) output[3] = 0x7fU;
    }
    /* Free cluster 5 is DATA+3 and free cluster 8 is DATA+6. After deletion
     * their contents persist until reuse, which zeroes the complete clusters. */
    if (state && (sector == UMICOM_DISK_FIXTURE_DATA + 3U || sector == UMICOM_DISK_FIXTURE_DATA + 6U)) {
        const UmicomU64 first = sector == UMICOM_DISK_FIXTURE_DATA + 3U ? 0U : 512U;
        for (UmicomSize i = 0U; i < 512U; ++i) {
            const UmicomU64 index = first + i;
            output[i] = index < 700U ? UmicomFat16LifecycleFixturePattern(state >= 5U ? 5U : 1U, index) :
                (state >= 2U && state <= 4U ? UmicomFat16LifecycleFixturePattern(2U, index - 700U) : 0U);
        }
    }
    /* Clusters 10 and 11 retain the append bytes after truncate and delete.
     * The empty-file append later reuses and completely zeroes both clusters. */
    if (state >= 2U && (sector == UMICOM_DISK_FIXTURE_DATA + 8U || sector == UMICOM_DISK_FIXTURE_DATA + 9U)) {
        const UmicomU64 first = sector == UMICOM_DISK_FIXTURE_DATA + 8U ? 0U : 512U;
        for (UmicomSize i = 0U; i < 512U; ++i) {
            const UmicomU64 index = first + i;
            output[i] = state == 6U ? (index < 600U ? UmicomFat16LifecycleFixturePattern(6U, index) : 0U) :
                (index < 576U ? UmicomFat16LifecycleFixturePattern(2U, index + 324U) : 0U);
        }
    }
}
#endif /* UMICOM_FAT16_LIFECYCLE_GUEST_FIXTURE_H */
