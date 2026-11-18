/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/tcp_receive.c
 *
 * PURPOSE:
 *   Copy admitted in-order payload into bounded storage before advancing its sequence cursor.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_receive.h"

static UmicomBoolean ReceiveValid(const UmicomTcpReceive *state)
{
    return state != 0 && state->initialized == UMICOM_TRUE &&
        state->head < UMICOM_TCP_RECEIVE_CAPACITY &&
        state->buffered <= UMICOM_TCP_RECEIVE_CAPACITY ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomTcpSequenceDecision UmicomKernelTcpReceiveInit(
    UmicomTcpReceive *state, UmicomU32 first)
{
    if(state == 0) return UMICOM_TCP_SEQUENCE_INVALID;
    for(UmicomU32 i=0U;i<UMICOM_TCP_RECEIVE_CAPACITY;++i) state->bytes[i]=0U;
    state->next=first;
    state->head=0U;
    state->buffered=0U;
    state->initialized=UMICOM_TRUE;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpReceiveAppend(UmicomTcpReceive *state,
    UmicomU32 sequence, const UmicomU8 *bytes, UmicomU32 count,
    UmicomU32 *nextAcknowledgment)
{
    if(ReceiveValid(state)!=UMICOM_TRUE || bytes==0 || nextAcknowledgment==0)
        return UMICOM_TCP_SEQUENCE_INVALID;
    if(count==0U || sequence!=state->next ||
        count>UMICOM_TCP_RECEIVE_CAPACITY-state->buffered)
        return UMICOM_TCP_SEQUENCE_REJECT;
    const UmicomTcpSequenceValue advanced=UmicomKernelTcpSequenceAdvance(state->next,count);
    if(advanced.valid!=UMICOM_TRUE) return UMICOM_TCP_SEQUENCE_INVALID;
    /* Both ring operands are <= capacity, so these additions cannot approach
     * U32 overflow. The sequence service separately owns modulo-2^32 advance. */
    UmicomU32 tail=(state->head+state->buffered)%UMICOM_TCP_RECEIVE_CAPACITY;
    for(UmicomU32 i=0U;i<count;++i) {
        state->bytes[tail]=bytes[i];
        tail=(tail+1U)%UMICOM_TCP_RECEIVE_CAPACITY;
    }
    /* No refusal remains after storage changes begin. Publish the cursor only
     * once every admitted byte has an owned slot, under caller serialization. */
    state->buffered+=count;
    state->next=advanced.value;
    *nextAcknowledgment=advanced.value;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpReceiveRead(UmicomTcpReceive *state,
    UmicomU8 *bytes, UmicomU32 capacity, UmicomU32 *readCount)
{
    if(ReceiveValid(state)!=UMICOM_TRUE || bytes==0 || readCount==0 || capacity==0U)
        return UMICOM_TCP_SEQUENCE_INVALID;
    if(state->buffered==0U) return UMICOM_TCP_SEQUENCE_REJECT;
    const UmicomU32 count=capacity<state->buffered ? capacity : state->buffered;
    UmicomU32 head=state->head;
    for(UmicomU32 i=0U;i<count;++i) {
        bytes[i]=state->bytes[head];
        state->bytes[head]=0U;
        head=(head+1U)%UMICOM_TCP_RECEIVE_CAPACITY;
    }
    state->head=head;
    state->buffered-=count;
    *readCount=count;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
UmicomTcpSequenceDecision UmicomKernelTcpReceiveInspect(
    const UmicomTcpReceive *state, UmicomTcpReceiveSnapshot *snapshot)
{
    if(ReceiveValid(state)!=UMICOM_TRUE || snapshot==0) return UMICOM_TCP_SEQUENCE_INVALID;
    snapshot->next=state->next;
    snapshot->buffered=state->buffered;
    snapshot->available=UMICOM_TCP_RECEIVE_CAPACITY-state->buffered;
    return UMICOM_TCP_SEQUENCE_ACCEPT;
}
