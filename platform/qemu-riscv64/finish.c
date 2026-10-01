/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/finish.c
 *
 * PURPOSE:
 *   Finish a QEMU RISC-V virt smoke test through the machine's SiFive test
 *   finisher. The generic kernel sees only success/halt operations and remains
 *   independent of the QEMU MMIO contract.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/platform.h"

#define UMI_QEMU_TEST_BASE ((UmiAddress)0x00100000ULL)
#define UMI_QEMU_FINISHER_PASS ((UmiU32)0x5555U)

void UmiPlatformFinishSuccess(void)
{
    volatile UmiU32 *const finisher = (volatile UmiU32 *)UMI_QEMU_TEST_BASE;
    *finisher = UMI_QEMU_FINISHER_PASS;

    /* If the machine ignores the finisher, never fall into unrelated memory. */
    UmiPlatformHalt();
}

void UmiPlatformHalt(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}
