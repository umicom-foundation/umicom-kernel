/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/ethernet_tests.c
 *
 * PURPOSE:
 *   Exercise Ethernet/VLAN observations with independent byte fixtures, extent limits and unchanged refusal outputs.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_ethernet.h"
#include "umicom/kernel/address.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
/* Independent fixed network-order fixtures: the outer tag has PCP=5, DEI=1,
 * VID=0x123; the inner has PCP=2, DEI=0, VID=4095. Payload is intentionally
 * opaque and is not a valid IP packet. */
static const UmicomU8 plain[] = {2,0,0,0,0,2, 2,0,0,0,0,1, 8,0, 0xde,0xad,0xbe};
static const UmicomU8 single[] = {2,0,0,0,0,2, 2,0,0,0,0,1, 0x81,0, 0xb1,0x23, 8,0, 0xde,0xad,0xbe};
static const UmicomU8 stacked[] = {2,0,0,0,0,2, 2,0,0,0,0,1, 0x88,0xa8, 0xb1,0x23, 0x81,0, 0x4f,0xff, 8,0, 0xde,0xad,0xbe};
static int Refused(const void *frame, UmicomSize size, UmicomNetworkPacketStatus expected)
{
    UmicomEthernetFrameView out,before;
    memset(&out,0xa5,sizeof out); memcpy(&before,&out,sizeof out);
    CHECK(UmicomKernelEthernetFrameRead(frame,size,&out)==expected);
    CHECK(memcmp(&out,&before,sizeof out)==0);
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2); const char *mode=argv[1];
    _Alignas(16) UmicomU8 frame[1523],before[1523],unaligned[1524];
    memset(frame,0xcc,sizeof frame);
    const UmicomU8 *fixture=stacked; UmicomSize fixtureSize=sizeof stacked, tags=2U;
    if(strcmp(mode,"plain")==0 || strcmp(mode,"ipv6")==0 || strcmp(mode,"arp")==0 ||
       strcmp(mode,"unknown-type")==0 || strcmp(mode,"first-type")==0 ||
       strcmp(mode,"last-type")==0 || strstr(mode,"-plain") ||
       strncmp(mode,"llc-",4)==0 || strncmp(mode,"gap-",4)==0 || strcmp(mode,"opaque-tpid")==0) {
        fixture=plain; fixtureSize=sizeof plain; tags=0U;
    } else if(strncmp(mode,"single-",7)==0 || strstr(mode,"-single") ||
              strncmp(mode,"priority-",9)==0 || strcmp(mode,"dei-bit")==0 ||
              strncmp(mode,"vid-",4)==0) {
        fixture=single; fixtureSize=sizeof single; tags=1U;
    }
    memcpy(frame,fixture,fixtureSize);
    UmicomSize length=fixtureSize,offset=14U+4U*tags;
    UmicomU16 type=0x0800U,outer=(tags==1U?0x8100U:0x88a8U),inner=0x8100U;
    UmicomU16 outerControl=0xb123U; UmicomU8 priority=5U;
    UmicomBoolean dei=UMICOM_TRUE; UmicomU16 vid=0x123U;
    if(strncmp(mode,"truncation-",11)==0) {
        for(UmicomSize n=0;n<offset;++n) CHECK(Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH)==0);
        return 0;
    }
    if(strncmp(mode,"over-",5)==0) return Refused(frame,offset+1501U,UMICOM_NETWORK_PACKET_LIMIT);
    if(strcmp(mode,"capture-limit")==0) return Refused(frame,sizeof frame,UMICOM_NETWORK_PACKET_LIMIT);
    if(strcmp(mode,"arguments")==0) {
        CHECK(Refused(NULL,length,UMICOM_NETWORK_PACKET_ARGUMENT)==0);
        CHECK(UmicomKernelEthernetFrameRead(frame,length,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
        return 0;
    }
    if(strcmp(mode,"address-wrap")==0) {
        const UmicomAddress maximum=~(UmicomAddress)0;
        CHECK(Refused((const void *)(maximum-7U),length,UMICOM_NETWORK_PACKET_ARGUMENT)==0);
        UmicomEthernetFrameView *wrapped=(UmicomEthernetFrameView *)(maximum-(_Alignof(UmicomEthernetFrameView)-1U));
        CHECK(UmicomKernelEthernetFrameRead(frame,length,wrapped)==UMICOM_NETWORK_PACKET_ARGUMENT); return 0;
    }
    if(strcmp(mode,"overlap")==0) {
        memcpy(before,frame,sizeof frame);
        CHECK(UmicomKernelEthernetFrameRead(frame,64U,(UmicomEthernetFrameView *)(void *)frame)==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(UmicomKernelEthernetFrameRead(frame,64U,(UmicomEthernetFrameView *)(void *)(frame+16U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(frame,before,sizeof frame)==0); return 0;
    }
    if(strcmp(mode,"output-alignment")==0) {
        _Alignas(16) UmicomU8 storage[sizeof(UmicomEthernetFrameView)+16U],saved[sizeof storage];
        memset(storage,0xa5,sizeof storage); memcpy(saved,storage,sizeof storage);
        CHECK(UmicomKernelEthernetFrameRead(frame,length,(UmicomEthernetFrameView *)(void *)(storage+1U))==UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(memcmp(storage,saved,sizeof storage)==0); return 0;
    }
    if(strcmp(mode,"third-tag")==0) {
        frame[20]=0x81U;frame[21]=0U;
        /* The third tag is unsupported even when only its TPID is captured. */
        CHECK(Refused(frame,22U,UMICOM_NETWORK_PACKET_UNSUPPORTED)==0);
        return Refused(frame,26U,UMICOM_NETWORK_PACKET_UNSUPPORTED);
    }
    if(strncmp(mode,"llc-",4)==0 || strncmp(mode,"gap-",4)==0 || strcmp(mode,"inner-length")==0) {
        UmicomU16 value=0U; UmicomNetworkPacketStatus expected=UMICOM_NETWORK_PACKET_UNSUPPORTED;
        if(strcmp(mode,"llc-maximum")==0)value=1500U;
        if(strcmp(mode,"gap-first")==0){value=1501U;expected=UMICOM_NETWORK_PACKET_HEADER;}
        if(strcmp(mode,"gap-last")==0){value=1535U;expected=UMICOM_NETWORK_PACKET_HEADER;}
        frame[offset-2U]=(UmicomU8)(value>>8U);frame[offset-1U]=(UmicomU8)value;
        return Refused(frame,length,expected);
    }
    if(strcmp(mode,"ipv6")==0){frame[12]=0x86U;frame[13]=0xddU;type=0x86ddU;}
    else if(strcmp(mode,"arp")==0){frame[13]=6U;type=0x0806U;}
    else if(strcmp(mode,"unknown-type")==0){frame[12]=0x12U;frame[13]=0x34U;type=0x1234U;}
    else if(strcmp(mode,"first-type")==0){frame[12]=6U;frame[13]=0U;type=1536U;}
    else if(strcmp(mode,"last-type")==0){frame[12]=0xffU;frame[13]=0xffU;type=65535U;}
    else if(strcmp(mode,"opaque-tpid")==0){frame[12]=0x91U;frame[13]=0U;type=0x9100U;}
    else if(strcmp(mode,"single-s")==0){frame[12]=0x88U;frame[13]=0xa8U;outer=0x88a8U;}
    else if(strcmp(mode,"double-cs")==0){frame[12]=0x81U;frame[13]=0U;frame[16]=0x88U;frame[17]=0xa8U;outer=0x8100U;inner=0x88a8U;}
    else if(strcmp(mode,"double-cc")==0){frame[12]=0x81U;frame[13]=0U;outer=0x8100U;}
    else if(strcmp(mode,"double-ss")==0){frame[16]=0x88U;frame[17]=0xa8U;inner=0x88a8U;}
    else if(strncmp(mode,"header-only-",12)==0)length=offset;
    else if(strncmp(mode,"maximum-",8)==0)length=offset+1500U;
    else if(strcmp(mode,"padding")==0)length=64U;
    else if(strcmp(mode,"priority-zero")==0 || strcmp(mode,"vid-zero")==0) {
        frame[14]=0U;frame[15]=0U;outerControl=0U;priority=0U;dei=UMICOM_FALSE;vid=0U;
    } else if(strcmp(mode,"priority-maximum")==0 || strcmp(mode,"vid-reserved")==0) {
        frame[14]=0xffU;frame[15]=0xffU;outerControl=65535U;priority=7U;vid=4095U;
    } else if(strcmp(mode,"priority-bits")==0) {
        for(UmicomU8 p=0U;p<8U;++p) {
            frame[14]=(UmicomU8)(p<<5U);frame[15]=0x42U;UmicomEthernetFrameView v;
            CHECK(UmicomKernelEthernetFrameRead(frame,length,&v)==UMICOM_NETWORK_PACKET_OK);
            CHECK(v.tags[0].priority==p && v.tags[0].dropEligible==UMICOM_FALSE && v.tags[0].vlanIdentifier==0x42U);
        } return 0;
    } else if(strcmp(mode,"dei-bit")==0) {
        frame[14]=0x10U;frame[15]=0U;outerControl=0x1000U;priority=0U;vid=0U;
    } else if(strcmp(mode,"plain") && strcmp(mode,"single-c") && strcmp(mode,"double-sc") &&
              strcmp(mode,"unaligned") && strcmp(mode,"lifetime")) CHECK(0);
    memcpy(before,frame,sizeof frame);
    const void *input=frame;
    if(strcmp(mode,"unaligned")==0){memcpy(unaligned+1U,frame,length);input=unaligned+1U;}
    UmicomEthernetFrameView out;
    memset(&out,0xa5,sizeof out);
    CHECK(UmicomKernelEthernetFrameRead(input,length,&out)==UMICOM_NETWORK_PACKET_OK);
    CHECK(out.tagCount==tags && out.etherType==type && out.payloadOffset==offset && out.payloadBytes==length-offset);
    CHECK(out.destinationMac[0]==2U && out.destinationMac[5]==2U && out.sourceMac[0]==2U && out.sourceMac[5]==1U);
    if(tags) {
        CHECK(out.tags[0].protocolIdentifier==outer && out.tags[0].controlInformation==outerControl);
        CHECK(out.tags[0].priority==priority && out.tags[0].dropEligible==dei && out.tags[0].vlanIdentifier==vid);
    }
    if(tags==2U)CHECK(out.tags[1].protocolIdentifier==inner && out.tags[1].controlInformation==0x4fffU &&
                    out.tags[1].priority==2U && out.tags[1].dropEligible==UMICOM_FALSE && out.tags[1].vlanIdentifier==4095U);
    UmicomEthernetTagView zero; memset(&zero,0,sizeof zero);
    for(UmicomSize i=tags;i<2U;++i)CHECK(memcmp(&out.tags[i],&zero,sizeof zero)==0);
    CHECK(memcmp(frame,before,sizeof frame)==0);
    if(strcmp(mode,"lifetime")==0) {
        memset(frame,0,sizeof frame);
        CHECK(out.tagCount==2U && out.tags[0].controlInformation==0xb123U && out.etherType==0x0800U);
        CHECK(out.sourceMac[5]==1U && out.payloadOffset==22U && out.payloadBytes==3U);
    }
    return 0;
}
