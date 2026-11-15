/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/icmp_tests.c
 *
 * PURPOSE:
 *   Exercise ICMP echo admission with independent golden bytes, mandatory checksums and buffer ownership refusals.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/network_icmp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while (0)

/* Independent odd-length echo request: IPv4 checksum 3c71; ICMP checksum
 * f62b from words 0800,0000,1234,abcd,6865,6c6c,6f00 before complementing.
 * Documentation addresses and RAM data never reach a network interface. */
static const unsigned char golden[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,33,0x12,0x34,0x40,0,64,1,0x3c,0x71,
    192,0,2,1,198,51,100,2,
    8,0,0xf6,0x2b,0x12,0x34,0xab,0xcd,'h','e','l','l','o'
};
static void Put16(unsigned char *p, unsigned value)
{
    p[0]=(unsigned char)(value >> 8U);p[1]=(unsigned char)value;
}
static unsigned Checksum(const unsigned char *p, size_t length)
{
    /* Fold each word rather than duplicating the production final-fold loop. */
    unsigned sum=0U;
    for(size_t i=0U;i<length;i+=2U){
        sum+=((unsigned)p[i]<<8U)|(i+1U<length?p[i+1U]:0U);
        sum=(sum&65535U)+(sum>>16U);
    }
    return ~sum & 65535U;
}
static void IpChecksum(unsigned char *frame)
{
    frame[24]=0U;frame[25]=0U;Put16(frame+24U,Checksum(frame+14U,20U));
}
static void EchoChecksum(unsigned char *frame,size_t length)
{
    frame[36]=0U;frame[37]=0U;Put16(frame+36U,Checksum(frame+34U,length));
}
static int Refused(const void *frame,size_t length,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv4IcmpEchoView out;
    unsigned char before[sizeof out];
    memset(&out,0xa5,sizeof out);memcpy(before,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,length,&out)==expected);
    CHECK(memcmp(before,&out,sizeof out)==0);
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *name=argv[1];
    unsigned char frame[1514];memset(frame,0,sizeof frame);memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv4IcmpEchoView out;
    if(!strcmp(name,"golden")){
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageType==8U && out.identifier==0x1234U && out.sequenceNumber==0xabcdU);
        CHECK(out.messageBytes==13U && out.payloadOffset==42U && out.payloadBytes==5U);
        CHECK(out.ipv4.protocol==1U && out.ipv4.ipBytes==33U && out.ipv4.sourceAddress[0]==192U);
        CHECK(!memcmp(frame,golden,sizeof golden));
    }else if(!strcmp(name,"reply")){
        frame[34]=0U;frame[36]=0xfeU;frame[37]=0x2bU;
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageType==UMICOM_ICMP_ECHO_REPLY && out.identifier==0x1234U && out.sequenceNumber==0xabcdU);
    }else if(!strcmp(name,"empty")||!strcmp(name,"even")||!strcmp(name,"maximum")){
        const size_t length=!strcmp(name,"empty")?8U:!strcmp(name,"even")?12U:1480U;
        Put16(frame+16U,(unsigned)length+20U);IpChecksum(frame);EchoChecksum(frame,length);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,34U+length,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageBytes==length && out.payloadBytes==length-8U);
    }else if(!strcmp(name,"padding")){
        memset(frame+sizeof golden,0xff,sizeof frame-sizeof golden);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof frame,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageBytes==13U && out.payloadBytes==5U);
    }else if(!strcmp(name,"unaligned")){
        _Alignas(16) unsigned char storage[sizeof golden+1U];memcpy(storage+1U,golden,sizeof golden);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(storage+1U,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
    }else if(!strcmp(name,"truncation")){
        for(size_t n=0U;n<sizeof golden;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(name,"short_header")){
        for(unsigned length=0U;length<8U;++length){
            Put16(frame+16U,20U+length);IpChecksum(frame);
            CHECK(!Refused(frame,34U+length,UMICOM_NETWORK_PACKET_LENGTH));
        }
    }else if(!strcmp(name,"protocol")){
        frame[23]=17U;IpChecksum(frame);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(name,"type")){
        const unsigned char types[]={3U,5U,11U,13U,255U};
        for(size_t i=0U;i<sizeof types;++i){frame[34]=types[i];EchoChecksum(frame,13U);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));}
    }else if(!strcmp(name,"code")){
        frame[35]=1U;EchoChecksum(frame,13U);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(name,"ip_checksum")){
        frame[24]^=1U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(name,"fragment")){
        Put16(frame+20U,0x2000U);IpChecksum(frame);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_FRAGMENT));
    }else if(!strcmp(name,"corruption")){
        for(size_t offset=36U;offset<sizeof golden;++offset)for(unsigned bit=0U;bit<8U;++bit){
            memcpy(frame,golden,sizeof golden);frame[offset]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        }
    }else if(!strcmp(name,"zero_checksum")){
        Put16(frame+36U,0U);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        Put16(frame+16U,28U);Put16(frame+38U,0xf7ffU);Put16(frame+40U,0U);IpChecksum(frame);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,42U,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.identifier==0xf7ffU && out.sequenceNumber==0U && out.payloadBytes==0U);
    }else if(!strcmp(name,"address_scope")){
        frame[26]^=1U;frame[30]^=1U;IpChecksum(frame);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.ipv4.sourceAddress[0]==193U && out.ipv4.destinationAddress[0]==199U);
    }else if(!strcmp(name,"identifiers")){
        Put16(frame+38U,0U);Put16(frame+40U,65535U);EchoChecksum(frame,13U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.identifier==0U && out.sequenceNumber==65535U);
    }else if(!strcmp(name,"arguments")){
        CHECK(!Refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(name,"overlap")){
        union {unsigned char bytes[256];UmicomEthernetIpv4IcmpEchoView view;} storage;
        unsigned char before[sizeof storage];
        memset(&storage,0,sizeof storage);memcpy(storage.bytes,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(storage.bytes,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        memcpy(storage.bytes+8U,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(storage.bytes+8U,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        /* Padding is still owned input storage even when excluded from checksum. */
        _Alignas(16) unsigned char capture[256];memset(capture,0,sizeof capture);memcpy(capture,golden,sizeof golden);
        unsigned char saved[sizeof capture];memcpy(saved,capture,sizeof capture);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(capture,sizeof capture,
            (UmicomEthernetIpv4IcmpEchoView *)(void *)(capture+64U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(saved,capture,sizeof capture));
    }else if(!strcmp(name,"output_alignment")){
        _Alignas(16) unsigned char storage[sizeof out+16U];unsigned char before[sizeof storage];
        memset(storage,0x5a,sizeof storage);memcpy(before,storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,
            (UmicomEthernetIpv4IcmpEchoView *)(void *)(storage+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,storage,sizeof storage));
    }else if(!strcmp(name,"address_wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,
            (UmicomEthernetIpv4IcmpEchoView *)(maximum-(_Alignof(UmicomEthernetIpv4IcmpEchoView)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(name,"lifetime")){
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        memset(frame,0,sizeof frame);
        CHECK(out.identifier==0x1234U && out.ipv4.sourceAddress[0]==192U && out.payloadBytes==5U);
    }else if(!strcmp(name,"capture_limit")){
        CHECK(!Refused(frame,1515U,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(frame,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_LIMIT));
    }else return 2;
    return 0;
}
