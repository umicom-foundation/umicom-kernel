/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/icmp6_tests.c
 *
 * PURPOSE:
 *   Exercise ICMPv6 echo framing, protocol-specific checksum rules and copied metadata ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_icmp6.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
/* Fixed request: documentation addresses, identifier 1234, sequence 0042,
 * payload hello, checksum cdfa. A separate reply has a valid zero checksum. */
static const unsigned char golden[]={0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x01,0x00,0x0d,0x3a,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x80,0x00,0xcd,0xfa,0x12,0x34,0x00,0x42,0x68,0x65,0x6c,0x6c,0x6f};
static const unsigned char zeroReply[]={0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x01,0x00,0x0a,0x3a,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x81,0x00,0x00,0x00,0xab,0xcd,0x10,0x00,0x67,0x78};
static void Put16(unsigned char *p,unsigned value){p[0]=(unsigned char)(value>>8U);p[1]=(unsigned char)value;}
static unsigned Add(unsigned sum,unsigned value){sum+=value;return (sum&65535U)+(sum>>16U);}
static unsigned Sum(const unsigned char *p,size_t length,unsigned sum)
{
    for(size_t i=0U;i<length;i+=2U)sum=Add(sum,((unsigned)p[i]<<8U)|(i+1U<length?p[i+1U]:0U));
    return sum;
}
static void Checksum(unsigned char *frame,size_t length,unsigned protocol,int includePseudo)
{
    frame[56]=frame[57]=0U;unsigned sum=0U;
    /* Fold each word rather than reproducing the production final-fold loop. */
    if(includePseudo){sum=Sum(frame+22U,32U,0U);sum=Add(Add(sum,(unsigned)length),protocol);}
    Put16(frame+56U,~Sum(frame+54U,length,sum)&65535U);
}
static int Refused(const void *bytes,UmicomSize size,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv6IcmpEchoView out;unsigned char before[sizeof out];
    memset(&out,0xa5,sizeof out);memcpy(before,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(bytes,size,&out)==expected);CHECK(!memcmp(before,&out,sizeof out));return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *mode=argv[1];unsigned char frame[1515]={0};memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv6IcmpEchoView out;
    if(!strcmp(mode,"golden")){
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageType==128U && out.identifier==0x1234U && out.sequenceNumber==0x42U && out.messageBytes==13U);
        CHECK(out.payloadOffset==62U && out.payloadBytes==5U && out.ipv6.ipBytes==53U);
        CHECK(out.ipv6.nextHeader==58U && out.ipv6.hopLimit==64U);
        CHECK(!memcmp(out.ipv6.sourceAddress,golden+22U,16U) && !memcmp(out.ipv6.destinationAddress,golden+38U,16U));
        CHECK(!memcmp(frame,golden,sizeof golden));
    }else if(!strcmp(mode,"reply")){
        frame[54]=129U;Checksum(frame,13U,58U,1);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK && out.messageType==129U);
    }else if(!strcmp(mode,"empty") || !strcmp(mode,"even") || !strcmp(mode,"maximum")){
        unsigned length=!strcmp(mode,"empty")?8U:!strcmp(mode,"even")?12U:1460U;
        Put16(frame+18U,length);if(length==1460U)memset(frame+62U,0xff,1452U);
        Checksum(frame,length,58U,1);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,54U+length,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes==length-8U && out.messageBytes==length);
    }else if(!strcmp(mode,"padding")){
        memset(frame+sizeof golden,0xff,1514U-sizeof golden);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,1514U,&out)==UMICOM_NETWORK_PACKET_OK && out.payloadBytes==5U);
    }else if(!strcmp(mode,"unaligned")){
        _Alignas(16) unsigned char storage[sizeof golden+1U];memcpy(storage+1U,golden,sizeof golden);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(storage+1U,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
    }else if(!strcmp(mode,"truncation")){
        for(UmicomSize n=0U;n<sizeof golden;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"short-header")){
        for(unsigned n=0U;n<8U;++n){Put16(frame+18U,n);CHECK(!Refused(frame,54U+n,UMICOM_NETWORK_PACKET_LENGTH));}
    }else if(!strcmp(mode,"protocol")){
        frame[20]=17U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"fragment")){
        frame[20]=44U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_FRAGMENT));
    }else if(!strcmp(mode,"extension")){
        frame[20]=0U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"type")){
        for(unsigned type=0U;type<256U;++type)if(type!=128U && type!=129U){frame[54]=(unsigned char)type;Checksum(frame,13U,58U,1);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));}
    }else if(!strcmp(mode,"code")){
        for(unsigned code=1U;code<256U;++code){frame[55]=(unsigned char)code;Checksum(frame,13U,58U,1);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(mode,"corruption")){
        for(unsigned pos=56U;pos<sizeof golden;++pos)for(unsigned bit=0U;bit<8U;++bit){
            memcpy(frame,golden,sizeof golden);frame[pos]^=(unsigned char)(1U<<bit);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));}
        memcpy(frame,golden,sizeof golden);frame[54]=129U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"source-address") || !strcmp(mode,"destination-address")){
        unsigned offset=!strcmp(mode,"source-address")?22U:38U;
        for(unsigned i=0U;i<16U;++i)for(unsigned bit=0U;bit<8U;++bit){
            memcpy(frame,golden,sizeof golden);frame[offset+i]^=(unsigned char)(1U<<bit);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));}
    }else if(!strcmp(mode,"zero-checksum")){
        Put16(frame+56U,0U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"valid-zero")){
        CHECK(zeroReply[56]==0U && zeroReply[57]==0U);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(zeroReply,sizeof zeroReply,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageType==129U && out.identifier==0xabcdU && out.sequenceNumber==0x1000U && out.payloadBytes==2U);
    }else if(!strcmp(mode,"ipv4-scope") || !strcmp(mode,"udp-scope")){
        Checksum(frame,13U,17U,strcmp(mode,"ipv4-scope")!=0);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"identity")){
        Put16(frame+58U,0U);Put16(frame+60U,65535U);Checksum(frame,13U,58U,1);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.identifier==0U && out.sequenceNumber==65535U);
    }else if(!strcmp(mode,"metadata-scope")){
        frame[14]=0x6fU;frame[15]=0xffU;frame[16]=0xffU;frame[17]=0xffU;frame[21]=0U;
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.ipv6.trafficClass==255U && out.ipv6.flowLabel==0xfffffU && out.ipv6.hopLimit==0U);
    }else if(!strcmp(mode,"pseudo-length")){
        Put16(frame+18U,12U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"arguments")){
        CHECK(!Refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"overlap")){
        union{unsigned char bytes[256];UmicomEthernetIpv6IcmpEchoView view;} storage;
        unsigned char before[sizeof storage];memset(&storage,0,sizeof storage);memcpy(storage.bytes,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(storage.bytes,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        memcpy(storage.bytes+8U,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(storage.bytes+8U,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        memcpy(storage.bytes,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(storage.bytes,128U,(UmicomEthernetIpv6IcmpEchoView *)(void *)(storage.bytes+80U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
    }else if(!strcmp(mode,"output-alignment")){
        _Alignas(16) unsigned char storage[sizeof out+16U],before[sizeof out+16U];memset(storage,0x5a,sizeof storage);memcpy(before,storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,(UmicomEthernetIpv6IcmpEchoView *)(void *)(storage+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,storage,sizeof storage));
    }else if(!strcmp(mode,"address-wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,(UmicomEthernetIpv6IcmpEchoView *)(maximum-(_Alignof(UmicomEthernetIpv6IcmpEchoView)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"lifetime")){
        CHECK(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        memset(frame,0,sizeof frame);CHECK(out.identifier==0x1234U && out.ipv6.sourceAddress[15]==1U && out.payloadBytes==5U);
    }else if(!strcmp(mode,"capture-limit")){
        CHECK(!Refused(frame,1515U,UMICOM_NETWORK_PACKET_LIMIT));CHECK(!Refused(frame,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_LIMIT));
    }else return 2;
    return 0;
}
