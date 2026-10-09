/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/disk_writable_filesystem_console.h
 *
 * Trusted console access to one explicitly selected writable disk VFS lifetime.
 * Each mutation completes its own checked persistence sequence before success.
 * Shutdown only releases ownership; it cannot finish a failed disk operation.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_CONSOLE_H
#define UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_CONSOLE_H
#include "umicom/kernel/console_shell.h"
#include "umicom/kernel/disk_writable_filesystem.h"

UmicomKernelShellStatus UmicomKernelDiskWritableFilesystemCommand(
    UmicomKernelConsoleShell *shell, const UmicomKernelShellCommand *command,
    UmicomBoolean *handled);
UmicomKernelVfsStatus UmicomKernelDiskWritableFilesystemConsoleClose(
    UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_CONSOLE_H */
