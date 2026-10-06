/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/serial.c
 *
 * PURPOSE:
 *   Drive the NS16550-compatible UART exposed by QEMU's RISC-V `virt` machine.
 *
 * EDUCATIONAL OVERVIEW:
 *   MMIO means memory-mapped input/output: a load or store to a particular
 *   physical address communicates with a device rather than ordinary RAM.
 *
 *   The `volatile` qualifier is essential for these pointers.  A compiler is
 *   normally free to remove or combine ordinary memory accesses when it can
 *   prove the program does not need them.  Device registers have effects
 *   outside the C abstract machine, so each access must remain visible in the
 *   generated instructions.
 *
 *   The early console deliberately polls the UART instead of using interrupts.
 *   Polling works before a scheduler or interrupt-driven console service exists
 *   and gives trap/paging failures a small diagnostic path with few dependencies.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import the first-boot foundation platform declarations and fixed-width integer/address types. */
#include "umicom/kernel/platform.h"

/* QEMU `virt` maps its first NS16550-compatible UART at this physical address. */
#define UMICOM_QEMU_UART_BASE ((UmicomAddress)0x10000000ULL)

/* Transmit Holding Register offset.
 *
 * With DLAB clear, writing this byte queues one character for transmission.
 * With DLAB set, the same address selects the divisor low byte instead. */
#define UMICOM_UART_THR 0U

/* Interrupt Enable Register offset.
 *
 * With DLAB set, this address becomes the divisor high byte. */
#define UMICOM_UART_IER 1U

/* FIFO Control Register offset. */
#define UMICOM_UART_FCR 2U

/* Line Control Register offset. */
#define UMICOM_UART_LCR 3U

/* Line Status Register offset. */
#define UMICOM_UART_LSR 5U

/* LSR bit indicating the transmitter can accept another byte. */
#define UMICOM_UART_LSR_TX_EMPTY ((UmicomU8)0x20U)

/* LCR bit exposing the divisor-latch registers at offsets 0 and 1. */
#define UMICOM_UART_LCR_DLAB ((UmicomU8)0x80U)

/* LCR value for eight data bits, no parity and one stop bit (8N1). */
#define UMICOM_UART_LCR_8N1 ((UmicomU8)0x03U)

/* Convert a register offset into a volatile byte-wide MMIO pointer.
 *
 * Keeping the address calculation in one helper prevents separate console
 * operations from silently drifting to different device bases. */
static volatile UmicomU8 *UartRegister(UmicomSize offset)
{
    /* Add the requested register offset to the device base and convert the
     * resulting integer address into an MMIO byte pointer. */
    return (volatile UmicomU8 *)(UMICOM_QEMU_UART_BASE + offset);
}

void UmicomPlatformConsoleInitialize(void)
{
    /* Disable UART-generated interrupts.
     *
     * the first-boot foundation has no trap/interrupt subsystem and intentionally uses polling. */
    *UartRegister(UMICOM_UART_IER) = (UmicomU8)0U;

    /* Set DLAB so offsets 0 and 1 temporarily refer to the baud divisor. */
    *UartRegister(UMICOM_UART_LCR) = UMICOM_UART_LCR_DLAB;

    /* Set divisor low byte to 3.
     *
     * Exact host terminal timing is abstracted by QEMU's character backend;
     * this conservative initialization also demonstrates the UART register
     * programming sequence for future real-hardware work. */
    *UartRegister(UMICOM_UART_THR) = (UmicomU8)3U;

    /* Set divisor high byte to zero, making the complete divisor equal to 3. */
    *UartRegister(UMICOM_UART_IER) = (UmicomU8)0U;

    /* Clear DLAB and select the standard 8N1 character format. */
    *UartRegister(UMICOM_UART_LCR) = UMICOM_UART_LCR_8N1;

    /* Enable/reset the simple UART FIFOs.
     *
     * 0x07 sets FIFO enable and requests RX/TX FIFO reset in the conventional
     * 16550 FCR layout. */
    *UartRegister(UMICOM_UART_FCR) = (UmicomU8)0x07U;
}

void UmicomPlatformConsoleWriteByte(UmicomU8 value)
{
    /* Wait until the line-status register reports that the transmitter can
     * accept a byte.
     *
     * Busy polling is intentional here: this path must remain usable when
     * scheduling, interrupt delivery or a higher-level console service is the
     * subsystem currently being diagnosed. */
    while (
        (*UartRegister(UMICOM_UART_LSR) & UMICOM_UART_LSR_TX_EMPTY) ==
        (UmicomU8)0U
    ) {
        /* Reading the line-status register repeatedly is the complete wait. */
    }

    /* Writing THR sends exactly the byte supplied by the caller. */
    *UartRegister(UMICOM_UART_THR) = value;
}

/* Polling input shares the established UART register helper and initialisation.
 * Output above is unchanged; input does not install an IRQ handler or alter DLAB. */
#include "umicom/kernel/console_input.h"

UmicomKernelConsoleInputStatus UmicomPlatformConsoleTryReadByte(UmicomU8 *outByte)
{
    if (!outByte) return UMICOM_CONSOLE_INPUT_INVALID_ARGUMENT;
    /* Read LSR once. DR says a byte is available; OE/PE/FE/BI say input may be
     * incomplete or damaged. Even an apparently printable byte is unsafe as
     * part of a command after an overrun, so the editor must discard the line. */
    const UmicomU8 status = *UartRegister(UMICOM_UART_LSR);
    const UmicomU8 dataReady = (UmicomU8)0x01U;
    const UmicomU8 lineErrors = (UmicomU8)0x1eU;
    if ((status & lineErrors) != 0U) {
        if ((status & dataReady) != 0U) {
            /* RBR shares offset zero with THR. Drain one damaged receive byte;
             * the volatile read must happen even though its value is discarded. */
            const UmicomU8 discarded = *UartRegister(0U);
            (void)discarded;
        }
        return UMICOM_CONSOLE_INPUT_ERROR;
    }
    if ((status & dataReady) == 0U) return UMICOM_CONSOLE_INPUT_IDLE;
    *outByte = *UartRegister(0U); /* Only BYTE publishes a new output value. */
    return UMICOM_CONSOLE_INPUT_BYTE;
}
