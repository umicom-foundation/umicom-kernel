/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/udp_write_tests.c
 *
 * PURPOSE:
 *   Verify UDP encoding against fixed wire bytes, admission and unchanged outputs on rejected spans.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_udp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static const unsigned char golden[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,33,0x12,0x34,0x40,0,64,17,0x3c,0x61,
    192,0,2,1,198,51,100,2,0x30,0x39,0,0x35,0,13,0x9f,0x5c,
    'h','e','l','l','o'
};
static UmicomEthernetIpv4UdpSpec MakeSpec(void)
{
    UmicomEthernetIpv4UdpSpec spec;
    memset(&spec,0,sizeof spec);
    memcpy(spec.destinationMac,golden,6U);memcpy(spec.sourceMac,golden+6U,6U);
    memcpy(spec.sourceAddress,golden+26U,4U);memcpy(spec.destinationAddress,golden+30U,4U);
    spec.sourcePort=12345U;spec.destinationPort=53U;spec.identification=0x1234U;spec.timeToLive=64U;
    return spec;
}
static int Refused(const UmicomEthernetIpv4UdpSpec *spec,const void *payload,
    UmicomSize length,UmicomSize capacity,UmicomNetworkPacketStatus expected)
{
    unsigned char bytes[1514],before[1514];UmicomSize written=999U;
    memset(bytes,0x5a,sizeof bytes);memcpy(before,bytes,sizeof bytes);
    CHECK(UmicomKernelEthernetIpv4UdpWrite(spec,payload,length,bytes,capacity,&written)==expected);
    CHECK(written==999U&&!memcmp(bytes,before,sizeof bytes));
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];
    UmicomEthernetIpv4UdpSpec spec=MakeSpec();
    unsigned char output[1514],payload[1472];UmicomSize written=999U;
    memset(output,0xa5,sizeof output);memset(payload,0xff,sizeof payload);
    UmicomEthernetIpv4UdpView view;
    if(!strcmp(mode,"golden")) {
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,"hello",5U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(written==sizeof golden&&!memcmp(output,golden,sizeof golden));
        for(size_t i=sizeof golden;i<sizeof output;++i)CHECK(output[i]==0xa5U);
        CHECK(UmicomKernelEthernetIpv4UdpRead(output,written,UMICOM_UDP_CHECKSUM_REQUIRE,&view)==UMICOM_NETWORK_PACKET_OK);
    } else if(!strcmp(mode,"all-lengths")) {
        for(UmicomSize size=0U;size<=sizeof payload;++size) {
            memset(output,0xa5,sizeof output);
            CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,size?payload:NULL,size,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
            CHECK(written==42U+size&&!memcmp(output+42U,payload,(size_t)size));
            CHECK(UmicomKernelEthernetIpv4UdpRead(output,written,UMICOM_UDP_CHECKSUM_REQUIRE,&view)==UMICOM_NETWORK_PACKET_OK);
            CHECK(view.payloadBytes==size&&view.checksumPresent==UMICOM_TRUE&&view.ipv4.dontFragment==UMICOM_TRUE);
            if(written<sizeof output)CHECK(output[written]==0xa5U);
        }
    } else if(!strcmp(mode,"capacity")) {
        for(UmicomSize capacity=0U;capacity<sizeof golden;++capacity)
            CHECK(!Refused(&spec,"hello",5U,capacity,UMICOM_NETWORK_PACKET_LENGTH));
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,"hello",5U,output,sizeof golden,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(written==sizeof golden&&output[sizeof golden]==0xa5U);
    } else if(!strcmp(mode,"limits")) {
        CHECK(!Refused(&spec,payload,1473U,sizeof output,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(&spec,payload,~(UmicomSize)0U,sizeof output,UMICOM_NETWORK_PACKET_LIMIT));
    } else if(!strcmp(mode,"ttl")) {
        spec.timeToLive=0U;CHECK(!Refused(&spec,"hello",5U,sizeof output,UMICOM_NETWORK_PACKET_HEADER));
        for(unsigned ttl=1U;ttl<=255U;++ttl) {
            spec.timeToLive=(UmicomU8)ttl;
            CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
            CHECK(UmicomKernelEthernetIpv4UdpRead(output,written,UMICOM_UDP_CHECKSUM_REQUIRE,&view)==UMICOM_NETWORK_PACKET_OK);
            CHECK(view.ipv4.timeToLive==ttl);
        }
    } else if(!strcmp(mode,"fields")) {
        spec.sourcePort=0U;spec.destinationPort=65535U;spec.identification=65535U;spec.differentiatedServices=255U;
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(UmicomKernelEthernetIpv4UdpRead(output,written,UMICOM_UDP_CHECKSUM_REQUIRE,&view)==UMICOM_NETWORK_PACKET_OK);
        CHECK(view.sourcePort==0U&&view.destinationPort==65535U&&view.ipv4.identification==65535U);
        CHECK(view.ipv4.differentiatedServices==255U&&!memcmp(view.ipv4.sourceMac,spec.sourceMac,6U));
        CHECK(!memcmp(view.ipv4.destinationAddress,spec.destinationAddress,4U));
    } else if(!strcmp(mode,"checksum-zero")) {
        const unsigned char zeroPayload[2]={0xe3U,0x34U};
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,zeroPayload,2U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(output[40]==255U&&output[41]==255U);
        CHECK(UmicomKernelEthernetIpv4UdpRead(output,written,UMICOM_UDP_CHECKSUM_REQUIRE,&view)==UMICOM_NETWORK_PACKET_OK);
    } else if(!strcmp(mode,"unaligned")) {
        _Alignas(16) unsigned char storage[1515],source[6];memcpy(source+1U,"hello",5U);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,source+1U,5U,storage+1U,1514U,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(!memcmp(storage+1U,golden,sizeof golden));
    } else if(!strcmp(mode,"arguments")) {
        CHECK(!Refused(NULL,"hello",5U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused(&spec,NULL,1U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,NULL,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(written==999U);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,output,sizeof output,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof output;++i)CHECK(output[i]==0xa5U);
    } else if(!strcmp(mode,"overlap")) {
        union { UmicomSize count; UmicomEthernetIpv4UdpSpec spec; unsigned char bytes[1600]; } store;
        unsigned char before[sizeof store];memset(&store,0x5a,sizeof store);store.spec=spec;memcpy(before,&store,sizeof store);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&store.spec,"hello",5U,store.bytes,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store)&&written==999U);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,store.bytes+10U,5U,store.bytes,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store)&&written==999U);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,store.bytes,1514U,&store.count)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store));
        /* Even unused destination capacity must not alias the input. */
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,store.bytes+1500U,5U,store.bytes,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&store,sizeof store));
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,&spec,1U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,&written,1U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof output;++i)CHECK(output[i]==0xa5U);
    } else if(!strcmp(mode,"alignment")) {
        _Alignas(16) unsigned char source[sizeof spec+1U],count[sizeof written+1U];
        memcpy(source+1U,&spec,sizeof spec);memset(count,0x5a,sizeof count);
        CHECK(!Refused((const UmicomEthernetIpv4UdpSpec *)(const void *)(source+1U),NULL,0U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,output,sizeof output,
            (UmicomSize *)(void *)(count+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof count;++i)CHECK(count[i]==0x5aU);
        for(size_t i=0U;i<sizeof output;++i)CHECK(output[i]==0xa5U);
    } else if(!strcmp(mode,"address-wrap")) {
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused(&spec,(const void *)(maximum-2U),5U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused((const UmicomEthernetIpv4UdpSpec *)(maximum-(_Alignof(UmicomEthernetIpv4UdpSpec)-1U)),NULL,0U,sizeof output,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,(void *)(maximum-10U),42U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,NULL,0U,output,sizeof output,
            (UmicomSize *)(maximum-(_Alignof(UmicomSize)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(written==999U);
    } else if(!strcmp(mode,"lifetime")) {
        unsigned char text[5]={'h','e','l','l','o'};
        CHECK(UmicomKernelEthernetIpv4UdpWrite(&spec,text,5U,output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK);
        memset(text,0,sizeof text);memset(&spec,0,sizeof spec);
        CHECK(!memcmp(output,golden,sizeof golden));
    } else return 2;
    return 0;
}
