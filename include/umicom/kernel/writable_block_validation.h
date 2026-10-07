/* Dedicated disposable-disk qualification entry points. Ordinary boot and
 * console images never select this path or expose a disk mutation command.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#ifndef UMICOM_KERNEL_WRITABLE_BLOCK_VALIDATION_H
#define UMICOM_KERNEL_WRITABLE_BLOCK_VALIDATION_H
#include "umicom/kernel/types.h"
_Noreturn void UmicomKernelWritableBlockBoot(UmicomU64 hart, UmicomAddress deviceTree);
void UmicomKernelWritableBlockValidateExecution(void);
#endif /* UMICOM_KERNEL_WRITABLE_BLOCK_VALIDATION_H */
