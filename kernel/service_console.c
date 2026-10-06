/*-----------------------------------------------------------------------------
 * Umicom Kernel interactive service-controller demonstration
 * File: kernel/service_console.c
 *
 * This optional plan runs alongside the existing foreground controller. Each
 * polling pass runs at most one bounded service quantum; machine state must be
 * restored before the shell or its foreground process is allowed to continue.
 * No service owns the UART. Status is printed only on request, so background
 * reports cannot overwrite somebody's partially edited command line.
 *
 * The sample is intentionally bounded by attempts and existing process budgets.
 * Stopping does not reset the owner or revive old tokens. A new boot provides
 * another fresh demonstration domain; this is not persistent configuration.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/service_console.h"
#include "umicom/kernel/service_manager.h"
#include "console_internal.h"
extern const UmicomU8 UmicomHealthExecutableStart[];
extern const UmicomU8 UmicomHealthExecutableEnd[];
static UmicomKernelServiceManager umicomConsoleManager;
static UmicomKernelConsoleShell *umicomManagedConsole;

static UmicomBoolean UmicomManagedWord(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < 32U; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static void UmicomManagedText(UmicomKernelConsoleShell *shell, const char *text)
{
    UmicomSize length = 0U;
    while (text[length]) ++length;
    shell->output(shell->outputContext, text, length);
}
static void UmicomManagedNumber(UmicomKernelConsoleShell *shell, UmicomU64 value)
{
    char digits[20]; UmicomSize used = 0U;
    do { digits[used++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (used) shell->output(shell->outputContext, &digits[--used], 1U);
}
static UmicomKernelShellStatus UmicomManagedShellResult(UmicomKernelServiceManagerStatus status)
{
    if (status == UMICOM_MANAGER_OK) return UMICOM_SHELL_OK;
    if (status == UMICOM_MANAGER_CLEANUP_FAILED) return UMICOM_SHELL_CLEANUP_FAILED;
    if (status == UMICOM_MANAGER_STATE_UNSAFE || status == UMICOM_MANAGER_CLOCK_REVERSED)
        return UMICOM_SHELL_UNSAFE;
    return UMICOM_SHELL_BAD_STATE;
}
UmicomKernelShellStatus UmicomKernelServiceConsoleCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled)
{
    if (!shell || !command || !handled || !shell->output) return UMICOM_SHELL_BAD_STATE;
    *handled = UMICOM_FALSE;
    if (!command->count || !UmicomManagedWord(command->bytes + command->offsets[0], "daemons")) return UMICOM_SHELL_OK;
    *handled = UMICOM_TRUE;
    const char *operation = command->count > 1U ? command->bytes + command->offsets[1] : "status";
    if (UmicomManagedWord(operation, "start") && command->count <= 3U) {
        if (umicomConsoleManager.initialised) {
            UmicomManagedText(shell, "Managed plan already used. Stop retains its report; reboot for a new plan.\r\n");
            return UMICOM_SHELL_BAD_STATE;
        }
        const char *mode = command->count == 3U ? command->bytes + command->offsets[2] : "healthy";
        if (!UmicomManagedWord(mode, "healthy") && !UmicomManagedWord(mode, "silent") &&
            !UmicomManagedWord(mode, "fault") && !UmicomManagedWord(mode, "unready")) {
            UmicomManagedText(shell, "Use: daemons start [healthy|silent|fault|unready]\r\n");
            return UMICOM_SHELL_BAD_STATE;
        }
        UmicomSize modeBytes = 0U;
        while (mode[modeBytes]) ++modeBytes;
        UmicomKernelLaunchString first[] = {{"/services/clock-sample", 22U}, {mode, modeBytes}};
        UmicomKernelLaunchString second[] = {{"/services/consumer-sample", 25U}, {"healthy", 7U}};
        /* Byte lengths exclude terminators; calculate names here to keep a
         * changed descriptive label from accidentally truncating launch data. */
        first[0].bytes = 0U; while (first[0].data[first[0].bytes]) ++first[0].bytes;
        second[0].bytes = 0U; while (second[0].data[second[0].bytes]) ++second[0].bytes;
        UmicomKernelManagedServiceSpec specs[2];
        UmicomConsoleClear(specs, sizeof(specs));
        for (UmicomSize i = 0U; i < 2U; ++i) {
            specs[i].name = i ? "consumer-sample" : "clock-sample";
            specs[i].image = UmicomHealthExecutableStart;
            specs[i].imageBytes = (UmicomSize)(UmicomHealthExecutableEnd - UmicomHealthExecutableStart);
            specs[i].launch.arguments = i ? second : first; specs[i].launch.argumentCount = 2U;
            specs[i].dependencies = i ? 1U : 0U; specs[i].attempts = 4U;
            specs[i].startupTicks = 20000000U; specs[i].healthTicks = 30000000U;
            specs[i].reportTicks = 10000000U; specs[i].retryTicks = 10000000U;
            specs[i].sliceLimit = 4096U; specs[i].required = UMICOM_TRUE;
        }
        const UmicomKernelServiceManagerStatus status = UmicomKernelServiceManagerInitialize(
            &umicomConsoleManager, specs, 2U, 0, 0);
        if (umicomConsoleManager.initialised) umicomManagedConsole = shell;
        if (status == UMICOM_MANAGER_OK)
            UmicomManagedText(shell, "Native service plan admitted. Readiness still requires a report. Use daemons status.\r\n");
        return UmicomManagedShellResult(status);
    }
    if (!umicomConsoleManager.initialised || umicomManagedConsole != shell) {
        UmicomManagedText(shell, "No managed service plan is attached to this console.\r\n"); return UMICOM_SHELL_OK;
    }
    if (UmicomManagedWord(operation, "status") && command->count <= 2U) {
        for (UmicomSize i = 0U; i < umicomConsoleManager.count; ++i) {
            UmicomKernelManagedServiceInfo info;
            UmicomConsoleClear(&info, sizeof(info));
            if (UmicomKernelServiceManagerQuery(&umicomConsoleManager, i, &info) != UMICOM_MANAGER_OK)
                return UMICOM_SHELL_BAD_STATE;
            UmicomManagedNumber(shell, i); UmicomManagedText(shell, " "); UmicomManagedText(shell, info.name);
            UmicomManagedText(shell, ": "); UmicomManagedText(shell, UmicomKernelManagedServiceStateName(info.state));
            UmicomManagedText(shell, " attempts="); UmicomManagedNumber(shell, info.attempts);
            UmicomManagedText(shell, " identity="); UmicomManagedNumber(shell, info.identity);
            UmicomManagedText(shell, " sequence="); UmicomManagedNumber(shell, info.sequence);
            UmicomManagedText(shell, " reason="); UmicomManagedText(shell, UmicomKernelManagedServiceReasonName(info.reason));
            UmicomManagedText(shell, "\r\n");
        }
        if (umicomConsoleManager.recovery)
            UmicomManagedText(shell, "Managed plan requires recovery; automatic admission has stopped.\r\n");
        return UMICOM_SHELL_OK;
    }
    if (UmicomManagedWord(operation, "stop") && command->count == 2U) {
        const UmicomKernelShellStatus status = UmicomKernelServiceConsoleClose(shell);
        if (status == UMICOM_SHELL_OK) UmicomManagedText(shell, "Managed tasks collected. Result snapshots retained.\r\n");
        return status;
    }
    if (UmicomManagedWord(operation, "restart") && command->count == 3U) {
        const char *index = command->bytes + command->offsets[2];
        if (index[0] < '0' || index[0] > '3' || index[1]) return UMICOM_SHELL_BAD_STATE;
        const UmicomKernelServiceManagerStatus status = UmicomKernelServiceManagerRestart(
            &umicomConsoleManager, (UmicomSize)(index[0] - '0'));
        if (status == UMICOM_MANAGER_OK)
            UmicomManagedText(shell, "Readiness revoked. Dependents stop before a replacement is admitted.\r\n");
        return UmicomManagedShellResult(status);
    }
    UmicomManagedText(shell, "Use daemons status | daemons restart INDEX | daemons stop.\r\n");
    return UMICOM_SHELL_BAD_STATE;
}
UmicomKernelShellStatus UmicomKernelServiceConsolePoll(UmicomKernelConsoleShell *shell)
{
    if (!umicomConsoleManager.initialised || umicomManagedConsole != shell || umicomConsoleManager.closed ||
        umicomConsoleManager.stopping)
        return UMICOM_SHELL_OK;
    /* A failed release keeps the cleanup fence closed. Another poll can retry
     * it; an unsafe machine return must still stop the enclosing console. */
    const UmicomKernelServiceManagerStatus status = UmicomKernelServiceManagerStep(&umicomConsoleManager, 100000U);
    return status == UMICOM_MANAGER_CLEANUP_FAILED ? UMICOM_SHELL_OK : UmicomManagedShellResult(status);
}
UmicomKernelShellStatus UmicomKernelServiceConsoleClose(UmicomKernelConsoleShell *shell)
{
    if (!umicomConsoleManager.initialised || umicomManagedConsole != shell) return UMICOM_SHELL_OK;
    return UmicomManagedShellResult(UmicomKernelServiceManagerClose(&umicomConsoleManager));
}
