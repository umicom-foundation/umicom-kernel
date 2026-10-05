/* Umicom Kernel object-cache execution boundary.
 * This is a gate, not another interrupt controller. The existing CSR reader
 * and ownership authority decide whether our serial, physical-memory view is
 * available. It never acquires a timer lease or changes the final interrupt
 * policy; the shared predicate may briefly mask delivery while inspecting it.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/object_cache.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/interrupt_state.h"

UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    /* A normal Kernel caller supplies a real machine stack. These observations
     * do not grant permission to call from an arbitrary trap or another hart. */
    UmicomU64 status = 0U;
    __asm__ volatile("csrr %0, mstatus" : "=r"(status));
    if ((status & 0x20008U) != 0U) return UMICOM_FALSE;
    UmicomRiscvInterruptState state;
    UmicomRiscvInterruptStateRead(&state);
    if (state.hart != 0U || state.translation != 0U || state.sources != 0U ||
        (state.status & 0x20008U) != 0U) {
        return UMICOM_FALSE;
    }
    /* Refuse to perform allocation while a caller owns a section or timer
     * lease. The existing service is the authority for those lifetimes. */
    return UmicomKernelInterruptContextSwitchAllowed();
}
