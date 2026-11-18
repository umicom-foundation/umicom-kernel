/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/tcp_receive.h
 *
 * PURPOSE:
 *   Define a bounded contiguous-payload receive owner without implementing a TCP connection.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_TCP_RECEIVE_H
#define UMICOM_KERNEL_TCP_RECEIVE_H
#include "umicom/kernel/tcp_sequence.h"

#define UMICOM_TCP_RECEIVE_CAPACITY 1024U

/* A single serialized owner retains payload bytes, not packets. Public fields
 * are inspectable for diagnostics but must be changed only through this API.
 * No caller pointer is retained; each successful append copies its payload. */
typedef struct UmicomTcpReceive {
    UmicomU32 next, head, buffered;
    UmicomBoolean initialized;
    UmicomU8 bytes[UMICOM_TCP_RECEIVE_CAPACITY];
} UmicomTcpReceive;

typedef struct UmicomTcpReceiveSnapshot {
    UmicomU32 next, buffered, available;
} UmicomTcpReceiveSnapshot;

/* Begin a new lifetime at a caller-admitted first payload sequence. This clears
 * all slots and deliberately discards any earlier buffered data. No handshake
 * or SYN processing occurs; first is already the first expected data byte. */
UmicomTcpSequenceDecision UmicomKernelTcpReceiveInit(
    UmicomTcpReceive *state, UmicomU32 first);

/* Copy positive, in-order payload only. sequence must equal state.next and the
 * complete payload must fit; duplicates, gaps, zero length and full capacity
 * return REJECT. Invalid state/pointers return INVALID. All refusals preserve
 * state and nextAcknowledgment. Success advances next modulo 2^32 only after
 * copying all bytes. The output is a next-byte cursor, not a transmitted ACK.
 *
 * The caller must first validate packet bounds, checksum, connection identity,
 * flags and protocol state. This helper has no SYN/FIN/RST/URG, overlap trimming,
 * out-of-order queue, window scaling, timers, congestion or socket behavior.
 * Source is readable for count bytes and disjoint from state and output. The
 * required output is writable and disjoint from state. Keep access serialized.
 */
UmicomTcpSequenceDecision UmicomKernelTcpReceiveAppend(UmicomTcpReceive *state,
    UmicomU32 sequence, const UmicomU8 *bytes, UmicomU32 count,
    UmicomU32 *nextAcknowledgment);

/* Copy and consume min(capacity, buffered) bytes in stream order, then zero
 * each consumed slot. Empty storage returns REJECT. Null arguments, zero output
 * capacity or malformed state return INVALID. Refusals preserve state, output
 * bytes and readCount. Success leaves the output tail unchanged and does not
 * change the next-byte sequence. The caller owns a valid capacity-byte output
 * span and a disjoint count output, both disjoint from state. Zeroing does not
 * promise forensic erasure of all copies or secret-safe compiler behavior. */
UmicomTcpSequenceDecision UmicomKernelTcpReceiveRead(UmicomTcpReceive *state,
    UmicomU8 *bytes, UmicomU32 capacity, UmicomU32 *readCount);

/* Read-only copied counters. Snapshot is required, writable and disjoint from
 * state. Refusal leaves it untouched. available is local storage room, not an
 * automatically negotiated TCP receive window or an estimate of total RAM. */
UmicomTcpSequenceDecision UmicomKernelTcpReceiveInspect(
    const UmicomTcpReceive *state, UmicomTcpReceiveSnapshot *snapshot);
#endif
