/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/timer.c
 *
 * PURPOSE:
 *   Provide the the trap/timer foundation machine-timer MMIO adapter for QEMU's RISC-V "virt" machine
 *   while keeping QEMU/CLINT addresses outside generic kernel code.
 *
 * EDUCATIONAL OVERVIEW:
 *   QEMU's `virt` machine exposes a SiFive-compatible CLINT when
 *   `-machine virt,aclint=off` is selected.
 *
 *   The relevant memory map for the trap/timer foundation is:
 *
 *     CLINT base        = 0x02000000
 *     MSWI area size    = 0x00004000
 *     MTIMER base       = 0x02004000
 *     mtimecmp[hart 0]  = 0x02004000
 *     mtime             = 0x0200bff8
 *
 *   mtime is a monotonically increasing 64-bit platform time counter.
 *
 *   A machine-timer interrupt becomes pending for a hart when:
 *
 *     mtime >= mtimecmp[hart]
 *
 *   The RISC-V privileged architecture then requires both:
 *
 *     mie.MTIE = 1
 *     mstatus.MIE = 1
 *
 *   before the pending machine-timer interrupt is actually taken in M-mode.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import platform contracts and the fixed-width address/value types they use. */
#include "umicom/kernel/platform.h"

/* QEMU `virt` places its CLINT-compatible region at this physical address. */
#define UMICOM_QEMU_CLINT_BASE ((UmicomAddress)0x02000000ULL)

/* QEMU reserves the first 0x4000 CLINT bytes for machine software interrupt
 * registers before the machine-timer block begins. */
#define UMICOM_QEMU_CLINT_MSWI_SIZE ((UmicomAddress)0x00004000ULL)

/* The machine-timer block therefore begins immediately after the MSWI region. */
#define UMICOM_QEMU_MTIMER_BASE \
    (UMICOM_QEMU_CLINT_BASE + UMICOM_QEMU_CLINT_MSWI_SIZE)

/* Inside QEMU's MTIMER block, the first compare register begins at offset 0. */
#define UMICOM_QEMU_MTIMECMP_OFFSET ((UmicomAddress)0x00000000ULL)

/* Each hart owns one 64-bit mtimecmp register, so compare slots are 8 bytes. */
#define UMICOM_QEMU_MTIMECMP_STRIDE ((UmicomAddress)8ULL)

/* QEMU's ACLINT/CLINT-compatible timer model places mtime at offset 0x7ff8
 * from the MTIMER block base. */
#define UMICOM_QEMU_MTIME_OFFSET ((UmicomAddress)0x00007ff8ULL)

/* Return a volatile pointer to the single shared 64-bit mtime counter. */
static volatile UmicomU64 *MachineTimeRegister(void)
{
    /* Add the platform-defined timer offset to the MTIMER base, then convert
     * the integer address to a volatile 64-bit MMIO pointer. */
    return (volatile UmicomU64 *)(UMICOM_QEMU_MTIMER_BASE + UMICOM_QEMU_MTIME_OFFSET);
}

/* Return a volatile pointer to one hart's 64-bit mtimecmp register. */
static volatile UmicomU64 *MachineTimeCompareRegister(UmicomU64 hartId)
{
    /* Convert the logical hart index into its byte offset.
     *
     * the trap/timer foundation starts only hart 0, but keeping the stride formula explicit teaches
     * how the per-hart timer register bank is organised for later SMP work. */
    const UmicomAddress hartOffset =
        (UmicomAddress)hartId * UMICOM_QEMU_MTIMECMP_STRIDE;

    /* Add the MTIMER base, compare-register offset and hart-specific stride. */
    const UmicomAddress registerAddress =
        UMICOM_QEMU_MTIMER_BASE +
        UMICOM_QEMU_MTIMECMP_OFFSET +
        hartOffset;

    /* Return a volatile pointer because reading/writing the location affects
     * emulated hardware rather than ordinary RAM. */
    return (volatile UmicomU64 *)registerAddress;
}

UmicomU64 UmicomPlatformTimerRead(void)
{
    /* Dereference the MMIO mtime register exactly once and return its current
     * 64-bit wall-clock tick value to the caller. */
    return *MachineTimeRegister();
}

void UmicomPlatformTimerSetCompare(UmicomU64 hartId, UmicomU64 deadline)
{
    /* Program the selected hart's absolute timer deadline.
     *
     * Once mtime reaches or passes this value, QEMU asserts the machine-timer
     * interrupt-pending condition for that hart. */
    *MachineTimeCompareRegister(hartId) = deadline;
}

void UmicomPlatformTimerDisable(UmicomU64 hartId)
{
    /* Use the largest representable 64-bit value as a practical "far future"
     * deadline for this early one-shot teaching timer. */
    const UmicomU64 farFuture = ~(UmicomU64)0U;

    /* Move the compare threshold away so the current timer interrupt condition
     * clears once the MMIO write is observed. */
    UmicomPlatformTimerSetCompare(hartId, farFuture);
}
