/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_transmit/transmit_tests.c
 *
 * PURPOSE:
 *   Exercise wraparound, capacity, new acknowledgements and failure atomicity against a bounded count model.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/tcp_transmit.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static int cycle(void)
{
    UmicomTcpTransmit state; UmicomU32 start=99,released=99; UmicomTcpTransmitSnapshot s;
    CHECK(UmicomKernelTcpTransmitInit(&state,100,8)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpTransmitReserve(&state,4,2,&start)==UMICOM_TCP_SEQUENCE_ACCEPT && start==100);
    CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(s.oldest==100 && s.next==105 && s.outstanding==5 && s.available==3);
    CHECK(UmicomKernelTcpTransmitAcknowledge(&state,103,&released)==UMICOM_TCP_SEQUENCE_ACCEPT && released==3);
    CHECK(UmicomKernelTcpTransmitReserve(&state,4,1,&start)==UMICOM_TCP_SEQUENCE_ACCEPT && start==105);
    CHECK(UmicomKernelTcpTransmitAcknowledge(&state,110,&released)==UMICOM_TCP_SEQUENCE_ACCEPT && released==7);
    CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_ACCEPT && s.available==8 && s.outstanding==0);
    return 0;
}
static int refusal(void)
{
    UmicomTcpTransmit state={0},before; UmicomU32 output=99; UmicomTcpTransmitSnapshot s={1,2,3,4};
    before=state;
    CHECK(UmicomKernelTcpTransmitInit(&state,5,0)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpTransmitInit(&state,5,0x80000000U)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(memcmp(&before,&state,sizeof state)==0);
    CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_INVALID && s.available==4);
    CHECK(UmicomKernelTcpTransmitReserve(NULL,1,0,&output)==UMICOM_TCP_SEQUENCE_INVALID && output==99);
    CHECK(UmicomKernelTcpTransmitAcknowledge(NULL,1,&output)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpTransmitInit(&state,10,4)==UMICOM_TCP_SEQUENCE_ACCEPT);
    before=state;
    CHECK(UmicomKernelTcpTransmitReserve(&state,0,0,&output)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpTransmitReserve(&state,5,0,&output)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpTransmitReserve(&state,0xffffffffU,3,&output)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpTransmitReserve(&state,1,0,NULL)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(output==99 && memcmp(&state,&before,sizeof state)==0);
    CHECK(UmicomKernelTcpTransmitReserve(&state,4,0,&output)==UMICOM_TCP_SEQUENCE_ACCEPT);
    before=state; output=99;
    const UmicomU32 acks[]={9,10,15,0x8000000aU,0xffffffffU};
    for(unsigned i=0;i<sizeof acks/sizeof acks[0];++i) {
        CHECK(UmicomKernelTcpTransmitAcknowledge(&state,acks[i],&output)==UMICOM_TCP_SEQUENCE_REJECT);
        CHECK(output==99 && memcmp(&state,&before,sizeof state)==0);
    }
    CHECK(UmicomKernelTcpTransmitReserve(&state,1,0,&output)==UMICOM_TCP_SEQUENCE_REJECT);
    state.next=state.oldest+5; before=state;
    CHECK(UmicomKernelTcpTransmitReserve(&state,1,0,&output)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpTransmitAcknowledge(&state,11,&output)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(memcmp(&state,&before,sizeof state)==0);
    return 0;
}
static int wrap(void)
{
    UmicomTcpTransmit state; UmicomTcpTransmitSnapshot s; UmicomU32 n;
    CHECK(UmicomKernelTcpTransmitInit(&state,0xfffffffeU,8)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpTransmitReserve(&state,4,2,&n)==UMICOM_TCP_SEQUENCE_ACCEPT && n==0xfffffffeU);
    CHECK(UmicomKernelTcpTransmitAcknowledge(&state,0,&n)==UMICOM_TCP_SEQUENCE_ACCEPT && n==2);
    CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_ACCEPT && s.next==3 && s.outstanding==3);
    CHECK(UmicomKernelTcpTransmitAcknowledge(&state,3,&n)==UMICOM_TCP_SEQUENCE_ACCEPT && n==3);
    CHECK(UmicomKernelTcpTransmitInit(&state,3,UMICOM_TCP_SEQUENCE_MAX_SPAN)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpTransmitReserve(&state,UMICOM_TCP_SEQUENCE_MAX_SPAN-2,3,&n)==UMICOM_TCP_SEQUENCE_ACCEPT && n==3);
    CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_ACCEPT && s.available==0);
    CHECK(UmicomKernelTcpTransmitAcknowledge(&state,s.next,&n)==UMICOM_TCP_SEQUENCE_ACCEPT && n==UMICOM_TCP_SEQUENCE_MAX_SPAN);
    return 0;
}
static int model(void)
{
    const UmicomU32 bases[]={0,1,0x7fffffffU,0xfffffff8U,0xffffffffU};
    for(unsigned b=0;b<5;++b) for(UmicomU32 cap=1;cap<=16;++cap)
    for(UmicomU32 sent=1;sent<=cap;++sent) for(UmicomU32 ack=1;ack<=sent;++ack) {
        UmicomTcpTransmit state; UmicomU32 output; UmicomTcpTransmitSnapshot s;
        CHECK(UmicomKernelTcpTransmitInit(&state,bases[b],cap)==UMICOM_TCP_SEQUENCE_ACCEPT);
        CHECK(UmicomKernelTcpTransmitReserve(&state,sent,0,&output)==UMICOM_TCP_SEQUENCE_ACCEPT);
        CHECK(output==bases[b]);
        CHECK(UmicomKernelTcpTransmitAcknowledge(&state,bases[b]+ack,&output)==UMICOM_TCP_SEQUENCE_ACCEPT && output==ack);
        CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_ACCEPT);
        CHECK(s.outstanding==sent-ack && s.available==cap-sent+ack);
        CHECK(UmicomKernelTcpTransmitReserve(&state,s.available,0,&output)==UMICOM_TCP_SEQUENCE_ACCEPT);
        CHECK(UmicomKernelTcpTransmitInspect(&state,&s)==UMICOM_TCP_SEQUENCE_ACCEPT && s.available==0 && s.outstanding==cap);
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    if(strcmp(argv[1],"cycle")==0)return cycle();
    if(strcmp(argv[1],"refusal")==0)return refusal();
    if(strcmp(argv[1],"wrap")==0)return wrap();
    if(strcmp(argv[1],"model")==0)return model();
    return 2;
}
