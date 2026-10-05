/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/blocking_ipc_abi.h
 *
 * PURPOSE:
 *   Add waiting message operations without changing the nonblocking calls.
 *
 * EDUCATIONAL NOTE:
 *   A wait is one ECALL, even when other programs run before it returns. The
 *   Kernel retains the continuation after that instruction and fills a0 once.
 *   A user address never becomes storage for a Kernel-owned wait record.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_BLOCKING_IPC_ABI_H
#define UMICOM_KERNEL_BLOCKING_IPC_ABI_H
#include "umicom/kernel/message_abi.h"

/* a0=endpoint, a1=user buffer, a2=payload bytes/receive capacity,
 * a3=relative timeout in platform ticks. Original calls keep their numbers. */
#define UMICOM_USER_CALL_MESSAGE_SEND_WAIT 20U
#define UMICOM_USER_CALL_MESSAGE_RECEIVE_WAIT 21U

/* Zero means try once. FOREVER is explicit, not an overflowing finite delay.
 * Finite waits are bounded to one second on the selected 10 MHz QEMU timer. */
#define UMICOM_IPC_WAIT_FOREVER (~(UmicomU64)0U)
#define UMICOM_IPC_WAIT_MAX_TICKS ((UmicomU64)10000000U)
#define UMICOM_IPC_RESULT_TIMED_OUT ((UmicomU64)0x100U)

/* SEND_WAIT snapshots 1..256 bytes before suspension. RECEIVE_WAIT accepts
 * 24..280 bytes of destination capacity and never truncates a packet. Ordinary
 * refusals reuse UmicomKernelMessageStatus. Only timeout adds the result above. */
#endif /* UMICOM_KERNEL_BLOCKING_IPC_ABI_H */
