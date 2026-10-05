/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/interrupt_ownership.c
 *
 * PURPOSE:
 *   Distinguish a masked timer from a timer which has no practical deadline.
 *
 * EDUCATIONAL NOTE:
 *   A future compare value can become pending after a source is released. The
 *   next owner would then inherit someone else's work even though mip was clear
 *   at release time. Reuse the existing compare reader and require the parked
 *   value used by UmicomPlatformTimerDisable. This check does not modify the
 *   timer, erase a pending event or claim support for other device controllers.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/platform.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/interrupt_state.h"

UmicomBoolean UmicomPlatformInterruptSourceQuiescent(UmicomU64 sources)
{
    /* Reject unsupported masks rather than guessing another device's protocol. */
    if (sources != UMICOM_INTERRUPT_SOURCE_MACHINE_TIMER) return UMICOM_FALSE;
    /* This machine profile and controller deliberately support only hart zero. */
    return UmicomPlatformTimerCompareRead(0U) == ~(UmicomU64)0U ? UMICOM_TRUE : UMICOM_FALSE;
}
