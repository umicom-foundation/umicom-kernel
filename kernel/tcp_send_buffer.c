/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/tcp_send_buffer.c
 *
 * PURPOSE:
 *   Compose sequence accounting with fixed-capacity copied payload ownership and acknowledgment cleanup.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_send_buffer.h"

static UmicomBoolean valid(const UmicomTcpSendBuffer *state, UmicomTcpTransmitSnapshot *snapshot)
{
    if (state == 0 || state->head >= UMICOM_TCP_SEND_CAPACITY ||
        state->sequence.capacity != UMICOM_TCP_SEND_CAPACITY)
        return UMICOM_FALSE;
    return UmicomKernelTcpTransmitInspect(&state->sequence, snapshot) == UMICOM_TCP_SEQUENCE_ACCEPT
        ? UMICOM_TRUE : UMICOM_FALSE;
}
/* Field-wise transfer keeps this small metadata operation independent of a
 * hosted memcpy symbol, including unoptimized freestanding toolchains. */
static void copy_sequence(UmicomTcpTransmit *to, const UmicomTcpTransmit *from)
{
    to->oldest = from->oldest;
    to->next = from->next;
    to->capacity = from->capacity;
    to->initialized = from->initialized;
}
UmicomTcpSequenceDecision UmicomKernelTcpSendInit(UmicomTcpSendBuffer *state, UmicomU32 first)
{
    if (state == 0) return UMICOM_TCP_SEQUENCE_INVALID;
    (void)UmicomKernelTcpTransmitInit(&state->sequence, first, UMICOM_TCP_SEND_CAPACITY);
    state->head = 0U;
    for (UmicomU32 i = 0U; i < UMICOM_TCP_SEND_CAPACITY; ++i) state->bytes[i] = 0U;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpSendAppend(
    UmicomTcpSendBuffer *state, const UmicomU8 *bytes, UmicomU32 count, UmicomU32 *start)
{
    UmicomTcpTransmitSnapshot snapshot;
    if (bytes == 0 || start == 0 || valid(state, &snapshot) != UMICOM_TRUE)
        return UMICOM_TCP_SEQUENCE_INVALID;
    UmicomTcpTransmit candidate;
    UmicomU32 beginning;
    copy_sequence(&candidate, &state->sequence);
    const UmicomTcpSequenceDecision decision =
        UmicomKernelTcpTransmitReserve(&candidate, count, 0U, &beginning);
    if (decision != UMICOM_TCP_SEQUENCE_ACCEPT) return decision;
    /* Admission bounds count before the first source read. Copy first so a
     * published sequence interval never promises bytes that we do not own. */
    for (UmicomU32 i = 0U; i < count; ++i)
        state->bytes[(state->head + snapshot.outstanding + i) % UMICOM_TCP_SEND_CAPACITY] = bytes[i];
    copy_sequence(&state->sequence, &candidate);
    *start = beginning;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpSendCopy(
    const UmicomTcpSendBuffer *state, UmicomU32 sequence,
    UmicomU8 *output, UmicomU32 capacity, UmicomU32 *copied)
{
    UmicomTcpTransmitSnapshot snapshot;
    if (output == 0 || copied == 0 || valid(state, &snapshot) != UMICOM_TRUE)
        return UMICOM_TCP_SEQUENCE_INVALID;
    /* The admitted interval is at most 1024 bytes, well below half-space.
     * Unsigned subtraction maps stale/future positions outside that interval,
     * including when the live interval itself crosses sequence zero. */
    const UmicomU32 offset = sequence - snapshot.oldest;
    if (capacity == 0U || offset >= snapshot.outstanding) return UMICOM_TCP_SEQUENCE_REJECT;
    const UmicomU32 remaining = snapshot.outstanding - offset;
    const UmicomU32 count = capacity < remaining ? capacity : remaining;
    for (UmicomU32 i = 0U; i < count; ++i)
        output[i] = state->bytes[(state->head + offset + i) % UMICOM_TCP_SEND_CAPACITY];
    *copied = count;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpSendAcknowledge(
    UmicomTcpSendBuffer *state, UmicomU32 ack, UmicomU32 *released)
{
    UmicomTcpTransmitSnapshot snapshot;
    if (released == 0 || valid(state, &snapshot) != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    UmicomTcpTransmit candidate;
    UmicomU32 count;
    copy_sequence(&candidate, &state->sequence);
    const UmicomTcpSequenceDecision decision =
        UmicomKernelTcpTransmitAcknowledge(&candidate, ack, &count);
    if (decision != UMICOM_TCP_SEQUENCE_ACCEPT) return decision;
    for (UmicomU32 i = 0U; i < count; ++i)
        state->bytes[(state->head + i) % UMICOM_TCP_SEND_CAPACITY] = 0U;
    state->head = (state->head + count) % UMICOM_TCP_SEND_CAPACITY;
    copy_sequence(&state->sequence, &candidate);
    *released = count;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpSendInspect(
    const UmicomTcpSendBuffer *state, UmicomTcpTransmitSnapshot *snapshot)
{
    UmicomTcpTransmitSnapshot checked;
    if (snapshot == 0 || valid(state, &checked) != UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    return UmicomKernelTcpTransmitInspect(&state->sequence, snapshot);
}
