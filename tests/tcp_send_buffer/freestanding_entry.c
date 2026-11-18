/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_send_buffer/freestanding_entry.c
 *
 * PURPOSE:
 *   Link every send-buffer operation for RV64 without a hosted C library.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_send_buffer.h"
static UmicomTcpSendBuffer sender;
static UmicomU8 payload[2]={11U,22U}, output[2];
static UmicomTcpTransmitSnapshot snapshot;
volatile UmicomU32 UmicomSendLinkResult;
void _start(void)
{
    UmicomU32 start=0U,count=0U;
    UmicomSendLinkResult=(UmicomU32)UmicomKernelTcpSendInit(&sender,0xffffffffU);
    UmicomSendLinkResult+=(UmicomU32)UmicomKernelTcpSendAppend(&sender,payload,2U,&start);
    UmicomSendLinkResult+=(UmicomU32)UmicomKernelTcpSendCopy(&sender,start,output,2U,&count);
    UmicomSendLinkResult+=(UmicomU32)UmicomKernelTcpSendAcknowledge(&sender,1U,&count);
    UmicomSendLinkResult+=(UmicomU32)UmicomKernelTcpSendInspect(&sender,&snapshot);
    UmicomSendLinkResult+=start+count+snapshot.available+output[1];
    for(;;) { }
}
