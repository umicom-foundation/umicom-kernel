/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/serial.c
 *
 * PURPOSE:
 *   Drive the NS16550-compatible UART exposed by QEMU's RISC-V "virt" machine.
 *
 * EDUCATIONAL NOTE:
 *   "MMIO" means memory-mapped input/output.  Reading or writing particular
 *   addresses talks to a device rather than ordinary RAM.  The volatile
 *   qualifier tells the C compiler that those accesses have observable
 *   hardware effects and must not be optimized away like normal memory.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import K1 platform declarations and fixed-width integer/address types. */
#include "umicom/kernel/platform.h"

/* QEMU virt's first NS16550-compatible UART MMIO base address. */
#define UMI_QEMU_UART_BASE ((UmiAddress)0x10000000ULL)

/* Transmit Holding Register offset.
 *
 * When DLAB is clear, writing this register sends one serial byte.
 * When DLAB is set, the same offset becomes Divisor Latch Low. */
#define UMI_UART_THR 0U

/* Interrupt Enable Register offset.
 *
 * When DLAB is set, this same offset becomes Divisor Latch High. */
#define UMI_UART_IER 1U

/* FIFO Control Register offset. */
#define UMI_UART_FCR 2U

/* Line Control Register offset. */
#define UMI_UART_LCR 3U

/* Line Status Register offset. */
#define UMI_UART_LSR 5U

/* LSR bit indicating the transmitter can accept another byte. */
#define UMI_UART_LSR_TX_EMPTY ((UmiU8)0x20U)

/* LCR bit exposing divisor-latch registers at offsets 0 and 1. */
#define UMI_UART_LCR_DLAB ((UmiU8)0x80U)

/* LCR value for 8 data bits, no parity and one stop bit ("8N1"). */
#define UMI_UART_LCR_8N1 ((UmiU8)0x03U)

/* Return a volatile pointer to one byte-wide UART register.
 *
 * Keeping address arithmetic in one helper reduces the chance that different
 * functions accidentally use different MMIO bases. */
static volatile UmiU8 *UartRegister(UmiUsize offset)
{
    /* Add the requested register offset to the device base and convert the
     * resulting integer address into an MMIO byte pointer. */
    return (volatile UmiU8 *)(UMI_QEMU_UART_BASE + offset);
}

void UmiPlatformConsoleInitialize(void)
{
    /* Disable UART-generated interrupts.
     *
     * K1 has no trap/interrupt subsystem and intentionally uses polling. */
    *UartRegister(UMI_UART_IER) = (UmiU8)0U;

    /* Set DLAB so offsets 0 and 1 temporarily refer to the baud divisor. */
    *UartRegister(UMI_UART_LCR) = UMI_UART_LCR_DLAB;

    /* Set divisor low byte to 3.
     *
     * Exact host terminal timing is abstracted by QEMU's character backend;
     * this conservative initialization also demonstrates the UART register
     * programming sequence for future real-hardware work. */
    *UartRegister(UMI_UART_THR) = (UmiU8)3U;

    /* Set divisor high byte to zero, making the complete divisor equal to 3. */
    *UartRegister(UMI_UART_IER) = (UmiU8)0U;

    /* Clear DLAB and select the standard 8N1 character format. */
    *UartRegister(UMI_UART_LCR) = UMI_UART_LCR_8N1;

    /* Enable/reset the simple UART FIFOs.
     *
     * 0x07 sets FIFO enable and requests RX/TX FIFO reset in the conventional
     * 16550 FCR layout. */
    *UartRegister(UMI_UART_FCR) = (UmiU8)0x07U;
}

void UmiPlatformConsoleWriteByte(UmiU8 value)
{
    /* Wait until the line-status register reports that the transmitter can
     * accept a byte.
     *
     * Busy polling is intentional at K1: interrupts and scheduling do not exist
     * yet, so there is nothing useful to block/wake a thread. */
    while (
        (*UartRegister(UMI_UART_LSR) & UMI_UART_LSR_TX_EMPTY) == (UmiU8)0U
    ) {
        /* The empty body is deliberate: reading LSR repeatedly is the wait. */
    }

    /* Write the caller's byte into the transmit holding register. */
    *UartRegister(UMI_UART_THR) = value;
}
