/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console_shell.c
 *
 * PURPOSE:
 *   Make the established file and process services usable from a small trusted
 *   console. All storage and executable ownership remains with those services.
 *
 * EDUCATIONAL OVERVIEW:
 *   The shell has at most one temporary descriptor and one foreground process.
 *   Neither is forgotten on an error. A failed close can be retried, and a
 *   process report is collected only after the original scheduler proves that
 *   execution stopped safely. This front end never frees another owner's pages.
 *
 *   No command is interpreted by a host shell. Paths go to VFS, byte buffers go
 *   through VFS I/O, and file snapshots go to the original executable loader.
 *   RAM contents are deliberately lost at poweroff; there is no disk driver here.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifdef UMICOM_KERNEL_READ_ONLY_BLOCK
/* Disk inspection is separate from the RAM filesystem and uses no raw addresses. */
#include "umicom/kernel/virtio_block.h"
#endif
#ifdef UMICOM_KERNEL_SERVICE_CONSOLE
/* Optional live-service domain; the original boot-job report is unchanged. */
#include "umicom/kernel/service_console.h"
#endif
#ifdef UMICOM_KERNEL_HARDWARE_CATALOGUE
/* Read a copied boot observation; commands never follow firmware addresses. */
#include "umicom/kernel/hardware_catalogue.h"
#endif
#include "umicom/kernel/console_shell.h"
#ifdef UMICOM_KERNEL_TERMINAL
#include "umicom/kernel/console_terminal.h"
#endif
#include "console_internal.h"
#ifdef UMICOM_KERNEL_STARTUP_SERVICES
#include "umicom/kernel/startup.h"
#endif

