/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/finish.c
 *
 * PURPOSE:
 *   Terminate the QEMU K1 smoke test through QEMU virt's SiFive test-finisher
 *   MMIO device, while keeping that machine-specific detail out of generic
 *   kernel code.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import the platform function declarations and fixed-width types. */
#include "umicom/kernel/platform.h"

/* QEMU virt maps its SiFive test device at physical address 0x00100000. */
#define UMI_QEMU_TEST_BASE ((UmiAddress)0x00100000ULL)

/* QEMU interprets low 16-bit status value 0x5555 as FINISHER_PASS.
 *
 * The high 16 bits can carry an exit code; K1 leaves them zero. */
#define UMI_QEMU_FINISHER_PASS ((UmiU32)0x00005555U)

void UmiPlatformFinishSuccess(void)
{
    /* Convert the device address into a volatile 32-bit MMIO pointer.
     *
     * volatile is essential because the store is a hardware action, not merely
     * a value kept in ordinary RAM. */
    volatile UmiU32 *const finisher =
        (volatile UmiU32 *)UMI_QEMU_TEST_BASE;

    /* Write the pass status.
     *
     * On QEMU virt this requests guest shutdown with a successful exit code. */
    *finisher = UMI_QEMU_FINISHER_PASS;

    /* The emulator should terminate before reaching this call.
     *
     * If a future machine ignores the finisher, halt instead of executing
     * through an invalid return path. */
    UmiPlatformHalt();
}

void UmiPlatformHalt(void)
{
    /* A kernel halt has no caller to return to.  Stay in this loop forever. */
    for (;;) {

        /* Wait For Interrupt reduces needless execution while the processor is
         * parked.  K1 keeps interrupts disabled, so no normal work resumes. */
        __asm__ volatile("wfi");
    }
}
