/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/tcp_transmit.h
 *
 * PURPOSE:
 *   Define bounded outbound sequence accounting independently of packet transmission.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_TCP_TRANSMIT_H
#define UMICOM_KERNEL_TCP_TRANSMIT_H
#include "umicom/kernel/tcp_sequence.h"

/* One owner tracks sequence positions reserved for an outbound byte stream.
 * This is bookkeeping, not a connection, socket, packet queue or peer window.
 * The caller retains bytes until acknowledged and handles retries separately.
 * Inspect fields but change them only through the functions below. */
typedef struct UmicomTcpTransmit {
    UmicomU32 oldest, next, capacity;
    UmicomBoolean initialized;
} UmicomTcpTransmit;
typedef struct UmicomTcpTransmitSnapshot {
    UmicomU32 oldest, next, outstanding, available;
} UmicomTcpTransmitSnapshot;

/* Capacity counts sequence positions, including SYN and FIN. It must be
 * 1..MAX_SPAN so ordering never spans half of the 32-bit sequence space.
 * Refused input leaves state untouched. This does not send a SYN. */
UmicomTcpSequenceDecision UmicomKernelTcpTransmitInit(
    UmicomTcpTransmit *state, UmicomU32 first, UmicomU32 capacity);
/* Reserve positive payload+control length and advance next modulo 2^32.
 * Zero sequence length and exhausted capacity return REJECT. Invalid bounds
 * return INVALID. Both leave state and start unchanged. Start is required,
 * must be disjoint from state, and receives the reserved starting sequence.
 * This function does not validate protocol flags or accept an ACK packet. */
UmicomTcpSequenceDecision UmicomKernelTcpTransmitReserve(
    UmicomTcpTransmit *state, UmicomU32 payload, UmicomU8 flags, UmicomU32 *start);
/* Accept only a new acknowledgement within the outstanding interval. The
 * caller must authenticate/admit the packet and validate connection state
 * before using this arithmetic operation. Duplicate/old/future ACKs leave
 * state and released unchanged. Released counts sequence positions, not
 * necessarily payload bytes when SYN/FIN is in the acknowledged interval. */
UmicomTcpSequenceDecision UmicomKernelTcpTransmitAcknowledge(
    UmicomTcpTransmit *state, UmicomU32 ack, UmicomU32 *released);
/* No allocation or mutation. Snapshot must be disjoint from state; refused
 * input preserves it. These counters do not estimate RAM or congestion. */
UmicomTcpSequenceDecision UmicomKernelTcpTransmitInspect(
    const UmicomTcpTransmit *state, UmicomTcpTransmitSnapshot *snapshot);
#endif