static void UmicomShellClear(void *target, UmicomSize bytes)
{
    /* Scrub staging bytes before they can be mistaken for a later executable. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static UmicomBoolean UmicomShellZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *in = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (in[i] != 0U) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomSize UmicomShellLength(const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes] != '\0') ++bytes; /* Only terminated Kernel strings enter here. */
    return bytes;
}
static UmicomBoolean UmicomShellEqual(const char *left, const char *right)
{
    for (UmicomSize i = 0U;; ++i) {
        if (left[i] != right[i]) return UMICOM_FALSE;
        if (left[i] == '\0') return UMICOM_TRUE;
    }
}
static void UmicomShellText(UmicomKernelConsoleShell *shell, const char *text)
{
    shell->output(shell->outputContext, text, UmicomShellLength(text));
}
static void UmicomShellNumber(UmicomKernelConsoleShell *shell, UmicomU64 value)
{
    char digits[20];
    UmicomSize used = 0U;
    do { digits[used++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
    while (used != 0U) shell->output(shell->outputContext, &digits[--used], 1U);
}
static void UmicomShellSafeBytes(UmicomKernelConsoleShell *shell, const UmicomU8 *bytes, UmicomSize count)
{
    static const char hex[] = "0123456789abcdef";
    for (UmicomSize i = 0U; i < count; ++i) {
        const UmicomU8 byte = bytes[i];
        if (byte == 10U) { UmicomShellText(shell, "\r\n"); continue; }
        if (byte >= 32U && byte <= 126U) {
            const char ch = (char)byte;
            shell->output(shell->outputContext, &ch, 1U);
        } else {
            /* A file can contain arbitrary bytes. Do not let an ESC, NUL or
             * other control byte issue commands to the developer's terminal. */
            const char escaped[4] = {'\\', 'x', hex[byte >> 4U], hex[byte & 15U]};
            shell->output(shell->outputContext, escaped, sizeof(escaped));
        }
    }
}
static UmicomKernelShellStatus UmicomShellFileError(UmicomKernelConsoleShell *shell, UmicomKernelVfsStatus status)
{
    shell->lastFileStatus = status;
    if (status == UMICOM_VFS_OK) return UMICOM_SHELL_OK;
    UmicomShellText(shell, "file error: ");
    UmicomShellText(shell, UmicomKernelVfsStatusName(status));
    UmicomShellText(shell, "\r\n");
    return UMICOM_SHELL_IO_ERROR;
}
static UmicomKernelShellStatus UmicomShellProcessError(UmicomKernelConsoleShell *shell,
    UmicomKernelSupervisionStatus status)
{
    shell->lastProcessStatus = status;
    UmicomShellText(shell, "process error: ");
    UmicomShellText(shell, UmicomKernelSupervisionStatusName(status));
    UmicomShellText(shell, "\r\n");
    /* A lower failure can mean an unverified machine return. Do not accept a
     * new command merely because the output device still happens to work. */
    if (shell->supervisor.poisoned || shell->supervisor.scheduler.poisoned) {
        shell->state = UMICOM_VFS_POISONED;
        return UMICOM_SHELL_UNSAFE;
    }
    return UMICOM_SHELL_PROCESS_ERROR;
}
static UmicomKernelVfsStatus UmicomShellDescriptorClose(UmicomKernelConsoleShell *shell)
{
    if (shell->descriptor == 0U) return UMICOM_VFS_OK;
    const UmicomKernelVfsStatus status = UmicomKernelVfsClose(&shell->client, shell->descriptor);
    if (status == UMICOM_VFS_OK) shell->descriptor = 0U;
    /* On failure the descriptor stays in this owner, not on a dead C stack. */
    return status;
}
static UmicomKernelShellStatus UmicomShellFileFinish(UmicomKernelConsoleShell *shell,
    UmicomKernelVfsStatus operation)
{
    const UmicomKernelVfsStatus closed = UmicomShellDescriptorClose(shell);
    if (operation != UMICOM_VFS_OK) (void)UmicomShellFileError(shell, operation);
    if (closed != UMICOM_VFS_OK) {
        (void)UmicomShellFileError(shell, closed);
        return UMICOM_SHELL_CLEANUP_FAILED;
    }
    return operation == UMICOM_VFS_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
}
static UmicomKernelVfsStatus UmicomShellInstall(UmicomKernelConsoleShell *shell,
    const char *path, const UmicomU8 *bytes, UmicomSize count)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsCreate(&shell->client, path, UMICOM_VFS_FILE);
    if (status == UMICOM_VFS_OK) status = UmicomKernelVfsOpen(&shell->client, path,
        UMICOM_VFS_RIGHT_WRITE, UMICOM_FALSE, &shell->descriptor);
    if (status == UMICOM_VFS_OK) {
        UmicomSize written = 0U;
        status = UmicomKernelVfsWrite(&shell->client, shell->descriptor, bytes, count, &written);
        if (status == UMICOM_VFS_OK && written != count) status = UMICOM_VFS_CORRUPT_STATE;
    }
    const UmicomKernelVfsStatus closed = UmicomShellDescriptorClose(shell);
    return closed != UMICOM_VFS_OK ? closed : status;
}
UmicomKernelShellStatus UmicomKernelConsoleShellInitialize(UmicomKernelConsoleShell *shell,
    UmicomKernelShellOutput output, void *context, const UmicomKernelShellImage *images, UmicomSize imageCount)
{
    if (!shell || !output || (imageCount != 0U && !images) || imageCount > 4U)
        return UMICOM_SHELL_INVALID_ARGUMENT;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_SHELL_UNSAFE;
    if (!UmicomShellZero(shell, sizeof(*shell))) return UMICOM_SHELL_BAD_STATE;
    /* Validate seed spans before publishing an owner. They are Kernel data,
     * not arbitrary strings or pointers received from the console. */
    for (UmicomSize i = 0U; i < imageCount; ++i) {
        if (!images[i].path || !images[i].bytes || images[i].count == 0U ||
            images[i].count > UMICOM_SHELL_IMAGE_BYTES) return UMICOM_SHELL_INVALID_ARGUMENT;
    }
    shell->self = shell;
    shell->state = UMICOM_VFS_CLOSING; /* Partial setup is closeable, never usable. */
    shell->output = output;
    shell->outputContext = context;
    UmicomKernelVfsStatus status = UmicomKernelRamfsInitialize(&shell->storage);
    if (status == UMICOM_VFS_OK)
        status = UmicomKernelVfsMount(&shell->vfs, UmicomKernelRamfsOperations(), &shell->storage);
    if (status == UMICOM_VFS_OK)
        status = UmicomKernelVfsClientOpen(&shell->client, &shell->vfs, ~(UmicomU64)1U, UMICOM_VFS_RIGHT_ALL);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    const UmicomKernelSupervisionStatus initialised = UmicomKernelProcessSupervisorInitialize(&shell->supervisor);
    if (initialised != UMICOM_SUPERVISION_OK) return UmicomShellProcessError(shell, initialised);
    /* This is admission-time composition, before any task exists. Subsequent
     * task execution and collection always use the public supervisor wrapper. */
    status = UmicomKernelUserFilesAttach(&shell->files, &shell->supervisor.scheduler, &shell->vfs);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    static const char *const directories[] = {"/bin", "/notes", "/records", "/shared"};
    for (UmicomSize i = 0U; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        status = UmicomKernelVfsCreate(&shell->client, directories[i], UMICOM_VFS_DIRECTORY);
        if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    }
    static const UmicomU8 readme[] =
        "Umicom Kernel development console\n"
        "Files are stored in RAM and disappear at poweroff.\n"
        "Use help for commands; paths are canonical absolute paths.\n"
        "run is read-only; runrw explicitly grants shared-filesystem writes.\n";
    static const UmicomU8 report[] = "Umicom report\n";
    status = UmicomShellInstall(shell, "/README", readme, sizeof(readme) - 1U);
    if (status == UMICOM_VFS_OK) status = UmicomShellInstall(shell, "/shared/report", report, sizeof(report) - 1U);
    for (UmicomSize i = 0U; status == UMICOM_VFS_OK && i < imageCount; ++i)
        status = UmicomShellInstall(shell, images[i].path, images[i].bytes, images[i].count);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    shell->state = UMICOM_VFS_OPEN;
    return UMICOM_SHELL_OK;
}
static UmicomKernelShellStatus UmicomShellInspect(UmicomKernelConsoleShell *shell,
    const char *path, UmicomBoolean listing)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsOpen(&shell->client, path,
        listing ? UMICOM_VFS_RIGHT_ENUMERATE : UMICOM_VFS_RIGHT_QUERY,
        UMICOM_FALSE, &shell->descriptor);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    if (!listing) {
        UmicomKernelVfsNodeInfo info;
        UmicomConsoleClear(&info, sizeof(info));
        status = UmicomKernelVfsQuery(&shell->client, shell->descriptor, &info);
        if (status == UMICOM_VFS_OK) {
            UmicomShellText(shell, info.kind == UMICOM_VFS_DIRECTORY ? "directory id=" : "file id=");
            UmicomShellNumber(shell, info.id); UmicomShellText(shell, " bytes=");
            UmicomShellNumber(shell, info.bytes); UmicomShellText(shell, "\r\n");
        }
    } else {
        UmicomSize entries = 0U;
        for (;;) {
            UmicomKernelVfsDirectoryEntry entry;
            UmicomConsoleClear(&entry, sizeof(entry));
            status = UmicomKernelVfsReadDirectory(&shell->client, shell->descriptor, &entry);
            if (status == UMICOM_VFS_END) { status = UMICOM_VFS_OK; break; }
            if (status != UMICOM_VFS_OK) break;
            if (++entries > UMICOM_RAMFS_NODE_LIMIT) { status = UMICOM_VFS_CORRUPT_STATE; break; }
            UmicomShellText(shell, entry.info.kind == UMICOM_VFS_DIRECTORY ? "dir   " : "file  ");
            UmicomShellText(shell, entry.name); UmicomShellText(shell, "  ");
            UmicomShellNumber(shell, entry.info.bytes); UmicomShellText(shell, "\r\n");
        }
    }
    return UmicomShellFileFinish(shell, status);
}
static UmicomKernelShellStatus UmicomShellCat(UmicomKernelConsoleShell *shell, const char *path)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsOpen(&shell->client, path,
        UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &shell->descriptor);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    UmicomSize total = 0U;
    while (total < UMICOM_SHELL_IMAGE_BYTES) {
        UmicomSize got = 0U;
        status = UmicomKernelVfsRead(&shell->client, shell->descriptor, shell->io, sizeof(shell->io), &got);
        if (got > sizeof(shell->io)) { status = UMICOM_VFS_CORRUPT_STATE; break; }
        UmicomShellSafeBytes(shell, shell->io, got);
        total += got;
        if (status != UMICOM_VFS_OK || got == 0U) break;
    }
    UmicomShellText(shell, "\r\n");
    return UmicomShellFileFinish(shell, status);
}
static UmicomKernelShellStatus UmicomShellWrite(UmicomKernelConsoleShell *shell,
    const char *path, const char *text, UmicomBoolean append)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsOpen(&shell->client, path,
        UMICOM_VFS_RIGHT_WRITE, append, &shell->descriptor);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    /* Overwrite is deliberately destructive, not an atomic replace. The old
     * file is truncated first; a later allocation failure reports its prefix. */
    if (!append) status = UmicomKernelVfsResize(&shell->client, shell->descriptor, 0U);
    UmicomSize written = 0U;
    if (status == UMICOM_VFS_OK) status = UmicomKernelVfsWrite(&shell->client, shell->descriptor,
        text, UmicomShellLength(text), &written);
    UmicomShellText(shell, "written="); UmicomShellNumber(shell, written); UmicomShellText(shell, "\r\n");
    return UmicomShellFileFinish(shell, status);
}
static UmicomKernelShellStatus UmicomShellRun(UmicomKernelConsoleShell *shell,
    const char *path, UmicomU64 argument, UmicomBoolean writable)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsOpen(&shell->client, path,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &shell->descriptor);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    UmicomKernelVfsNodeInfo info;
    UmicomConsoleClear(&info, sizeof(info));
    status = UmicomKernelVfsQuery(&shell->client, shell->descriptor, &info);
    if (status == UMICOM_VFS_OK && info.kind != UMICOM_VFS_FILE) status = UMICOM_VFS_NOT_FILE;
    if (status == UMICOM_VFS_OK && (info.bytes == 0U || info.bytes > sizeof(shell->image))) status = UMICOM_VFS_RANGE;
    UmicomSize copied = 0U;
    if (status == UMICOM_VFS_OK) {
        status = UmicomKernelVfsRead(&shell->client, shell->descriptor, shell->image, info.bytes, &copied);
        if (status == UMICOM_VFS_OK && copied != info.bytes) status = UMICOM_VFS_CORRUPT_STATE;
    }
    const UmicomKernelShellStatus readResult = UmicomShellFileFinish(shell, status);
    if (readResult != UMICOM_SHELL_OK) {
        UmicomShellClear(shell->image, sizeof(shell->image));
        return readResult;
    }
    /* Close the source descriptor before executing. Spawn copies ELF segments;
     * no live process borrows this staging buffer or the file's physical pages. */
    const UmicomKernelSupervisionStatus spawned = UmicomKernelProcessSupervisorSpawn(&shell->supervisor,
        UMICOM_SUPERVISION_GUARDIAN, shell->image, copied, argument, UMICOM_SHELL_SLICE_LIMIT,
        UMICOM_CHILDREN_CANCEL_TREE, &shell->foreground);
    UmicomShellClear(shell->image, sizeof(shell->image));
    if (spawned != UMICOM_SUPERVISION_OK) {
        UmicomSize reaped = 0U;
        const UmicomKernelSupervisionStatus cleanup = UmicomKernelProcessSupervisorReapRetained(&shell->supervisor, &reaped);
        return UmicomShellProcessError(shell, cleanup != UMICOM_SUPERVISION_OK ? cleanup : spawned);
    }
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
    if (shell->launchSpec && UmicomKernelProcessSupervisorSetLaunch(&shell->supervisor,
            UMICOM_SUPERVISION_GUARDIAN, shell->foreground, shell->launchSpec) != UMICOM_SUPERVISION_OK) {
        UmicomShellText(shell, "structured launch refused before execution\r\n");
        status = UMICOM_VFS_INVALID_ARGUMENT;
    }
