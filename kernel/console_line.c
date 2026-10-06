/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console_line.c
 *
 * PURPOSE:
 *   Turn a byte stream into bounded, complete commands without interpreting
 *   terminal escape sequences or running a truncated prefix after input loss.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/console_shell.h"
#include "console_internal.h"

void UmicomKernelConsoleLineConsume(UmicomKernelConsoleLine *line)
{
    if (!line) return;
    /* Erase even the unused tail. A later short command cannot expose an older
     * argument if diagnostic code inspects this owner. Keep only CR/LF framing. */
    for (UmicomSize i = 0U; i < sizeof(line->bytes); ++i) line->bytes[i] = '\0';
    line->length = 0U;
    line->discard = UMICOM_FALSE;
}
UmicomKernelConsoleLineEvent UmicomKernelConsoleLineFeed(UmicomKernelConsoleLine *line, UmicomU8 byte)
{
    if (!line) return UMICOM_CONSOLE_LINE_REJECTED;
    /* A corrupt length is not a reason to index outside the editor's storage. */
    if (line->length >= UMICOM_SHELL_LINE_BYTES) {
        line->length = 0U;
        line->discard = UMICOM_TRUE;
    }
    if (line->afterCr && byte == 10U) {
        line->afterCr = UMICOM_FALSE;
        return UMICOM_CONSOLE_LINE_NONE;
    }
    line->afterCr = UMICOM_FALSE;
    if (byte == 13U || byte == 10U) {
        line->afterCr = byte == 13U ? UMICOM_TRUE : UMICOM_FALSE;
        if (line->discard) {
            UmicomKernelConsoleLineConsume(line);
            return UMICOM_CONSOLE_LINE_REJECTED;
        }
        line->bytes[line->length] = '\0';
        return UMICOM_CONSOLE_LINE_READY;
    }
    if (byte == 3U) {
        UmicomKernelConsoleLineConsume(line);
        return UMICOM_CONSOLE_LINE_CANCEL;
    }
    /* An explicit clear cancels even an overlong line. Backspacing a damaged
     * line, however, cannot repair bytes which the receiver already lost. */
    if (byte == 21U) { UmicomKernelConsoleLineConsume(line); return UMICOM_CONSOLE_LINE_NONE; }
    if (line->discard) return UMICOM_CONSOLE_LINE_NONE;
    if (byte == 8U || byte == 127U) {
        if (line->length != 0U) line->bytes[--line->length] = '\0';
        return UMICOM_CONSOLE_LINE_NONE;
    }
    if (byte == 9U) byte = 32U; /* Tab is one separator, not terminal cursor motion. */
    if (byte < 32U || byte > 126U || line->length == UMICOM_SHELL_LINE_BYTES - 1U) {
        line->discard = UMICOM_TRUE;
        return UMICOM_CONSOLE_LINE_NONE;
    }
    line->bytes[line->length++] = (char)byte;
    line->bytes[line->length] = '\0';
    return UMICOM_CONSOLE_LINE_NONE;
}
UmicomKernelShellStatus UmicomKernelShellParse(const char *line, UmicomSize bytes,
    UmicomKernelShellCommand *out)
{
    /* The old parser below is retained verbatim. Its fixed four-token storage
     * could not represent a useful argv. Both old commands and structured
     * launch now share the bounded tokenizer; this wrapper keeps the original
     * public output type and four-token behaviour for all existing callers. */
    if (!out) return UMICOM_SHELL_INVALID_ARGUMENT;
    return UmicomKernelShellTokenize(line, bytes, out->bytes, out->offsets,
        UMICOM_SHELL_ARGUMENT_LIMIT, &out->count);
#if 0 /* Superseded fixed-storage parser; retained for comparison and teaching. */
    if (!out || (!line && bytes != 0U) || bytes >= UMICOM_SHELL_LINE_BYTES) return UMICOM_SHELL_INVALID_ARGUMENT;
    /* Parse into temporary storage: the caller receives no partially accepted
     * argv when a final quote or extra token makes the complete command invalid. */
    UmicomKernelShellCommand command;
    UmicomConsoleClear(&command, sizeof(command));
    UmicomSize at = 0U;
    UmicomSize used = 0U;
    for (UmicomSize i = 0U; i < bytes; ++i) {
        const unsigned char ch = (unsigned char)line[i];
        if ((ch < 32U && ch != 9U) || ch > 126U) return UMICOM_SHELL_SYNTAX;
    }
    while (at < bytes) {
        while (at < bytes && (line[at] == ' ' || line[at] == '\t')) ++at;
        if (at == bytes) break;
        if (command.count == UMICOM_SHELL_ARGUMENT_LIMIT) return UMICOM_SHELL_SYNTAX;
        command.offsets[command.count++] = used;
        const char quote = line[at] == '"' || line[at] == '\'' ? line[at++] : '\0';
        UmicomBoolean closed = quote == '\0' ? UMICOM_TRUE : UMICOM_FALSE;
        while (at < bytes) {
            const char ch = line[at];
            if (quote != '\0' && ch == quote) { ++at; closed = UMICOM_TRUE; break; }
            if (quote == '\0' && (ch == ' ' || ch == '\t')) break;
            if (quote == '\0' && (ch == '"' || ch == '\'')) return UMICOM_SHELL_SYNTAX;
            if (used >= sizeof(command.bytes) - 1U) return UMICOM_SHELL_RANGE;
            command.bytes[used++] = ch;
            ++at;
        }
        if (!closed || (at < bytes && line[at] != ' ' && line[at] != '\t')) return UMICOM_SHELL_SYNTAX;
        if (used >= sizeof(command.bytes)) return UMICOM_SHELL_RANGE;
        command.bytes[used++] = '\0';
    }
    UmicomConsoleCopy(out, &command, sizeof(command));
    return UMICOM_SHELL_OK;
#endif /* Retained parser body. */
}
UmicomBoolean UmicomKernelShellUnsigned(const char *text, UmicomU64 *out)
{
    if (!text || !out || text[0] == '\0') return UMICOM_FALSE;
    UmicomU64 value = 0U;
    /* Decimal only. Refuse signs, suffixes, empty values and arithmetic wrap;
     * no strtoull/libc dependency is introduced into the freestanding Kernel. */
    for (UmicomSize i = 0U; i < UMICOM_SHELL_LINE_BYTES; ++i) {
        const char ch = text[i];
        if (ch == '\0') { *out = value; return UMICOM_TRUE; }
        if (ch < '0' || ch > '9') return UMICOM_FALSE;
        const UmicomU64 digit = (UmicomU64)(ch - '0');
        if (value > (~(UmicomU64)0U - digit) / 10U) return UMICOM_FALSE;
        value = value * 10U + digit;
    }
    return UMICOM_FALSE;
}


