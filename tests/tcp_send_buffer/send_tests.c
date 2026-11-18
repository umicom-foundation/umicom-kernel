/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_send_buffer/send_tests.c
 *
 * PURPOSE:
 *   Verify owned transmit payload, repeated reads, acknowledgment cleanup and atomic refusal against a linear reference queue.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_send_buffer.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#define ACCEPT UMICOM_TCP_SEQUENCE_ACCEPT
#define REJECT UMICOM_TCP_SEQUENCE_REJECT
#define INVALID UMICOM_TCP_SEQUENCE_INVALID
static int cycle(void)
{
    UmicomTcpSendBuffer state; UmicomTcpTransmitSnapshot s;
    UmicomU8 data[]={10,20,30,40}, output[8]; UmicomU32 n=99;
    CHECK(UmicomKernelTcpSendInit(&state,100)==ACCEPT);
    CHECK(UmicomKernelTcpSendInspect(&state,&s)==ACCEPT && s.available==1024 && s.outstanding==0);
    CHECK(UmicomKernelTcpSendAppend(&state,data,4,&n)==ACCEPT && n==100);
    CHECK(UmicomKernelTcpSendInspect(&state,&s)==ACCEPT && s.next==104 && s.outstanding==4);
    memset(output,0xcc,sizeof output);
    CHECK(UmicomKernelTcpSendCopy(&state,101,output,8,&n)==ACCEPT && n==3);
    CHECK(output[0]==20 && output[1]==30 && output[2]==40 && output[3]==0xcc);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,102,&n)==ACCEPT && n==2);
    CHECK(UmicomKernelTcpSendCopy(&state,102,output,1,&n)==ACCEPT && n==1 && output[0]==30);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,104,&n)==ACCEPT && n==2);
    CHECK(UmicomKernelTcpSendInspect(&state,&s)==ACCEPT && s.oldest==104 && s.next==104 && s.available==1024);
    return 0;
}
static int ownership(void)
{
    UmicomTcpSendBuffer state; UmicomU8 data[]={11,22,33}, output[3], before[sizeof state]; UmicomU32 n;
    CHECK(UmicomKernelTcpSendInit(&state,50)==ACCEPT);
    CHECK(UmicomKernelTcpSendAppend(&state,data,3,&n)==ACCEPT);
    memset(data,0,sizeof data); memcpy(before,&state,sizeof state);
    for(unsigned i=0;i<5;++i) {
        CHECK(UmicomKernelTcpSendCopy(&state,50,output,3,&n)==ACCEPT && n==3);
        CHECK(output[0]==11 && output[1]==22 && output[2]==33);
        CHECK(memcmp(before,&state,sizeof state)==0);
    }
    return 0;
}
static int capacity(void)
{
    UmicomTcpSendBuffer state; UmicomTcpTransmitSnapshot s;
    UmicomU8 data[1024],output[1024],before[sizeof state]; UmicomU32 n;
    for(unsigned i=0;i<1024;++i)data[i]=(UmicomU8)i;
    CHECK(UmicomKernelTcpSendInit(&state,200)==ACCEPT);
    CHECK(UmicomKernelTcpSendAppend(&state,data,1024,&n)==ACCEPT && n==200);
    CHECK(UmicomKernelTcpSendInspect(&state,&s)==ACCEPT && s.available==0 && s.outstanding==1024);
    memcpy(before,&state,sizeof state); n=99;
    CHECK(UmicomKernelTcpSendAppend(&state,data,1,&n)==REJECT && n==99 && memcmp(before,&state,sizeof state)==0);
    CHECK(UmicomKernelTcpSendCopy(&state,200,output,1024,&n)==ACCEPT && n==1024 && memcmp(data,output,1024)==0);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,1224,&n)==ACCEPT && n==1024);
    CHECK(UmicomKernelTcpSendAppend(&state,data,1024,&n)==ACCEPT && n==1224);
    return 0;
}
static int wrap(void)
{
    UmicomTcpSendBuffer state; UmicomTcpTransmitSnapshot s;
    UmicomU8 data[1024],output[1024];UmicomU32 n;
    for(unsigned i=0;i<1024;++i)data[i]=(UmicomU8)(i%251U);
    const UmicomU32 base=0xfffffff0U;
    CHECK(UmicomKernelTcpSendInit(&state,base)==ACCEPT);
    CHECK(UmicomKernelTcpSendAppend(&state,data,1024,&n)==ACCEPT && n==base);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,base+1000U,&n)==ACCEPT && n==1000);
    CHECK(UmicomKernelTcpSendAppend(&state,data,1000,&n)==ACCEPT && n==base+1024U);
    CHECK(UmicomKernelTcpSendCopy(&state,base+1000U,output,1024,&n)==ACCEPT && n==1024);
    CHECK(memcmp(output,data+1000,24)==0 && memcmp(output+24,data,1000)==0);
    CHECK(UmicomKernelTcpSendInspect(&state,&s)==ACCEPT && s.oldest==base+1000U && s.next==base+2024U);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,s.next,&n)==ACCEPT && n==1024);
    return 0;
}
static int refusal(void)
{
    UmicomTcpSendBuffer state={0};UmicomU8 input[1]={7},output[8],before[sizeof state];
    UmicomTcpTransmitSnapshot s={1,2,3,4};UmicomU32 n=99;
    CHECK(UmicomKernelTcpSendInit(NULL,1)==INVALID);
    CHECK(UmicomKernelTcpSendInspect(&state,&s)==INVALID && s.oldest==1 && s.available==4);
    CHECK(UmicomKernelTcpSendInit(&state,10)==ACCEPT);
    memcpy(before,&state,sizeof state);memset(output,0xcc,sizeof output);
    CHECK(UmicomKernelTcpSendAppend(&state,input,0,&n)==REJECT);
    /* Oversized admission must refuse before reading the one-byte input. */
    CHECK(UmicomKernelTcpSendAppend(&state,input,1025,&n)==REJECT);
    CHECK(UmicomKernelTcpSendAppend(&state,input,0xffffffffU,&n)==INVALID);
    CHECK(UmicomKernelTcpSendAppend(&state,NULL,1,&n)==INVALID);
    CHECK(UmicomKernelTcpSendAppend(&state,input,1,NULL)==INVALID);
    CHECK(UmicomKernelTcpSendAppend(NULL,input,1,&n)==INVALID);
    CHECK(UmicomKernelTcpSendCopy(&state,10,output,8,&n)==REJECT);
    CHECK(UmicomKernelTcpSendCopy(&state,10,NULL,8,&n)==INVALID);
    CHECK(UmicomKernelTcpSendCopy(&state,10,output,8,NULL)==INVALID);
    CHECK(UmicomKernelTcpSendCopy(NULL,10,output,8,&n)==INVALID);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,10,&n)==REJECT);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,11,&n)==REJECT);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,11,NULL)==INVALID);
    CHECK(UmicomKernelTcpSendAcknowledge(NULL,11,&n)==INVALID);
    CHECK(UmicomKernelTcpSendInspect(&state,NULL)==INVALID);
    CHECK(n==99 && memcmp(before,&state,sizeof state)==0);
    for(unsigned i=0;i<8;++i)CHECK(output[i]==0xcc);
    CHECK(UmicomKernelTcpSendAppend(&state,input,1,&n)==ACCEPT);
    memcpy(before,&state,sizeof state);n=99;
    const UmicomU32 positions[]={0,9,11,12,0x8000000aU,0xffffffffU};
    for(unsigned i=0;i<sizeof positions/sizeof positions[0];++i) {
        CHECK(UmicomKernelTcpSendCopy(&state,positions[i],output,8,&n)==REJECT);
        CHECK(n==99 && memcmp(before,&state,sizeof state)==0 && output[0]==0xcc);
    }
    const UmicomU32 acks[]={0,9,10,12,0x8000000aU,0xffffffffU};
    for(unsigned i=0;i<sizeof acks/sizeof acks[0];++i) {
        CHECK(UmicomKernelTcpSendAcknowledge(&state,acks[i],&n)==REJECT);
        CHECK(n==99 && memcmp(before,&state,sizeof state)==0);
    }
    CHECK(UmicomKernelTcpSendCopy(&state,10,output,0,&n)==REJECT && n==99);
    return 0;
}
static int corrupt(void)
{
    for(unsigned fault=0;fault<5;++fault) {
        UmicomTcpSendBuffer state;UmicomU8 input=7,output=0xcc,before[sizeof state];
        UmicomU32 n=99;UmicomTcpTransmitSnapshot s={1,2,3,4};
        CHECK(UmicomKernelTcpSendInit(&state,100)==ACCEPT);
        if(fault==0)state.head=1024;
        if(fault==1)state.sequence.capacity=1023;
        if(fault==2)state.sequence.initialized=UMICOM_FALSE;
        if(fault==3)state.sequence.next=1125;
        if(fault==4)state.sequence.oldest=101;
        memcpy(before,&state,sizeof state);
        CHECK(UmicomKernelTcpSendAppend(&state,&input,1,&n)==INVALID);
        CHECK(UmicomKernelTcpSendCopy(&state,100,&output,1,&n)==INVALID);
        CHECK(UmicomKernelTcpSendAcknowledge(&state,101,&n)==INVALID);
        CHECK(UmicomKernelTcpSendInspect(&state,&s)==INVALID);
        CHECK(n==99 && output==0xcc && s.oldest==1 && s.available==4 && memcmp(before,&state,sizeof state)==0);
    }
    return 0;
}
static int zeroing(void)
{
    UmicomTcpSendBuffer state;UmicomU8 data[1024];UmicomU32 n;
    memset(&state,0xaa,sizeof state);memset(data,0x7f,sizeof data);
    CHECK(UmicomKernelTcpSendInit(&state,0)==ACCEPT);
    for(unsigned i=0;i<1024;++i)CHECK(state.bytes[i]==0);
    CHECK(UmicomKernelTcpSendAppend(&state,data,1024,&n)==ACCEPT);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,1020,&n)==ACCEPT && n==1020);
    for(unsigned i=0;i<1024;++i)CHECK(state.bytes[i]==(i<1020?0:0x7f));
    CHECK(UmicomKernelTcpSendAppend(&state,data,10,&n)==ACCEPT);
    CHECK(UmicomKernelTcpSendAcknowledge(&state,1034,&n)==ACCEPT && n==14);
    for(unsigned i=0;i<1024;++i)CHECK(state.bytes[i]==0);
    CHECK(UmicomKernelTcpSendAppend(&state,data,7,&n)==ACCEPT);
    CHECK(UmicomKernelTcpSendInit(&state,42)==ACCEPT && state.head==0 && state.sequence.next==42);
    for(unsigned i=0;i<1024;++i)CHECK(state.bytes[i]==0);
    return 0;
}
static int model(void)
{
    /* A linear queue, shifted after ACK, has no ring indices. It independently
     * checks byte ordering while the production buffer wraps thousands of times. */
/* Keep the draft oversized fixture for review; the active half-space boundary fits the 32-bit sequence type. The former implementation is retained for owner review. */
#if 0
    const UmicomU32 bases[]={0,0xfffffff0U,0x7fffffff0U};
#endif
    const UmicomU32 bases[]={0,0xfffffff0U,0x7ffffff0U};
    for(unsigned b=0;b<3;++b) {
        UmicomTcpSendBuffer state;UmicomTcpTransmitSnapshot s;
        UmicomU8 queue[1024],input[97],output[1024];UmicomU32 used=0,oldest=bases[b],next=bases[b],seed=41;
        CHECK(UmicomKernelTcpSendInit(&state,oldest)==ACCEPT);
        for(unsigned step=0;step<12000;++step) {
            seed=seed*1664525U+1013904223U;
            UmicomU32 n=99;
            if((seed&3U)!=0U) {
                UmicomU32 amount=1U+((seed>>8)%97U);
                if(amount>1024U-used)amount=1024U-used;
                if(amount!=0U) {
                    for(UmicomU32 i=0;i<amount;++i)input[i]=(UmicomU8)(step+i);
                    CHECK(UmicomKernelTcpSendAppend(&state,input,amount,&n)==ACCEPT && n==next);
                    memcpy(queue+used,input,amount);used+=amount;next+=amount;
                }
            } else if(used!=0U) {
                const UmicomU32 amount=1U+((seed>>8)%used);
                CHECK(UmicomKernelTcpSendAcknowledge(&state,oldest+amount,&n)==ACCEPT && n==amount);
                used-=amount;memmove(queue,queue+amount,used);oldest+=amount;
            }
            CHECK(UmicomKernelTcpSendInspect(&state,&s)==ACCEPT && s.oldest==oldest && s.next==next &&
                s.outstanding==used && s.available==1024U-used);
            if(used!=0U) {
                const UmicomU32 offset=(seed>>16)%used;
                CHECK(UmicomKernelTcpSendCopy(&state,oldest+offset,output,1024,&n)==ACCEPT && n==used-offset);
                CHECK(memcmp(output,queue+offset,n)==0);
                CHECK(UmicomKernelTcpSendCopy(&state,oldest,output,1024,&n)==ACCEPT && n==used && memcmp(output,queue,used)==0);
            }
        }
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    if(strcmp(argv[1],"cycle")==0)return cycle();
    if(strcmp(argv[1],"ownership")==0)return ownership();
    if(strcmp(argv[1],"capacity")==0)return capacity();
    if(strcmp(argv[1],"wrap")==0)return wrap();
    if(strcmp(argv[1],"refusal")==0)return refusal();
    if(strcmp(argv[1],"corrupt")==0)return corrupt();
    if(strcmp(argv[1],"zeroing")==0)return zeroing();
    if(strcmp(argv[1],"model")==0)return model();
    return 2;
}
