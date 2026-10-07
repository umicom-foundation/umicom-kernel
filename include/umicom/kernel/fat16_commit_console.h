/* Ordered FAT16 data-update commands for the trusted Kernel console.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_FAT16_COMMIT_CONSOLE_H
#define UMICOM_KERNEL_FAT16_COMMIT_CONSOLE_H
#include "umicom/kernel/fat16_commit.h"
#include "umicom/kernel/console_shell.h"
UmicomKernelShellStatus UmicomKernelFat16CommitCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled);
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitConsoleClose(UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_FAT16_COMMIT_CONSOLE_H */
