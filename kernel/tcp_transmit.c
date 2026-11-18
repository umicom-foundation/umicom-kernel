/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/tcp_transmit.c
 *
 * PURPOSE:
 *   Retain a bounded outstanding sequence interval and release only newly acknowledged positions.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/tcp_transmit.h"

static UmicomBoolean valid(const UmicomTcpTransmit *state)
{
    return state != 0 && state->initialized == UMICOM_TRUE &&
        state->capacity != 0U && state->capacity < UMICOM_TCP_SEQUENCE_HALF &&
        state->next - state->oldest <= state->capacity ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomTcpSequenceDecision UmicomKernelTcpTransmitInit(
    UmicomTcpTransmit *state, UmicomU32 first, UmicomU32 capacity)
{
    if (state == 0 || capacity == 0U || capacity >= UMICOM_TCP_SEQUENCE_HALF)
        return UMICOM_TCP_SEQUENCE_INVALID;
    state->oldest = first;
    state->next = first;
    state->capacity = capacity;
    state->initialized = UMICOM_TRUE;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpTransmitReserve(
    UmicomTcpTransmit *state, UmicomU32 payload, UmicomU8 flags, UmicomU32 *start)
{
    if (start == 0 || valid(state) != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    const UmicomTcpSequenceValue length = UmicomKernelTcpSequenceLength(payload, flags);
    if (length.valid != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    const UmicomU32 outstanding = state->next - state->oldest;
    /* Subtract only after admitting the invariant; a huge request cannot wrap
     * into a small reservation. Reuse the sequence service for modulo advance. */
    if (length.value == 0U || length.value > state->capacity - outstanding)
        return UMICOM_TCP_SEQUENCE_REJECT;
    const UmicomTcpSequenceValue advanced = UmicomKernelTcpSequenceAdvance(state->next, length.value);
    if (advanced.valid != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    *start = state->next;
    state->next = advanced.value;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpTransmitAcknowledge(
    UmicomTcpTransmit *state, UmicomU32 ack, UmicomU32 *released)
{
    if (released == 0 || valid(state) != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    const UmicomTcpSequenceDecision decision =
        UmicomKernelTcpNewAcknowledgment(state->oldest, state->next, ack);
    if (decision != UMICOM_TCP_SEQUENCE_ACCEPT) return decision;
    *released = ack - state->oldest;
    state->oldest = ack;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpTransmitInspect(
    const UmicomTcpTransmit *state, UmicomTcpTransmitSnapshot *snapshot)
{
    if (snapshot == 0 || valid(state) != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    snapshot->oldest = state->oldest;
    snapshot->next = state->next;
    snapshot->outstanding = state->next - state->oldest;
    snapshot->available = state->capacity - snapshot->outstanding;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