#endif
    const UmicomKernelVfsRights rights = writable ? UMICOM_VFS_RIGHT_ALL :
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_ENUMERATE;
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
    /* A rejected launch uses the original grant-failure cancellation path. */
    if (status == UMICOM_VFS_OK)
#endif
    status = UmicomKernelUserFilesGrant(&shell->files, shell->foreground, rights);
#ifdef UMICOM_KERNEL_TERMINAL
    /* Admission grants streams separately from filesystem write authority. If
     * it fails, the existing cancellation/collection branch below owns rollback. */
    if (status == UMICOM_VFS_OK && shell->terminal && !UmicomKernelConsoleTerminalGrant(shell))
        status = UMICOM_VFS_BAD_STATE;
#endif
    if (status != UMICOM_VFS_OK) {
        /* Keep the task token even if cancellation or collection must be retried.
         * A failed grant never admits the user to run with accidental authority. */
        const UmicomKernelSupervisionStatus cancelled = UmicomKernelProcessSupervisorCancel(&shell->supervisor,
            UMICOM_SUPERVISION_GUARDIAN, shell->foreground);
        if (cancelled != UMICOM_SUPERVISION_OK) return UmicomShellProcessError(shell, cancelled);
        UmicomKernelProcessCompletion completion;
        UmicomConsoleClear(&completion, sizeof(completion));
        if (UmicomKernelProcessSupervisorCollect(&shell->supervisor, UMICOM_SUPERVISION_GUARDIAN,
                shell->foreground, &completion) == UMICOM_SUPERVISION_OK) shell->foreground = 0U;
        return UmicomShellFileError(shell, status);
    }
    UmicomShellText(shell, writable ? "running with shared-filesystem write rights\r\n" : "running with read-only file rights\r\n");
