/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/tagged_ipv4_tests.c
 *
 * PURPOSE:
 *   Verify tagged IPv4 coordinates and shared admission using independent wire vectors and refusal invariants.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_tagged_ipv4.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
/* Fixed synthetic IPv4 header/checksum; the following 12 bytes are opaque,
 * not a valid UDP packet. Tags must never change the IPv4 checksum domain. */
static const UmicomU8 packet[]={
    0x45,0,0,0x20,0x12,0x34,0x40,0,0x40,0x11,0x3c,0x62,
    0xc0,0,2,1,0xc6,0x33,0x64,2,1,2,3,4,5,6,7,8,9,10,11,12};
static void Put16(UmicomU8 *p,unsigned value)
{
    p[0]=(UmicomU8)(value>>8U);p[1]=(UmicomU8)value;
}
static void Checksum(UmicomU8 *ip)
{
    unsigned sum=0U;ip[10]=0U;ip[11]=0U;
    for(unsigned i=0U;i<20U;i+=2U){sum+=((unsigned)ip[i]<<8U)|ip[i+1U];sum=(sum&65535U)+(sum>>16U);}
    Put16(ip+10U,(~sum)&65535U);
}
static int Refused(const void *frame,UmicomSize length,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetTaggedIpv4View out,before;memset(&out,0xa5,sizeof out);memcpy(&before,&out,sizeof out);
    CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,&out)==expected);
    CHECK(memcmp(&before,&out,sizeof out)==0);return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];UmicomSize tags=2U;
    if(!strcmp(mode,"plain"))tags=0U;
    if(!strncmp(mode,"single-",7))tags=1U;
    _Alignas(16) UmicomU8 frame[1523],before[1523];memset(frame,0xcc,sizeof frame);
    const UmicomU8 macs[]={2,0,0,0,0,2,2,0,0,0,0,1};memcpy(frame,macs,sizeof macs);
    UmicomSize offset=14U+4U*tags,length=offset+sizeof packet;
    if(tags==0U)Put16(frame+12U,0x0800U);
    else{
        unsigned outer=tags==1U?0x8100U:0x88a8U,inner=0x8100U;
        if(!strcmp(mode,"single-s"))outer=0x88a8U;
        if(!strcmp(mode,"double-cs")){outer=0x8100U;inner=0x88a8U;}
        if(!strcmp(mode,"double-cc"))outer=0x8100U;
        if(!strcmp(mode,"double-ss"))inner=0x88a8U;
        Put16(frame+12U,outer);Put16(frame+14U,0xb123U);
        Put16(frame+16U,tags==1U?0x0800U:inner);
        if(tags==2U){Put16(frame+18U,0x4fffU);Put16(frame+20U,0x0800U);}
    }
    memcpy(frame+offset,packet,sizeof packet);UmicomU8 *ip=frame+offset;
    if(!strcmp(mode,"truncation")){for(UmicomSize n=0U;n<length;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));return 0;}
    if(!strcmp(mode,"short-ip"))return Refused(frame,offset+19U,UMICOM_NETWORK_PACKET_LENGTH);
    if(!strcmp(mode,"inner-type")){Put16(frame+offset-2U,0x86ddU);return Refused(frame,length,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(!strcmp(mode,"third-tag")){Put16(frame+20U,0x8100U);return Refused(frame,length,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(!strcmp(mode,"llc")){Put16(frame+offset-2U,1500U);return Refused(frame,length,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(!strcmp(mode,"gap")){Put16(frame+offset-2U,1501U);return Refused(frame,length,UMICOM_NETWORK_PACKET_HEADER);}
    if(!strcmp(mode,"version")){ip[0]=0x65U;return Refused(frame,length,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(!strcmp(mode,"ihl-small")){ip[0]=0x44U;return Refused(frame,length,UMICOM_NETWORK_PACKET_HEADER);}
    if(!strcmp(mode,"options")){ip[0]=0x46U;return Refused(frame,length,UMICOM_NETWORK_PACKET_UNSUPPORTED);}
    if(!strcmp(mode,"options-truncated")){ip[0]=0x46U;return Refused(frame,offset+20U,UMICOM_NETWORK_PACKET_LENGTH);}
    if(!strcmp(mode,"total-small")){Put16(ip+2U,19U);return Refused(frame,length,UMICOM_NETWORK_PACKET_HEADER);}
    if(!strcmp(mode,"total-truncated")){Put16(ip+2U,33U);return Refused(frame,length,UMICOM_NETWORK_PACKET_LENGTH);}
    if(!strcmp(mode,"total-limit")){Put16(ip+2U,1501U);return Refused(frame,length,UMICOM_NETWORK_PACKET_LIMIT);}
    if(!strcmp(mode,"fragment-more")){Put16(ip+6U,0x2000U);return Refused(frame,length,UMICOM_NETWORK_PACKET_FRAGMENT);}
    if(!strcmp(mode,"fragment-offset")){Put16(ip+6U,1U);return Refused(frame,length,UMICOM_NETWORK_PACKET_FRAGMENT);}
    if(!strcmp(mode,"reserved")){Put16(ip+6U,0x8000U);return Refused(frame,length,UMICOM_NETWORK_PACKET_HEADER);}
    if(!strcmp(mode,"checksum")){ip[8]^=1U;return Refused(frame,length,UMICOM_NETWORK_PACKET_CHECKSUM);}
    if(!strcmp(mode,"capture-limit"))return Refused(frame,sizeof frame,UMICOM_NETWORK_PACKET_LIMIT);
    if(!strcmp(mode,"arguments")){CHECK(!Refused(NULL,length,UMICOM_NETWORK_PACKET_ARGUMENT));CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);return 0;}
    if(!strcmp(mode,"address-wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0;
        CHECK(!Refused((const void *)(maximum-7U),length,UMICOM_NETWORK_PACKET_ARGUMENT));
        UmicomEthernetTaggedIpv4View *wrapped=(UmicomEthernetTaggedIpv4View *)(maximum-(_Alignof(UmicomEthernetTaggedIpv4View)-1U));
        CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,wrapped)==UMICOM_NETWORK_PACKET_ARGUMENT);return 0;
    }
    if(!strcmp(mode,"overlap")){
        memcpy(before,frame,sizeof frame);
        CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,(UmicomEthernetTaggedIpv4View *)(void *)frame)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,(UmicomEthernetTaggedIpv4View *)(void *)(frame+16U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before,frame,sizeof frame));return 0;
    }
    if(!strcmp(mode,"output-alignment")){
        _Alignas(16) UmicomU8 raw[sizeof(UmicomEthernetTaggedIpv4View)+16U],saved[sizeof raw];
        memset(raw,0xa5,sizeof raw);memcpy(saved,raw,sizeof raw);
        CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,(UmicomEthernetTaggedIpv4View *)(void *)(raw+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(raw,saved,sizeof raw));return 0;
    }
    if(!strcmp(mode,"legacy-scope")){
        UmicomEthernetIpv4View old,beforeOld;memset(&old,0xa5,sizeof old);memcpy(&beforeOld,&old,sizeof old);
        CHECK(UmicomKernelEthernetIpv4Read(frame,length,&old)==UMICOM_NETWORK_PACKET_UNSUPPORTED);
        CHECK(!memcmp(&old,&beforeOld,sizeof old));return 0;
    }
    UmicomSize ipBytes=32U;
    if(!strcmp(mode,"padding"))length=64U;
    if(!strcmp(mode,"minimum")){ipBytes=20U;length=offset+ipBytes;Put16(ip+2U,(unsigned)ipBytes);Checksum(ip);}
    if(!strcmp(mode,"maximum")){ipBytes=1500U;length=offset+ipBytes;Put16(ip+2U,(unsigned)ipBytes);Checksum(ip);}
    if(!strcmp(mode,"zero-ttl")){ip[8]=0U;Checksum(ip);}
    if(!strcmp(mode,"transport-opaque")){ip[9]=6U;Checksum(ip);}
    UmicomEthernetTaggedIpv4View out;memcpy(before,frame,sizeof frame);
    if(!strcmp(mode,"unaligned")){
        _Alignas(16) UmicomU8 raw[1524];memcpy(raw+1U,frame,length);
        CHECK(UmicomKernelEthernetTaggedIpv4Read(raw+1U,length,&out)==UMICOM_NETWORK_PACKET_OK);
    }else CHECK(UmicomKernelEthernetTaggedIpv4Read(frame,length,&out)==UMICOM_NETWORK_PACKET_OK);
    CHECK(!memcmp(frame,before,sizeof frame));
    CHECK(out.frame.tagCount==tags && out.frame.payloadOffset==offset && out.frame.payloadBytes==length-offset);
    CHECK(out.ipv4.payloadOffset==offset+20U && out.ipv4.ipBytes==ipBytes && out.ipv4.payloadBytes==ipBytes-20U);
    CHECK(out.ipv4.identification==0x1234U && out.ipv4.dontFragment==UMICOM_TRUE);
    CHECK(!memcmp(out.ipv4.sourceAddress,packet+12U,4U) && !memcmp(out.ipv4.destinationAddress,packet+16U,4U));
    CHECK(!memcmp(out.frame.sourceMac,out.ipv4.sourceMac,6U) && !memcmp(out.frame.destinationMac,out.ipv4.destinationMac,6U));
    CHECK(out.ipv4.timeToLive==(!strcmp(mode,"zero-ttl")?0U:64U));
    CHECK(out.ipv4.protocol==(!strcmp(mode,"transport-opaque")?6U:17U));
    if(tags){CHECK(out.frame.tags[0].controlInformation==0xb123U);CHECK(out.frame.tags[0].vlanIdentifier==0x123U);}
    if(tags==2U)CHECK(out.frame.tags[1].vlanIdentifier==4095U);
    if(!strcmp(mode,"lifetime")){memset(frame,0,sizeof frame);CHECK(out.ipv4.sourceAddress[0]==192U && out.frame.tags[0].priority==5U);}
    return 0;
}
