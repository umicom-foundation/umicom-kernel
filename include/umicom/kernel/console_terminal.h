/*-----------------------------------------------------------------------------
 * Umicom Kernel foreground terminal adapter
 * File: include/umicom/kernel/console_terminal.h
 *
 * Keep line editing and safe display outside user traps. This adapter owns no
 * process or filesystem: it borrows the existing shell and supplies streams to
 * its single foreground task. The input editor is separate from the shell's
 * command editor, so program input can never be interpreted as a shell command.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_CONSOLE_TERMINAL_H
#define UMICOM_KERNEL_CONSOLE_TERMINAL_H
#include "umicom/kernel/console_shell.h"
#include "umicom/kernel/user_streams.h"

typedef struct UmicomKernelConsoleTerminal {
    const struct UmicomKernelConsoleTerminal *self;
    UmicomKernelConsoleShell *shell;
    UmicomKernelUserStreams streams;
    UmicomKernelConsoleLine input;
    UmicomBoolean closed;
} UmicomKernelConsoleTerminal;

UmicomKernelShellStatus UmicomKernelConsoleTerminalAttach(UmicomKernelConsoleTerminal *terminal,
    UmicomKernelConsoleShell *shell);
UmicomBoolean UmicomKernelConsoleTerminalGrant(UmicomKernelConsoleShell *shell);
void UmicomKernelConsoleTerminalAnnounce(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelConsoleTerminalFeed(UmicomKernelConsoleShell *shell, UmicomU8 byte);
void UmicomKernelConsoleTerminalInputLost(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelConsoleTerminalDrain(UmicomKernelConsoleShell *shell);
void UmicomKernelConsoleTerminalFinish(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelConsoleTerminalClose(UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_CONSOLE_TERMINAL_H */
