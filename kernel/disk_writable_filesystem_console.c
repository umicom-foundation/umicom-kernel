/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_writable_filesystem_console.c
 *
 * PURPOSE:
 *   Exercise the same writable VFS operations that native process file services
 *   use, with an explicit device, partition and calendar selected by the owner.
 *
 * EDUCATIONAL OVERVIEW:
 *   A successful non-empty write is one accepted commit, including FLUSH and
 *   readback. It does not need a later console commit command. The older staged
 *   FAT16 commands retain their separate explicit Stage/Finish contract.
 *   A console descriptor is retained until Close succeeds. Neither shutdown
 *   nor a retry may erase a failed mount's cleanup obligations or generations.
 *   This profile admits one writable mount attempt per boot, even on failure.
 *   Its root is
 *   a separate domain, while the established RAMFS and read-only disk commands
 *   keep their original owners. Commands are trusted, serial Kernel requests.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_writable_filesystem_console.h"

static UmicomKernelDiskWritableMount umicomKernelConsoleWritableMount;
static UmicomKernelVfsClient umicomKernelConsoleWritableClient;
static UmicomKernelConsoleShell *umicomKernelConsoleWritableShell;
static UmicomKernelFileDescriptor umicomKernelConsoleWritableDescriptor;
static UmicomBoolean umicomKernelConsoleWritableBusy;
static UmicomU8 umicomKernelConsoleWritableBytes[UMICOM_FAT16_READ_BYTES];
static UmicomKernelVfsDirectoryEntry umicomKernelConsoleWritableEntries[UMICOM_FAT16_ENTRY_LIMIT];

