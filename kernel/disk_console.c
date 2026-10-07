/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_console.c
 *
 * Inspect a partitioned read-only disk through the existing VirtIO owner. All
 * command-local interpretations end before the transport is reset. A failed
 * reset retains the real handle in static storage, never on an abandoned stack.
 *
 * The console publishes complete bounded listings or complete small-file reads.
 * Metadata, file bytes and labels cannot issue terminal control sequences.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_console.h"
#include "umicom/kernel/platform.h"

static UmicomKernelBlockDomain *umicomDiskDomain;
static UmicomKernelBlockHandle umicomDiskHandle;
static UmicomKernelBlockStatus umicomDiskLastBlock;
static UmicomKernelFat16 umicomDiskVolume;
static UmicomKernelFat16Directory umicomDiskDirectory;
static UmicomU8 umicomDiskContent[UMICOM_FAT16_READ_BYTES];
static UmicomU64 umicomDiskStarted, umicomDiskClock;
static UmicomBoolean umicomDiskBusy;

static void UmicomDiskClear(void *target, UmicomSize count)
{
    volatile UmicomU8 *bytes = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < count; ++i) bytes[i] = 0U;
}
static void UmicomDiskText(void *context, UmicomKernelBlockOutput output, const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    output(context, text, bytes);
}
static void UmicomDiskNumber(void *context, UmicomKernelBlockOutput output, UmicomU64 value)
{
    char text[20]; UmicomSize count = 0U;
    do { text[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (count) output(context, &text[--count], 1U);
}
static UmicomBoolean UmicomDiskEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U;; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
}
static void UmicomDiskSafe(void *context, UmicomKernelBlockOutput output,
    const UmicomU8 *bytes, UmicomSize count)
{
    static const char digits[] = "0123456789abcdef";
    for (UmicomSize i = 0U; i < count; ++i) {
        const UmicomU8 byte = bytes[i];
        if (byte == 10U) output(context, "\r\n", 2U);
        else if (byte >= 32U && byte <= 126U) {
            const char printable = (char)byte; output(context, &printable, 1U);
        } else {
            const char escaped[4] = {'\\', 'x', digits[byte >> 4U], digits[byte & 15U]};
            output(context, escaped, sizeof(escaped));
        }
    }
}
static UmicomBoolean UmicomDiskRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    if (context != &umicomDiskVolume || !umicomDiskHandle) return UMICOM_FALSE;
    const UmicomU64 now = UmicomPlatformTimerRead();
    if (now < umicomDiskClock) { umicomDiskLastBlock = UMICOM_BLOCK_CLOCK_ERROR; return UMICOM_FALSE; }
    umicomDiskClock = now;
    /* Ten seconds bounds a complete command on the qualified 10 MHz profile.
     * A single request is additionally bounded by the existing block driver.
     * The core parser also caps I/O calls if a clock makes no progress. */
    if (now - umicomDiskStarted >= 100000000U) {
        umicomDiskLastBlock = UMICOM_BLOCK_TIMEOUT; return UMICOM_FALSE;
    }
    umicomDiskLastBlock = UmicomKernelBlockRead(umicomDiskDomain, umicomDiskHandle,
        sector, 1U, output, UMICOM_DISK_SECTOR_BYTES);
    return umicomDiskLastBlock == UMICOM_BLOCK_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelBlockStatus UmicomKernelDiskInspectionClose(void)
{
    if (umicomDiskBusy) return UMICOM_BLOCK_BUSY;
    if (umicomDiskVolume.open) {
        if (UmicomKernelFat16Close(&umicomDiskVolume) != UMICOM_DISK_OK) return UMICOM_BLOCK_BUSY;
    }
    if (!umicomDiskHandle) return UMICOM_BLOCK_OK;
    const UmicomKernelBlockStatus status = UmicomKernelBlockClose(umicomDiskDomain, umicomDiskHandle);
    /* Never discard a token while reset or page release remains incomplete. */
    if (status == UMICOM_BLOCK_OK) umicomDiskHandle = 0U;
    return status;
}
UmicomKernelDiskStatus UmicomKernelDiskInspect(UmicomSize slot, UmicomSize partition,
    const char *operation, const char *path, void *context, UmicomKernelBlockOutput output)
{
    if (!operation || !path || !output || slot >= UMICOM_BLOCK_SLOT_LIMIT ||
        partition >= UMICOM_DISK_PRIMARY_PARTITIONS) return UMICOM_DISK_INVALID_ARGUMENT;
    const UmicomBoolean partitions = UmicomDiskEqual(operation, "partitions");
    const UmicomBoolean infoOnly = UmicomDiskEqual(operation, "fatinfo");
    const UmicomBoolean list = UmicomDiskEqual(operation, "fatls");
    const UmicomBoolean cat = UmicomDiskEqual(operation, "fatcat");
    if (!partitions && !infoOnly && !list && !cat) return UMICOM_DISK_INVALID_ARGUMENT;
    if (umicomDiskBusy) return UMICOM_DISK_BUSY;
    UmicomKernelBlockStatus block = UmicomKernelDiskInspectionClose();
    if (block == UMICOM_BLOCK_OK) block = UmicomKernelBlockRetryClose();
    if (block == UMICOM_BLOCK_OK) block = UmicomPlatformBlockDomainGet(&umicomDiskDomain);
    if (block == UMICOM_BLOCK_OK)
        block = UmicomKernelBlockOpen(umicomDiskDomain, slot, 10000000U, &umicomDiskHandle);
    UmicomKernelBlockInfo blockInfo;
    UmicomDiskClear(&blockInfo, sizeof(blockInfo));
    if (block == UMICOM_BLOCK_OK) block = UmicomKernelBlockProbe(umicomDiskDomain, slot, &blockInfo);
    umicomDiskLastBlock = block;
    UmicomKernelDiskStatus status = block == UMICOM_BLOCK_OK ? UMICOM_DISK_OK : UMICOM_DISK_IO_ERROR;
    umicomDiskBusy = UMICOM_TRUE;
    umicomDiskStarted = UmicomPlatformTimerRead(); umicomDiskClock = umicomDiskStarted;
    const UmicomKernelDiskReader reader = {blockInfo.sectors, UmicomDiskRead, &umicomDiskVolume};
    if (status == UMICOM_DISK_OK && partitions) {
        UmicomKernelPartitionTable table;
        UmicomDiskClear(&table, sizeof(table));
        status = UmicomKernelDiskPartitionsInspect(&reader, &table);
        if (status == UMICOM_DISK_OK) {
            UmicomDiskText(context, output, "disk.partition-table=primary-mbr count=");
            UmicomDiskNumber(context, output, table.count); output(context, "\r\n", 2U);
            for (UmicomSize i = 0U; i < UMICOM_DISK_PRIMARY_PARTITIONS; ++i) {
                const UmicomKernelDiskPartition *part = &table.entries[i];
                if (!part->present) continue;
                UmicomDiskText(context, output, "partition="); UmicomDiskNumber(context, output, i);
                UmicomDiskText(context, output, " type-decimal="); UmicomDiskNumber(context, output, part->type);
                UmicomDiskText(context, output, " first-sector="); UmicomDiskNumber(context, output, part->firstSector);
                UmicomDiskText(context, output, " sectors="); UmicomDiskNumber(context, output, part->sectors);
                output(context, "\r\n", 2U);
            }
        }
    } else if (status == UMICOM_DISK_OK) {
        status = UmicomKernelFat16Open(&umicomDiskVolume, &reader, partition);
        if (status == UMICOM_DISK_OK && infoOnly) {
            UmicomDiskText(context, output, "disk.filesystem=FAT16 label=");
            UmicomDiskText(context, output, umicomDiskVolume.info.label);
            UmicomDiskText(context, output, " clusters="); UmicomDiskNumber(context, output, umicomDiskVolume.info.clusters);
            UmicomDiskText(context, output, " sectors-per-cluster=");
            UmicomDiskNumber(context, output, umicomDiskVolume.info.sectorsPerCluster);
            UmicomDiskText(context, output, "\r\nRead-only short-name inspection; not mounted into RAMFS.\r\n");
        }
        if (status == UMICOM_DISK_OK && list) {
            status = UmicomKernelFat16List(&umicomDiskVolume, path, &umicomDiskDirectory);
            if (status == UMICOM_DISK_OK) {
                for (UmicomSize i = 0U; i < umicomDiskDirectory.count; ++i) {
                    const UmicomKernelFat16Entry *entry = &umicomDiskDirectory.entries[i];
                    UmicomDiskText(context, output, entry->directory ? "dir  " : "file ");
                    UmicomDiskText(context, output, entry->name); UmicomDiskText(context, output, " bytes=");
                    UmicomDiskNumber(context, output, entry->bytes); output(context, "\r\n", 2U);
                }
                UmicomDiskText(context, output, "long-name-records-skipped=");
                UmicomDiskNumber(context, output, umicomDiskDirectory.longNameRecords); output(context, "\r\n", 2U);
            }
        }
        if (status == UMICOM_DISK_OK && cat) {
            UmicomKernelFat16Entry entry;
            UmicomDiskClear(&entry, sizeof(entry));
            status = UmicomKernelFat16Stat(&umicomDiskVolume, path, &entry);
            if (status == UMICOM_DISK_OK && entry.directory) status = UMICOM_DISK_IS_DIRECTORY;
            /* Refuse a large cat before publishing a prefix. The C Read API
             * supports offsets; this terminal command is deliberately bounded. */
            if (status == UMICOM_DISK_OK && entry.bytes > sizeof(umicomDiskContent)) status = UMICOM_DISK_LIMIT;
            UmicomSize bytes = 0U;
            if (status == UMICOM_DISK_OK)
                status = UmicomKernelFat16Read(&umicomDiskVolume, path, 0U,
                    umicomDiskContent, sizeof(umicomDiskContent), &bytes);
            if (status == UMICOM_DISK_OK) {
                UmicomDiskSafe(context, output, umicomDiskContent, bytes);
                if (bytes && umicomDiskContent[bytes - 1U] != 10U) output(context, "\r\n", 2U);
            }
        }
    }
    /* Even a failed Open may retain DMA memory. Drop the interpretation first,
     * then ask the original transport to close and report both outcomes. */
    if (umicomDiskVolume.open) (void)UmicomKernelFat16Close(&umicomDiskVolume);
    umicomDiskBusy = UMICOM_FALSE;
    const UmicomKernelBlockStatus closed = UmicomKernelDiskInspectionClose();
    for (UmicomSize i = 0U; i < sizeof(umicomDiskContent); ++i) umicomDiskContent[i] = 0U;
    UmicomDiskText(context, output, "disk.inspect=");
    UmicomDiskText(context, output, UmicomKernelDiskStatusName(status));
    UmicomDiskText(context, output, " block=");
    UmicomDiskText(context, output, UmicomKernelBlockStatusName(umicomDiskLastBlock));
    UmicomDiskText(context, output, " close="); UmicomDiskText(context, output, UmicomKernelBlockStatusName(closed));
    output(context, "\r\n", 2U);
    return closed == UMICOM_BLOCK_OK ? status : UMICOM_DISK_IO_ERROR;
}
UmicomKernelShellStatus UmicomKernelDiskInspectionCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count) return UMICOM_SHELL_OK;
    const char *name = command->bytes + command->offsets[0];
    const UmicomBoolean parts = UmicomDiskEqual(name, "partitions");
    const UmicomBoolean info = UmicomDiskEqual(name, "fatinfo");
    const UmicomBoolean list = UmicomDiskEqual(name, "fatls"), cat = UmicomDiskEqual(name, "fatcat");
    const UmicomBoolean close = UmicomDiskEqual(name, "diskclose");
    if (!parts && !info && !list && !cat && !close) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    if (close) {
        if (command->count != 1U) return UMICOM_SHELL_INVALID_ARGUMENT;
        const UmicomKernelBlockStatus status = UmicomKernelDiskInspectionClose();
        UmicomDiskText(shell->outputContext, shell->output, "disk.close=");
        UmicomDiskText(shell->outputContext, shell->output, UmicomKernelBlockStatusName(status));
        shell->output(shell->outputContext, "\r\n", 2U);
        return status == UMICOM_BLOCK_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_CLEANUP_FAILED;
    }
    const UmicomSize expected = parts ? 2U : (info ? 3U : 4U);
    UmicomU64 slot = 0U, partition = 0U;
    if (command->count != expected || !UmicomKernelShellUnsigned(command->bytes + command->offsets[1], &slot) ||
        (!parts && !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition)) ||
        slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS)
        return UMICOM_SHELL_INVALID_ARGUMENT;
    const char *path = (list || cat) ? command->bytes + command->offsets[3] : "/";
    const UmicomKernelDiskStatus status = UmicomKernelDiskInspect(slot, partition, name, path,
        shell->outputContext, shell->output);
    return status == UMICOM_DISK_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}
