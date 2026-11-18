/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_receive/receive_tests.c
 *
 * PURPOSE:
 *   Check bounded receive storage, atomic refusals and stream order against an independent queue.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_receive.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define CHECK(c) do { if(!(c)) {fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;} } while(0)
static int cycle(void)
{
    UmicomTcpReceive state={0}; UmicomTcpReceiveSnapshot view={0};
    UmicomU32 ack=99U,read=99U;UmicomU8 out[8]={91,91,91,91,91,91,91,91};
    const UmicomU8 first[]={1,2,3,4},second[]={5,6};
    CHECK(UmicomKernelTcpReceiveInit(&state,100)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,first,4,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT && ack==104);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,2,&read)==UMICOM_TCP_SEQUENCE_ACCEPT && read==2);
    CHECK(out[0]==1 && out[1]==2 && out[2]==91);
    CHECK(UmicomKernelTcpReceiveAppend(&state,104,second,2,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT && ack==106);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,8,&read)==UMICOM_TCP_SEQUENCE_ACCEPT && read==4);
    CHECK(out[0]==3 && out[1]==4 && out[2]==5 && out[3]==6 && out[4]==91);
    CHECK(UmicomKernelTcpReceiveInspect(&state,&view)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(view.next==106 && view.buffered==0 && view.available==UMICOM_TCP_RECEIVE_CAPACITY);
    read=73;out[0]=81;
    CHECK(UmicomKernelTcpReceiveRead(&state,out,8,&read)==UMICOM_TCP_SEQUENCE_REJECT && read==73 && out[0]==81);
    return 0;
}
static int capacity(void)
{
    UmicomTcpReceive state={0}; UmicomU8 data[UMICOM_TCP_RECEIVE_CAPACITY];
    UmicomU32 ack=0,read=0;for(size_t i=0;i<sizeof data;++i)data[i]=(UmicomU8)(i%251U);
    CHECK(UmicomKernelTcpReceiveInit(&state,1)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,1,data,sizeof data,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(ack==1025U && state.buffered==1024U);
    UmicomTcpReceive saved=state;ack=99;
    CHECK(UmicomKernelTcpReceiveAppend(&state,1025,data,1,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(memcmp(&state,&saved,sizeof state)==0 && ack==99);
    UmicomU8 out[UMICOM_TCP_RECEIVE_CAPACITY];
    CHECK(UmicomKernelTcpReceiveRead(&state,out,sizeof out,&read)==UMICOM_TCP_SEQUENCE_ACCEPT && read==1024);
    CHECK(memcmp(data,out,sizeof data)==0 && state.next==1025);
    CHECK(UmicomKernelTcpReceiveAppend(&state,1025,data,1,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT && ack==1026);
    return 0;
}
static int wrap(void)
{
    UmicomTcpReceive state={0};UmicomU8 data[1024],out[1024];UmicomU32 ack=0,read=0;
    for(size_t i=0;i<sizeof data;++i)data[i]=(UmicomU8)(i%251U);
    CHECK(UmicomKernelTcpReceiveInit(&state,UINT32_MAX-1U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,UINT32_MAX-1U,data,1000,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(ack==998U);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,990,&read)==UMICOM_TCP_SEQUENCE_ACCEPT && read==990);
    CHECK(UmicomKernelTcpReceiveAppend(&state,998,data,100,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT && ack==1098U);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,1024,&read)==UMICOM_TCP_SEQUENCE_ACCEPT && read==110);
    CHECK(memcmp(out,data+990,10)==0 && memcmp(out+10,data,100)==0);
    CHECK(state.head==76U && state.buffered==0);
    return 0;
}
static int ownership(void)
{
    UmicomTcpReceive state={0};UmicomU32 ack=0,read=0;UmicomU8 data[]={10,20,30},out[3];
    CHECK(UmicomKernelTcpReceiveInit(&state,42)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,42,data,3,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT);
    memset(data,255,sizeof data);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,3,&read)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(out[0]==10 && out[1]==20 && out[2]==30 && read==3);
    return 0;
}
static int refusal(void)
{
    UmicomTcpReceive state={0},saved;const UmicomU8 data[]={1,2,3};UmicomU32 ack=99,read=77;
    UmicomU8 out[]={81,82,83};UmicomTcpReceiveSnapshot view={11,22,33};
    CHECK(UmicomKernelTcpReceiveInit(NULL,0)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveInspect(&state,&view)==UMICOM_TCP_SEQUENCE_INVALID && view.next==11);
    CHECK(UmicomKernelTcpReceiveAppend(&state,0,data,1,&ack)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveInit(&state,100)==UMICOM_TCP_SEQUENCE_ACCEPT);
    saved=state;
    CHECK(UmicomKernelTcpReceiveAppend(NULL,100,data,1,&ack)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,NULL,1,&ack)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,data,1,NULL)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,data,0,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,data,UINT32_MAX,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,99,data,1,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveAppend(&state,101,data,1,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveRead(NULL,out,3,&read)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveRead(&state,NULL,3,&read)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,0,&read)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,3,NULL)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveInspect(&state,NULL)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(memcmp(&state,&saved,sizeof state)==0 && ack==99 && read==77);
    CHECK(out[0]==81 && out[1]==82 && out[2]==83 && view.next==11 && view.buffered==22 && view.available==33);
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,data,3,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT);
    saved=state;ack=99;
    CHECK(UmicomKernelTcpReceiveAppend(&state,100,data,3,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(memcmp(&state,&saved,sizeof state)==0 && ack==99);
    return 0;
}
static int corrupt(void)
{
    UmicomTcpReceive state={0},saved;UmicomU8 data=3,out=9;UmicomU32 count=71;
    UmicomTcpReceiveSnapshot view={13,17,19};
    for(unsigned i=0;i<3U;++i) {
        CHECK(UmicomKernelTcpReceiveInit(&state,100)==UMICOM_TCP_SEQUENCE_ACCEPT);
        if(i==0)state.initialized=UMICOM_FALSE;
        if(i==1)state.head=UMICOM_TCP_RECEIVE_CAPACITY;
        if(i==2)state.buffered=UMICOM_TCP_RECEIVE_CAPACITY+1U;
        saved=state;
        CHECK(UmicomKernelTcpReceiveAppend(&state,100,&data,1,&count)==UMICOM_TCP_SEQUENCE_INVALID);
        CHECK(UmicomKernelTcpReceiveRead(&state,&out,1,&count)==UMICOM_TCP_SEQUENCE_INVALID);
        CHECK(UmicomKernelTcpReceiveInspect(&state,&view)==UMICOM_TCP_SEQUENCE_INVALID);
        CHECK(memcmp(&state,&saved,sizeof state)==0 && out==9 && count==71 && view.next==13);
    }
    return 0;
}
static int zeroing(void)
{
    UmicomTcpReceive state;memset(&state,0xaa,sizeof state);UmicomU8 data[]={7,8,9},out[2];UmicomU32 n;
    CHECK(UmicomKernelTcpReceiveInit(&state,0)==UMICOM_TCP_SEQUENCE_ACCEPT);
    for(size_t i=0;i<sizeof state.bytes;++i)CHECK(state.bytes[i]==0);
    CHECK(UmicomKernelTcpReceiveAppend(&state,0,data,3,&n)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveRead(&state,out,2,&n)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(state.bytes[0]==0 && state.bytes[1]==0 && state.bytes[2]==9 && state.buffered==1);
    CHECK(UmicomKernelTcpReceiveInit(&state,200)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(state.next==200 && state.buffered==0 && state.head==0);
    for(size_t i=0;i<sizeof state.bytes;++i)CHECK(state.bytes[i]==0);
    return 0;
}
static int model(void)
{
    const UmicomU32 starts[]={0U,1U,UINT32_MAX-50U};
    for(size_t s=0;s<sizeof starts/sizeof starts[0];++s) {
        UmicomTcpReceive state={0};UmicomU8 queue[1024],data[128],out[128];
        size_t queued=0;UmicomU32 next=starts[s],ack=0,read=0;
        CHECK(UmicomKernelTcpReceiveInit(&state,next)==UMICOM_TCP_SEQUENCE_ACCEPT);
        for(unsigned step=0;step<4000U;++step) {
            const UmicomU32 request=(step*17U)%127U+1U;
            if(step%3U!=0) {
                for(UmicomU32 i=0;i<request;++i)data[i]=(UmicomU8)((step+i)%251U);
                if(request<=sizeof queue-queued) {
                    CHECK(UmicomKernelTcpReceiveAppend(&state,next,data,request,&ack)==UMICOM_TCP_SEQUENCE_ACCEPT);
                    memcpy(queue+queued,data,request);queued+=request;next+=request;CHECK(ack==next);
                } else {
                    UmicomTcpReceive before=state;ack=73;
                    CHECK(UmicomKernelTcpReceiveAppend(&state,next,data,request,&ack)==UMICOM_TCP_SEQUENCE_REJECT);
                    CHECK(memcmp(&before,&state,sizeof state)==0 && ack==73);
                }
            } else if(queued!=0) {
                const size_t expected=request<queued?request:queued;memset(out,255,sizeof out);
                CHECK(UmicomKernelTcpReceiveRead(&state,out,request,&read)==UMICOM_TCP_SEQUENCE_ACCEPT);
                CHECK(read==expected && memcmp(out,queue,expected)==0 && out[expected]==255);
                memmove(queue,queue+expected,queued-expected);queued-=expected;
            }
            UmicomTcpReceiveSnapshot view;
            CHECK(UmicomKernelTcpReceiveInspect(&state,&view)==UMICOM_TCP_SEQUENCE_ACCEPT);
            CHECK(view.next==next && view.buffered==queued && view.available==1024U-queued);
        }
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const struct {const char *name;int (*run)(void);} cases[]={
        {"cycle",cycle},{"capacity",capacity},{"wrap",wrap},{"ownership",ownership},
        {"refusal",refusal},{"corrupt",corrupt},{"zeroing",zeroing},{"model",model}};
    for(size_t i=0;i<sizeof cases/sizeof cases[0];++i)if(strcmp(argv[1],cases[i].name)==0)return cases[i].run();
    return 2;
}
