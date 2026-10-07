/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_filesystem_console.c
 *
 * Keep a read-only disk domain mounted across trusted console commands. The
 * normal ls/cat/write namespace remains RAMFS. Disk commands use the existing
 * VFS descriptors; there is no second implementation of file positions or
 * directory iteration. A failed close keeps its original token for retry.
 *
 * The first console profile admits one successful mount lifetime per boot,
 * matching VFS's single-use stable owners. It does not erase a closed client
 * to make old descriptor generations look new. An unsuccessful admission can
 * be retried after all acquired transport ownership has been released.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_filesystem_console.h"

static UmicomKernelDiskMount umicomMountedDisk;
static UmicomKernelVfsClient umicomMountedDiskClient;
static UmicomKernelConsoleShell *umicomMountedDiskConsole;
static UmicomKernelFileDescriptor umicomMountedDiskDescriptor;
static UmicomBoolean umicomMountedDiskBusy;
static UmicomU8 umicomMountedDiskBytes[UMICOM_FAT16_READ_BYTES];
static UmicomKernelVfsDirectoryEntry umicomMountedDiskEntries[UMICOM_FAT16_ENTRY_LIMIT];

static void UmicomMountedClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static UmicomBoolean UmicomMountedEqual(const char *left, const char *right)
{
    for (UmicomSize i = 0U;; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (!left[i]) return UMICOM_TRUE;
    }
}
static void UmicomMountedText(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomSize count = 0U;
    while (text[count]) ++count;
    shell->output(shell->outputContext, text, count);
}
static void UmicomMountedNumber(UmicomKernelConsoleShell *shell, UmicomU64 number)
{
    char digits[20]; UmicomSize count = 0U;
    do { digits[count++] = (char)('0' + number % 10U); number /= 10U; } while (number);
    while (count) shell->output(shell->outputContext, &digits[--count], 1U);
}
static void UmicomMountedSafe(UmicomKernelConsoleShell *shell, const UmicomU8 *input,
    UmicomSize bytes)
{
    static const char hexadecimal[] = "0123456789abcdef";
    for (UmicomSize i = 0U; i < bytes; ++i) {
        const UmicomU8 byte = input[i];
        if (byte == 10U) UmicomMountedText(shell, "\r\n");
        else if (byte >= 32U && byte <= 126U) {
            const char character = (char)byte;
            shell->output(shell->outputContext, &character, 1U);
        } else {
            const char escaped[4] = {'\\', 'x', hexadecimal[byte >> 4U], hexadecimal[byte & 15U]};
            shell->output(shell->outputContext, escaped, sizeof(escaped));
        }
    }
}
static void UmicomMountedReport(UmicomKernelConsoleShell *shell, const char *operation,
    UmicomKernelVfsStatus status)
{
    UmicomMountedText(shell, operation); UmicomMountedText(shell, "=");
    UmicomMountedText(shell, UmicomKernelVfsStatusName(status));
    UmicomMountedText(shell, " block=");
    UmicomMountedText(shell, UmicomKernelBlockStatusName(umicomMountedDisk.lastBlockStatus));
    UmicomMountedText(shell, " cleanup=");
    UmicomMountedText(shell, UmicomKernelBlockStatusName(umicomMountedDisk.lastCleanupStatus));
    UmicomMountedText(shell, "\r\n");
    if (status != UMICOM_VFS_OK) {
        UmicomMountedText(shell, "disk.media=");
        UmicomMountedText(shell, UmicomKernelDiskStatusName(umicomMountedDisk.provider.lastDiskStatus));
        UmicomMountedText(shell, "\r\n");
    }
}
static UmicomKernelVfsStatus UmicomMountedDescriptorClose(void)
{
    if (!umicomMountedDiskDescriptor) return UMICOM_VFS_OK;
    const UmicomKernelVfsStatus status = UmicomKernelVfsClose(&umicomMountedDiskClient,
        umicomMountedDiskDescriptor);
    if (status == UMICOM_VFS_OK) umicomMountedDiskDescriptor = 0U;
    return status;
}
static UmicomKernelVfsStatus UmicomMountedClose(void)
{
    UmicomKernelVfsStatus status = UmicomMountedDescriptorClose();
    if (status == UMICOM_VFS_OK && umicomMountedDiskClient.self &&
        umicomMountedDiskClient.state != UMICOM_VFS_CLOSED) {
        UmicomSize closed = 0U;
        status = UmicomKernelVfsClientClose(&umicomMountedDiskClient, &closed);
    }
    if (status == UMICOM_VFS_OK) status = UmicomKernelDiskMountClose(&umicomMountedDisk);
    UmicomMountedClear(umicomMountedDiskBytes, sizeof(umicomMountedDiskBytes));
    UmicomMountedClear(umicomMountedDiskEntries, sizeof(umicomMountedDiskEntries));
    return status;
}
UmicomKernelVfsStatus UmicomKernelDiskFilesystemConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!shell) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!umicomMountedDiskConsole) return UMICOM_VFS_OK;
    if (umicomMountedDiskConsole != shell) return UMICOM_VFS_ACCESS_DENIED;
    if (umicomMountedDiskBusy) return UMICOM_VFS_BUSY;
    umicomMountedDiskBusy = UMICOM_TRUE;
    const UmicomKernelVfsStatus status = UmicomMountedClose();
    umicomMountedDiskBusy = UMICOM_FALSE;
    return status;
}
static UmicomKernelVfsStatus UmicomMountedRead(UmicomKernelConsoleShell *shell,
    const char *path, UmicomBoolean list)
{
    if (umicomMountedDisk.state != UMICOM_VFS_OPEN ||
        umicomMountedDiskClient.state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    UmicomKernelVfsStatus status = UmicomMountedDescriptorClose();
    if (status != UMICOM_VFS_OK) return status;
    const UmicomKernelVfsRights rights = list ? UMICOM_VFS_RIGHT_ENUMERATE :
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY;
    status = UmicomKernelVfsOpen(&umicomMountedDiskClient, path, rights, UMICOM_FALSE,
        &umicomMountedDiskDescriptor);
    if (status != UMICOM_VFS_OK) return status;
    UmicomSize count = 0U;
    if (list) {
        while (count < UMICOM_FAT16_ENTRY_LIMIT) {
            status = UmicomKernelVfsReadDirectory(&umicomMountedDiskClient,
                umicomMountedDiskDescriptor, &umicomMountedDiskEntries[count]);
            if (status != UMICOM_VFS_OK) break;
            ++count;
        }
        if (status == UMICOM_VFS_OK) {
            UmicomKernelVfsDirectoryEntry extra;
            UmicomMountedClear(&extra, sizeof(extra));
            status = UmicomKernelVfsReadDirectory(&umicomMountedDiskClient,
                umicomMountedDiskDescriptor, &extra);
            if (status == UMICOM_VFS_OK) status = UMICOM_VFS_INSPECTION_LIMIT;
        }
        if (status == UMICOM_VFS_END) status = UMICOM_VFS_OK;
    } else {
        UmicomKernelVfsNodeInfo info;
        UmicomMountedClear(&info, sizeof(info));
        status = UmicomKernelVfsQuery(&umicomMountedDiskClient, umicomMountedDiskDescriptor, &info);
        if (status == UMICOM_VFS_OK && info.bytes > sizeof(umicomMountedDiskBytes))
            status = UMICOM_VFS_INSPECTION_LIMIT;
        if (status == UMICOM_VFS_OK)
            status = UmicomKernelVfsRead(&umicomMountedDiskClient, umicomMountedDiskDescriptor,
                umicomMountedDiskBytes, sizeof(umicomMountedDiskBytes), &count);
        if (status == UMICOM_VFS_OK && count != info.bytes) status = UMICOM_VFS_CORRUPT_FILESYSTEM;
    }
    const UmicomKernelVfsStatus closed = UmicomMountedDescriptorClose();
    if (status == UMICOM_VFS_OK) status = closed;
    /* The terminal receives a complete bounded result only after successful
     * iteration/read and descriptor release, never an unlabelled prefix. */
    if (status == UMICOM_VFS_OK && list) {
        for (UmicomSize i = 0U; i < count; ++i) {
            const UmicomKernelVfsDirectoryEntry *const entry = &umicomMountedDiskEntries[i];
            UmicomMountedText(shell, entry->info.kind == UMICOM_VFS_DIRECTORY ? "dir  " : "file ");
            UmicomMountedText(shell, entry->name); UmicomMountedText(shell, " bytes=");
            UmicomMountedNumber(shell, entry->info.bytes); UmicomMountedText(shell, "\r\n");
        }
    } else if (status == UMICOM_VFS_OK) {
        UmicomMountedSafe(shell, umicomMountedDiskBytes, count);
        if (count && umicomMountedDiskBytes[count - 1U] != 10U) UmicomMountedText(shell, "\r\n");
    }
    UmicomMountedClear(umicomMountedDiskBytes, sizeof(umicomMountedDiskBytes));
    UmicomMountedClear(umicomMountedDiskEntries, sizeof(umicomMountedDiskEntries));
    return status;
}
UmicomKernelShellStatus UmicomKernelDiskFilesystemCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count) return UMICOM_SHELL_OK;
    const char *const name = command->bytes + command->offsets[0];
    const UmicomBoolean open = UmicomMountedEqual(name, "mountdisk");
    const UmicomBoolean close = UmicomMountedEqual(name, "unmountdisk");
    const UmicomBoolean info = UmicomMountedEqual(name, "mountinfo");
    const UmicomBoolean list = UmicomMountedEqual(name, "diskls");
    const UmicomBoolean cat = UmicomMountedEqual(name, "diskcat");
    if (!open && !close && !info && !list && !cat) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    const UmicomSize expected = open ? 3U : ((list || cat) ? 2U : 1U);
    if (command->count != expected) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomMountedDiskBusy) return UMICOM_SHELL_BUSY;
    if (umicomMountedDiskConsole && umicomMountedDiskConsole != shell) return UMICOM_SHELL_BAD_STATE;
    umicomMountedDiskBusy = UMICOM_TRUE;
    UmicomKernelVfsStatus status = UMICOM_VFS_OK;
    if (open) {
        UmicomU64 slot = 0U, partition = 0U;
        if (!UmicomKernelShellUnsigned(command->bytes + command->offsets[1], &slot) ||
            !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition) ||
            slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS) {
            umicomMountedDiskBusy = UMICOM_FALSE;
            return UMICOM_SHELL_INVALID_ARGUMENT;
        }
        if (umicomMountedDisk.state != UMICOM_VFS_UNUSED || umicomMountedDisk.admitted) {
            status = UMICOM_VFS_BAD_STATE;
        } else {
            UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
            const UmicomKernelBlockStatus block = UmicomPlatformBlockDomainGet(&domain);
            if (block != UMICOM_BLOCK_OK) {
                /* A platform refusal acquires no mount. Do not damage its
                 * zero-filled admission storage merely to retain a message. */
                UmicomMountedText(shell, "disk.transport=");
                UmicomMountedText(shell, UmicomKernelBlockStatusName(block));
                UmicomMountedText(shell, "\r\n");
                status = UMICOM_VFS_IO_ERROR;
            } else {
                /* Establish stable console ownership even if admission retains
                 * only a cleanup handle. Poweroff must be able to find it. */
                umicomMountedDiskConsole = shell;
                status = UmicomKernelDiskMountOpen(&umicomMountedDisk, domain, slot, partition, 10000000U);
                if (status == UMICOM_VFS_OK) {
                    status = UmicomKernelDiskMountClientOpen(&umicomMountedDisk,
                        &umicomMountedDiskClient, 1U, UMICOM_DISK_MOUNT_READ_RIGHTS);
                    if (status != UMICOM_VFS_OK) (void)UmicomMountedClose();
                }
            }
        }
        UmicomMountedReport(shell, "disk.mount", status);
        if (status == UMICOM_VFS_OK)
            UmicomMountedText(shell, "Read-only disk namespace mounted. Use diskls / and diskcat /README.TXT.\r\n");
        else if (umicomMountedDisk.state == UMICOM_VFS_CLOSED)
            UmicomMountedText(shell, "This mount lifetime is closed. Restart the system for a new disk mount.\r\n");
    } else if (close) {
        status = UmicomMountedClose();
        UmicomMountedReport(shell, "disk.unmount", status);
    } else if (info) {
        UmicomMountedText(shell, "disk.mount-state=");
        UmicomMountedText(shell, umicomMountedDisk.state == UMICOM_VFS_OPEN ? "mounted" :
            (umicomMountedDisk.state == UMICOM_VFS_CLOSING ? "closing" :
            (umicomMountedDisk.state == UMICOM_VFS_CLOSED ? "closed" : "unused")));
        UmicomMountedText(shell, " slot="); UmicomMountedNumber(shell, umicomMountedDisk.slot);
        UmicomMountedText(shell, " partition="); UmicomMountedNumber(shell, umicomMountedDisk.partition);
        UmicomMountedText(shell, " clients="); UmicomMountedNumber(shell, umicomMountedDisk.vfs.clients);
        UmicomMountedText(shell, "\r\n");
    } else {
        status = UmicomMountedRead(shell, command->bytes + command->offsets[1], list);
        UmicomMountedReport(shell, list ? "disk.list" : "disk.read", status);
    }
    umicomMountedDiskBusy = UMICOM_FALSE;
    return status == UMICOM_VFS_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}
