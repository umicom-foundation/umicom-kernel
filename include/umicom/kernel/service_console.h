/* Umicom Kernel interactive managed-service adapter. The ordinary startup-job
 * report remains read-only and separate. This adapter owns one opt-in sample
 * domain per console boot, not a persistent service configuration database.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_SERVICE_CONSOLE_H
#define UMICOM_KERNEL_SERVICE_CONSOLE_H
#include "umicom/kernel/console_shell.h"
UmicomKernelShellStatus UmicomKernelServiceConsoleCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *outHandled);
UmicomKernelShellStatus UmicomKernelServiceConsolePoll(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelServiceConsoleClose(UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_SERVICE_CONSOLE_H */
