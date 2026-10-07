/* Umicom Kernel read-only partition/filesystem command adapter.
 * A failed transport close remains owned here until explicit retry succeeds.
 * This is a trusted inspection interface, not a disk mount or file syscall.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_DISK_CONSOLE_H
#define UMICOM_KERNEL_DISK_CONSOLE_H
#include "umicom/kernel/console_shell.h"
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/fat16_inspector.h"

UmicomKernelShellStatus UmicomKernelDiskInspectionCommand(UmicomKernelConsoleShell *shell,
    const UmicomKernelShellCommand *command, UmicomBoolean *handled);
UmicomKernelBlockStatus UmicomKernelDiskInspectionClose(void);
/* The same read-only command engine is testable without an interactive prompt. */
UmicomKernelDiskStatus UmicomKernelDiskInspect(UmicomSize slot, UmicomSize partition,
    const char *operation, const char *path, void *context, UmicomKernelBlockOutput output);
/* Separate fixture image: no existing 128-sector block test is bypassed in
 * ordinary diagnostic images merely to accommodate a different test disk. */
void UmicomKernelDiskInspectionValidate(void);
_Noreturn void UmicomKernelDiskInspectionBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif
