/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/tcp_write_tests.c
 *
 * PURPOSE:
 *   Verify TCP serialization against literal bytes, independent checksum folding and complete-span refusal contracts.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_tcp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); return 1; } } while (0)
static const unsigned char golden[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,45,0x12,0x34,0x40,0,64,6,0x3c,0x60,192,0,2,1,198,51,100,2,
    0x30,0x39,0,80,0x12,0x34,0x56,0x78,0x90,0xab,0xcd,0xef,
    0x50,0x18,0x10,0,0x77,0xed,0,0,'h','e','l','l','o'
};
static UmicomEthernetIpv4TcpSpec Fixture(void)
{
    const UmicomEthernetIpv4TcpSpec s = {
        .destinationMac={2,0,0,0,0,2}, .sourceMac={2,0,0,0,0,1},
        .sourceAddress={192,0,2,1}, .destinationAddress={198,51,100,2},
        .identification=0x1234U, .timeToLive=64U,
        .sourcePort=12345U, .destinationPort=80U,
        .sequenceNumber=0x12345678U, .acknowledgmentNumber=0x90abcdefU,
        .window=4096U, .flags=UMICOM_TCP_ACK|UMICOM_TCP_PSH
    };
    return s;
}
/* Independent checksum oracle folds after every word, then complements. Its
 * explicit pseudo-header differs from the production numeric seed approach. */
static unsigned Checksum(const unsigned char *p, size_t n)
{
    unsigned sum=0U;
    for(size_t i=0U;i<n;i+=2U){
        sum+=((unsigned)p[i]<<8U)|(i+1U<n?p[i+1U]:0U);
        sum=(sum&65535U)+(sum>>16U);
    }
    return ~sum&65535U;
}
static int Checksums(const unsigned char *frame,size_t n)
{
    unsigned char pseudo[1492];
    CHECK(n>=54U && n<=1514U);
    CHECK(Checksum(frame+14U,20U)==0U);
    memcpy(pseudo,frame+26U,8U);pseudo[8]=0U;pseudo[9]=6U;
    pseudo[10]=(unsigned char)((n-34U)>>8U);pseudo[11]=(unsigned char)(n-34U);
    memcpy(pseudo+12U,frame+34U,n-34U);
    CHECK(Checksum(pseudo,n-22U)==0U);
    return 0;
}
static int Refused(const UmicomEthernetIpv4TcpSpec *spec,const void *payload,
    size_t bytes,size_t capacity,UmicomNetworkPacketStatus expected)
{
    unsigned char out[1518],before[sizeof out];size_t written=999U;
    memset(out,0xa5,sizeof out);memcpy(before,out,sizeof out);
    CHECK(UmicomKernelEthernetIpv4TcpWrite(spec,payload,bytes,out,capacity,&written)==expected);
    CHECK(written==999U && !memcmp(out,before,sizeof out));return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *name=argv[1];
    UmicomEthernetIpv4TcpSpec s=Fixture();
    unsigned char payload[1461],out[1518];size_t written=999U;
    for(size_t i=0U;i<sizeof payload;++i)payload[i]=(unsigned char)(i%251U);
    memcpy(payload,"hello",5U);memset(out,0xa5,sizeof out);
    if(!strcmp(name,"golden")){
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(written==sizeof golden && !memcmp(out,golden,sizeof golden));
        CHECK(out[written]==0xa5U && !Checksums(out,written));
    }else if(!strcmp(name,"all-lengths")){
        for(size_t n=0U;n<=1460U;++n){
            memset(out,0xa5,sizeof out);written=999U;
            CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,n?payload:NULL,n,out,54U+n,&written)==UMICOM_NETWORK_PACKET_OK);
            CHECK(written==54U+n && out[written]==0xa5U && !memcmp(out+54U,payload,n));
            CHECK(!Checksums(out,written));UmicomEthernetIpv4TcpView v;
            CHECK(UmicomKernelEthernetIpv4TcpRead(out,written,&v)==UMICOM_NETWORK_PACKET_OK);
            CHECK(v.payloadBytes==n && v.segmentBytes==20U+n && v.ipv4.ipBytes==40U+n);
        }
    }else if(!strcmp(name,"flags")){
        for(unsigned flags=0U;flags<=255U;++flags){s.flags=(UmicomU8)flags;
            CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,NULL,0U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out[46]==0x50U && out[47]==flags && !Checksums(out,written));}
    }else if(!strcmp(name,"fields")){
        s.sourcePort=0U;s.destinationPort=65535U;s.sequenceNumber=0xffffffffU;s.acknowledgmentNumber=0U;
        s.window=65535U;s.urgentPointer=65535U;s.differentiatedServices=255U;s.timeToLive=255U;s.identification=65535U;
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_OK);
        UmicomEthernetIpv4TcpView v;
        CHECK(UmicomKernelEthernetIpv4TcpRead(out,written,&v)==UMICOM_NETWORK_PACKET_OK);
        CHECK(v.sourcePort==0U && v.destinationPort==65535U && v.sequenceNumber==0xffffffffU && v.acknowledgmentNumber==0U);
        CHECK(v.window==65535U && v.urgentPointer==65535U && v.reservedBits==0U);
        CHECK(v.ipv4.identification==65535U && v.ipv4.timeToLive==255U && v.ipv4.differentiatedServices==255U);
        CHECK(v.ipv4.dontFragment==UMICOM_TRUE && !Checksums(out,written));
    }else if(!strcmp(name,"checksum-zero")){
        s.window=0x87edU;
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out[50]==0U && out[51]==0U && !Checksums(out,written));
        UmicomEthernetIpv4TcpView v;CHECK(UmicomKernelEthernetIpv4TcpRead(out,written,&v)==UMICOM_NETWORK_PACKET_OK);
    }else if(!strcmp(name,"capacity")){
        for(size_t n=0U;n<59U;++n)CHECK(!Refused(&s,payload,5U,n,UMICOM_NETWORK_PACKET_LENGTH));
        CHECK(!Refused(&s,payload,1460U,1513U,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(name,"limits")){
        CHECK(!Refused(&s,payload,1461U,sizeof out,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(&s,payload,~(UmicomSize)0U,sizeof out,UMICOM_NETWORK_PACKET_LIMIT));
    }else if(!strcmp(name,"ttl")){
        s.timeToLive=0U;CHECK(!Refused(&s,payload,5U,sizeof out,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(name,"arguments")){
        CHECK(!Refused(NULL,payload,5U,sizeof out,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused(&s,NULL,5U,sizeof out,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,NULL,sizeof out,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(written==999U);
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof out;++i)CHECK(out[i]==0xa5U);
    }else if(!strcmp(name,"overlap")){
        _Alignas(16) unsigned char arena[2048],before[sizeof arena];
        memset(arena,0x5a,sizeof arena);memcpy(before,arena,sizeof arena);
        /* All six pairs of nonempty spans are forbidden, including overlap
         * solely in the unused tail of destination capacity. No input read. */
        CHECK(UmicomKernelEthernetIpv4TcpWrite((void *)arena,payload,5U,arena,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4TcpWrite((void *)arena,payload,5U,out,sizeof out,(void *)arena)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4TcpWrite((void *)arena,arena,5U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,arena,1514U,(void *)(arena+1504U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,arena+1000U,5U,arena,1514U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,arena,5U,out,sizeof out,(void *)arena)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(written==999U && !memcmp(arena,before,sizeof arena));
        for(size_t i=0U;i<sizeof out;++i)CHECK(out[i]==0xa5U);
    }else if(!strcmp(name,"alignment")){
        _Alignas(16) unsigned char storage[sizeof s+16U],counter[sizeof written+16U];
        memset(storage,0x5a,sizeof storage);memset(counter,0x5a,sizeof counter);
        CHECK(!Refused((void *)(storage+1U),payload,5U,sizeof out,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,(void *)(counter+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        for(size_t i=0U;i<sizeof counter;++i)CHECK(counter[i]==0x5aU);
    }else if(!strcmp(name,"address-wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-(_Alignof(UmicomEthernetIpv4TcpSpec)-1U)),payload,5U,sizeof out,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused(&s,(const void *)(maximum-1U),5U,sizeof out,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,(void *)(maximum-1U),59U,&written)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,(void *)(maximum-(_Alignof(UmicomSize)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!Refused(&s,payload,5U,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(written==999U);
    }else if(!strcmp(name,"unaligned")){
        _Alignas(16) unsigned char bytes[80];memcpy(bytes+1U,"hello",5U);
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,bytes+1U,5U,out+1U,59U,&written)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out[0]==0xa5U && out[60]==0xa5U && written==59U && !memcmp(out+1U,golden,59U));
    }else if(!strcmp(name,"lifetime")){
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_OK);
        memset(&s,0,sizeof s);memset(payload,0,sizeof payload);
        CHECK(!memcmp(out,golden,sizeof golden));
    }else if(!strcmp(name,"corruption")){
        CHECK(UmicomKernelEthernetIpv4TcpWrite(&s,payload,5U,out,sizeof out,&written)==UMICOM_NETWORK_PACKET_OK);
        for(size_t i=54U;i<written;++i){UmicomEthernetIpv4TcpView v;out[i]^=1U;
            CHECK(UmicomKernelEthernetIpv4TcpRead(out,written,&v)==UMICOM_NETWORK_PACKET_CHECKSUM);out[i]^=1U;}
    }else return 2;
    return 0;
}
