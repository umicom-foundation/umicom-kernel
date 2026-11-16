/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/tcp6_tests.c
 *
 * PURPOSE:
 *   Verify IPv6 TCP framing, pseudo-header scope, field observations and refusal ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_tcp6.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
/* Independent documentation-address vector: TCP checksum 08b0 for hello. */
static const unsigned char golden[]={0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x01,0x00,0x19,0x06,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x30,0x39,0x00,0x50,0x12,0x34,0x56,0x78,0x90,0xab,0xcd,0xef,0x50,0x18,0x10,0x00,0x08,0xb0,0x00,0x00,0x68,0x65,0x6c,0x6c,0x6f};
static const unsigned char zeroCapture[]={0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x01,0x00,0x16,0x06,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0xbb,0xc0,0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x50,0x12,0x4f,0x46,0x00,0x00,0x00,0x00,0x42,0x19};
static void Put16(unsigned char *p,unsigned value){p[0]=(unsigned char)(value>>8U);p[1]=(unsigned char)value;}
static unsigned Sum(const unsigned char *p,size_t length,unsigned sum)
{
    for(size_t i=0U;i<length;i+=2U){sum+=((unsigned)p[i]<<8U)|(i+1U<length?p[i+1U]:0U);sum=(sum&65535U)+(sum>>16U);}
    return sum;
}
static void Checksum(unsigned char *frame,size_t length,unsigned protocol)
{
    /* Construct the complete conceptual header and fold each word. No shared
     * production checksum code or omitted address words can bless a fixture. */
    unsigned char input[1500]={0};
    memcpy(input,frame+22U,32U);Put16(input+34U,(unsigned)length);input[39]=(unsigned char)protocol;
    frame[70]=frame[71]=0U;memcpy(input+40U,frame+54U,length);
    unsigned sum=Sum(input,40U+length,0U);while(sum>>16U)sum=(sum&65535U)+(sum>>16U);
    Put16(frame+70U,~sum&65535U);
}
static int Refused(const void *frame,size_t length,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv6TcpView out;unsigned char before[sizeof out];
    memset(&out,0xa5,sizeof out);memcpy(before,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv6TcpRead(frame,length,&out)==expected);
    CHECK(!memcmp(before,&out,sizeof out));return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *mode=argv[1];unsigned char frame[1515]={0};memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv6TcpView out;
    if(!strcmp(mode,"golden")){
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sourcePort==12345U && out.destinationPort==80U && out.sequenceNumber==0x12345678U && out.acknowledgmentNumber==0x90abcdefU);
        CHECK(out.flags==(UMICOM_TCP_ACK|UMICOM_TCP_PSH) && out.reservedBits==0U && out.window==4096U && out.urgentPointer==0U);
        CHECK(out.segmentBytes==25U && out.payloadOffset==74U && out.payloadBytes==5U && out.ipv6.nextHeader==6U);
        CHECK(!memcmp(out.ipv6.sourceAddress,golden+22U,16U) && !memcmp(out.ipv6.destinationAddress,golden+38U,16U));
        CHECK(!memcmp(frame,golden,sizeof golden));
    }else if(!strcmp(mode,"empty")||!strcmp(mode,"even")||!strcmp(mode,"maximum")){
        unsigned length=!strcmp(mode,"empty")?20U:!strcmp(mode,"even")?24U:1460U;
        Put16(frame+18U,length);if(length==1460U)memset(frame+74U,0xff,1440U);Checksum(frame,length,6U);
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,54U+length,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.segmentBytes==length && out.payloadBytes==length-20U);
    }else if(!strcmp(mode,"padding")){
        memset(frame+sizeof golden,0xff,1514U-sizeof golden);
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,1514U,&out)==UMICOM_NETWORK_PACKET_OK && out.payloadBytes==5U);
    }else if(!strcmp(mode,"unaligned")){
        _Alignas(16) unsigned char bytes[sizeof golden+1U];memcpy(bytes+1U,golden,sizeof golden);
        CHECK(UmicomKernelEthernetIpv6TcpRead(bytes+1U,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
    }else if(!strcmp(mode,"truncation")){
        for(size_t n=0U;n<sizeof golden;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"short-header")){
        for(unsigned n=0U;n<20U;++n){Put16(frame+18U,n);CHECK(!Refused(frame,54U+n,UMICOM_NETWORK_PACKET_LENGTH));}
    }else if(!strcmp(mode,"data-offset")){
        for(unsigned n=0U;n<5U;++n){frame[66]=(unsigned char)(n<<4U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(mode,"options")){
        frame[66]=0x60U;Checksum(frame,25U,6U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"option-truncation")){
        frame[66]=0xf0U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"protocol")){
        frame[20]=17U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"fragment")){
        frame[20]=44U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_FRAGMENT));
    }else if(!strcmp(mode,"extension")){
        frame[20]=0U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"corruption")){
        for(size_t offset=54U;offset<sizeof golden;++offset)for(unsigned bit=0U;bit<8U;++bit){
            if(offset==66U && bit>=4U)continue;
            memcpy(frame,golden,sizeof golden);frame[offset]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        }
    }else if(!strcmp(mode,"source-address")||!strcmp(mode,"destination-address")){
        size_t start=!strcmp(mode,"source-address")?22U:38U;
        for(size_t offset=start;offset<start+16U;++offset)for(unsigned bit=0U;bit<8U;++bit){
            memcpy(frame,golden,sizeof golden);frame[offset]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        }
    }else if(!strcmp(mode,"zero-checksum")){
        frame[70]=frame[71]=0U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"valid-zero")){
        CHECK(UmicomKernelEthernetIpv6TcpRead(zeroCapture,sizeof zeroCapture,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.window==0x4f46U && out.payloadBytes==2U && out.sequenceNumber==0x10203040U);
    }else if(!strcmp(mode,"flags")||!strcmp(mode,"reserved")){
        unsigned limit=!strcmp(mode,"flags")?256U:16U;
        for(unsigned n=0U;n<limit;++n){
            if(limit==256U)frame[67]=(unsigned char)n;else frame[66]=(unsigned char)(0x50U|n);
            Checksum(frame,25U,6U);CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK((limit==256U?out.flags:out.reservedBits)==n);
        }
    }else if(!strcmp(mode,"ports")){
        Put16(frame+54U,0U);Put16(frame+56U,65535U);Checksum(frame,25U,6U);
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK && out.sourcePort==0U && out.destinationPort==65535U);
    }else if(!strcmp(mode,"sequence")){
        memset(frame+58U,0xff,4U);memset(frame+62U,0,4U);Checksum(frame,25U,6U);
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK && out.sequenceNumber==0xffffffffU && out.acknowledgmentNumber==0U);
    }else if(!strcmp(mode,"window-urgent")){
        Put16(frame+68U,65535U);Put16(frame+72U,65535U);Checksum(frame,25U,6U);
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK && out.window==65535U && out.urgentPointer==65535U);
    }else if(!strcmp(mode,"extra-payload")){
        Put16(frame+18U,26U);frame[79]=0x5aU;CHECK(!Refused(frame,80U,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"wrong-pseudo-protocol")){
        Checksum(frame,25U,17U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(mode,"metadata-scope")){
        frame[0]^=1U;frame[15]^=0x10U;frame[17]^=1U;frame[21]=0U;
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK && out.ipv6.hopLimit==0U);
    }else if(!strcmp(mode,"arguments")){
        CHECK(!Refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"overlap")){
        _Alignas(16) unsigned char bytes[512],saved[512];memset(bytes,0,sizeof bytes);memcpy(bytes,golden,sizeof golden);memcpy(saved,bytes,sizeof bytes);
        CHECK(UmicomKernelEthernetIpv6TcpRead(bytes,sizeof golden,(UmicomEthernetIpv6TcpView *)(void *)bytes)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(bytes,saved,sizeof bytes));
        CHECK(UmicomKernelEthernetIpv6TcpRead(bytes,sizeof bytes,(UmicomEthernetIpv6TcpView *)(void *)(bytes+256U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(bytes,saved,sizeof bytes));
    }else if(!strcmp(mode,"output-alignment")){
        _Alignas(16) unsigned char bytes[sizeof out+16U],saved[sizeof bytes];memset(bytes,0xa5,sizeof bytes);memcpy(saved,bytes,sizeof bytes);
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,(UmicomEthernetIpv6TcpView *)(void *)(bytes+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(bytes,saved,sizeof bytes));
    }else if(!strcmp(mode,"address-wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,(UmicomEthernetIpv6TcpView *)(maximum-(_Alignof(UmicomEthernetIpv6TcpView)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"lifetime")){
        CHECK(UmicomKernelEthernetIpv6TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);memset(frame,0,sizeof frame);
        CHECK(out.sequenceNumber==0x12345678U && out.ipv6.sourceAddress[15]==1U && out.payloadBytes==5U);
    }else if(!strcmp(mode,"capture-limit")){
        CHECK(!Refused(frame,1515U,UMICOM_NETWORK_PACKET_LIMIT));CHECK(!Refused(frame,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_LIMIT));
    }else return 2;
    return 0;
}
