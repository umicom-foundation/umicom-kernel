/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/service_console.h
 *
 * PURPOSE:
 *   Connect the interactive shell and the opt-in normal boot to managed services.
 *   The ordinary startup-job report remains read-only and separate. This adapter
 *   owns one opt-in sample domain per console boot, not a persistent service
 *   configuration database.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_SERVICE_CONSOLE_H
#define UMICOM_KERNEL_SERVICE_CONSOLE_H
#include "umicom/kernel/console_shell.h"
UmicomKernelShellStatus UmicomKernelServiceConsoleCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *outHandled);
/* Start the compiled normal plan and require actual readiness. The caller
 * owns cleanup on refusal before considering recovery. */
UmicomKernelShellStatus UmicomKernelServiceConsoleStartDefault(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelServiceConsolePoll(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelServiceConsoleClose(UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_SERVICE_CONSOLE_H */