UmicomKernelShellStatus UmicomKernelShellTokenize(const char *line, UmicomSize bytes,
    char *text, UmicomSize *offsets, UmicomSize capacity, UmicomSize *outCount)
{
    if (!text || !offsets || !outCount || (!line && bytes != 0U) ||
        bytes >= UMICOM_SHELL_LINE_BYTES || capacity == 0U || capacity > UMICOM_SHELL_TOKEN_LIMIT)
        return UMICOM_SHELL_INVALID_ARGUMENT;
    /* The grammar is unchanged: quotes surround a whole token, and nothing is
     * expanded. Local staging prevents partial output after a late syntax error. */
    char staged[UMICOM_SHELL_LINE_BYTES];
    UmicomSize positions[UMICOM_SHELL_TOKEN_LIMIT];
    UmicomConsoleClear(staged, sizeof(staged));
    UmicomConsoleClear(positions, sizeof(positions));
    UmicomSize count = 0U, used = 0U, at = 0U;
    for (UmicomSize i = 0U; i < bytes; ++i) {
        const unsigned char ch = (unsigned char)line[i];
        if ((ch < 32U && ch != 9U) || ch > 126U) return UMICOM_SHELL_SYNTAX;
    }
    while (at < bytes) {
        while (at < bytes && (line[at] == ' ' || line[at] == '\t')) ++at;
        if (at == bytes) break;
        if (count == capacity) return UMICOM_SHELL_SYNTAX;
        positions[count++] = used;
        const char quote = line[at] == '"' || line[at] == '\'' ? line[at++] : '\0';
        UmicomBoolean closed = quote == '\0' ? UMICOM_TRUE : UMICOM_FALSE;
        while (at < bytes) {
            const char ch = line[at];
            if (quote != '\0' && ch == quote) { ++at; closed = UMICOM_TRUE; break; }
            if (quote == '\0' && (ch == ' ' || ch == '\t')) break;
            if (quote == '\0' && (ch == '"' || ch == '\'')) return UMICOM_SHELL_SYNTAX;
            if (used >= sizeof(staged) - 1U) return UMICOM_SHELL_RANGE;
            staged[used++] = ch;
            ++at;
        }
        if (!closed || (at < bytes && line[at] != ' ' && line[at] != '\t')) return UMICOM_SHELL_SYNTAX;
        if (used >= sizeof(staged)) return UMICOM_SHELL_RANGE;
        staged[used++] = '\0';
    }
    /* Publish arrays before the count. None of these values points into this
     * temporary stack, and the unused output tail remains cleared. */
    UmicomConsoleCopy(text, staged, sizeof(staged));
    UmicomConsoleCopy(offsets, positions, capacity * sizeof(positions[0]));
    *outCount = count;
    return UMICOM_SHELL_OK;
}
