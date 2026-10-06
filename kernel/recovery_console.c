/*-----------------------------------------------------------------------------
 * Umicom Kernel independent recovery command policy
 * File: kernel/recovery_console.c
 *
 * A failing normal startup must not depend on mounting the very filesystem
 * which failed. This interpreter uses the existing bounded tokenizer and only
 * fixed stack storage. It has no file, process-launch or memory-release verbs.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console_shell.h"
#include "console_internal.h"

static UmicomBoolean UmicomRecoveryEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < UMICOM_SHELL_LINE_BYTES; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static void UmicomRecoveryText(UmicomKernelStartupOutput output, void *context, const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes; /* Only fixed strings and a trusted boot reason. */
    output(context, text, bytes);
}
UmicomKernelRecoveryAction UmicomKernelRecoveryCommand(const char *line, UmicomSize bytes,
    const char *reason, UmicomKernelStartupOutput output, UmicomKernelRecoveryReport report, void *context)
{
    if (!output || !reason) return UMICOM_RECOVERY_CONTINUE;
    UmicomKernelShellCommand command;
    UmicomConsoleClear(&command, sizeof(command));
    if (UmicomKernelShellParse(line, bytes, &command) != UMICOM_SHELL_OK || command.count != 1U) {
        UmicomRecoveryText(output, context, "Use one command: help, status or poweroff.\r\n");
        return UMICOM_RECOVERY_CONTINUE;
    }
    const char *name = command.bytes + command.offsets[0];
    if (UmicomRecoveryEqual(name, "poweroff")) return UMICOM_RECOVERY_POWEROFF;
    if (UmicomRecoveryEqual(name, "status")) {
        UmicomRecoveryText(output, context, "reason: "); UmicomRecoveryText(output, context, reason);
        UmicomRecoveryText(output, context, "\r\n");
        if (report) report(context); /* Value-only observations, not a repair callback. */
    } else if (UmicomRecoveryEqual(name, "help")) {
        UmicomRecoveryText(output, context, "help | status | poweroff\r\n"
            "Recovery has no file, program, reset or allocation commands.\r\n");
    } else UmicomRecoveryText(output, context, "Unsupported recovery command. Use help.\r\n");
    return UMICOM_RECOVERY_CONTINUE;
}
