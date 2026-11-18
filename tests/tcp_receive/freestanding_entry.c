/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_receive/freestanding_entry.c
 *
 * PURPOSE:
 *   Link every receive operation for RV64 without assuming a hosted C library.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_receive.h"
static UmicomTcpReceive receiver;
static UmicomU8 payload[2] = {11U, 22U};
static UmicomU8 output[2];
static UmicomTcpReceiveSnapshot snapshot;
volatile UmicomU32 UmicomReceiveLinkResult;
void _start(void)
{
    UmicomU32 cursor=0U, count=0U;
    UmicomReceiveLinkResult=(UmicomU32)UmicomKernelTcpReceiveInit(&receiver,0xffffffffU);
    UmicomReceiveLinkResult+=(UmicomU32)UmicomKernelTcpReceiveAppend(&receiver,0xffffffffU,payload,2U,&cursor);
    UmicomReceiveLinkResult+=(UmicomU32)UmicomKernelTcpReceiveRead(&receiver,output,2U,&count);
    UmicomReceiveLinkResult+=(UmicomU32)UmicomKernelTcpReceiveInspect(&receiver,&snapshot);
    UmicomReceiveLinkResult+=cursor+count+snapshot.available+output[1];
    for(;;) { }
}
