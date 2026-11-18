/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/tcp_option_values_tests.c
 *
 * PURPOSE:
 *   Verify known TCP option values, strict duplicate policy and atomic refusal independently of connection state.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/tcp_options.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s%c",__LINE__,#c,10); return 1; } } while (0)
static const unsigned char golden[20] = {
    2,4,0x05,0xB4, 3,3,15, 4,2, 8,10,0x11,0x22,0x33,0x44,0x88,0x99,0xAA,0xBB,0
};
static int Refused(const void *bytes, UmicomSize size, UmicomNetworkPacketStatus expected)
{
    UmicomTcpOptionValues out;
    unsigned char before[sizeof out];
    memset(&out,0xA5,sizeof out);memcpy(before,&out,sizeof before);
    CHECK(UmicomKernelTcpOptionValuesRead(bytes,size,&out)==expected);
    CHECK(memcmp(before,&out,sizeof out)==0);
    return 0;
}
static int Absent(const UmicomTcpOptionValues *out)
{
    return !out->hasMss && !out->hasWindowScale && !out->sackPermitted && !out->hasTimestamp &&
        !out->mss && !out->windowScaleRaw && !out->windowScaleBounded &&
        !out->timestampValue && !out->timestampEcho && !out->unknownCount;
}
static int Expected(const UmicomTcpOptionValues *out)
{
    return out->hasMss && out->mss==1460 && out->hasWindowScale &&
        out->windowScaleRaw==15 && out->windowScaleBounded==14 &&
        out->sackPermitted && out->hasTimestamp &&
        out->timestampValue==0x11223344U && out->timestampEcho==0x8899AABBU &&
        !out->unknownCount;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    const char *mode=argv[1];
    unsigned char bytes[40]={0};
    UmicomTcpOptionValues out;
    if(!strcmp(mode,"golden")) {
        CHECK(UmicomKernelTcpOptionValuesRead(golden,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(Expected(&out));
    } else if(!strcmp(mode,"empty")) {
        memset(&out,0xA5,sizeof out);
        CHECK(UmicomKernelTcpOptionValuesRead(NULL,0,&out)==UMICOM_NETWORK_PACKET_OK && Absent(&out));
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK && Absent(&out));
        memset(bytes,1,sizeof bytes);
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK && Absent(&out));
    } else if(!strcmp(mode,"mss-values")) {
        bytes[0]=2;bytes[1]=4;
        /* Bytewise decoding must not swap host and network order. Include zero
         * as a copied value, without silently replacing it with a default. */
        for(unsigned value=0;value<65536U;++value) {
            bytes[2]=(unsigned char)(value>>8U);bytes[3]=(unsigned char)value;
            CHECK(UmicomKernelTcpOptionValuesRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.hasMss && out.mss==value && !out.hasTimestamp && !out.unknownCount);
        }
    } else if(!strcmp(mode,"scale-values")) {
        bytes[0]=3;bytes[1]=3;
        for(unsigned value=0;value<256U;++value) {
            bytes[2]=(unsigned char)value;
            CHECK(UmicomKernelTcpOptionValuesRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.hasWindowScale && out.windowScaleRaw==value &&
                out.windowScaleBounded==(value>14U?14U:value));
        }
    } else if(!strcmp(mode,"timestamp-bits")) {
        bytes[0]=8;bytes[1]=10;
        for(unsigned bit=0;bit<32U;++bit) {
            memset(bytes+2,0,8);
            bytes[2U+3U-bit/8U]=(unsigned char)(1U<<(bit%8U));
            bytes[6U+3U-bit/8U]=(unsigned char)(1U<<(bit%8U));
            CHECK(UmicomKernelTcpOptionValuesRead(bytes,12,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.hasTimestamp && out.timestampValue==((UmicomU32)1U<<bit) &&
                out.timestampEcho==((UmicomU32)1U<<bit));
        }
        memset(bytes+2,0xFF,8);
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,12,&out)==UMICOM_NETWORK_PACKET_OK &&
            out.timestampValue==0xFFFFFFFFU && out.timestampEcho==0xFFFFFFFFU);
        memset(bytes+2,0,8);
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,12,&out)==UMICOM_NETWORK_PACKET_OK &&
            out.hasTimestamp && !out.timestampValue && !out.timestampEcho);
    } else if(!strcmp(mode,"sack-permitted")) {
        bytes[0]=4;bytes[1]=2;
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK &&
            out.sackPermitted && !out.hasMss && !out.unknownCount);
    } else if(!strcmp(mode,"permutations")) {
        const unsigned offset[4]={0,4,7,9},length[4]={4,3,2,10};
        for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)
        for(unsigned c=0;c<4;++c)for(unsigned d=0;d<4;++d) {
            if(a==b||a==c||a==d||b==c||b==d||c==d)continue;
            const unsigned order[4]={a,b,c,d};
            unsigned at=0;
            memset(bytes,0,sizeof bytes);
            for(unsigned i=0;i<4;++i) {
                memcpy(bytes+at,golden+offset[order[i]],length[order[i]]);
                at+=length[order[i]];
            }
            CHECK(at==19 && UmicomKernelTcpOptionValuesRead(bytes,20,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(Expected(&out));
        }
    } else if(!strcmp(mode,"opaque")) {
        for(unsigned kind=2;kind<256U;++kind) {
            if(kind==2||kind==3||kind==4||kind==8)continue;
            bytes[0]=(unsigned char)kind;bytes[1]=2;
            CHECK(UmicomKernelTcpOptionValuesRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.unknownCount==1 && !out.hasMss && !out.sackPermitted && !out.hasTimestamp);
        }
        /* A kind-5 area is opaque here, even with no SACK block payload. */
        bytes[0]=5;bytes[1]=2;bytes[2]=5;bytes[3]=2;
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK &&
            out.unknownCount==2 && !out.sackPermitted);
    } else if(!strcmp(mode,"maximum-opaque")) {
        for(unsigned i=0;i<40;i+=2) {bytes[i]=253;bytes[i+1U]=2;}
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK && out.unknownCount==20);
        memset(bytes,0xFF,sizeof bytes);bytes[0]=253;bytes[1]=40;
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK && out.unknownCount==1);
    } else if(!strcmp(mode,"known-lengths")) {
        const unsigned kinds[4]={2,3,4,8},lengths[4]={4,3,2,10};
        for(unsigned k=0;k<4;++k)for(unsigned n=2;n<=40;++n) {
            if(n==lengths[k])continue;
            memset(bytes,0,sizeof bytes);bytes[0]=(unsigned char)kinds[k];bytes[1]=(unsigned char)n;
            UmicomTcpOptionsView structure;
            CHECK(UmicomKernelTcpOptionsRead(bytes,40,&structure)==UMICOM_NETWORK_PACKET_OK);
            CHECK(!Refused(bytes,40,UMICOM_NETWORK_PACKET_HEADER));
        }
    } else if(!strcmp(mode,"duplicates")) {
        const unsigned offset[4]={0,4,7,9},length[4]={4,3,2,10};
        for(unsigned i=0;i<4;++i) {
            memset(bytes,0,sizeof bytes);
            memcpy(bytes,golden+offset[i],length[i]);memcpy(bytes+length[i],golden+offset[i],length[i]);
            CHECK(!Refused(bytes,40,UMICOM_NETWORK_PACKET_HEADER));
        }
    } else if(!strcmp(mode,"late-refusal")) {
        memcpy(bytes,golden,19);bytes[19]=2;bytes[20]=2;
        CHECK(!Refused(bytes,24,UMICOM_NETWORK_PACKET_HEADER));
        memcpy(bytes,golden,20);bytes[23]=1;
        CHECK(!Refused(bytes,24,UMICOM_NETWORK_PACKET_HEADER));
    } else if(!strcmp(mode,"structural-refusal")) {
        bytes[0]=253;bytes[1]=5;
        CHECK(!Refused(bytes,4,UMICOM_NETWORK_PACKET_LENGTH));
        bytes[1]=1;
        CHECK(!Refused(bytes,4,UMICOM_NETWORK_PACKET_HEADER));
        memset(bytes,1,4);bytes[3]=253;
        CHECK(!Refused(bytes,4,UMICOM_NETWORK_PACKET_LENGTH));
        CHECK(!Refused(bytes,3,UMICOM_NETWORK_PACKET_HEADER));
        CHECK(!Refused(bytes,41,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(bytes,~(UmicomSize)0,UMICOM_NETWORK_PACKET_LIMIT));
    } else if(!strcmp(mode,"unaligned")) {
        _Alignas(16) unsigned char storage[21];
        memcpy(storage+1,golden,20);
        CHECK(UmicomKernelTcpOptionValuesRead(storage+1,20,&out)==UMICOM_NETWORK_PACKET_OK && Expected(&out));
    } else if(!strcmp(mode,"overlap")) {
        _Alignas(UmicomTcpOptionValues) unsigned char storage[sizeof out+40],before[sizeof out+40];
        memset(storage,0,sizeof storage);memcpy(before,storage,sizeof before);
        CHECK(UmicomKernelTcpOptionValuesRead(storage,4,(UmicomTcpOptionValues *)storage)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,before,sizeof storage)==0);
        CHECK(UmicomKernelTcpOptionValuesRead(storage+sizeof out-1U,4,(UmicomTcpOptionValues *)storage)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,before,sizeof storage)==0);
        CHECK(UmicomKernelTcpOptionValuesRead(storage+sizeof out,4,(UmicomTcpOptionValues *)storage)==UMICOM_NETWORK_PACKET_OK);
    } else if(!strcmp(mode,"output-alignment")) {
        _Alignas(UmicomTcpOptionValues) unsigned char storage[sizeof out+1],before[sizeof out+1];
        memset(storage,0xA5,sizeof storage);memcpy(before,storage,sizeof before);
        CHECK(UmicomKernelTcpOptionValuesRead(golden,20,(UmicomTcpOptionValues *)(storage+1))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,before,sizeof storage)==0);
    } else if(!strcmp(mode,"address-wrap")) {
        const UmicomAddress high=~(UmicomAddress)0;
        CHECK(!Refused((const void *)(high-3U),4,UMICOM_NETWORK_PACKET_ARGUMENT));
        const UmicomAddress output=(high-sizeof out+1U)&~((UmicomAddress)_Alignof(UmicomTcpOptionValues)-1U);
        CHECK(UmicomKernelTcpOptionValuesRead(golden,20,(UmicomTcpOptionValues *)output)==UMICOM_NETWORK_PACKET_ARGUMENT);
    } else if(!strcmp(mode,"ownership")) {
        memcpy(bytes,golden,20);
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,20,&out)==UMICOM_NETWORK_PACKET_OK && Expected(&out));
        CHECK(memcmp(bytes,golden,20)==0);
        memset(bytes,0,sizeof bytes);CHECK(Expected(&out));
        memset(&out,0xA5,sizeof out);
        CHECK(UmicomKernelTcpOptionValuesRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK && Absent(&out));
    } else if(!strcmp(mode,"arguments")) {
        CHECK(!Refused(NULL,4,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelTcpOptionValuesRead(golden,20,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    } else return 2;
    return 0;
}