#ifdef UMICOM_KERNEL_TERMINAL
    if (shell->terminal) {
        /* The original discard-only behaviour below remains the unattached path. */
        UmicomKernelConsoleTerminalAnnounce(shell);
        return UMICOM_SHELL_OK;
    }
#endif
    UmicomShellText(shell, "Ctrl-C cancels the foreground program. Other input is discarded.\r\n");
    return UMICOM_SHELL_OK;
}
static UmicomKernelShellStatus UmicomShellStatus(UmicomKernelConsoleShell *shell)
{
    UmicomKernelRamfsInfo info;
    UmicomConsoleClear(&info, sizeof(info));
    const UmicomKernelVfsStatus status = UmicomKernelRamfsSnapshot(&shell->storage, &info);
    if (status != UMICOM_VFS_OK) return UmicomShellFileError(shell, status);
    {
        UmicomShellText(shell, "RAM-only filesystem: nodes="); UmicomShellNumber(shell, info.nodes);
        UmicomShellText(shell, " data-pages="); UmicomShellNumber(shell, info.dataPages);
        UmicomShellText(shell, " metadata-pages="); UmicomShellNumber(shell, info.metadataPages);
        UmicomShellText(shell, "\r\n");
    }
    if (shell->hasCompletion) {
        UmicomShellText(shell, "last process: id="); UmicomShellNumber(shell, shell->lastCompletion.identity);
        UmicomShellText(shell, " state="); UmicomShellText(shell, UmicomKernelUserTaskStateName(shell->lastCompletion.state));
        UmicomShellText(shell, " exit="); UmicomShellNumber(shell, shell->lastCompletion.exitValue);
        UmicomShellText(shell, "\r\n");
    }
    return UMICOM_SHELL_OK;
}
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
static UmicomKernelShellStatus UmicomShellStructured(UmicomKernelConsoleShell *shell,
    const char *line, UmicomSize bytes, UmicomBoolean *handled)
{
    char words[UMICOM_SHELL_LINE_BYTES];
    UmicomSize offsets[UMICOM_SHELL_TOKEN_LIMIT];
    UmicomSize count = 0U;
    UmicomConsoleClear(words, sizeof(words));
    UmicomConsoleClear(offsets, sizeof(offsets));
    *handled = UMICOM_FALSE;
    const UmicomKernelShellStatus parsed = UmicomKernelShellTokenize(line, bytes,
        words, offsets, UMICOM_SHELL_TOKEN_LIMIT, &count);
    /* Invalid or ordinary lines still go through the established command path.
     * In particular, adding exec does not widen the old four-token grammar. */
    if (parsed != UMICOM_SHELL_OK || count == 0U) return UMICOM_SHELL_OK;
    const UmicomBoolean writable = UmicomShellEqual(words, "execrw");
    if (!writable && !UmicomShellEqual(words, "exec")) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    if (count < 2U) return UMICOM_SHELL_SYNTAX;
    if (shell->commands != ~(UmicomU64)0U) ++shell->commands;
    const UmicomKernelVfsStatus closed = UmicomShellDescriptorClose(shell);
    if (closed != UMICOM_VFS_OK) return UmicomShellFileError(shell, closed);
    UmicomKernelLaunchString arguments[UMICOM_LAUNCH_ARGUMENT_LIMIT];
    UmicomConsoleClear(arguments, sizeof(arguments));
    for (UmicomSize i = 1U; i < count; ++i) {
        arguments[i - 1U].data = words + offsets[i];
        arguments[i - 1U].bytes = UmicomShellLength(arguments[i - 1U].data);
    }
    /* These explicit values are descriptive data, not authority and not host
     * environment inheritance. No working-directory or PATH search is implied. */
    const UmicomKernelLaunchString environment[] = {{"LANG=C", 6U}, {"UMICOM_CONSOLE=serial", 21U}};
    const UmicomKernelProgramLaunchSpec spec = {arguments, count - 1U, environment, 2U};
    shell->launchSpec = &spec;
    const UmicomKernelShellStatus result = UmicomShellRun(shell, arguments[0].data, 0U, writable);
    shell->launchSpec = (const UmicomKernelProgramLaunchSpec *)0;
    /* No execution step occurs inside Run. All admitted strings are now owned
     * by user pages, and this short-lived parser storage may safely disappear. */
    UmicomConsoleClear(words, sizeof(words));
    return result;
}
#endif