static void UmicomKernelWritableConsoleClear(void *storage, UmicomSize bytes)
{
    volatile UmicomU8 *const output = storage;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static UmicomBoolean UmicomKernelWritableConsoleEqual(const char *left, const char *right)
{
    for (UmicomSize i = 0U;; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (!left[i]) return UMICOM_TRUE;
    }
}
static void UmicomKernelWritableConsoleText(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    shell->output(shell->outputContext, text, bytes);
}
static void UmicomKernelWritableConsoleNumber(UmicomKernelConsoleShell *shell, UmicomU64 number)
{
    char digits[20]; UmicomSize bytes = 0U;
    do { digits[bytes++] = (char)('0' + number % 10U); number /= 10U; } while (number);
    while (bytes) shell->output(shell->outputContext, &digits[--bytes], 1U);
}
static void UmicomKernelWritableConsoleSafe(UmicomKernelConsoleShell *shell,
    const UmicomU8 *input, UmicomSize bytes)
{
    static const char hexadecimal[] = "0123456789abcdef";
    for (UmicomSize i = 0U; i < bytes; ++i) {
        const UmicomU8 byte = input[i];
        if (byte == 10U) UmicomKernelWritableConsoleText(shell, "\r\n");
        else if (byte >= 32U && byte <= 126U) {
            const char character = (char)byte;
            shell->output(shell->outputContext, &character, 1U);
        } else {
            const char escaped[] = {'\\', 'x', hexadecimal[byte >> 4U], hexadecimal[byte & 15U]};
            shell->output(shell->outputContext, escaped, sizeof(escaped));
        }
    }
}
static UmicomBoolean UmicomKernelWritableConsoleTime(const char *text,
    UmicomKernelFat16FileTime *time)
{
    UmicomSize length = 0U;
    while (length < 20U && text[length]) ++length;
    if (length != 19U || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') return UMICOM_FALSE;
    const UmicomSize starts[] = {0U, 5U, 8U, 11U, 14U, 17U};
    UmicomU16 values[6] = {0U};
    for (UmicomSize field = 0U; field < 6U; ++field) {
        const UmicomSize digits = field ? 2U : 4U;
        for (UmicomSize i = 0U; i < digits; ++i) {
            const char byte = text[starts[field] + i];
            if (byte < '0' || byte > '9') return UMICOM_FALSE;
            values[field] = (UmicomU16)(values[field] * 10U + (UmicomU16)(byte - '0'));
        }
    }
    const UmicomKernelFat16FileTime selected = {values[0], values[1], values[2],
        values[3], values[4], values[5]};
    UmicomKernelFat16FileTimeEncoding encoded;
    if (UmicomKernelFat16FileTimeEncode(&selected, &encoded) != UMICOM_DISK_OK)
        return UMICOM_FALSE;
    *time = selected;
    return UMICOM_TRUE;
}
static void UmicomKernelWritableConsoleReport(UmicomKernelConsoleShell *shell,
    const char *operation, UmicomKernelVfsStatus status)
{
    UmicomKernelWritableConsoleText(shell, operation);
    UmicomKernelWritableConsoleText(shell, "=");
    UmicomKernelWritableConsoleText(shell, UmicomKernelVfsStatusName(status));
    UmicomKernelWritableConsoleText(shell, " committed-operations=");
    UmicomKernelWritableConsoleNumber(shell, umicomKernelConsoleWritableMount.lifecycle.committedOperations);
    UmicomKernelWritableConsoleText(shell, "\r\n");
    if (status != UMICOM_VFS_OK) {
        const UmicomKernelFat16LifecycleCommitter *const lifecycle = &umicomKernelConsoleWritableMount.lifecycle;
        UmicomKernelWritableConsoleText(shell, "diskrw.media=");
        UmicomKernelWritableConsoleText(shell, UmicomKernelDiskStatusName(lifecycle->commit.updater.lastDiskStatus));
        UmicomKernelWritableConsoleText(shell, " block=");
        UmicomKernelWritableConsoleText(shell, UmicomKernelBlockStatusName(lifecycle->commit.updater.lastBlockStatus));
        UmicomKernelWritableConsoleText(shell, " cleanup=");
        UmicomKernelWritableConsoleText(shell, UmicomKernelBlockStatusName(lifecycle->commit.updater.lastCleanupStatus));
        UmicomKernelWritableConsoleText(shell, "\r\n");
    }
}
static UmicomKernelVfsStatus UmicomKernelWritableConsoleDescriptorClose(void)
{
    if (!umicomKernelConsoleWritableDescriptor) return UMICOM_VFS_OK;
    const UmicomKernelVfsStatus status = UmicomKernelVfsClose(&umicomKernelConsoleWritableClient,
        umicomKernelConsoleWritableDescriptor);
    if (status == UMICOM_VFS_OK) umicomKernelConsoleWritableDescriptor = 0U;
    return status;
}
static UmicomKernelVfsStatus UmicomKernelWritableConsoleClose(void)
{
    UmicomKernelVfsStatus status = UmicomKernelWritableConsoleDescriptorClose();
    if (status == UMICOM_VFS_OK && umicomKernelConsoleWritableClient.self &&
        umicomKernelConsoleWritableClient.state != UMICOM_VFS_CLOSED) {
        UmicomSize closed = 0U;
        status = UmicomKernelVfsClientClose(&umicomKernelConsoleWritableClient, &closed);
    }
    if (status == UMICOM_VFS_OK)
        status = UmicomKernelDiskWritableMountClose(&umicomKernelConsoleWritableMount);
    UmicomKernelWritableConsoleClear(umicomKernelConsoleWritableBytes, sizeof(umicomKernelConsoleWritableBytes));
    UmicomKernelWritableConsoleClear(umicomKernelConsoleWritableEntries, sizeof(umicomKernelConsoleWritableEntries));
    return status;
}
UmicomKernelVfsStatus UmicomKernelDiskWritableFilesystemConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!shell) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!umicomKernelConsoleWritableShell) return UMICOM_VFS_OK;
    if (umicomKernelConsoleWritableShell != shell) return UMICOM_VFS_ACCESS_DENIED;
    if (umicomKernelConsoleWritableBusy) return UMICOM_VFS_BUSY;
    umicomKernelConsoleWritableBusy = UMICOM_TRUE;
    const UmicomKernelVfsStatus status = UmicomKernelWritableConsoleClose();
    umicomKernelConsoleWritableBusy = UMICOM_FALSE;
    return status;
}
static UmicomKernelVfsStatus UmicomKernelWritableConsoleRead(UmicomKernelConsoleShell *shell,
    const char *path, UmicomBoolean list)
{
    UmicomKernelVfsStatus status = UmicomKernelWritableConsoleDescriptorClose();
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelVfsOpen(&umicomKernelConsoleWritableClient, path,
        list ? UMICOM_VFS_RIGHT_ENUMERATE : UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY,
        UMICOM_FALSE, &umicomKernelConsoleWritableDescriptor);
    if (status != UMICOM_VFS_OK) return status;
    UmicomSize count = 0U;
    if (list) {
        while (count < UMICOM_FAT16_ENTRY_LIMIT) {
            status = UmicomKernelVfsReadDirectory(&umicomKernelConsoleWritableClient,
                umicomKernelConsoleWritableDescriptor, &umicomKernelConsoleWritableEntries[count]);
            if (status != UMICOM_VFS_OK) break;
            ++count;
        }
        if (status == UMICOM_VFS_OK) {
            UmicomKernelVfsDirectoryEntry extra;
            status = UmicomKernelVfsReadDirectory(&umicomKernelConsoleWritableClient,
                umicomKernelConsoleWritableDescriptor, &extra);
            if (status == UMICOM_VFS_OK) status = UMICOM_VFS_INSPECTION_LIMIT;
        }
        if (status == UMICOM_VFS_END) status = UMICOM_VFS_OK;
    } else {
        UmicomKernelVfsNodeInfo info;
        status = UmicomKernelVfsQuery(&umicomKernelConsoleWritableClient,
            umicomKernelConsoleWritableDescriptor, &info);
        if (status == UMICOM_VFS_OK && info.bytes > sizeof(umicomKernelConsoleWritableBytes))
            status = UMICOM_VFS_INSPECTION_LIMIT;
        if (status == UMICOM_VFS_OK)
            status = UmicomKernelVfsRead(&umicomKernelConsoleWritableClient,
                umicomKernelConsoleWritableDescriptor, umicomKernelConsoleWritableBytes,
                sizeof(umicomKernelConsoleWritableBytes), &count);
        if (status == UMICOM_VFS_OK && count != info.bytes) status = UMICOM_VFS_CORRUPT_FILESYSTEM;
    }
    const UmicomKernelVfsStatus closed = UmicomKernelWritableConsoleDescriptorClose();
    if (status == UMICOM_VFS_OK) status = closed;
    if (status == UMICOM_VFS_OK && list) {
        for (UmicomSize i = 0U; i < count; ++i) {
            const UmicomKernelVfsDirectoryEntry *const entry = &umicomKernelConsoleWritableEntries[i];
            UmicomKernelWritableConsoleText(shell, entry->info.kind == UMICOM_VFS_DIRECTORY ? "dir  " : "file ");
            UmicomKernelWritableConsoleText(shell, entry->name);
            UmicomKernelWritableConsoleText(shell, " bytes=");
            UmicomKernelWritableConsoleNumber(shell, entry->info.bytes);
            UmicomKernelWritableConsoleText(shell, "\r\n");
        }
    } else if (status == UMICOM_VFS_OK) {
        UmicomKernelWritableConsoleSafe(shell, umicomKernelConsoleWritableBytes, count);
        if (count && umicomKernelConsoleWritableBytes[count - 1U] != 10U)
            UmicomKernelWritableConsoleText(shell, "\r\n");
    }
    UmicomKernelWritableConsoleClear(umicomKernelConsoleWritableBytes, sizeof(umicomKernelConsoleWritableBytes));
    UmicomKernelWritableConsoleClear(umicomKernelConsoleWritableEntries, sizeof(umicomKernelConsoleWritableEntries));
    return status;
}
static UmicomKernelVfsStatus UmicomKernelWritableConsoleWrite(const char *path,
    UmicomU64 offset, const char *text, UmicomBoolean append, UmicomBoolean resize)
{
    UmicomKernelVfsStatus status = UmicomKernelWritableConsoleDescriptorClose();
    if (status != UMICOM_VFS_OK) return status;
    status = UmicomKernelVfsOpen(&umicomKernelConsoleWritableClient, path, UMICOM_VFS_RIGHT_WRITE,
        append, &umicomKernelConsoleWritableDescriptor);
    if (status != UMICOM_VFS_OK) return status;
    if (resize) status = UmicomKernelVfsResize(&umicomKernelConsoleWritableClient,
        umicomKernelConsoleWritableDescriptor, (UmicomSize)offset);
    else {
        if (!append) status = UmicomKernelVfsSeek(&umicomKernelConsoleWritableClient,
            umicomKernelConsoleWritableDescriptor, (UmicomSize)offset);
        UmicomSize bytes = 0U, written = 0U;
        while (text[bytes]) ++bytes;
        if (status == UMICOM_VFS_OK) status = UmicomKernelVfsWrite(&umicomKernelConsoleWritableClient,
            umicomKernelConsoleWritableDescriptor, text, bytes, &written);
        if (status == UMICOM_VFS_OK && written != bytes) status = UMICOM_VFS_CORRUPT_STATE;
    }
    const UmicomKernelVfsStatus closed = UmicomKernelWritableConsoleDescriptorClose();
    return status == UMICOM_VFS_OK ? closed : status;
}

