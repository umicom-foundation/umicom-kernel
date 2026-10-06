/*-----------------------------------------------------------------------------
 * Umicom Kernel terminal line discipline and safe stream display
 * File: kernel/console_terminal.c
 *
 * Input becomes visible to a program at Enter, or as a final unterminated piece
 * at Ctrl-D. Empty-line Ctrl-D closes stdin for this invocation. The terminal
 * is not a filesystem descriptor, and closing stdin does not close stdout.
 *
 * Accepted output is drained between quanta, never from the trap handler. A
 * program cannot inject terminal escape commands: nonprinting bytes are escaped.
 * stderr is tagged while stdout is displayed normally; their accepted order is
 * preserved by one task-local queue, not reconstructed from two later snapshots.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/console_terminal.h"
#include "console_internal.h"

static void UmicomTerminalText(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    shell->output(shell->outputContext, text, bytes);
}
static UmicomBoolean UmicomTerminalValid(UmicomKernelConsoleShell *shell)
{
    return shell && shell->terminal && shell->terminal->self == shell->terminal &&
        shell->terminal->shell == shell && !shell->terminal->closed ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelShellStatus UmicomKernelConsoleTerminalAttach(UmicomKernelConsoleTerminal *terminal,
    UmicomKernelConsoleShell *shell)
{
    if (!terminal || !shell || shell->self != shell || shell->state != UMICOM_VFS_OPEN ||
        shell->busy || shell->foreground || shell->terminal) return UMICOM_SHELL_BAD_STATE;
    const UmicomU8 *bytes = (const UmicomU8 *)terminal;
    for (UmicomSize i = 0U; i < sizeof(*terminal); ++i) if (bytes[i]) return UMICOM_SHELL_BAD_STATE;
    if (UmicomKernelUserStreamsAttach(&terminal->streams, &shell->supervisor.scheduler) != UMICOM_STREAM_OK)
        return UMICOM_SHELL_BAD_STATE;
    terminal->self = terminal;
    terminal->shell = shell;
    shell->terminal = terminal; /* Publish only after attachment succeeded. */
    return UMICOM_SHELL_OK;
}
UmicomBoolean UmicomKernelConsoleTerminalGrant(UmicomKernelConsoleShell *shell)
{
    if (!UmicomTerminalValid(shell)) return UMICOM_FALSE;
    if (UmicomKernelUserStreamsGrant(&shell->terminal->streams, shell->foreground) != UMICOM_STREAM_OK)
        return UMICOM_FALSE;
    UmicomConsoleClear(&shell->terminal->input, sizeof(shell->terminal->input));
    /* A command entered as CR/LF must not donate its LF half to the new stdin.
     * The command editor's pending CR is copied before it consumes that line. */
    shell->terminal->input.afterCr = shell->line.afterCr;
    return UMICOM_TRUE;
}
void UmicomKernelConsoleTerminalAnnounce(UmicomKernelConsoleShell *shell)
{
    UmicomTerminalText(shell, "Foreground stdin active. Enter submits; Ctrl-D ends input; Ctrl-C cancels.\r\n");
}
static UmicomKernelShellStatus UmicomTerminalCommit(UmicomKernelConsoleShell *shell, UmicomBoolean newline)
{
    UmicomKernelConsoleLine *line = &shell->terminal->input;
    UmicomU8 copy[UMICOM_SHELL_LINE_BYTES];
    UmicomConsoleClear(copy, sizeof(copy)); /* Define the whole outbound record before its const-pointer call. */
    for (UmicomSize i = 0U; i < line->length; ++i) copy[i] = (UmicomU8)line->bytes[i];
    const UmicomSize bytes = line->length + (newline ? 1U : 0U);
    if (newline) copy[line->length] = 10U;
    const UmicomKernelStreamStatus status = UmicomKernelUserStreamsInput(&shell->terminal->streams,
        shell->foreground, copy, bytes);
    UmicomConsoleClear(copy, sizeof(copy));
    UmicomKernelConsoleLineConsume(line);
    shell->inputDuringRun = UMICOM_FALSE;
    if (status != UMICOM_STREAM_OK) {
        /* Reject the entire line. Do not turn a partially accepted command-like
         * string into a different application request when the queue is full. */
        UmicomTerminalText(shell, "stdin line not accepted (full or closed); enter it again when ready\r\n");
        return UMICOM_SHELL_IO_ERROR;
    }
    return UMICOM_SHELL_OK;
}
UmicomKernelShellStatus UmicomKernelConsoleTerminalFeed(UmicomKernelConsoleShell *shell, UmicomU8 byte)
{
    if (!UmicomTerminalValid(shell) || !shell->foreground) return UMICOM_SHELL_BAD_STATE;
    UmicomKernelConsoleLine *line = &shell->terminal->input;
    if (byte == 4U) {
        if (line->discard) {
            UmicomTerminalText(shell, "stdin line damaged; press Enter before EOF\r\n");
            return UMICOM_SHELL_SYNTAX;
        }
        if (line->length) return UmicomTerminalCommit(shell, UMICOM_FALSE);
        const UmicomKernelStreamStatus status = UmicomKernelUserStreamsEndInput(&shell->terminal->streams, shell->foreground);
        UmicomTerminalText(shell, status == UMICOM_STREAM_OK ? "^D\r\n" : "stdin already stopped\r\n");
        shell->inputDuringRun = UMICOM_FALSE;
        return status == UMICOM_STREAM_OK ? UMICOM_SHELL_OK : UMICOM_SHELL_BAD_STATE;
    }
    const UmicomSize old = line->length < UMICOM_SHELL_LINE_BYTES ? line->length : 0U;
    const UmicomKernelConsoleLineEvent event = UmicomKernelConsoleLineFeed(line, byte);
    shell->inputDuringRun = line->length || line->discard ? UMICOM_TRUE : UMICOM_FALSE;
    if (event == UMICOM_CONSOLE_LINE_READY) {
        UmicomTerminalText(shell, "\r\n");
        return UmicomTerminalCommit(shell, UMICOM_TRUE);
    }
    if (event == UMICOM_CONSOLE_LINE_REJECTED) {
        shell->inputDuringRun = UMICOM_FALSE;
        UmicomTerminalText(shell, "\r\nstdin line discarded: unsupported byte, input loss or line too long\r\n");
        return UMICOM_SHELL_SYNTAX;
    }
    if (!line->discard && line->length > old)
        shell->output(shell->outputContext, &line->bytes[old], 1U);
    else if (!line->discard && line->length < old)
        for (UmicomSize i = line->length; i < old; ++i) UmicomTerminalText(shell, "\b \b");
    return UMICOM_SHELL_OK;
}
void UmicomKernelConsoleTerminalInputLost(UmicomKernelConsoleShell *shell)
{
    if (!UmicomTerminalValid(shell)) return;
    shell->terminal->input.discard = UMICOM_TRUE;
    shell->inputDuringRun = UMICOM_TRUE;
    UmicomTerminalText(shell, "\r\nstdin input error; discard through Enter\r\n");
}
UmicomKernelShellStatus UmicomKernelConsoleTerminalDrain(UmicomKernelConsoleShell *shell)
{
    if (!shell->terminal || !shell->foreground) return UMICOM_SHELL_OK;
    if (!UmicomTerminalValid(shell)) return UMICOM_SHELL_BAD_STATE;
    /* Draining is bounded by the queue capacity. No output callback may reenter
     * the shell or stream owner; the shell's existing busy guard stays active. */
    for (UmicomSize record = 0U; record < UMICOM_STREAM_OUTPUT_RECORDS; ++record) {
        UmicomKernelStreamPacket packet;
        UmicomConsoleClear(&packet, sizeof(packet));
        const UmicomKernelStreamStatus status = UmicomKernelUserStreamsDrain(&shell->terminal->streams,
            shell->foreground, &packet);
        if (status == UMICOM_STREAM_WOULD_BLOCK) return UMICOM_SHELL_OK;
        if (status != UMICOM_STREAM_OK) return UMICOM_SHELL_UNSAFE;
        if (packet.selector == UMICOM_STREAM_ERROR) UmicomTerminalText(shell, "[stderr] ");
        static const char hex[] = "0123456789abcdef";
        for (UmicomSize i = 0U; i < packet.bytes; ++i) {
            const UmicomU8 byte = packet.data[i];
            if (byte == 10U) UmicomTerminalText(shell, "\r\n");
            else if (byte >= 32U && byte <= 126U) {
                const char printable = (char)byte;
                shell->output(shell->outputContext, &printable, 1U);
            } else {
                const char escaped[] = {'\\','x',hex[byte >> 4U],hex[byte & 15U]};
                shell->output(shell->outputContext, escaped, sizeof(escaped));
            }
        }
        UmicomConsoleClear(&packet, sizeof(packet));
    }
    return UMICOM_SHELL_OK;
}
void UmicomKernelConsoleTerminalFinish(UmicomKernelConsoleShell *shell)
{
    if (!shell->terminal) return;
    /* Preserve the old type-ahead barrier only for an incomplete stdin line.
     * Completed program input is never copied into the command editor. */
    shell->inputDuringRun = shell->inputDuringRun || shell->terminal->input.length ||
        shell->terminal->input.discard ? UMICOM_TRUE : UMICOM_FALSE;
    shell->line.afterCr = shell->terminal->input.afterCr;
    UmicomConsoleClear(&shell->terminal->input, sizeof(shell->terminal->input));
}
UmicomKernelShellStatus UmicomKernelConsoleTerminalClose(UmicomKernelConsoleShell *shell)
{
    if (!shell->terminal || shell->terminal->closed) return UMICOM_SHELL_OK;
    if (!UmicomTerminalValid(shell)) return UMICOM_SHELL_BAD_STATE;
    if (UmicomKernelUserStreamsClose(&shell->terminal->streams) != UMICOM_STREAM_OK)
        return UMICOM_SHELL_CLEANUP_FAILED;
    UmicomConsoleClear(&shell->terminal->input, sizeof(shell->terminal->input));
    shell->terminal->closed = UMICOM_TRUE;
    return UMICOM_SHELL_OK;
}
