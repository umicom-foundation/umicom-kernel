/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/tcp_send_buffer.h
 *
 * PURPOSE:
 *   Own a bounded payload interval independently of caller buffers and packet transmission.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_TCP_SEND_BUFFER_H
#define UMICOM_KERNEL_TCP_SEND_BUFFER_H
#include "umicom/kernel/tcp_transmit.h"
#define UMICOM_TCP_SEND_CAPACITY 1024U

/* One owner retains payload until a newly admitted ACK releases it. The
 * existing transmit service owns sequence arithmetic; this layer adds bytes.
 * Inspect fields if needed, but mutate them only through this interface.
 * No allocation, sockets, device access or connection state is involved.
 * Only payload is represented: this buffer does not reserve SYN/FIN positions.
 * All pointer arguments must refer to stable, disjoint, suitably sized objects.
 * The caller serializes access; these functions are not interrupt-safe locks. */
typedef struct UmicomTcpSendBuffer {
    UmicomTcpTransmit sequence;
    UmicomU32 head;
    UmicomU8 bytes[UMICOM_TCP_SEND_CAPACITY];
} UmicomTcpSendBuffer;

/* Initialize an empty stream at any 32-bit sequence and clear owned storage.
 * Reinitializing discards retained payload; the caller must deliberately end
 * the old ownership interval first. A null state returns INVALID. */
UmicomTcpSequenceDecision UmicomKernelTcpSendInit(
    UmicomTcpSendBuffer *state, UmicomU32 first);
/* Copy a positive payload span, then publish its reserved starting sequence.
 * Count zero or insufficient room returns REJECT. Invalid arguments/bounds
 * return INVALID. No refused call reads payload or changes state/start.
 * Success does not mean that a packet was sent or acknowledged. */
UmicomTcpSequenceDecision UmicomKernelTcpSendAppend(
    UmicomTcpSendBuffer *state, const UmicomU8 *bytes, UmicomU32 count, UmicomU32 *start);
/* Copy up to capacity retained bytes beginning at sequence. Repeated copies
 * return the same bytes until ACK/reinitialization releases them. This does
 * not consume storage or change accounting. Empty, old, future and endpoint
 * positions return REJECT, as does zero capacity. Refusal preserves output
 * and copied. On success only copied bytes are written, leaving the tail alone.
 * Output capacity is a caller memory contract, not an allocation request. */
UmicomTcpSequenceDecision UmicomKernelTcpSendCopy(
    const UmicomTcpSendBuffer *state, UmicomU32 sequence,
    UmicomU8 *output, UmicomU32 capacity, UmicomU32 *copied);
/* Release only a new ACK inside the outstanding payload interval and clear
 * those slots before reusing them. Caller must authenticate/admit the packet
 * and validate the connection separately. Refused ACKs preserve all state
 * and released. Clearing owned slots is not a cryptographic erasure guarantee. */
UmicomTcpSequenceDecision UmicomKernelTcpSendAcknowledge(
    UmicomTcpSendBuffer *state, UmicomU32 ack, UmicomU32 *released);
/* Report the payload interval and free capacity without mutation. Refusal
 * preserves snapshot. Counters describe this buffer, not peer or NIC state. */
UmicomTcpSequenceDecision UmicomKernelTcpSendInspect(
    const UmicomTcpSendBuffer *state, UmicomTcpTransmitSnapshot *snapshot);
#endif
