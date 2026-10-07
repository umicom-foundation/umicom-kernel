/* Umicom Kernel trusted console for one read-only disk mount lifetime.
 * RAMFS commands and process-file bindings retain their existing domain.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_DISK_FILESYSTEM_CONSOLE_H
#define UMICOM_KERNEL_DISK_FILESYSTEM_CONSOLE_H
#include "umicom/kernel/console_shell.h"
#include "umicom/kernel/disk_filesystem.h"
UmicomKernelShellStatus UmicomKernelDiskFilesystemCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled);
UmicomKernelVfsStatus UmicomKernelDiskFilesystemConsoleClose(UmicomKernelConsoleShell *shell);
#endif
