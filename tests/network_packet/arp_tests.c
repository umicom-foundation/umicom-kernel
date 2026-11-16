/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/arp_tests.c
 *
 * PURPOSE:
 *   Exercise independent ARP capture vectors, refusal preservation and copied metadata lifetime on the host.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_arp.h"
#include "umicom/kernel/address.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while(0)
static const UmicomU8 golden[42]={
    0xff,0xff,0xff,0xff,0xff,0xff, 0x02,0,0,0,0,1, 0x08,0x06,
    0,1, 0x08,0, 6,4, 0,1, 0x02,0,0,0,0,1, 192,0,2,1,
    0,0,0,0,0,0, 192,0,2,2
};
static int refused(const void *frame,UmicomSize size,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetArpView out,before;
    memset(&out,0xa5,sizeof out); memcpy(&before,&out,sizeof out);
    CHECK(UmicomKernelEthernetArpRead(frame,size,&out)==expected);
    CHECK(memcmp(&out,&before,sizeof out)==0); return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2); const char *mode=argv[1];
    _Alignas(16) UmicomU8 frame[1515]; memset(frame,0,sizeof frame); memcpy(frame,golden,sizeof golden);
    UmicomEthernetArpView out;
    if(strcmp(mode,"truncation")==0) {
        for(UmicomSize n=0;n<sizeof golden;++n) CHECK(refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH)==0);
        return 0;
    }
    if(strcmp(mode,"capture_limit")==0) return refused(frame,sizeof frame,UMICOM_NETWORK_PACKET_LIMIT);
    if(strcmp(mode,"arguments")==0) {
        CHECK(refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT)==0);
        CHECK(UmicomKernelEthernetArpRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT); return 0;
    }
    if(strcmp(mode,"overlap")==0) {
        UmicomU8 before[sizeof frame]; memcpy(before,frame,sizeof frame);
        CHECK(UmicomKernelEthernetArpRead(frame,60,(UmicomEthernetArpView *)(void *)frame)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetArpRead(frame,60,(UmicomEthernetArpView *)(void *)(frame+42))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(before,frame,sizeof frame)==0); return 0;
    }
    if(strcmp(mode,"output_alignment")==0) {
        _Alignas(16) UmicomU8 storage[sizeof out+16],before[sizeof out+16];
        memset(storage,0xa5,sizeof storage); memcpy(before,storage,sizeof storage);
        CHECK(UmicomKernelEthernetArpRead(frame,sizeof golden,(UmicomEthernetArpView *)(void *)(storage+1))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(before,storage,sizeof storage)==0); return 0;
    }
    if(strcmp(mode,"address_wrap")==0) {
        const UmicomAddress maximum=~(UmicomAddress)0;
        CHECK(refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT)==0);
        UmicomEthernetArpView *wrapped=(UmicomEthernetArpView *)(maximum-(_Alignof(UmicomEthernetArpView)-1U));
        CHECK(UmicomKernelEthernetArpRead(frame,sizeof golden,wrapped)==UMICOM_NETWORK_PACKET_ARGUMENT); return 0;
    }
    if(strcmp(mode,"ether_type")==0) {frame[13]=0;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"hardware_type")==0) {frame[15]=2;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"protocol_type")==0) {frame[16]=0x86;frame[17]=0xdd;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"hardware_length")==0) {frame[18]=8;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"protocol_length")==0) {frame[19]=16;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"opcode")==0) {frame[21]=3;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"network_order")==0) {frame[20]=1;frame[21]=0;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    UmicomSize length=sizeof golden; const void *input=frame;
    _Alignas(16) UmicomU8 unaligned[sizeof golden+1];
    if(strcmp(mode,"golden")==0 || strcmp(mode,"lifetime")==0) {}
    else if(strcmp(mode,"reply")==0) frame[21]=2;
    else if(strcmp(mode,"padding")==0 || strcmp(mode,"maximum")==0) {
        length=strcmp(mode,"padding")==0?60U:1514U;
        memset(frame+sizeof golden,0xff,length-sizeof golden);
    }
    else if(strcmp(mode,"unaligned")==0) {memcpy(unaligned+1,golden,sizeof golden);input=unaligned+1;}
    else if(strcmp(mode,"addresses")==0) {memset(frame+22,0,10);memset(frame+32,0xff,10);}
    else if(strcmp(mode,"sender_mismatch")==0) frame[22]=0x04;
    else CHECK(0);
    CHECK(UmicomKernelEthernetArpRead(input,length,&out)==UMICOM_NETWORK_PACKET_OK);
    CHECK(out.operation==(strcmp(mode,"reply")==0?UMICOM_ARP_REPLY:UMICOM_ARP_REQUEST));
    CHECK(out.destinationMac[0]==0xff && out.sourceMac[0]==2 && out.sourceMac[5]==1);
    if(strcmp(mode,"addresses")==0) CHECK(out.senderProtocolAddress[0]==0 && out.targetProtocolAddress[0]==0xff);
    else {
        CHECK(out.senderProtocolAddress[0]==192 && out.senderProtocolAddress[3]==1);
        CHECK(out.targetProtocolAddress[0]==192 && out.targetProtocolAddress[3]==2);
        CHECK(out.senderHardwareAddress[0]==(strcmp(mode,"sender_mismatch")==0?4U:2U));
    }
    if(strcmp(mode,"lifetime")==0) {memset(frame,0,sizeof frame);CHECK(out.senderProtocolAddress[0]==192 && out.operation==UMICOM_ARP_REQUEST);}
    return 0;
}
