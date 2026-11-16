/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/ipv6_tests.c
 *
 * PURPOSE:
 *   Verify independent IPv6 vectors, bounded capture policy, refusal preservation and metadata lifetime.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_ipv6.h"
#include "umicom/kernel/address.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
/* 2001:db8::1 -> 2001:db8::2, traffic class 0xab, flow label 0xcdef0.
 * Five arbitrary bytes demonstrate that base-header admission is not UDP
 * validation. The encoded payload length, not capture padding, bounds them. */
static const UmicomU8 golden[59] = {
    2,0,0,0,0,2, 2,0,0,0,0,1, 0x86,0xdd,
    0x6a,0xbc,0xde,0xf0,0,5,17,64,
    0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1,
    0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,2,
    'h','e','l','l','o'
};
static int refused(const void *frame,UmicomSize size,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv6View out,before;
    memset(&out,0xa5,sizeof out); memcpy(&before,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv6Read(frame,size,&out)==expected);
    CHECK(memcmp(&out,&before,sizeof out)==0); return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2); const char *mode=argv[1];
    _Alignas(16) UmicomU8 frame[1515]; memset(frame,0,sizeof frame); memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv6View out;
    if(strcmp(mode,"truncation")==0) {
        for(UmicomSize n=0;n<sizeof golden;++n) CHECK(refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH)==0);
        return 0;
    }
    if(strcmp(mode,"capture_limit")==0) return refused(frame,sizeof frame,UMICOM_NETWORK_PACKET_LIMIT);
    if(strcmp(mode,"payload_limit")==0) {frame[18]=5;frame[19]=0xb5;return refused(frame,1514,UMICOM_NETWORK_PACKET_LIMIT);}
    if(strcmp(mode,"payload_truncation")==0) {frame[19]=6;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_LENGTH);}
    if(strcmp(mode,"ether_type")==0) {frame[12]=8;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"version")==0) {frame[14]=0x4a;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER);}
    if(strcmp(mode,"fragments")==0) {frame[20]=44;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_FRAGMENT);}
    if(strcmp(mode,"extensions")==0) {
        for(unsigned n=0;n<256;++n) {
            if(n==6 || n==17 || n==58 || n==59 || n==44) continue;
            frame[20]=(UmicomU8)n;CHECK(refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED)==0);
        }
        return 0;
    }
    if(strcmp(mode,"jumbogram")==0) {frame[18]=frame[19]=frame[20]=0;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"no_next_payload")==0) {frame[20]=59;return refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(strcmp(mode,"arguments")==0) {
        CHECK(refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT)==0);
        CHECK(UmicomKernelEthernetIpv6Read(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);return 0;
    }
    if(strcmp(mode,"overlap")==0) {
        UmicomU8 before[sizeof frame];memcpy(before,frame,sizeof frame);
        CHECK(UmicomKernelEthernetIpv6Read(frame,60,(UmicomEthernetIpv6View *)(void *)frame)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv6Read(frame,60,(UmicomEthernetIpv6View *)(void *)(frame+56))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(before,frame,sizeof frame)==0);return 0;
    }
    if(strcmp(mode,"output_alignment")==0) {
        _Alignas(16) UmicomU8 storage[sizeof out+16],before[sizeof out+16];
        memset(storage,0xa5,sizeof storage);memcpy(before,storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6Read(frame,sizeof golden,(UmicomEthernetIpv6View *)(void *)(storage+1))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(before,storage,sizeof storage)==0);return 0;
    }
    if(strcmp(mode,"address_wrap")==0) {
        const UmicomAddress maximum=~(UmicomAddress)0;
        CHECK(refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT)==0);
        UmicomEthernetIpv6View *wrapped=(UmicomEthernetIpv6View *)(maximum-(_Alignof(UmicomEthernetIpv6View)-1U));
        CHECK(UmicomKernelEthernetIpv6Read(frame,sizeof golden,wrapped)==UMICOM_NETWORK_PACKET_ARGUMENT);return 0;
    }
    UmicomSize length=sizeof golden; const void *input=frame;
    _Alignas(16) UmicomU8 unaligned[sizeof golden+1];
    if(strcmp(mode,"golden")==0 || strcmp(mode,"lifetime")==0 || strcmp(mode,"transport_scope")==0) {}
    else if(strcmp(mode,"tcp")==0) frame[20]=6;
    else if(strcmp(mode,"icmpv6")==0) frame[20]=58;
    else if(strcmp(mode,"empty")==0 || strcmp(mode,"no_next")==0) {
        frame[19]=0; length=54; if(strcmp(mode,"no_next")==0) frame[20]=59;
    }
    else if(strcmp(mode,"padding")==0) {length=1514;memset(frame+sizeof golden,0xff,length-sizeof golden);}
    else if(strcmp(mode,"maximum")==0) {length=1514;frame[18]=5;frame[19]=0xb4;}
    else if(strcmp(mode,"network_order")==0) {length=310;frame[18]=1;frame[19]=0;}
    else if(strcmp(mode,"unaligned")==0) {memcpy(unaligned+1,golden,sizeof golden);input=unaligned+1;}
    else if(strcmp(mode,"addresses")==0) {memset(frame+22,0,16);memset(frame+38,0xff,16);}
    else if(strcmp(mode,"traffic")==0) {frame[14]=0x6f;frame[15]=0xfc;}
    else if(strcmp(mode,"flow")==0) {frame[15]=0xbf;frame[16]=frame[17]=0xff;}
    else if(strcmp(mode,"hop")==0) frame[21]=0;
    else CHECK(0);
    CHECK(UmicomKernelEthernetIpv6Read(input,length,&out)==UMICOM_NETWORK_PACKET_OK);
    const UmicomU8 *expected=(const UmicomU8 *)input;
    CHECK(memcmp(out.sourceAddress,expected+22,16)==0 && memcmp(out.destinationAddress,expected+38,16)==0);
    CHECK(memcmp(out.destinationMac,golden,6)==0 && memcmp(out.sourceMac,golden+6,6)==0);
    CHECK(out.payloadOffset==54 && out.ipBytes==(UmicomU16)(40U+out.payloadBytes));
    CHECK(out.payloadBytes==(strcmp(mode,"maximum")==0?1460:strcmp(mode,"network_order")==0?256:
        (strcmp(mode,"empty")==0 || strcmp(mode,"no_next")==0)?0:5));
    CHECK(out.trafficClass==(strcmp(mode,"traffic")==0?255:171));
    CHECK(out.flowLabel==(strcmp(mode,"flow")==0?0xfffffU:0xcdef0U));
    CHECK(out.hopLimit==(strcmp(mode,"hop")==0?0:64));
    CHECK(out.nextHeader==expected[20]);
    if(strcmp(mode,"lifetime")==0) {memset(frame,0,sizeof frame);CHECK(out.sourceAddress[0]==0x20 && out.sourceAddress[15]==1 && out.destinationAddress[15]==2);}
    return 0;
}
