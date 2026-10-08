/* Umicom Kernel dedicated directory qualification boot entry points.
 * These entry points are selected only by disposable-image test builds.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_FAT16_DIRECTORY_VALIDATION_H
#define UMICOM_KERNEL_FAT16_DIRECTORY_VALIDATION_H
#include "umicom/kernel/fat16_lifecycle_commit.h"
void UmicomKernelFat16DirectoryValidate(void);
void UmicomKernelFat16DirectoryReadbackValidate(void);
void UmicomKernelFat16DirectoryInterruptedValidate(void);
void UmicomKernelFat16DirectoryRejectedValidate(void);
_Noreturn void UmicomKernelFat16DirectoryBoot(UmicomU64 hart, UmicomAddress deviceTree);
#endif /* UMICOM_KERNEL_FAT16_DIRECTORY_VALIDATION_H */
