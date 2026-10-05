/* Umicom Kernel test-only CSR adapter. Native policy tests control these values
 * to exercise refusal and publication order. This does not emulate instruction
 * execution or prove that a RISC-V interrupt was delivered.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "model.h"
UmicomInterruptTestModel umicomInterruptModel;
void UmicomInterruptModelPrepare(void)
{
    /* One test per executable invocation also gives the production C controller
     * fresh BSS. We do not add a reset operation to the actual controller. */
    umicomInterruptModel.state.vector = 0x80200100U;
    umicomInterruptModel.state.scratch = 0x80230000U;
    umicomInterruptModel.compare = ~(UmicomU64)0U;
}
UmicomU64 UmicomRiscvInterruptMaskSave(void)
{
    const UmicomU64 old = umicomInterruptModel.state.status & 8U;
    umicomInterruptModel.state.status &= ~(UmicomU64)8U;
    return old;
}
void UmicomRiscvInterruptMaskRestore(UmicomU64 mieBit)
{
    umicomInterruptModel.state.status = (umicomInterruptModel.state.status & ~(UmicomU64)8U) | (mieBit & 8U);
    if ((mieBit & 8U) != 0U && umicomInterruptModel.observeUnmask != UMICOM_FALSE) {
        /* This observation models immediate execution at the publication point.
         * It checks bookkeeping order, not the real trap entry's instruction path. */
        umicomInterruptModel.observedDepth = UmicomKernelInterruptTestDepth();
        ++umicomInterruptModel.deliveries;
        umicomInterruptModel.observeUnmask = UMICOM_FALSE;
        umicomInterruptModel.state.pending &= ~(UmicomU64)0x80U;
        umicomInterruptModel.state.sources &= ~(UmicomU64)0x80U;
        umicomInterruptModel.state.status &= ~(UmicomU64)8U;
    }
}
void UmicomRiscvInterruptStateRead(UmicomRiscvInterruptState *outState)
{
    *outState = umicomInterruptModel.state;
}
void UmicomRiscvInterruptSourcesEnable(UmicomU64 sources)
{
    if (umicomInterruptModel.refuseEnable == UMICOM_FALSE) umicomInterruptModel.state.sources |= sources;
}
void UmicomRiscvInterruptSourcesDisable(UmicomU64 sources)
{
    if (umicomInterruptModel.refuseDisable == UMICOM_FALSE) umicomInterruptModel.state.sources &= ~sources;
}
UmicomU64 UmicomPlatformTimerCompareRead(UmicomU64 hart)
{
    /* The actual board-side quiescence C code is linked into the suite. */
    return hart == 0U ? umicomInterruptModel.compare : 0U;
}
