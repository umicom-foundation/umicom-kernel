/* Timestamped FAT16 file commits for the trusted Kernel console.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_FAT16_FILE_COMMIT_CONSOLE_H
#define UMICOM_KERNEL_FAT16_FILE_COMMIT_CONSOLE_H
#include "umicom/kernel/fat16_file_commit.h"
#include "umicom/kernel/console_shell.h"
UmicomKernelShellStatus UmicomKernelFat16FileCommitCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled);
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitConsoleClose(UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_FAT16_FILE_COMMIT_CONSOLE_H */
