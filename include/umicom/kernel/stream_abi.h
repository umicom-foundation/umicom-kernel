/*-----------------------------------------------------------------------------
 * Umicom Kernel native standard-stream contract
 * File: include/umicom/kernel/stream_abi.h
 *
 * A selector names one of the three directions granted by Kernel admission.
 * These selectors are not VFS descriptor tokens: this small native interface
 * has no descriptor inheritance, dup2, FILE object or POSIX compatibility claim.
 * Programs provide only an address and a count, never a Kernel stream owner.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_STREAM_ABI_H
#define UMICOM_KERNEL_STREAM_ABI_H
#ifndef __ASSEMBLER__
#include "umicom/kernel/types.h"
#endif

#define UMICOM_USER_CALL_STREAM_READ 40U
#define UMICOM_USER_CALL_STREAM_WRITE 41U
#define UMICOM_STREAM_INPUT 0U
#define UMICOM_STREAM_OUTPUT 1U
#define UMICOM_STREAM_ERROR 2U
#define UMICOM_STREAM_NONBLOCK 1U
#define UMICOM_STREAM_TRANSFER_BYTES 256U

/* Status is returned in a0 and the accepted byte count in a1. Other registers
 * retain their values. In particular, a1 is an output here, not the original
 * buffer address. EOF is distinct from a temporarily empty input queue. */
#ifndef __ASSEMBLER__
typedef enum UmicomKernelStreamStatus {
    UMICOM_STREAM_OK,
    UMICOM_STREAM_INVALID_ARGUMENT,
    UMICOM_STREAM_BAD_STATE,
    UMICOM_STREAM_UNBOUND,
    UMICOM_STREAM_WRONG_DIRECTION,
    UMICOM_STREAM_BAD_BUFFER,
    UMICOM_STREAM_TOO_LARGE,
    UMICOM_STREAM_WOULD_BLOCK,
    UMICOM_STREAM_EOF,
    UMICOM_STREAM_INVALID_HANDLE,
    UMICOM_STREAM_CORRUPT_STATE
} UmicomKernelStreamStatus;
#endif

/* a7 = READ or WRITE, a0 = selector, a1 = address, a2 = bytes, a3 = flags.
 * Only NONBLOCK is defined. Zero bytes is a successful no-op after authority
 * and direction checks; it never follows the address or blocks.
 *
 * READ returns an available prefix, at most the requested count. With no data,
 * a blocking read suspends until input or EOF. EOF follows buffered input.
 * WRITE accepts one complete bounded chunk or none. A full output queue blocks
 * unless NONBLOCK is requested. Accepted bytes belong to the Kernel, not to the
 * caller's source buffer. A cancelled pending write was not accepted.
 *
 * There is no wall-clock timeout here. A trusted controller can cancel a waiting
 * task; the existing syscall and execution budgets continue across resumptions.
 */
#endif /* UMICOM_KERNEL_STREAM_ABI_H */
