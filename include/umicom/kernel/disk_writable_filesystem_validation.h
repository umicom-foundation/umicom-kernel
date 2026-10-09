/* Umicom Kernel writable filesystem qualification entry points. Dedicated
 * images receive only explicitly prepared disposable test media.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_VALIDATION_H
#define UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_VALIDATION_H
#include "umicom/kernel/types.h"
void UmicomKernelDiskWritableFilesystemValidate(void);
void UmicomKernelDiskWritableFilesystemReadbackValidate(void);
void UmicomKernelDiskWritableFilesystemReadOnlyValidate(void);
_Noreturn void UmicomKernelDiskWritableFilesystemBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_VALIDATION_H */
