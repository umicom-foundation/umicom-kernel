/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/block_console.c
 *
 * Read-only disk inspection for the trusted development shell. Listing probes
 * identity words but does not reset, negotiate or allocate. A sector command
 * owns one handle from Open through Close; failed cleanup keeps that handle in
 * static storage so the next command cannot overwrite the remaining ownership.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/virtio_block.h"

static UmicomKernelBlockDomain *umicomConsoleBlockDomain;
static UmicomKernelBlockHandle umicomConsoleBlockHandle;
static UmicomU8 umicomConsoleSector[UMICOM_BLOCK_SECTOR_BYTES];
static void UmicomBlockText(void *context, UmicomKernelBlockOutput output, const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    output(context, text, bytes);
}
static void UmicomBlockNumber(void *context, UmicomKernelBlockOutput output, UmicomU64 value, UmicomBoolean hex)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[24];
    UmicomSize count = 0U;
    const UmicomU64 base = hex ? 16U : 10U;
    do { buffer[count++] = digits[value % base]; value /= base; } while (value);
    if (hex) UmicomBlockText(context, output, "0x");
    while (count) output(context, &buffer[--count], 1U);
}
void UmicomKernelBlockReport(void *context, UmicomKernelBlockOutput output)
{
    if (!output) return;
    UmicomKernelBlockDomain *domain = 0;
    const UmicomKernelBlockStatus status = UmicomPlatformBlockDomainGet(&domain);
    UmicomBlockText(context, output, "block.domain=");
    UmicomBlockText(context, output, UmicomKernelBlockStatusName(status));
    UmicomBlockText(context, output, "\r\n");
    if (status != UMICOM_BLOCK_OK) return;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info = {0};
        const UmicomKernelBlockStatus probed = UmicomKernelBlockProbe(domain, i, &info);
        UmicomBlockText(context, output, "block.slot="); UmicomBlockNumber(context, output, i, UMICOM_FALSE);
        UmicomBlockText(context, output, " base="); UmicomBlockNumber(context, output, info.base, UMICOM_TRUE);
        UmicomBlockText(context, output, " probe="); UmicomBlockText(context, output, UmicomKernelBlockStatusName(probed));
        UmicomBlockText(context, output, " device-id="); UmicomBlockNumber(context, output, info.deviceId, UMICOM_FALSE);
        UmicomBlockText(context, output, " held-frames="); UmicomBlockNumber(context, output, info.heldFrames, UMICOM_FALSE);
        UmicomBlockText(context, output, "\r\n");
    }
    UmicomBlockText(context, output,
        "Identity probe only. readsector SLOT LBA requires a modern, read-only block backend.\r\n"
        /* This earlier system-wide sentence predates the read-only inspectors.
         * Keep it for review, but describe this raw transport accurately now. */
#if 0
        "No partitions, filesystems or disk writes are enabled. blockclose retries retained cleanup.\r\n");
#endif
        "Raw reads only; no mounts or disk writes. blockclose retries retained cleanup.\r\n");
}
UmicomKernelBlockStatus UmicomKernelBlockRetryClose(void)
{
    if (!umicomConsoleBlockHandle) return UMICOM_BLOCK_OK;
    const UmicomKernelBlockStatus status = UmicomKernelBlockClose(umicomConsoleBlockDomain, umicomConsoleBlockHandle);
    if (status == UMICOM_BLOCK_OK) umicomConsoleBlockHandle = 0U;
    return status;
}
UmicomKernelBlockStatus UmicomKernelBlockInspectSector(UmicomSize index, UmicomU64 sector,
    void *context, UmicomKernelBlockOutput output)
{
    if (!output) return UMICOM_BLOCK_INVALID_ARGUMENT;
    UmicomKernelBlockStatus status = UmicomKernelBlockRetryClose();
    if (status == UMICOM_BLOCK_OK) status = UmicomPlatformBlockDomainGet(&umicomConsoleBlockDomain);
    if (status == UMICOM_BLOCK_OK)
        status = UmicomKernelBlockOpen(umicomConsoleBlockDomain, index, 10000000U, &umicomConsoleBlockHandle);
    if (status == UMICOM_BLOCK_OK)
        status = UmicomKernelBlockRead(umicomConsoleBlockDomain, umicomConsoleBlockHandle, sector, 1U,
            umicomConsoleSector, sizeof(umicomConsoleSector));
    if (status == UMICOM_BLOCK_OK) {
        static const char hex[] = "0123456789abcdef";
        for (UmicomSize row = 0U; row < UMICOM_BLOCK_SECTOR_BYTES; row += 16U) {
            /* Fixed-width hex and printable-only ASCII cannot execute terminal
             * control sequences from an untrusted disk sector. */
            char line[80];
            UmicomSize length = 0U;
            line[length++] = hex[(row >> 8U) & 15U];
            line[length++] = hex[(row >> 4U) & 15U];
            line[length++] = hex[row & 15U];
            line[length++] = ':'; line[length++] = ' ';
            for (UmicomSize i = 0U; i < 16U; ++i) {
                const UmicomU8 value = umicomConsoleSector[row + i];
                line[length++] = hex[value >> 4U]; line[length++] = hex[value & 15U]; line[length++] = ' ';
            }
            line[length++] = '|';
            for (UmicomSize i = 0U; i < 16U; ++i) {
                const UmicomU8 value = umicomConsoleSector[row + i];
                line[length++] = value >= 32U && value <= 126U ? (char)value : '.';
            }
            line[length++] = '|'; line[length++] = '\r'; line[length++] = '\n';
            output(context, line, length);
        }
    }
    /* A setup failure may also leave a handle when reset/release was refused.
     * Always attempt closure, without hiding the original operation result. */
    const UmicomKernelBlockStatus closed = UmicomKernelBlockRetryClose();
    for (UmicomSize i = 0U; i < sizeof(umicomConsoleSector); ++i) umicomConsoleSector[i] = 0U;
    UmicomBlockText(context, output, "block.read=");
    UmicomBlockText(context, output, UmicomKernelBlockStatusName(status));
    UmicomBlockText(context, output, " close=");
    UmicomBlockText(context, output, UmicomKernelBlockStatusName(closed));
    UmicomBlockText(context, output, "\r\n");
    return closed == UMICOM_BLOCK_OK ? status : closed;
}
