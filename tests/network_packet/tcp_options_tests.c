/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/tcp_options_tests.c
 *
 * PURPOSE:
 *   Check bounded TCP option coordinates, forward-compatible opaque kinds and unchanged refused metadata.
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
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c); return 1; } } while (0)
static int Refused(const void *bytes,UmicomSize length,UmicomNetworkPacketStatus expected)
{
    UmicomTcpOptionsView out;
    unsigned char before[sizeof out];
    memset(&out,0xA5,sizeof out);memcpy(before,&out,sizeof before);
    CHECK(UmicomKernelTcpOptionsRead(bytes,length,&out)==expected);
    CHECK(memcmp(before,&out,sizeof out)==0);
    return 0;
}
static int UnusedZero(const UmicomTcpOptionsView *out)
{
    for(UmicomSize i=out->count;i<UMICOM_TCP_OPTIONS_MAX_BYTES;++i)
        if(out->entries[i].kind || out->entries[i].length || out->entries[i].offset)return 0;
    return 1;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    const char *mode=argv[1];
    unsigned char bytes[40]={1,2,4,0x05,0xB4,1,0,0};
    UmicomTcpOptionsView out;
    if(!strcmp(mode,"golden")) {
        unsigned char before[sizeof bytes];memcpy(before,bytes,sizeof before);
        CHECK(UmicomKernelTcpOptionsRead(bytes,8,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==3 && out.consumedBytes==7 && out.paddingBytes==1 && out.endSeen);
        CHECK(out.entries[0].kind==1 && out.entries[0].length==1 && out.entries[0].offset==0);
        CHECK(out.entries[1].kind==2 && out.entries[1].length==4 && out.entries[1].offset==1);
        CHECK(out.entries[2].kind==1 && out.entries[2].length==1 && out.entries[2].offset==5);
        CHECK(UnusedZero(&out) && memcmp(bytes,before,sizeof bytes)==0);
    } else if(!strcmp(mode,"empty")) {
        memset(&out,0xA5,sizeof out);
        CHECK(UmicomKernelTcpOptionsRead(NULL,0,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==0 && out.consumedBytes==0 && out.paddingBytes==0 && !out.endSeen && UnusedZero(&out));
    } else if(!strcmp(mode,"eol")) {
        memset(bytes,0,sizeof bytes);
        CHECK(UmicomKernelTcpOptionsRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==0 && out.consumedBytes==1 && out.paddingBytes==39 && out.endSeen && UnusedZero(&out));
        memset(bytes,1,3);bytes[3]=0;
        CHECK(UmicomKernelTcpOptionsRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==3 && out.consumedBytes==4 && out.paddingBytes==0 && out.endSeen);
    } else if(!strcmp(mode,"maximum-nops")) {
        memset(bytes,1,sizeof bytes);
        CHECK(UmicomKernelTcpOptionsRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==40 && out.consumedBytes==40 && out.paddingBytes==0 && !out.endSeen);
        for(unsigned i=0;i<40;++i)CHECK(out.entries[i].kind==1 && out.entries[i].length==1 && out.entries[i].offset==i);
    } else if(!strcmp(mode,"maximum-option")) {
        memset(bytes,0xAA,sizeof bytes);bytes[0]=254;bytes[1]=40;
        CHECK(UmicomKernelTcpOptionsRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==1 && out.entries[0].length==40 && out.consumedBytes==40 && !out.endSeen && UnusedZero(&out));
    } else if(!strcmp(mode,"opaque-kinds")) {
        /* Structural admission deliberately does not infer known-kind semantics. */
        for(unsigned kind=2;kind<256;++kind) {
            bytes[0]=(unsigned char)kind;bytes[1]=2;bytes[2]=0;bytes[3]=0;
            CHECK(UmicomKernelTcpOptionsRead(bytes,4,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.count==1 && out.entries[0].kind==kind && out.entries[0].length==2);
        }
    } else if(!strcmp(mode,"duplicates")) {
        const unsigned char repeated[8]={2,4,0x05,0xB4,2,4,0x04,0x00};
        CHECK(UmicomKernelTcpOptionsRead(repeated,8,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.count==2 && out.entries[0].kind==2 && out.entries[1].kind==2 &&
            out.entries[1].offset==4 && !out.endSeen);
    } else if(!strcmp(mode,"all-offsets")) {
        /* Assemble independently known boundaries, including unaligned options.
         * Payload deliberately contains apparent EOL/NOP/kind bytes to ensure
         * only item boundaries are interpreted as kinds. */
        for(unsigned prefix=0;prefix<39;++prefix)for(unsigned length=2;length<=40-prefix;++length) {
            memset(bytes,0,sizeof bytes);memset(bytes,1,prefix);
            bytes[prefix]=253;bytes[prefix+1U]=(unsigned char)length;
            for(unsigned data=2;data<length;++data)bytes[prefix+data]=(unsigned char)(data%4U);
            CHECK(UmicomKernelTcpOptionsRead(bytes,40,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.count==prefix+1U && out.entries[prefix].offset==prefix && out.entries[prefix].length==length);
            const unsigned end=prefix+length;
            CHECK(out.consumedBytes==(end<40?end+1U:40U));
            CHECK(out.paddingBytes==(end<40?39U-end:0U) && out.endSeen==(end<40?UMICOM_TRUE:UMICOM_FALSE));
            CHECK(UnusedZero(&out));
        }
    } else if(!strcmp(mode,"missing-length")) {
        const unsigned char tail[4]={1,1,1,253};
        CHECK(!Refused(tail,4,UMICOM_NETWORK_PACKET_LENGTH));
    } else if(!strcmp(mode,"illegal-length")) {
        for(unsigned kind=2;kind<256;++kind)for(unsigned length=0;length<2;++length) {
            bytes[0]=(unsigned char)kind;bytes[1]=(unsigned char)length;
            CHECK(!Refused(bytes,4,UMICOM_NETWORK_PACKET_HEADER));
        }
    } else if(!strcmp(mode,"overrun")) {
        for(unsigned length=5;length<256;++length) {
            bytes[0]=253;bytes[1]=(unsigned char)length;
            CHECK(!Refused(bytes,4,UMICOM_NETWORK_PACKET_LENGTH));
        }
        const unsigned char late[8]={2,4,0,0,1,1,253,3};
        CHECK(!Refused(late,8,UMICOM_NETWORK_PACKET_LENGTH));
    } else if(!strcmp(mode,"nonzero-padding")) {
        for(unsigned i=1;i<40;++i) {
            memset(bytes,0,sizeof bytes);bytes[i]=1;
            CHECK(!Refused(bytes,40,UMICOM_NETWORK_PACKET_HEADER));
        }
    } else if(!strcmp(mode,"area-size")) {
        for(unsigned i=1;i<40;++i)if(i%4U)CHECK(!Refused(bytes,i,UMICOM_NETWORK_PACKET_HEADER));
        CHECK(!Refused(bytes,41,UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(bytes,~(UmicomSize)0,UMICOM_NETWORK_PACKET_LIMIT));
    } else if(!strcmp(mode,"unaligned")) {
        _Alignas(16) unsigned char storage[9];memcpy(storage+1,bytes,8);
        CHECK(UmicomKernelTcpOptionsRead(storage+1,8,&out)==UMICOM_NETWORK_PACKET_OK && out.count==3);
    } else if(!strcmp(mode,"overlap")) {
        _Alignas(UmicomTcpOptionsView) unsigned char storage[sizeof out+40],before[sizeof out+40];
        memset(storage,0,sizeof storage);memcpy(before,storage,sizeof before);
        CHECK(UmicomKernelTcpOptionsRead(storage,4,(UmicomTcpOptionsView *)storage)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,before,sizeof storage)==0);
        CHECK(UmicomKernelTcpOptionsRead(storage+sizeof out-1U,4,(UmicomTcpOptionsView *)storage)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,before,sizeof storage)==0);
        CHECK(UmicomKernelTcpOptionsRead(storage+sizeof out,4,(UmicomTcpOptionsView *)storage)==UMICOM_NETWORK_PACKET_OK);
    } else if(!strcmp(mode,"output-alignment")) {
        _Alignas(UmicomTcpOptionsView) unsigned char storage[sizeof out+1],before[sizeof out+1];
        memset(storage,0xA5,sizeof storage);memcpy(before,storage,sizeof before);
        CHECK(UmicomKernelTcpOptionsRead(bytes,8,(UmicomTcpOptionsView *)(storage+1))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,before,sizeof storage)==0);
    } else if(!strcmp(mode,"address-wrap")) {
        const UmicomAddress high=~(UmicomAddress)0;
        CHECK(!Refused((const void *)(high-3U),4,UMICOM_NETWORK_PACKET_ARGUMENT));
        const UmicomAddress output=(high-sizeof out+1U)&~((UmicomAddress)_Alignof(UmicomTcpOptionsView)-1U);
        CHECK(UmicomKernelTcpOptionsRead(bytes,8,(UmicomTcpOptionsView *)output)==UMICOM_NETWORK_PACKET_ARGUMENT);
    } else if(!strcmp(mode,"lifetime")) {
        CHECK(UmicomKernelTcpOptionsRead(bytes,8,&out)==UMICOM_NETWORK_PACKET_OK);
        memset(bytes,0xFF,sizeof bytes);
        CHECK(out.entries[1].kind==2 && out.entries[1].length==4 && out.entries[1].offset==1 && out.count==3);
    } else if(!strcmp(mode,"arguments")) {
        CHECK(!Refused(NULL,4,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelTcpOptionsRead(bytes,8,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    } else return 2;
    return 0;
}