UmicomKernelShellStatus UmicomKernelDiskWritableFilesystemCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count) return UMICOM_SHELL_OK;
    const char *const name = command->bytes + command->offsets[0];
    const UmicomBoolean mount = UmicomKernelWritableConsoleEqual(name, "mountdiskrw");
    const UmicomBoolean unmount = UmicomKernelWritableConsoleEqual(name, "unmountdiskrw");
    const UmicomBoolean info = UmicomKernelWritableConsoleEqual(name, "diskrwinfo");
    const UmicomBoolean time = UmicomKernelWritableConsoleEqual(name, "diskrwtime");
    const UmicomBoolean list = UmicomKernelWritableConsoleEqual(name, "diskrwls");
    const UmicomBoolean read = UmicomKernelWritableConsoleEqual(name, "diskrwcat");
    const UmicomBoolean create = UmicomKernelWritableConsoleEqual(name, "diskcreate");
    const UmicomBoolean makeDirectory = UmicomKernelWritableConsoleEqual(name, "diskmkdir");
    const UmicomBoolean remove = UmicomKernelWritableConsoleEqual(name, "diskdelete");
    const UmicomBoolean removeDirectory = UmicomKernelWritableConsoleEqual(name, "diskrmdir");
    const UmicomBoolean write = UmicomKernelWritableConsoleEqual(name, "diskwrite");
    const UmicomBoolean append = UmicomKernelWritableConsoleEqual(name, "diskappend");
    const UmicomBoolean resize = UmicomKernelWritableConsoleEqual(name, "diskresize");
    if (!mount && !unmount && !info && !time && !list && !read && !create &&
        !makeDirectory && !remove && !removeDirectory && !write && !append && !resize)
        return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    const UmicomSize expected = mount || write ? 4U : append || resize ? 3U :
        unmount || info ? 1U : 2U;
    if (command->count != expected) return UMICOM_SHELL_INVALID_ARGUMENT;
    if (umicomKernelConsoleWritableBusy) return UMICOM_SHELL_BUSY;
    if (umicomKernelConsoleWritableShell && umicomKernelConsoleWritableShell != shell)
        return UMICOM_SHELL_BAD_STATE;
    const char *const path = expected > 1U ? command->bytes + command->offsets[1] : "";
    UmicomKernelFat16FileTime calendar = {0};
    UmicomU64 slot = 0U, partition = 0U, offset = 0U;
    if (mount && (!UmicomKernelShellUnsigned(path, &slot) || slot >= UMICOM_BLOCK_SLOT_LIMIT ||
        !UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &partition) ||
        partition >= UMICOM_DISK_PRIMARY_PARTITIONS ||
        !UmicomKernelWritableConsoleTime(command->bytes + command->offsets[3], &calendar)))
        return UMICOM_SHELL_INVALID_ARGUMENT;
    if (time && !UmicomKernelWritableConsoleTime(path, &calendar)) return UMICOM_SHELL_INVALID_ARGUMENT;
    if ((write || resize) && (!UmicomKernelShellUnsigned(command->bytes + command->offsets[2], &offset) ||
        offset > (UmicomU64)(~(UmicomSize)0U))) return UMICOM_SHELL_INVALID_ARGUMENT;
    umicomKernelConsoleWritableBusy = UMICOM_TRUE;
    UmicomKernelVfsStatus status = UMICOM_VFS_OK;
    if (mount) {
        if (umicomKernelConsoleWritableMount.state != UMICOM_VFS_UNUSED ||
            umicomKernelConsoleWritableMount.admitted) status = UMICOM_VFS_BAD_STATE;
        else {
            UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
            const UmicomKernelBlockStatus block = UmicomPlatformBlockDomainGet(&domain);
            if (block != UMICOM_BLOCK_OK) {
                UmicomKernelWritableConsoleText(shell, "diskrw.transport=");
                UmicomKernelWritableConsoleText(shell, UmicomKernelBlockStatusName(block));
                UmicomKernelWritableConsoleText(shell, "\r\n");
                status = UMICOM_VFS_IO_ERROR;
            } else {
                umicomKernelConsoleWritableShell = shell;
                status = UmicomKernelDiskWritableMountOpen(&umicomKernelConsoleWritableMount,
                    domain, (UmicomSize)slot, (UmicomSize)partition, 10000000U, &calendar);
                if (status == UMICOM_VFS_OK) {
                    status = UmicomKernelDiskWritableMountClientOpen(&umicomKernelConsoleWritableMount,
                        &umicomKernelConsoleWritableClient, 1U, UMICOM_VFS_RIGHT_ALL);
                    if (status != UMICOM_VFS_OK) (void)UmicomKernelWritableConsoleClose();
                }
            }
        }
        if (status == UMICOM_VFS_OK)
            UmicomKernelWritableConsoleText(shell, "Writable disk mounted. Each successful mutation is flushed and verified.\r\n");
        else if (umicomKernelConsoleWritableMount.state == UMICOM_VFS_CLOSED)
            UmicomKernelWritableConsoleText(shell, "This writable mount lifetime is closed. Restart for a new mount.\r\n");
    } else if (unmount) status = UmicomKernelWritableConsoleClose();
    else if (info) {
        UmicomKernelWritableConsoleText(shell, "diskrw.state=");
        UmicomKernelWritableConsoleText(shell, umicomKernelConsoleWritableMount.state == UMICOM_VFS_OPEN ? "mounted" :
            umicomKernelConsoleWritableMount.state == UMICOM_VFS_CLOSING ? "closing" :
            umicomKernelConsoleWritableMount.state == UMICOM_VFS_CLOSED ? "closed" : "unused");
        UmicomKernelWritableConsoleText(shell, " clients=");
        UmicomKernelWritableConsoleNumber(shell, umicomKernelConsoleWritableMount.vfs.clients);
        UmicomKernelWritableConsoleText(shell, " last-commit-accepted=");
        UmicomKernelWritableConsoleNumber(shell, umicomKernelConsoleWritableMount.lifecycle.lastResult.commit.commitAccepted);
        UmicomKernelWritableConsoleText(shell, "\r\n");
    } else if (umicomKernelConsoleWritableMount.state != UMICOM_VFS_OPEN) status = UMICOM_VFS_BAD_STATE;
    else if (time) status = UmicomKernelDiskWritableMountSetTime(&umicomKernelConsoleWritableMount, &calendar);
    else if (list || read) status = UmicomKernelWritableConsoleRead(shell, path, list);
    else if (create || makeDirectory) status = UmicomKernelVfsCreate(&umicomKernelConsoleWritableClient,
        path, makeDirectory ? UMICOM_VFS_DIRECTORY : UMICOM_VFS_FILE);
    else if (remove || removeDirectory) status = UmicomKernelVfsRemove(&umicomKernelConsoleWritableClient,
        path, removeDirectory ? UMICOM_VFS_DIRECTORY : UMICOM_VFS_FILE);
    else status = UmicomKernelWritableConsoleWrite(path, offset,
        write ? command->bytes + command->offsets[3] : append ? command->bytes + command->offsets[2] : "",
        append, resize);
    UmicomKernelWritableConsoleReport(shell, name, status);
    umicomKernelConsoleWritableBusy = UMICOM_FALSE;
    return status == UMICOM_VFS_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}
