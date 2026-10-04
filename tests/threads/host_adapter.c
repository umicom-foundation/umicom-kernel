/* Umicom Kernel test-only context preparation. Linux x86-64 is used solely to
 * exercise the unchanged scheduler with real alternate call stacks locally.
 * It is not evidence of RV64 register preservation or machine-mode admission.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/threads.h"
extern void UmicomHostThreadBootstrap(void);
UmicomBoolean umicomHostMachineReady = UMICOM_TRUE;
UmicomBoolean UmicomKernelThreadMachineReady(void) { return umicomHostMachineReady; }
void UmicomKernelThreadContextPrepare(UmicomKernelThreadContext *context,
    UmicomAddress stackTop, UmicomAddress scheduler, UmicomSize slot)
{
    /* The scheduler zeroed all context bytes before calling this adapter. */
    context->returnAddress = (UmicomU64)(UmicomUIntPtr)UmicomHostThreadBootstrap;
    context->stackPointer = stackTop;
    context->saved[2] = (UmicomU64)scheduler; /* r12 in the host switch. */
    context->saved[3] = (UmicomU64)slot; /* r13 in the host switch. */
}
