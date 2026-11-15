/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/tcp_tests.c
 *
 * PURPOSE:
 *   Verify TCP capture fields, mandatory pseudo-header checksums, strict bounds and unchanged refused outputs.
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
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); return 1; } } while (0)

/* Independent fixture: documentation addresses, IP checksum 3c60 and TCP
 * checksum 77ed. ACK/PSH and the five payload bytes are observations only. */
static const unsigned char golden[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,45,0x12,0x34,0x40,0,64,6,0x3c,0x60,
    192,0,2,1,198,51,100,2,
    0x30,0x39,0,80,0x12,0x34,0x56,0x78,0x90,0xab,0xcd,0xef,
    0x50,0x18,0x10,0,0x77,0xed,0,0,'h','e','l','l','o'
};
static void Put16(unsigned char *p, unsigned value)
{
    p[0]=(unsigned char)(value>>8U); p[1]=(unsigned char)value;
}
static unsigned Checksum(const unsigned char *p, size_t length)
{
    /* Fold every word, independently of the production final-fold loop. */
    unsigned sum=0U;
    for(size_t i=0U;i<length;i+=2U){
        sum+=((unsigned)p[i]<<8U)|(i+1U<length?p[i+1U]:0U);
        sum=(sum&65535U)+(sum>>16U);
    }
    return ~sum&65535U;
}
static void IpChecksum(unsigned char *frame)
{
    frame[24]=0U;frame[25]=0U;Put16(frame+24U,Checksum(frame+14U,20U));
}
static void TcpChecksum(unsigned char *frame,size_t length)
{
    /* Construct the conceptual header explicitly instead of sharing parser
     * code. Callers in this test keep length between 20 and 1480 bytes. */
    unsigned char input[1492];
    memcpy(input,frame+26U,8U);
    input[8]=0U;input[9]=6U;Put16(input+10U,(unsigned)length);
    frame[50]=0U;frame[51]=0U;
    memcpy(input+12U,frame+34U,length);
    Put16(frame+50U,Checksum(input,12U+length));
}
static int Refused(const void *frame,size_t length,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv4TcpView out; unsigned char before[sizeof out];
    memset(&out,0xa5,sizeof out);memcpy(before,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv4TcpRead(frame,length,&out)==expected);
    CHECK(!memcmp(before,&out,sizeof out));
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *name=argv[1];
    unsigned char frame[1514];memset(frame,0,sizeof frame);memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv4TcpView out;
    if(!strcmp(name,"golden")){
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sourcePort==12345U && out.destinationPort==80U);
        CHECK(out.sequenceNumber==0x12345678U && out.acknowledgmentNumber==0x90abcdefU);
        CHECK(out.flags==(UMICOM_TCP_ACK|UMICOM_TCP_PSH) && out.reservedBits==0U);
        CHECK(out.window==4096U && out.urgentPointer==0U);
        CHECK(out.segmentBytes==25U && out.payloadOffset==54U && out.payloadBytes==5U);
        CHECK(out.ipv4.protocol==6U && out.ipv4.ipBytes==45U && out.ipv4.sourceAddress[0]==192U);
        CHECK(!memcmp(frame,golden,sizeof golden));
    }else if(!strcmp(name,"empty")||!strcmp(name,"even")||!strcmp(name,"maximum")){
        size_t length=!strcmp(name,"empty")?20U:!strcmp(name,"even")?24U:1480U;
        Put16(frame+16U,(unsigned)length+20U);IpChecksum(frame);TcpChecksum(frame,length);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,34U+length,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.segmentBytes==length && out.payloadBytes==length-20U);
    }else if(!strcmp(name,"padding")){
        memset(frame+sizeof golden,0xff,sizeof frame-sizeof golden);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof frame,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes==5U && out.segmentBytes==25U);
    }else if(!strcmp(name,"unaligned")){
        _Alignas(16) unsigned char bytes[sizeof golden+1U];memcpy(bytes+1U,golden,sizeof golden);
        CHECK(UmicomKernelEthernetIpv4TcpRead(bytes+1U,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sequenceNumber==0x12345678U);
    }else if(!strcmp(name,"truncation")){
        for(size_t n=0U;n<sizeof golden;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(name,"short_header")){
        for(unsigned n=0U;n<20U;++n){Put16(frame+16U,20U+n);IpChecksum(frame);
            CHECK(!Refused(frame,34U+n,UMICOM_NETWORK_PACKET_LENGTH));}
    }else if(!strcmp(name,"data_offset")){
        for(unsigned n=0U;n<5U;++n){frame[46]=(unsigned char)(n<<4U);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(name,"options")){
        frame[46]=0x60U;TcpChecksum(frame,25U);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(name,"option_truncation")){
        frame[46]=0xf0U;
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(name,"protocol")){
        frame[23]=17U;IpChecksum(frame);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(name,"ip_checksum")){
        frame[24]^=1U;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(name,"fragments")){
        Put16(frame+20U,0x2000U);IpChecksum(frame);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_FRAGMENT));
    }else if(!strcmp(name,"corruption")){
        for(size_t offset=34U;offset<sizeof golden;++offset)for(unsigned bit=0U;bit<8U;++bit){
            if(offset==46U)continue; /* Header-shape errors have their own cases. */
            memcpy(frame,golden,sizeof golden);frame[offset]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        }
    }else if(!strcmp(name,"source_address")||!strcmp(name,"destination_address")){
        frame[!strcmp(name,"source_address")?26U:30U]^=1U;IpChecksum(frame);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(name,"checksum_zero")){
        Put16(frame+50U,0U);
        CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));
        /* Adding the old checksum into the window makes zero a valid checksum
         * field. It still passes only because the complete sum validates. */
        Put16(frame+48U,0x87edU);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.window==0x87edU);
    }else if(!strcmp(name,"flags")){
        for(unsigned flags=0U;flags<=255U;++flags){
            frame[47]=(unsigned char)flags;TcpChecksum(frame,25U);
            CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.flags==flags);
        }
    }else if(!strcmp(name,"reserved")){
        for(unsigned bits=0U;bits<16U;++bits){
            frame[46]=(unsigned char)(0x50U|bits);TcpChecksum(frame,25U);
            CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.reservedBits==bits && out.payloadOffset==54U);
        }
    }else if(!strcmp(name,"ports")){
        Put16(frame+34U,0U);Put16(frame+36U,65535U);TcpChecksum(frame,25U);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sourcePort==0U && out.destinationPort==65535U);
    }else if(!strcmp(name,"sequence")){
        memset(frame+38U,0xff,4U);memset(frame+42U,0,4U);TcpChecksum(frame,25U);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sequenceNumber==0xffffffffU && out.acknowledgmentNumber==0U);
    }else if(!strcmp(name,"window_urgent")){
        Put16(frame+48U,65535U);Put16(frame+52U,65535U);TcpChecksum(frame,25U);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.window==65535U && out.urgentPointer==65535U);
    }else if(!strcmp(name,"extra_ip_payload")){
        Put16(frame+16U,46U);frame[59]=0x5aU;IpChecksum(frame);
        CHECK(!Refused(frame,60U,UMICOM_NETWORK_PACKET_CHECKSUM));
    }else if(!strcmp(name,"arguments")){
        CHECK(!Refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(name,"overlap")){
        union {unsigned char bytes[256];UmicomEthernetIpv4TcpView view;} storage;
        unsigned char before[sizeof storage];
        memset(&storage,0,sizeof storage);memcpy(storage.bytes,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv4TcpRead(storage.bytes,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        memcpy(storage.bytes+8U,golden,sizeof golden);memcpy(before,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv4TcpRead(storage.bytes+8U,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,&storage,sizeof storage));
        _Alignas(16) unsigned char capture[256];memset(capture,0,sizeof capture);memcpy(capture,golden,sizeof golden);
        unsigned char saved[sizeof capture];memcpy(saved,capture,sizeof capture);
        CHECK(UmicomKernelEthernetIpv4TcpRead(capture,sizeof capture,
            (UmicomEthernetIpv4TcpView *)(void *)(capture+64U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(saved,capture,sizeof capture));
    }else if(!strcmp(name,"output_alignment")){
        _Alignas(16) unsigned char storage[sizeof out+16U];unsigned char before[sizeof storage];
        memset(storage,0x5a,sizeof storage);memcpy(before,storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,
            (UmicomEthernetIpv4TcpView *)(void *)(storage+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,storage,sizeof storage));
    }else if(!strcmp(name,"address_wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,
            (UmicomEthernetIpv4TcpView *)(maximum-(_Alignof(UmicomEthernetIpv4TcpView)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(name,"lifetime")){
        CHECK(UmicomKernelEthernetIpv4TcpRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        memset(frame,0,sizeof frame);
        CHECK(out.sequenceNumber==0x12345678U && out.ipv4.sourceAddress[0]==192U && out.payloadBytes==5U);
    }else if(!strcmp(name,"capture_limit")){
        CHECK(!Refused(frame,1515U,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(frame,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_LIMIT));
    }else return 2;
    return 0;
}
