/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/icmp_write_tests.c
 *
 * PURPOSE:
 *   Verify ICMP echo encoding against independent bytes, checksum-zero semantics and unchanged rejected outputs.
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
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
/* Independent ICMP words 0800, 0000, 1234, abcd, 6865, 6c6c, 6f00
 * produce checksum f62b. IP protocol 1 changes the IPv4 checksum to 3c71. */
static const unsigned char golden[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,33,0x12,0x34,0x40,0,64,1,0x3c,0x71,
    192,0,2,1,198,51,100,2,8,0,0xf6,0x2b,0x12,0x34,0xab,0xcd,
    'h','e','l','l','o'
};
static UmicomEthernetIpv4IcmpEchoSpec MakeSpec(void)
{
    UmicomEthernetIpv4IcmpEchoSpec spec;
    memset(&spec,0,sizeof spec);
    memcpy(spec.destinationMac,golden,6U);memcpy(spec.sourceMac,golden+6U,6U);
    memcpy(spec.sourceAddress,golden+26U,4U);memcpy(spec.destinationAddress,golden+30U,4U);
    spec.echoIdentifier=0x1234U;spec.sequenceNumber=0xabcdU;spec.identification=0x1234U;spec.timeToLive=64U;spec.messageType=UMICOM_ICMP_ECHO_REQUEST;
    return spec;
}
static int Refused(const UmicomEthernetIpv4IcmpEchoSpec *spec,const void *payload,
    UmicomSize length,UmicomSize capacity,UmicomNetworkPacketStatus expected)
{
    unsigned char bytes[1514],before[1514];UmicomSize written=999U;
    memset(bytes,0x5a,sizeof bytes);memcpy(before,bytes,sizeof bytes);
    CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(spec,payload,length,bytes,capacity,&written)==expected);
    CHECK(written==999U&&!memcmp(bytes,before,sizeof bytes));
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];
    UmicomEthernetIpv4IcmpEchoSpec spec=MakeSpec();
    unsigned char output[1514],payload[1472];UmicomSize written=999U;
    memset(output,0xa5,sizeof output);memset(payload,0xff,sizeof payload);
    UmicomEthernetIpv4IcmpEchoView view;
    if(!strcmp(mode,"golden")) {
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,"hello",5U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(written==sizeof golden&&!memcmp(output,golden,sizeof golden));
        for(size_t i=sizeof golden;i<sizeof output;++i)CHECK(output[i]==0xa5U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_OK);
    } else if(!strcmp(mode,"all-lengths")) {
        for(UmicomSize size=0U;size<=sizeof payload;++size) {
            memset(output,0xa5,sizeof output);
            CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,size?payload:NULL,size,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
            CHECK(written==42U+size&&!memcmp(output+42U,payload,(size_t)size));
            CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_OK);
            CHECK(view.payloadBytes==size&&view.messageType==UMICOM_ICMP_ECHO_REQUEST&&view.ipv4.dontFragment==UMICOM_TRUE);
            if(written<sizeof output)CHECK(output[written]==0xa5U);
        }
    } else if(!strcmp(mode,"capacity")) {
        for(UmicomSize capacity=0U;capacity<sizeof golden;++capacity)
            CHECK(!Refused(&spec,"hello",5U,capacity,UMICOM_NETWORK_PACKET_LENGTH));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,"hello",5U,output,sizeof golden,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(written==sizeof golden&&output[sizeof golden]==0xa5U);
    } else if(!strcmp(mode,"limits")) {
        CHECK(!Refused(&spec,payload,1473U,sizeof output,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(&spec,payload,~(UmicomSize)0U,sizeof output,UMICOM_NETWORK_PACKET_LIMIT));
    } else if(!strcmp(mode,"ttl")) {
        spec.timeToLive=0U;CHECK(!Refused(&spec,"hello",5U,sizeof output,UMICOM_NETWORK_PACKET_HEADER));
        for(unsigned ttl=1U;ttl<=255U;++ttl) {
            spec.timeToLive=(UmicomU8)ttl;
            CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
            CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_OK);
            CHECK(view.ipv4.timeToLive==ttl);
        }
    } else if(!strcmp(mode,"fields")) {
        spec.echoIdentifier=0U;spec.sequenceNumber=65535U;spec.identification=65535U;spec.differentiatedServices=255U;
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_OK);
        CHECK(view.identifier==0U&&view.sequenceNumber==65535U&&view.ipv4.identification==65535U);
        CHECK(view.ipv4.differentiatedServices==255U&&!memcmp(view.ipv4.sourceMac,spec.sourceMac,6U));
        CHECK(!memcmp(view.ipv4.destinationAddress,spec.destinationAddress,4U));
    } else if(!strcmp(mode,"checksum-zero")) {
        const unsigned char zeroPayload[2]={0x39U,0xfeU};
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,zeroPayload,2U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(output[36]==0U&&output[37]==0U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_OK);
    } else if(!strcmp(mode,"reply")) {
        unsigned char expected[sizeof golden];memcpy(expected,golden,sizeof golden);
        expected[34]=0U;expected[36]=0xfeU;expected[37]=0x2bU;
        spec.messageType=UMICOM_ICMP_ECHO_REPLY;
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,"hello",5U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(written==sizeof golden&&!memcmp(output,expected,sizeof expected));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_OK);
        CHECK(view.messageType==UMICOM_ICMP_ECHO_REPLY && view.identifier==0x1234U && view.sequenceNumber==0xabcdU);
    } else if(!strcmp(mode,"types")) {
        for(unsigned type=0U;type<=255U;++type) {
            if(type==UMICOM_ICMP_ECHO_REQUEST || type==UMICOM_ICMP_ECHO_REPLY)continue;
            spec.messageType=(UmicomU8)type;
            CHECK(!Refused(&spec,NULL,0U,sizeof output,UMICOM_NETWORK_PACKET_UNSUPPORTED));
        }
    } else if(!strcmp(mode,"corruption")) {
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,"hello",5U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        output[written-1U]^=1U;
        CHECK(UmicomKernelEthernetIpv4IcmpEchoRead(output,written,&view)==UMICOM_NETWORK_PACKET_CHECKSUM);
    } else if(!strcmp(mode,"unaligned")) {
        _Alignas(16) unsigned char storage[1515],source[6];memcpy(source+1U,"hello",5U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,source+1U,5U,storage+1U,1514U,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(!memcmp(storage+1U,golden,sizeof golden));
    } else if(!strcmp(mode,"arguments")) {
        CHECK(!Refused(NULL,"hello",5U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused(&spec,NULL,1U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,NULL,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(written==999U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,output,sizeof output,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof output;++i)CHECK(output[i]==0xa5U);
    } else if(!strcmp(mode,"overlap")) {
        union { UmicomSize count; UmicomEthernetIpv4IcmpEchoSpec spec; unsigned char bytes[1600]; } store;
        unsigned char before[sizeof store];memset(&store,0x5a,sizeof store);store.spec=spec;memcpy(before,&store,sizeof store);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&store.spec,"hello",5U,store.bytes,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store)&&written==999U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,store.bytes+10U,5U,store.bytes,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store)&&written==999U);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,store.bytes,1514U,&store.count)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store));
        /* Even unused destination capacity must not alias the input. */
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,store.bytes+1500U,5U,store.bytes,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,&spec,1U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,&written,1U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof output;++i)CHECK(output[i]==0xa5U);
    } else if(!strcmp(mode,"alignment")) {
        _Alignas(16) unsigned char source[sizeof spec+1U],count[sizeof written+1U];
        memcpy(source+1U,&spec,sizeof spec);memset(count,0x5a,sizeof count);
        CHECK(!Refused((const UmicomEthernetIpv4IcmpEchoSpec *)(const void *)(source+1U),NULL,0U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,output,sizeof output,
            (UmicomSize *)(void *)(count+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof count;++i)CHECK(count[i]==0x5aU);
        for(size_t i=0U;i<sizeof output;++i)CHECK(output[i]==0xa5U);
    } else if(!strcmp(mode,"address-wrap")) {
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused(&spec,(const void *)(maximum-2U),5U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused((const UmicomEthernetIpv4IcmpEchoSpec *)(maximum-(_Alignof(UmicomEthernetIpv4IcmpEchoSpec)-1U)),NULL,0U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,(void *)(maximum-10U),42U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,NULL,0U,output,sizeof output,
            (UmicomSize *)(maximum-(_Alignof(UmicomSize)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(written==999U);
    } else if(!strcmp(mode,"lifetime")) {
        unsigned char text[5]={'h','e','l','l','o'};
        CHECK(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec,text,5U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        memset(text,0,sizeof text);memset(&spec,0,sizeof spec);
        CHECK(!memcmp(output,golden,sizeof golden));
    } else return 2;
    return 0;
}