UmicomKernelShellStatus UmicomKernelConsoleShellExecute(UmicomKernelConsoleShell *shell,
    const char *line, UmicomSize bytes)
{
    if (!shell || shell->self != shell || !shell->output) return UMICOM_SHELL_BAD_STATE;
    if (shell->busy || shell->foreground != 0U) return UMICOM_SHELL_BUSY;
    if (shell->state == UMICOM_VFS_CLOSING) {
        /* After a partial shutdown, only the exact retry command is admitted.
         * No namespace operation may re-open an owner which is closing. */
        static const char retry[] = "poweroff";
        UmicomBoolean matches = line && bytes == sizeof(retry) - 1U ? UMICOM_TRUE : UMICOM_FALSE;
        for (UmicomSize i = 0U; matches && i < bytes; ++i)
            if (line[i] != retry[i]) matches = UMICOM_FALSE;
        if (!matches) return UMICOM_SHELL_BAD_STATE;
        const UmicomKernelShellStatus retried = UmicomKernelConsoleShellClose(shell);
        if (retried == UMICOM_SHELL_OK) shell->exitRequested = UMICOM_TRUE;
        return retried;
    }
    if (shell->state != UMICOM_VFS_OPEN) return UMICOM_SHELL_BAD_STATE;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_SHELL_UNSAFE;
    shell->busy = UMICOM_TRUE;
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
    UmicomBoolean handled = UMICOM_FALSE;
    const UmicomKernelShellStatus structured = UmicomShellStructured(shell, line, bytes, &handled);
    if (handled) { shell->busy = UMICOM_FALSE; return structured; }
#endif
    UmicomKernelShellCommand command;
    UmicomConsoleClear(&command, sizeof(command));
    UmicomKernelShellStatus result = UmicomKernelShellParse(line, bytes, &command);
    if (result != UMICOM_SHELL_OK) {
        UmicomShellText(shell, "syntax error: use complete tokens and matching quotes\r\n");
        shell->busy = UMICOM_FALSE; return result;
    }
    if (command.count == 0U) { shell->busy = UMICOM_FALSE; return UMICOM_SHELL_OK; }
    if (shell->commands != ~(UmicomU64)0U) ++shell->commands;
    const char *const name = command.bytes + command.offsets[0];
    const char *const path = command.count > 1U ? command.bytes + command.offsets[1] : "/";
    const char *const text = command.count > 2U ? command.bytes + command.offsets[2] : "";
    /* A descriptor whose close was refused must be resolved before opening any
     * new command-local descriptor. Its token is never silently overwritten. */
    const UmicomKernelVfsStatus closed = UmicomShellDescriptorClose(shell);
    if (closed != UMICOM_VFS_OK) { shell->busy = UMICOM_FALSE; return UmicomShellFileError(shell, closed); }
#ifdef UMICOM_KERNEL_STARTUP_SERVICES
    /* Read the closed boot report; this does not restart or mutate a service. */
    if (UmicomShellEqual(name, "services") && command.count == 1U) {
        UmicomKernelNormalBootReport(shell->outputContext, shell->output);
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_OK;
    }
#endif
#ifdef UMICOM_KERNEL_SERVICE_CONSOLE
    UmicomBoolean serviceHandled = UMICOM_FALSE;
    const UmicomKernelShellStatus serviceResult = UmicomKernelServiceConsoleCommand(shell, &command, &serviceHandled);
    if (serviceHandled) { shell->busy = UMICOM_FALSE; return serviceResult; }
#endif
#ifdef UMICOM_KERNEL_HARDWARE_CATALOGUE
    if (UmicomShellEqual(name, "hardware") && command.count == 1U) {
        UmicomKernelHardwareReport(shell->outputContext, shell->output);
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_OK;
    }
#endif
#ifdef UMICOM_KERNEL_READ_ONLY_BLOCK
    if (UmicomShellEqual(name, "disks") && command.count == 1U) {
        UmicomKernelBlockReport(shell->outputContext, shell->output);
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_OK;
    }
    if ((UmicomShellEqual(name, "readsector") && command.count == 3U) ||
        (UmicomShellEqual(name, "blockclose") && command.count == 1U)) {
        UmicomU64 slot = 0U, sector = 0U;
        UmicomKernelBlockStatus block = UMICOM_BLOCK_INVALID_ARGUMENT;
        if (command.count == 1U) block = UmicomKernelBlockRetryClose();
        else if (UmicomKernelShellUnsigned(path, &slot) && UmicomKernelShellUnsigned(text, &sector))
            block = UmicomKernelBlockInspectSector(slot, sector, shell->outputContext, shell->output);
        UmicomShellText(shell, "block.status=");
        UmicomShellText(shell, UmicomKernelBlockStatusName(block));
        UmicomShellText(shell, "\r\n");
        shell->busy = UMICOM_FALSE;
        return block == UMICOM_BLOCK_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_IO_ERROR;
    }
#endif
    UmicomBoolean poweroff = UMICOM_FALSE;
    if (UmicomShellEqual(name, "help") && command.count == 1U) {
#ifdef UMICOM_KERNEL_READ_ONLY_BLOCK
        UmicomShellText(shell, "disks | readsector SLOT LBA | blockclose (read-only block inspection)\r\n");
#endif
#ifdef UMICOM_KERNEL_HARDWARE_CATALOGUE
        UmicomShellText(shell, "hardware (copied firmware inventory; no device probing)\r\n");
#endif
#ifdef UMICOM_KERNEL_SERVICE_CONSOLE
        UmicomShellText(shell, "daemons start [MODE] | daemons status | daemons restart INDEX | daemons stop\r\n");
#endif
#ifdef UMICOM_KERNEL_STARTUP_SERVICES
        UmicomShellText(shell, "services (normal-startup result snapshots)\r\n");
#endif
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
        UmicomShellText(shell, "exec PATH [ARG ...] | execrw PATH [ARG ...] (structured argv)\r\n");
#endif
        UmicomShellText(shell,
            "help | about | status | mem | pwd\r\n"
            "ls [PATH] | stat PATH | cat PATH\r\n"
            "mkdir PATH | create PATH | rm PATH | rmdir PATH | reap\r\n"
            "write PATH \"TEXT\" | append PATH \"TEXT\"\r\n"
            "run PATH [UNSIGNED] | runrw PATH [UNSIGNED]\r\n"
            "poweroff\r\n"
            "Paths are absolute. Quotes preserve spaces; no expansions or escapes.\r\n"
            "write truncates an existing file. runrw grants all rights on this RAMFS.\r\n");
    } else if (UmicomShellEqual(name, "about") && command.count == 1U) {
        UmicomShellText(shell, "Umicom Kernel - trusted development console\r\n"
            "RAM-backed files, foreground native programs; no login or persistent disk.\r\n");
    } else if (UmicomShellEqual(name, "pwd") && command.count == 1U) {
        UmicomShellText(shell, "/\r\n"); /* There is no mutable working directory. */
    } else if (UmicomShellEqual(name, "status") && command.count == 1U) {
        result = UmicomShellStatus(shell);
    } else if (UmicomShellEqual(name, "mem") && command.count == 1U) {
        UmicomKernelPhysicalMemorySnapshot memory;
        UmicomConsoleClear(&memory, sizeof(memory));
        if (UmicomKernelPhysicalMemorySnapshotRead(&memory) != UMICOM_KERNEL_MEMORY_OK) result = UMICOM_SHELL_BAD_STATE;
        else {
            UmicomShellText(shell, "frames: total="); UmicomShellNumber(shell, memory.totalFrames);
            UmicomShellText(shell, " reserved="); UmicomShellNumber(shell, memory.reservedFrames);
            UmicomShellText(shell, " allocated="); UmicomShellNumber(shell, memory.allocatedFrames);
            UmicomShellText(shell, " free="); UmicomShellNumber(shell, memory.freeFrames); UmicomShellText(shell, "\r\n");
        }
    } else if (UmicomShellEqual(name, "ls") && command.count <= 2U) {
        result = UmicomShellInspect(shell, path, UMICOM_TRUE);
    } else if (UmicomShellEqual(name, "stat") && command.count == 2U) {
        result = UmicomShellInspect(shell, path, UMICOM_FALSE);
    } else if (UmicomShellEqual(name, "cat") && command.count == 2U) {
        result = UmicomShellCat(shell, path);
    } else if ((UmicomShellEqual(name, "write") || UmicomShellEqual(name, "append")) && command.count == 3U) {
        result = UmicomShellWrite(shell, path, text, UmicomShellEqual(name, "append"));
    } else if ((UmicomShellEqual(name, "mkdir") || UmicomShellEqual(name, "create")) && command.count == 2U) {
        result = UmicomShellFileError(shell, UmicomKernelVfsCreate(&shell->client, path,
            UmicomShellEqual(name, "mkdir") ? UMICOM_VFS_DIRECTORY : UMICOM_VFS_FILE));
    } else if ((UmicomShellEqual(name, "rm") || UmicomShellEqual(name, "rmdir")) && command.count == 2U) {
        const UmicomKernelVfsStatus removed = UmicomKernelVfsRemove(&shell->client, path,
            UmicomShellEqual(name, "rm") ? UMICOM_VFS_FILE : UMICOM_VFS_DIRECTORY);
        UmicomSize reaped = 0U;
        result = UmicomShellFileError(shell, removed == UMICOM_VFS_OK ?
            UmicomKernelRamfsReap(&shell->storage, &reaped) : removed);
    } else if (UmicomShellEqual(name, "reap") && command.count == 1U) {
        UmicomSize reaped = 0U;
        result = UmicomShellFileError(shell, UmicomKernelRamfsReap(&shell->storage, &reaped));
        UmicomShellText(shell, "reclaimed nodes="); UmicomShellNumber(shell, reaped); UmicomShellText(shell, "\r\n");
    } else if ((UmicomShellEqual(name, "run") || UmicomShellEqual(name, "runrw")) &&
        (command.count == 2U || command.count == 3U)) {
        UmicomU64 argument = 0U;
        if (command.count == 3U && !UmicomKernelShellUnsigned(text, &argument)) result = UMICOM_SHELL_RANGE;
        else result = UmicomShellRun(shell, path, argument, UmicomShellEqual(name, "runrw"));
    } else if (UmicomShellEqual(name, "poweroff") && command.count == 1U) {
        poweroff = UMICOM_TRUE; /* Close below after releasing the command reentry guard. */
    } else {
        UmicomShellText(shell, "unknown command or incorrect arguments; use help\r\n");
        result = UMICOM_SHELL_UNKNOWN_COMMAND;
    }
    if (result == UMICOM_SHELL_RANGE) UmicomShellText(shell, "argument outside supported range\r\n");
    shell->busy = UMICOM_FALSE;
    if (poweroff) {
        result = UmicomKernelConsoleShellClose(shell);
        if (result == UMICOM_SHELL_OK) {
            shell->exitRequested = UMICOM_TRUE;
            UmicomShellText(shell, "RAM filesystem closed. Powering off.\r\n");
        }
    }
    return result;
}
UmicomKernelShellStatus UmicomKernelConsoleShellStep(UmicomKernelConsoleShell *shell)
{
    if (!shell || shell->self != shell || shell->state != UMICOM_VFS_OPEN) return UMICOM_SHELL_BAD_STATE;
    if (shell->busy) return UMICOM_SHELL_BUSY;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_SHELL_UNSAFE;
    if (shell->foreground == 0U) return UMICOM_SHELL_OK;
    shell->busy = UMICOM_TRUE;
    UmicomKernelSupervisedProcessInfo info;
    UmicomConsoleClear(&info, sizeof(info));
    UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorQuery(&shell->supervisor,
        UMICOM_SUPERVISION_GUARDIAN, shell->foreground, &info);
    if (status == UMICOM_SUPERVISION_OK && !info.terminal) {
        UmicomKernelSupervisedProcessHandle selected = 0U;
        status = UmicomKernelProcessSupervisorRunOne(&shell->supervisor, UMICOM_SHELL_QUANTUM_TICKS, &selected);
        if (status == UMICOM_SUPERVISION_OK || status == UMICOM_SUPERVISION_IDLE)
            status = UmicomKernelProcessSupervisorQuery(&shell->supervisor,
                UMICOM_SUPERVISION_GUARDIAN, shell->foreground, &info);
    }
    if (status != UMICOM_SUPERVISION_OK) {
        shell->busy = UMICOM_FALSE;
        return UmicomShellProcessError(shell, status);
    }
    if (info.unsafe) {
        shell->state = UMICOM_VFS_POISONED;
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_UNSAFE;
    }
#ifdef UMICOM_KERNEL_TERMINAL
    /* Drain before collection: accepted stdout/stderr outlives user execution. */
    if (UmicomKernelConsoleTerminalDrain(shell) != UMICOM_SHELL_OK) {
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_UNSAFE;
    }
#endif
    if (!info.terminal) { shell->busy = UMICOM_FALSE; return UMICOM_SHELL_OK; }
    UmicomKernelProcessCompletion completion;
    UmicomConsoleClear(&completion, sizeof(completion));
    status = UmicomKernelProcessSupervisorCollect(&shell->supervisor, UMICOM_SUPERVISION_GUARDIAN,
        shell->foreground, &completion);
    if (status != UMICOM_SUPERVISION_OK) {
        shell->busy = UMICOM_FALSE;
        return UmicomShellProcessError(shell, status);
    }
#ifdef UMICOM_KERNEL_TERMINAL
    UmicomKernelConsoleTerminalFinish(shell);
#endif
    UmicomConsoleCopy(&shell->lastCompletion, &completion, sizeof(completion));
    shell->hasCompletion = UMICOM_TRUE;
    shell->foreground = 0U;
    UmicomShellText(shell, "process id="); UmicomShellNumber(shell, completion.identity);
    UmicomShellText(shell, " state="); UmicomShellText(shell, UmicomKernelUserTaskStateName(completion.state));
    if (completion.state == UMICOM_USER_TASK_EXITED) {
        UmicomShellText(shell, " exit="); UmicomShellNumber(shell, completion.exitValue);
    } else if (completion.state == UMICOM_USER_TASK_FAULTED) {
        UmicomShellText(shell, " trap="); UmicomShellNumber(shell, completion.trapCause);
    }
    UmicomShellText(shell, " slices="); UmicomShellNumber(shell, completion.slices);
    UmicomShellText(shell, " calls="); UmicomShellNumber(shell, completion.systemCalls);
    UmicomShellText(shell, "\r\n");
    /* A user file program can leave unlinked nodes behind. Descriptor cleanup
     * already happened in the scheduler; this asks RAMFS to reclaim eligible
     * storage rather than treating a vanished name as proof of a freed node. */
    UmicomSize reaped = 0U;
    const UmicomKernelVfsStatus reclaimed = UmicomKernelRamfsReap(&shell->storage, &reaped);
    if (shell->inputDuringRun) {
        shell->line.discard = UMICOM_TRUE;
        shell->inputDuringRun = UMICOM_FALSE;
        UmicomShellText(shell, "partial foreground input discarded; press Enter\r\n");
    }
    shell->busy = UMICOM_FALSE;
    UmicomKernelConsoleShellPrompt(shell);
    return UmicomShellFileError(shell, reclaimed);
}
void UmicomKernelConsoleShellPrompt(UmicomKernelConsoleShell *shell)
{
    if (shell && shell->self == shell && shell->output && !shell->foreground && !shell->exitRequested)
        UmicomShellText(shell, shell->state == UMICOM_VFS_CLOSING ? "cleanup> " : "umicom> ");
}
void UmicomKernelConsoleShellInputLost(UmicomKernelConsoleShell *shell)
{
    if (!shell || shell->self != shell || shell->busy || !shell->output) return;
#ifdef UMICOM_KERNEL_TERMINAL
    if (shell->foreground && shell->terminal) {
        UmicomKernelConsoleTerminalInputLost(shell);
        return;
    }
#endif
    /* Neither a terminal error nor FIFO overrun tells us which characters were
     * lost. Require a fresh delimiter, not a best guess at the damaged command. */
    shell->line.discard = UMICOM_TRUE;
    shell->inputDuringRun = shell->foreground != 0U ? UMICOM_TRUE : shell->inputDuringRun;
    UmicomShellText(shell, "\r\nconsole input error; discard through Enter\r\n");
}
UmicomKernelShellStatus UmicomKernelConsoleShellFeed(UmicomKernelConsoleShell *shell, UmicomU8 byte)
{
    if (!shell || shell->self != shell || shell->busy || !shell->output || shell->exitRequested)
        return UMICOM_SHELL_BAD_STATE;
    if (shell->state != UMICOM_VFS_OPEN && shell->state != UMICOM_VFS_CLOSING) return UMICOM_SHELL_BAD_STATE;
    if (shell->foreground != 0U) {
        if (byte == 3U) {
            UmicomShellText(shell, "^C\r\n");
#ifdef UMICOM_KERNEL_TERMINAL
            if (shell->terminal) UmicomKernelConsoleLineConsume(&shell->terminal->input);
#endif
            UmicomKernelConsoleLineConsume(&shell->line);
            shell->inputDuringRun = UMICOM_FALSE;
            const UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorCancel(&shell->supervisor,
                UMICOM_SUPERVISION_GUARDIAN, shell->foreground);
            if (status != UMICOM_SUPERVISION_OK) return UmicomShellProcessError(shell, status);
            return UmicomKernelConsoleShellStep(shell); /* Collect without running a cancelled image. */
        }
#ifdef UMICOM_KERNEL_TERMINAL
        /* Program input belongs to a different editor, never the command parser. */
        if (shell->terminal) return UmicomKernelConsoleTerminalFeed(shell, byte);
#endif
        /* No type-ahead after a program. Otherwise the tail of a pasted line
         * could become a different command when the foreground exits. */
        shell->inputDuringRun = byte == 13U || byte == 10U ? UMICOM_FALSE : UMICOM_TRUE;
        return UMICOM_SHELL_OK;
    }
    const UmicomSize previous = shell->line.length < UMICOM_SHELL_LINE_BYTES ? shell->line.length : 0U;
    const UmicomKernelConsoleLineEvent event = UmicomKernelConsoleLineFeed(&shell->line, byte);
    if (event == UMICOM_CONSOLE_LINE_READY) {
        UmicomShellText(shell, "\r\n");
        const UmicomKernelShellStatus status = UmicomKernelConsoleShellExecute(shell,
            shell->line.bytes, shell->line.length);
        UmicomKernelConsoleLineConsume(&shell->line);
        if (!shell->foreground) UmicomKernelConsoleShellPrompt(shell);
        return status;
    }
    if (event == UMICOM_CONSOLE_LINE_CANCEL || event == UMICOM_CONSOLE_LINE_REJECTED) {
        UmicomShellText(shell, event == UMICOM_CONSOLE_LINE_CANCEL ? "^C\r\n" :
            "\r\ncommand discarded: unsupported byte, input loss or line too long\r\n");
        UmicomKernelConsoleShellPrompt(shell);
        return event == UMICOM_CONSOLE_LINE_CANCEL ? UMICOM_SHELL_OK : UMICOM_SHELL_SYNTAX;
    }
    if (!shell->line.discard && shell->line.length > previous)
        shell->output(shell->outputContext, &shell->line.bytes[previous], 1U);
    else if (!shell->line.discard && shell->line.length < previous)
        for (UmicomSize i = shell->line.length; i < previous; ++i) UmicomShellText(shell, "\b \b");
    return UMICOM_SHELL_OK;
}
UmicomKernelShellStatus UmicomKernelConsoleShellClose(UmicomKernelConsoleShell *shell)
{
    if (!shell || shell->self != shell || !shell->output) return UMICOM_SHELL_BAD_STATE;
    if (shell->busy) return UMICOM_SHELL_BUSY;
    if (shell->state == UMICOM_VFS_CLOSED) return UMICOM_SHELL_OK;
    if (shell->state == UMICOM_VFS_POISONED || !UmicomKernelObjectCacheAccessAllowed()) return UMICOM_SHELL_UNSAFE;
    shell->busy = UMICOM_TRUE;
    shell->state = UMICOM_VFS_CLOSING;
#ifdef UMICOM_KERNEL_READ_ONLY_BLOCK
    /* Retry device reset before losing the console's retained DMA ownership. */
    if (UmicomKernelBlockRetryClose() != UMICOM_BLOCK_OK) {
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_CLEANUP_FAILED;
    }
#endif
    if (shell->supervisor.initialised) {
        UmicomKernelSupervisionStatus status = UmicomKernelProcessSupervisorBeginShutdown(&shell->supervisor);
        if (status != UMICOM_SUPERVISION_OK) { shell->busy = UMICOM_FALSE; return UmicomShellProcessError(shell, status); }
#ifdef UMICOM_KERNEL_TERMINAL
        if (UmicomKernelConsoleTerminalDrain(shell) != UMICOM_SHELL_OK) {
            shell->busy = UMICOM_FALSE;
            return UMICOM_SHELL_CLEANUP_FAILED;
        }
#endif
        /* There are at most four task owners. Cancellation precedes collection,
         * so no retained instruction stream can refer to a reclaimed image. */
        for (UmicomSize i = 0U; i < UMICOM_USER_TASK_LIMIT; ++i) {
            UmicomKernelProcessCompletion completion;
            UmicomConsoleClear(&completion, sizeof(completion));
            status = UmicomKernelProcessSupervisorCollectAny(&shell->supervisor, UMICOM_SUPERVISION_GUARDIAN, &completion);
            if (status == UMICOM_SUPERVISION_NO_CHILDREN) break;
            if (status != UMICOM_SUPERVISION_OK) { shell->busy = UMICOM_FALSE; return UmicomShellProcessError(shell, status); }
        }
        UmicomSize reaped = 0U;
        status = UmicomKernelProcessSupervisorReapRetained(&shell->supervisor, &reaped);
        if (status != UMICOM_SUPERVISION_OK) { shell->busy = UMICOM_FALSE; return UmicomShellProcessError(shell, status); }
        shell->foreground = 0U;
    }
#ifdef UMICOM_KERNEL_TERMINAL
    if (UmicomKernelConsoleTerminalClose(shell) != UMICOM_SHELL_OK) {
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_CLEANUP_FAILED;
    }
#endif
#ifdef UMICOM_KERNEL_SERVICE_CONSOLE
    /* Stop background instances before dismantling this console's file owners.
     * A refused cleanup remains retryable; poweroff must not bypass it. */
    if (UmicomKernelServiceConsoleClose(shell) != UMICOM_SHELL_OK) {
        shell->busy = UMICOM_FALSE;
        return UMICOM_SHELL_CLEANUP_FAILED;
    }
#endif
    UmicomKernelVfsStatus status = UMICOM_VFS_OK;
    if (shell->files.self && shell->files.state != UMICOM_VFS_CLOSED)
        status = UmicomKernelUserFilesClose(&shell->files);
    if (status == UMICOM_VFS_OK && shell->client.self && shell->client.state != UMICOM_VFS_CLOSED) {
        UmicomSize closed = 0U;
        status = UmicomKernelVfsClientClose(&shell->client, &closed);
        if (status == UMICOM_VFS_OK) shell->descriptor = 0U;
    }
    if (status == UMICOM_VFS_OK && shell->vfs.self && shell->vfs.state != UMICOM_VFS_CLOSED)
        status = UmicomKernelVfsUnmount(&shell->vfs);
    if (status == UMICOM_VFS_OK && shell->storage.self && shell->storage.state != UMICOM_VFS_CLOSED)
        status = UmicomKernelRamfsClose(&shell->storage);
    shell->busy = UMICOM_FALSE;
    if (status != UMICOM_VFS_OK) {
        (void)UmicomShellFileError(shell, status);
        UmicomShellText(shell, "cleanup incomplete; type poweroff to retry\r\n");
        return UMICOM_SHELL_CLEANUP_FAILED;
    }
    UmicomShellClear(shell->image, sizeof(shell->image));
    UmicomShellClear(shell->io, sizeof(shell->io));
    shell->state = UMICOM_VFS_CLOSED;
    return UMICOM_SHELL_OK;
}
