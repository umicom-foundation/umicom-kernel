/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console_file_range.inc
 *
 * PURPOSE:
 *   Inspect bounded RAM file ranges through existing VFS rights and descriptor ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

/* The console owns presentation only. VFS owns lookup, rights, position and
 * data lifetime. Reuse the escaping and close/retry path used by cat; a ranged
 * view must not introduce a second filesystem reader or leak a descriptor. */
/* The text-only reader is retained for review. The shared reader below
 * preserves its behavior while adding a terminal-safe binary presentation. */
#if 0
static UmicomKernelShellStatus UmicomShellReadRange(
    UmicomKernelConsoleShell *shell, const char *path, UmicomSize offset, UmicomSize bytes)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsOpen(&shell->client, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &shell->descriptor);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    UmicomKernelVfsNodeInfo info;
    UmicomShellClear(&info, sizeof(info));
    status = UmicomKernelVfsQuery(&shell->client, shell->descriptor, &info);
    if (status == UMICOM_VFS_OK && info.kind != UMICOM_VFS_FILE) status = UMICOM_VFS_NOT_FILE;
    if (status == UMICOM_VFS_OK) status = UmicomKernelVfsSeek(&shell->client, shell->descriptor, offset);
    if (status != UMICOM_VFS_OK) return UmicomShellFileFinish(shell, status);
    UmicomSize total = 0U;
    while (total < bytes) {
        const UmicomSize remaining = bytes - total;
        const UmicomSize requested = remaining < sizeof(shell->io) ? remaining : sizeof(shell->io);
        UmicomSize got = 0U;
        status = UmicomKernelVfsRead(&shell->client, shell->descriptor, shell->io, requested, &got);
        if (got > requested) { status = UMICOM_VFS_CORRUPT_STATE; break; }
        /* A failed backend can still return a real prefix. Display and count
         * that prefix, then report the failure; never silently retry a read. */
        UmicomShellSafeBytes(shell, shell->io, got);
        total += got;
        if (status != UMICOM_VFS_OK || got == 0U) break;
    }
    UmicomShellText(shell, "\r\nfile.read offset="); UmicomShellNumber(shell, offset);
    UmicomShellText(shell, " requested="); UmicomShellNumber(shell, bytes);
    UmicomShellText(shell, " read="); UmicomShellNumber(shell, total);
    UmicomShellText(shell, " next="); UmicomShellNumber(shell, offset + total);
    UmicomShellText(shell, " eof=");
    UmicomShellText(shell, offset + total >= info.bytes ? "yes\r\n" : "no\r\n");
    return UmicomShellFileFinish(shell, status);
}
#endif

/* A byte view is presentation, not another file service. Keep complete rows
 * across short backend reads. Every output byte is printable ASCII or CR/LF;
 * file contents cannot emit terminal control sequences. */
static void UmicomShellHexRow(UmicomKernelConsoleShell *shell, UmicomSize offset,
    const UmicomU8 *bytes, UmicomSize count)
{
    static const char digits[] = "0123456789abcdef";
    char line[80];
    UmicomSize used = 0U;
    for (unsigned i = 0U; i < 8U; ++i) {
        const unsigned shift = (7U - i) * 4U;
        line[used++] = digits[(offset >> shift) & 15U];
    }
    line[used++] = ' '; line[used++] = ' ';
    for (UmicomSize i = 0U; i < 16U; ++i) {
        line[used++] = i < count ? digits[bytes[i] >> 4U] : ' ';
        line[used++] = i < count ? digits[bytes[i] & 15U] : ' ';
        line[used++] = ' ';
    }
    line[used++] = '|';
    for (UmicomSize i = 0U; i < count; ++i)
        line[used++] = bytes[i] >= 32U && bytes[i] <= 126U ? (char)bytes[i] : '.';
    line[used++] = '|'; line[used++] = '\r'; line[used++] = '\n';
    shell->output(shell->outputContext, line, used);
}
static UmicomKernelShellStatus UmicomShellInspectRange(
    UmicomKernelConsoleShell *shell, const char *path, UmicomSize offset, UmicomSize bytes,
    UmicomBoolean hexadecimal)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsOpen(&shell->client, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &shell->descriptor);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    UmicomKernelVfsNodeInfo info;
    UmicomShellClear(&info, sizeof(info));
    status = UmicomKernelVfsQuery(&shell->client, shell->descriptor, &info);
    if (status == UMICOM_VFS_OK && info.kind != UMICOM_VFS_FILE) status = UMICOM_VFS_NOT_FILE;
    if (status == UMICOM_VFS_OK) status = UmicomKernelVfsSeek(&shell->client, shell->descriptor, offset);
    if (status != UMICOM_VFS_OK) return UmicomShellFileFinish(shell, status);
    UmicomSize total = 0U, rowCount = 0U, rowOffset = offset;
    UmicomU8 row[16];
    while (total < bytes) {
        const UmicomSize remaining = bytes - total;
        const UmicomSize requested = remaining < sizeof(shell->io) ? remaining : sizeof(shell->io);
        UmicomSize got = 0U;
        status = UmicomKernelVfsRead(&shell->client, shell->descriptor, shell->io, requested, &got);
        if (got > requested) { status = UMICOM_VFS_CORRUPT_STATE; break; }
        /* A failed backend can still return a real prefix. Display and count
         * that prefix, then report the failure; never silently retry a read. */
        if (hexadecimal) {
            for (UmicomSize i = 0U; i < got; ++i) {
                row[rowCount++] = shell->io[i];
                if (rowCount == sizeof(row)) {
                    UmicomShellHexRow(shell, rowOffset, row, rowCount);
                    rowOffset += rowCount;
                    rowCount = 0U;
                }
            }
        } else UmicomShellSafeBytes(shell, shell->io, got);
        total += got;
        if (status != UMICOM_VFS_OK || got == 0U) break;
    }
    if (hexadecimal && rowCount != 0U) UmicomShellHexRow(shell, rowOffset, row, rowCount);
    UmicomShellText(shell, hexadecimal ? "file.hex offset=" : "\r\nfile.read offset=");
    UmicomShellNumber(shell, offset);
    UmicomShellText(shell, " requested="); UmicomShellNumber(shell, bytes);
    UmicomShellText(shell, " read="); UmicomShellNumber(shell, total);
    UmicomShellText(shell, " next="); UmicomShellNumber(shell, offset + total);
    UmicomShellText(shell, " eof=");
    UmicomShellText(shell, offset + total >= info.bytes ? "yes\r\n" : "no\r\n");
    return UmicomShellFileFinish(shell, status);
}

/* Keep the original text entry point and its output unchanged. Both views
 * share the same checked read owner, including failed-close retention. */
static UmicomKernelShellStatus UmicomShellReadRange(
    UmicomKernelConsoleShell *shell, const char *path, UmicomSize offset, UmicomSize bytes)
{
    return UmicomShellInspectRange(shell, path, offset, bytes, UMICOM_FALSE);
}
static UmicomKernelShellStatus UmicomShellHexRange(
    UmicomKernelConsoleShell *shell, const char *path, UmicomSize offset, UmicomSize bytes)
{
    return UmicomShellInspectRange(shell, path, offset, bytes, UMICOM_TRUE);
}
