/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/serial.c
 *
 * PURPOSE:
 *   Drive the QEMU RISC-V virt machine's NS16550-compatible UART directly via
 *   MMIO for K1 serial boot evidence. This is machine-specific code and does
 *   not leak the UART address into generic kernel services.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/platform.h"

#define UMI_QEMU_UART_BASE ((UmiAddress)0x10000000ULL)
#define UMI_UART_THR 0U
#define UMI_UART_IER 1U
#define UMI_UART_FCR 2U
#define UMI_UART_LCR 3U
#define UMI_UART_LSR 5U
#define UMI_UART_LSR_TX_EMPTY 0x20U
#define UMI_UART_LCR_DLAB 0x80U
#define UMI_UART_LCR_8N1 0x03U

static volatile UmiU8 *UartRegister(UmiUsize offset)
{
    return (volatile UmiU8 *)(UMI_QEMU_UART_BASE + offset);
}

void UmiPlatformConsoleInitialize(void)
{
    /* Disable UART interrupts. K1 uses polling only. */
    *UartRegister(UMI_UART_IER) = 0U;

    /* Program a conservative divisor, then return to 8 data bits, no parity,
     * one stop bit. QEMU presents this as a character device to the host. */
    *UartRegister(UMI_UART_LCR) = UMI_UART_LCR_DLAB;
    *UartRegister(UMI_UART_THR) = 3U;
    *UartRegister(UMI_UART_IER) = 0U;
    *UartRegister(UMI_UART_LCR) = UMI_UART_LCR_8N1;
    *UartRegister(UMI_UART_FCR) = 0x07U;
}

void UmiPlatformConsoleWriteByte(UmiU8 value)
{
    while ((*UartRegister(UMI_UART_LSR) & UMI_UART_LSR_TX_EMPTY) == 0U) {
        /* Polling is intentional during K1 before interrupts/scheduling exist. */
    }

    *UartRegister(UMI_UART_THR) = value;
}
