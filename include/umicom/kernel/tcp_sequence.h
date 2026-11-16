/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/tcp_sequence.h
 *
 * PURPOSE:
 *   Define bounded TCP sequence arithmetic and read-only acknowledgement/window decisions without host services.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_TCP_SEQUENCE_H
#define UMICOM_KERNEL_TCP_SEQUENCE_H
#include "umicom/kernel/types.h"

/* Ordering is meaningful only inside a half space. Opposite points have no
 * selected order. These results are not ordinary signed integer comparisons. */
typedef enum UmicomTcpSequenceOrder {
    UMICOM_TCP_SEQUENCE_BEFORE = -1,
    UMICOM_TCP_SEQUENCE_EQUAL = 0,
    UMICOM_TCP_SEQUENCE_AFTER = 1,
    UMICOM_TCP_SEQUENCE_AMBIGUOUS = 2
} UmicomTcpSequenceOrder;
typedef enum UmicomTcpSequenceDecision {
    UMICOM_TCP_SEQUENCE_INVALID = -1,
    UMICOM_TCP_SEQUENCE_REJECT = 0,
    UMICOM_TCP_SEQUENCE_ACCEPT = 1
} UmicomTcpSequenceDecision;
typedef struct UmicomTcpSequenceValue {
    UmicomBoolean valid;
    UmicomU32 value; /* Zero when invalid, never a partially computed value. */
} UmicomTcpSequenceValue;

#define UMICOM_TCP_SEQUENCE_HALF ((UmicomU32)0x80000000U)
#define UMICOM_TCP_SEQUENCE_MAX_SPAN ((UmicomU32)0x7fffffffU)

UmicomTcpSequenceOrder UmicomKernelTcpSequenceCompare(UmicomU32 left, UmicomU32 right);
/* Advance is modulo 2^32, but amounts >= half a sequence space are refused. */
UmicomTcpSequenceValue UmicomKernelTcpSequenceAdvance(UmicomU32 sequence, UmicomU32 amount);
/* Payload plus SYN(0x02) and FIN(0x01). Other flag bits occupy no sequence
 * positions. This is arithmetic only, not validation of flag combinations. */
UmicomTcpSequenceValue UmicomKernelTcpSequenceLength(UmicomU32 payload, UmicomU8 flags);
/* New acknowledgements only: una < ack <= next over an outstanding span less
 * than half the sequence space. Duplicate/old ACKs are rejected, not acted on. */
UmicomTcpSequenceDecision UmicomKernelTcpNewAcknowledgment(
    UmicomU32 una, UmicomU32 next, UmicomU32 ack);
/* RFC 9293 section 3.4 endpoint test, including its four zero-length/window
 * cases. Both lengths must be below half space. A segment surrounding the
 * entire window with neither endpoint inside is rejected by this predicate.
 * This does not admit a packet, process RST/URG, trim bytes or advance state. */
UmicomTcpSequenceDecision UmicomKernelTcpReceiveSequence(
    UmicomU32 next, UmicomU32 window, UmicomU32 sequence, UmicomU32 length);
#endif
