/*-----------------------------------------------------------------------------
 * Umicom Kernel independent writable-VFS media oracle
 *
 * Begin with the published archive-clear synthetic fixture, then encode the
 * known final directory records, allocation links and user-written bytes.
 * Literal clusters describe this disposable disk only. No production planner,
 * mount result or post-write image determines the expected result.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_WRITABLE_GUEST_FIXTURE_H
#define UMICOM_KERNEL_DISK_WRITABLE_GUEST_FIXTURE_H
#include "../fat16_lifecycle/guest_fixture.h"

#define UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_BYTES 900U
#define UMICOM_KERNEL_DISK_WRITABLE_FIXTURE_COMMITS 12U

static inline UmicomKernelFat16FileTime UmicomKernelDiskWritableFixtureTime(void)
{
    UmicomKernelFat16FileTime time;
    time.year = 2044U; time.month = 2U; time.day = 29U;
    time.hour = 23U; time.minute = 58U; time.second = 56U;
    return time;
}
static inline UmicomU8 UmicomKernelDiskWritableFixtureByte(UmicomU64 index)
{
    if (index >= 480U && index < 560U)
        return (UmicomU8)(((index - 480U) * 17U + 0x91U) & 255U);
    if (index < 700U) return (UmicomU8)((index * 29U + 0x31U) & 255U);
    if (index < 1100U) return (UmicomU8)(((index - 700U) * 53U + 0xb7U) & 255U);
    return 0U;
}
static inline void UmicomKernelDiskWritableFixtureEntry(UmicomU8 *entry,
    const char *name, UmicomU8 attributes, UmicomU16 cluster, UmicomU32 bytes)
{
    for (UmicomSize i = 0U; i < 32U; ++i) entry[i] = 0U;
    UmicomFat16UpdateFixtureEntry(entry, name, attributes, cluster, bytes);
    /* 2044-02-29 23:58:56 is encoded independently of the production encoder. */
    UmicomFat16UpdateFixturePut16(entry + 14U, 0xbf5cU);
    UmicomFat16UpdateFixturePut16(entry + 16U, 0x805dU);
    UmicomFat16UpdateFixturePut16(entry + 18U, 0x805dU);
    UmicomFat16UpdateFixturePut16(entry + 22U, 0xbf5cU);
    UmicomFat16UpdateFixturePut16(entry + 24U, 0x805dU);
}
static inline void UmicomKernelDiskWritableFixtureDots(UmicomU8 *output,
    UmicomU16 self, UmicomU16 parent)
{
    for (UmicomSize i = 0U; i < 512U; ++i) output[i] = 0U;
    UmicomKernelDiskWritableFixtureEntry(output, ".          ", 0x10U, self, 0U);
    UmicomKernelDiskWritableFixtureEntry(output + 32U, "..         ", 0x10U, parent, 0U);
}
static inline void UmicomKernelDiskWritableFixtureSector(UmicomU64 sector,
    UmicomU8 *output, UmicomBoolean committed)
{
    UmicomFat16LifecycleFixtureSector(sector, output, 0U, UMICOM_FALSE);
    if (!committed) return;
    if (sector == UMICOM_DISK_FIXTURE_ROOT)
        UmicomKernelDiskWritableFixtureEntry(output + 160U, "WORK       ", 0x10U, 5U, 0U);
    if (sector == UMICOM_DISK_FIXTURE_FIRST + 1U ||
        sector == UMICOM_DISK_FIXTURE_FIRST + 1U + UMICOM_DISK_FIXTURE_FAT_SECTORS) {
        UmicomFat16UpdateFixturePut16(output + 10U, 0xffffU); /* WORK owns cluster 5. */
        UmicomFat16UpdateFixturePut16(output + 16U, 10U); /* LOG uses fragmented 8 -> 10. */
        UmicomFat16UpdateFixturePut16(output + 20U, 0xffffU);
        UmicomFat16UpdateFixturePut16(output + 22U, 0U); /* Reclaimed, reused, then reclaimed. */
    }
    if (sector == UMICOM_DISK_FIXTURE_DATA + 3U) {
        UmicomKernelDiskWritableFixtureDots(output, 5U, 0U);
        UmicomKernelDiskWritableFixtureEntry(output + 64U, "LOG     BIN", 0x20U, 8U, 900U);
        UmicomKernelDiskWritableFixtureEntry(output + 96U, "SUB        ", 0x10U, 11U, 0U);
        output[96U] = 0xe5U;
    }
    if (sector == UMICOM_DISK_FIXTURE_DATA + 6U || sector == UMICOM_DISK_FIXTURE_DATA + 8U) {
        const UmicomU64 first = sector == UMICOM_DISK_FIXTURE_DATA + 6U ? 0U : 512U;
        /* Shrink changes logical length and allocation; retained cluster slack
         * still contains the earlier accepted append bytes after byte 899. */
        for (UmicomSize i = 0U; i < 512U; ++i)
            output[i] = UmicomKernelDiskWritableFixtureByte(first + i);
    }
    if (sector == UMICOM_DISK_FIXTURE_DATA + 9U)
        UmicomKernelDiskWritableFixtureDots(output, 11U, 5U);
}
#endif /* UMICOM_KERNEL_DISK_WRITABLE_GUEST_FIXTURE_H */
