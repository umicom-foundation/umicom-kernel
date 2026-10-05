/* Umicom Kernel test-only CSR model. These are not hardware observations.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_INTERRUPT_TEST_MODEL_H
#define UMICOM_INTERRUPT_TEST_MODEL_H
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/interrupt_state.h"
typedef struct UmicomInterruptTestModel {
    UmicomRiscvInterruptState state;
    UmicomU64 compare;
    UmicomBoolean refuseEnable;
    UmicomBoolean refuseDisable;
    UmicomBoolean observeUnmask;
    UmicomSize observedDepth;
    UmicomU64 deliveries;
} UmicomInterruptTestModel;
extern UmicomInterruptTestModel umicomInterruptModel;
void UmicomInterruptModelPrepare(void);
/* Compiled only with UMICOM_INTERRUPT_NATIVE_TESTING, never in either ELF. */
void UmicomKernelInterruptTestTokenLimit(void);
UmicomSize UmicomKernelInterruptTestDepth(void);
#endif
