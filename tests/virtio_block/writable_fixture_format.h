/* Deterministic changes to a disposable copy of the original block fixture.
 * This is a shared test-byte description, never a filesystem or disk parser.
 * Every changed byte differs from the original; all other sectors retain the
 * established fixture bytes so whole-disk readback also checks the boundaries.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_BLOCK_WRITABLE_FIXTURE_FORMAT_H
#define UMICOM_BLOCK_WRITABLE_FIXTURE_FORMAT_H
#include "fixture_format.h"
#define UMICOM_BLOCK_WRITE_FIRST_SECTOR 2U
#define UMICOM_BLOCK_WRITE_FULL_FIRST_SECTOR 7U
#define UMICOM_BLOCK_WRITE_FULL_SECTORS 8U
#define UMICOM_BLOCK_WRITE_REUSE_FIRST_SECTOR 16U
#define UMICOM_BLOCK_WRITE_REUSE_SECTORS 12U
#define UMICOM_BLOCK_WRITE_LAST_SECTOR (UMICOM_BLOCK_FIXTURE_SECTORS - 1U)
static inline UmicomBoolean UmicomBlockWriteFixtureModified(UmicomU64 sector)
{
    return sector == UMICOM_BLOCK_WRITE_FIRST_SECTOR ||
        (sector >= UMICOM_BLOCK_WRITE_FULL_FIRST_SECTOR &&
            sector < UMICOM_BLOCK_WRITE_FULL_FIRST_SECTOR + UMICOM_BLOCK_WRITE_FULL_SECTORS) ||
        (sector >= UMICOM_BLOCK_WRITE_REUSE_FIRST_SECTOR &&
            sector < UMICOM_BLOCK_WRITE_REUSE_FIRST_SECTOR + UMICOM_BLOCK_WRITE_REUSE_SECTORS) ||
        sector == UMICOM_BLOCK_WRITE_LAST_SECTOR;
}
static inline UmicomU8 UmicomBlockWriteFixtureByte(UmicomU64 sector, UmicomSize offset)
{
    const UmicomU8 original = UmicomBlockFixtureByte(sector, offset);
    if (!UmicomBlockWriteFixtureModified(sector)) return original;
    const UmicomU8 mask = (UmicomU8)((((sector * 11U + offset * 3U +
        (offset >> 5U)) & 127U) << 1U) | 1U);
    return (UmicomU8)(original ^ mask);
}
#endif /* UMICOM_BLOCK_WRITABLE_FIXTURE_FORMAT_H */
