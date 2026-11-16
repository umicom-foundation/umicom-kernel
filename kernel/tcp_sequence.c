/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/tcp_sequence.c
 *
 * PURPOSE:
 *   Use unsigned modulo arithmetic to decide bounded TCP sequence relations without allocation or implementation-defined signed casts.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/tcp_sequence.h"

UmicomTcpSequenceOrder UmicomKernelTcpSequenceCompare(UmicomU32 left, UmicomU32 right)
{
    const UmicomU32 distance = left - right;
    if (distance == 0U) return UMICOM_TCP_SEQUENCE_EQUAL;
    if (distance == UMICOM_TCP_SEQUENCE_HALF) return UMICOM_TCP_SEQUENCE_AMBIGUOUS;
    return distance < UMICOM_TCP_SEQUENCE_HALF ? UMICOM_TCP_SEQUENCE_AFTER : UMICOM_TCP_SEQUENCE_BEFORE;
}
UmicomTcpSequenceValue UmicomKernelTcpSequenceAdvance(UmicomU32 sequence, UmicomU32 amount)
{
    UmicomTcpSequenceValue result = {UMICOM_FALSE, 0U};
    if (amount >= UMICOM_TCP_SEQUENCE_HALF) return result;
    result.value = sequence + amount;
    result.valid = UMICOM_TRUE;
    return result;
}
UmicomTcpSequenceValue UmicomKernelTcpSequenceLength(UmicomU32 payload, UmicomU8 flags)
{
    UmicomTcpSequenceValue result = {UMICOM_FALSE, 0U};
    const UmicomU32 controls = ((flags & 2U) != 0U ? 1U : 0U) + ((flags & 1U) != 0U ? 1U : 0U);
    /* Check before addition; untrusted lengths cannot wrap into admission. */
    if (payload > UMICOM_TCP_SEQUENCE_MAX_SPAN - controls) return result;
    result.value = payload + controls;
    result.valid = UMICOM_TRUE;
    return result;
}
UmicomTcpSequenceDecision UmicomKernelTcpNewAcknowledgment(
    UmicomU32 una, UmicomU32 next, UmicomU32 ack)
{
    const UmicomU32 outstanding = next - una;
    const UmicomU32 acknowledged = ack - una;
    if (outstanding >= UMICOM_TCP_SEQUENCE_HALF) return UMICOM_TCP_SEQUENCE_INVALID;
    return acknowledged != 0U && acknowledged <= outstanding ? UMICOM_TCP_SEQUENCE_ACCEPT : UMICOM_TCP_SEQUENCE_REJECT;
}
static UmicomBoolean sequence_in_window(UmicomU32 sequence, UmicomU32 next, UmicomU32 window)
{
    return sequence - next < window ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomTcpSequenceDecision UmicomKernelTcpReceiveSequence(
    UmicomU32 next, UmicomU32 window, UmicomU32 sequence, UmicomU32 length)
{
    if (window >= UMICOM_TCP_SEQUENCE_HALF || length >= UMICOM_TCP_SEQUENCE_HALF)
        return UMICOM_TCP_SEQUENCE_INVALID;
    if (window == 0U)
        return length == 0U && sequence == next ? UMICOM_TCP_SEQUENCE_ACCEPT : UMICOM_TCP_SEQUENCE_REJECT;
    if (sequence_in_window(sequence, next, window) == UMICOM_TRUE)
        return UMICOM_TCP_SEQUENCE_ACCEPT;
    if (length != 0U && sequence_in_window(sequence + length - 1U, next, window) == UMICOM_TRUE)
        return UMICOM_TCP_SEQUENCE_ACCEPT;
    return UMICOM_TCP_SEQUENCE_REJECT;
}
