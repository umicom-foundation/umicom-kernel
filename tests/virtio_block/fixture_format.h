/* Deterministic, synthetic block-test bytes; never an image of a real disk.
 * Shared by the protocol model and guest validation, not by the actual driver.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_BLOCK_FIXTURE_FORMAT_H
#define UMICOM_BLOCK_FIXTURE_FORMAT_H
#include "umicom/kernel/types.h"
#define UMICOM_BLOCK_FIXTURE_SECTORS 128U
static inline UmicomU8 UmicomBlockFixtureByte(UmicomU64 sector, UmicomSize offset)
{
    static const char label[] = "Umicom Kernel READ-ONLY block fixture. No filesystem.\n";
    if (sector == 0U && offset < sizeof(label) - 1U) return (UmicomU8)label[offset];
    return (UmicomU8)((sector * 37U + offset * 13U + ((offset >> 4U) ^ sector)) & 255U);
}
#endif /* UMICOM_BLOCK_FIXTURE_FORMAT_H */
