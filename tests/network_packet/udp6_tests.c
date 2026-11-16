/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/udp6_tests.c
 *
 * PURPOSE:
 *   Exercise real IPv6 UDP admission with fixed checksum vectors, independent mutation checks and owned output boundaries.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_udp6.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
/* Documentation addresses 2001:db8::1 and ::2, ports 12345 and 53, hello.
 * Fixed checksum 301f; the negative-zero two-byte payload is 73f7. */
static const unsigned char golden[]={

    0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,
    0x60,0x00,0x00,0x01,0x00,0x0d,0x11,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x30,0x39,
    0x00,0x35,0x00,0x0d,0x30,0x1f,0x68,0x65,0x6c,0x6c,0x6f
};
static void Put16(unsigned char *p,unsigned value){p[0]=(unsigned char)(value>>8U);p[1]=(unsigned char)value;}
static unsigned Add(unsigned sum,unsigned value){sum+=value;return (sum&65535U)+(sum>>16U);}
static unsigned Sum(const unsigned char *p,size_t length,unsigned sum)
{
    for(size_t i=0U;i<length;i+=2U)sum=Add(sum,((unsigned)p[i]<<8U)|(i+1U<length?p[i+1U]:0U));
    return sum;
}
static void Checksum(unsigned char *frame,size_t length)
{
    frame[60]=frame[61]=0U;
    /* Fold every word, independently of the production final-fold loop. */
    unsigned sum=Sum(frame+22U,32U,0U);sum=Add(Add(sum,(unsigned)length),17U);
    unsigned check=~Sum(frame+54U,length,sum)&65535U;Put16(frame+60U,check?check:65535U);
}
static int Refused(const void *bytes,UmicomSize size,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv6UdpView out;unsigned char before[sizeof out];
    memset(&out,0xa5,sizeof out);memcpy(before,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv6UdpRead(bytes,size,&out)==expected);CHECK(!memcmp(before,&out,sizeof out));return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *mode=argv[1];
    unsigned char frame[1515];memset(frame,0,sizeof frame);memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv6UdpView out;
    if(!strcmp(mode,"golden")){
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sourcePort==12345U && out.destinationPort==53U && out.datagramBytes==13U);
        CHECK(out.payloadOffset==62U && out.payloadBytes==5U && out.ipv6.ipBytes==53U);
        CHECK(out.ipv6.payloadBytes==13U && out.ipv6.nextHeader==17U && out.ipv6.hopLimit==64U);
        CHECK(!memcmp(out.ipv6.sourceAddress,golden+22U,16U) && !memcmp(out.ipv6.destinationAddress,golden+38U,16U));
        CHECK(!memcmp(frame,golden,sizeof golden));
    }else if(!strcmp(mode,"empty") || !strcmp(mode,"even") || !strcmp(mode,"maximum")){
        unsigned length=!strcmp(mode,"empty")?8U:!strcmp(mode,"even")?12U:1460U;
        Put16(frame+18U,length);Put16(frame+58U,length);
        if(length==1460U)memset(frame+62U,0xff,1452U);
        Checksum(frame,length);
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,54U+length,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes==length-8U && out.datagramBytes==length);
    }else if(!strcmp(mode,"padding")){
        memset(frame+sizeof golden,0xff,1514U-sizeof golden);
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,1514U,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes==5U);
    }else if(!strcmp(mode,"unaligned")){
        _Alignas(16) unsigned char storage[sizeof golden+1U];memcpy(storage+1U,golden,sizeof golden);
        CHECK(UmicomKernelEthernetIpv6UdpRead(storage+1U,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
    }else if(!strcmp(mode,"truncation")){
        for(UmicomSize n=0U;n<sizeof golden;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"short_header")){
        for(unsigned n=0U;n<8U;++n){Put16(frame+18U,n);CHECK(!Refused(frame,54U+n,UMICOM_NETWORK_PACKET_LENGTH));}
    }else if(!strcmp(mode,"length_min")){
        for(unsigned n=0U;n<8U;++n){Put16(frame+58U,n);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(mode,"short_length") || !strcmp(mode,"long_length")){
        Put16(frame+58U,!strcmp(mode,"short_length")?12U:14U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"protocol")){
        frame[20]=6U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"fragment")){
        frame[20]=44U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_FRAGMENT));
    }else if(!strcmp(mode,"extension")){
        frame[20]=0U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"zero_checksum")){
        Put16(frame+60U,0U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"corruption")){
        const unsigned positions[]={54U,55U,56U,57U,60U,61U,62U,63U,64U,65U,66U};
        for(unsigned i=0U;i<sizeof positions/sizeof positions[0];++i)for(unsigned bit=0U;bit<8U;++bit){
            memcpy(frame,golden,sizeof golden);frame[positions[i]]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        }
    }else if(!strcmp(mode,"source_address") || !strcmp(mode,"destination_address")){
        unsigned offset=!strcmp(mode,"source_address")?22U:38U;
        for(unsigned i=0U;i<16U;++i)for(unsigned bit=0U;bit<8U;++bit){
            memcpy(frame,golden,sizeof golden);frame[offset+i]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        }
    }else if(!strcmp(mode,"zero_encoding")){
        Put16(frame+18U,10U);Put16(frame+58U,10U);Put16(frame+60U,65535U);Put16(frame+62U,0x73f7U);
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,64U,&out)==UMICOM_NETWORK_PACKET_OK && out.payloadBytes==2U);
        Put16(frame+60U,0U);CHECK(!Refused(frame,64U,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"ports")){
        Put16(frame+54U,0U);Put16(frame+56U,65535U);Checksum(frame,13U);
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sourcePort==0U && out.destinationPort==65535U);
    }else if(!strcmp(mode,"metadata_scope")){
        frame[14]=0x6fU;frame[15]=0xffU;frame[16]=0xffU;frame[17]=0xffU;frame[21]=0U;
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.ipv6.trafficClass==255U && out.ipv6.flowLabel==0xfffffU && out.ipv6.hopLimit==0U);
    }else if(!strcmp(mode,"pseudo_length")){
        Put16(frame+18U,12U);Put16(frame+58U,12U);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"extra_ip_payload")){
        Put16(frame+18U,14U);CHECK(!Refused(frame,68U,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"arguments")){
        CHECK(!Refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"overlap")){
        union{unsigned char bytes[256];UmicomEthernetIpv6UdpView view;} storage;
        unsigned char before[sizeof storage];memset(&storage,0,sizeof storage);
        memcpy(storage.bytes,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6UdpRead(storage.bytes,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        memcpy(storage.bytes+8U,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6UdpRead(storage.bytes+8U,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        memcpy(storage.bytes,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6UdpRead(storage.bytes,128U,(UmicomEthernetIpv6UdpView *)(void *)(storage.bytes+80U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
    }else if(!strcmp(mode,"output_alignment")){
        _Alignas(16) unsigned char storage[sizeof out+16U],before[sizeof out+16U];
        memset(storage,0x5a,sizeof storage);memcpy(before,storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,(UmicomEthernetIpv6UdpView *)(void *)(storage+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,storage,sizeof storage));
    }else if(!strcmp(mode,"address_wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,(UmicomEthernetIpv6UdpView *)(maximum-(_Alignof(UmicomEthernetIpv6UdpView)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"lifetime")){
        CHECK(UmicomKernelEthernetIpv6UdpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        memset(frame,0,sizeof frame);CHECK(out.sourcePort==12345U && out.ipv6.sourceAddress[15]==1U && out.payloadBytes==5U);
    }else if(!strcmp(mode,"capture_limit")){
        CHECK(!Refused(frame,1515U,UMICOM_NETWORK_PACKET_LIMIT));CHECK(!Refused(frame,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_LIMIT));
    }else return 2;
    return 0;
}
