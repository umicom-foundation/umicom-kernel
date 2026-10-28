/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/cvm_validation.h
 *
 * PURPOSE:
 *   Connect the bounded CVM runtime to the native user-program contract.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_CVM_VALIDATION_H
#define UMICOM_KERNEL_CVM_VALIDATION_H
/* Only the optional diagnostic profile calls this entry. Normal startup does
 * not become dependent on a compiler, a VM or an additional boot service. */
void UmicomKernelCvmValidate(void);
#endif
