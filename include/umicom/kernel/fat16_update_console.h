/* Umicom Kernel trusted console for bounded updates of existing FAT16 data.
 * A separate exclusive lease exposes explicit write and flush observations.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_FAT16_UPDATE_CONSOLE_H
#define UMICOM_KERNEL_FAT16_UPDATE_CONSOLE_H
#include "umicom/kernel/console_shell.h"
#include "umicom/kernel/fat16_update.h"
UmicomKernelShellStatus UmicomKernelFat16UpdateCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled);
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateConsoleClose(UmicomKernelConsoleShell *shell);
#endif /* UMICOM_KERNEL_FAT16_UPDATE_CONSOLE_H */
