/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/console_input.h
 *
 * PURPOSE:
 *   Read at most one byte from the early platform console without waiting for
 *   input. A caller can service a running program between polling attempts.
 *
 * EDUCATIONAL NOTE:
 *   No byte available is different from a damaged byte. After an overrun or
 *   line error, a command editor must discard the incomplete command rather
 *   than execute a plausible-looking prefix with missing characters.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_CONSOLE_INPUT_H
#define UMICOM_KERNEL_CONSOLE_INPUT_H
#include "umicom/kernel/types.h"

typedef enum UmicomKernelConsoleInputStatus {
    UMICOM_CONSOLE_INPUT_IDLE,
    UMICOM_CONSOLE_INPUT_BYTE,
    UMICOM_CONSOLE_INPUT_ERROR,
    UMICOM_CONSOLE_INPUT_INVALID_ARGUMENT
} UmicomKernelConsoleInputStatus;

/* The output is changed only for BYTE. This polling API neither enables UART
 * interrupts nor changes baud/divisor registers. Initialise the console first;
 * one trusted Kernel caller owns input. It is not a concurrent terminal driver. */
UmicomKernelConsoleInputStatus UmicomPlatformConsoleTryReadByte(UmicomU8 *outByte);
#endif /* UMICOM_KERNEL_CONSOLE_INPUT_H */
