/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/neighbor6_tests.c
 *
 * PURPOSE:
 *   Verify neighbor capture framing, option bounds, address conditions and output ownership using independent packets.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/network_neighbor6.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
/* Independent fixed captures: NS checksum 7a97, NA checksum 9819, DAD-shaped
 * NS checksum 7c23. A reserved-word variant carries a correctly computed zero. */
static const unsigned char golden[]={0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x20,0x3a,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0x00,0x00,0x02,0x87,0x00,0x7a,0x97,0x00,0x00,0x00,0x00,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0x01,0x02,0x00,0x00,0x00,0x00,0x01};
static const unsigned char advert[]={0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x20,0x3a,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x88,0x00,0x98,0x19,0xe0,0x00,0x00,0x00,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x01,0x02,0x00,0x00,0x00,0x00,0x02};
static const unsigned char dad[]={0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x18,0x3a,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0x00,0x00,0x02,0x87,0x00,0x7c,0x23,0x00,0x00,0x00,0x00,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02};
static const unsigned char zeroAdvert[]={0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x20,0x3a,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x88,0x00,0x00,0x00,0xe0,0x00,0x98,0x19,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x01,0x02,0x00,0x00,0x00,0x00,0x02};
static void Put16(unsigned char *p,unsigned n){p[0]=(unsigned char)(n>>8U);p[1]=(unsigned char)n;}
static unsigned Add(unsigned sum,unsigned n){sum+=n;return (sum&65535U)+(sum>>16U);}
static unsigned Sum(const unsigned char *p,size_t n,unsigned sum)
{
    for(size_t i=0U;i<n;i+=2U)sum=Add(sum,((unsigned)p[i]<<8U)|(i+1U<n?p[i+1U]:0U));
    return sum;
}
static void Seal(unsigned char *p,unsigned length)
{
    Put16(p+18U,length);p[56]=p[57]=0U;
    unsigned sum=Add(Add(Sum(p+22U,32U,0U),length),58U);
    Put16(p+56U,~Sum(p+54U,length,sum)&65535U);
}
static int Refused(const void *bytes,UmicomSize size,UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv6NeighborView out;unsigned char prior[sizeof out];
    memset(&out,0xa5,sizeof out);memcpy(prior,&out,sizeof out);
    CHECK(UmicomKernelEthernetIpv6NeighborRead(bytes,size,&out)==expected);
    CHECK(!memcmp(prior,&out,sizeof out));return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const char *mode=argv[1];unsigned char frame[1515]={0};memcpy(frame,golden,sizeof golden);
    UmicomEthernetIpv6NeighborView out;
    if(!strcmp(mode,"golden")){
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageType==135U && out.messageBytes==32U && out.optionCount==1U && out.ignoredOptionCount==0U);
        CHECK(out.hasLinkLayerAddress && !out.router && !out.solicited && !out.override);
        CHECK(!memcmp(out.targetAddress,golden+62U,16U)&&!memcmp(out.linkLayerAddress,golden+80U,6U));
        CHECK(out.ipv6.hopLimit==255U && out.ipv6.nextHeader==58U && !memcmp(frame,golden,sizeof golden));
    }else if(!strcmp(mode,"advertisement")||!strcmp(mode,"valid-zero")){
        const unsigned char *p=!strcmp(mode,"valid-zero")?zeroAdvert:advert;
        CHECK(UmicomKernelEthernetIpv6NeighborRead(p,sizeof advert,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageType==136U && out.router && out.solicited && out.override && out.hasLinkLayerAddress);
        CHECK(out.linkLayerAddress[5]==2U && out.targetAddress[15]==2U);
        if(!strcmp(mode,"valid-zero"))CHECK(p[56]==0U && p[57]==0U);
    }else if(!strcmp(mode,"dad")){
        CHECK(UmicomKernelEthernetIpv6NeighborRead(dad,sizeof dad,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.messageBytes==24U && !out.hasLinkLayerAddress && out.optionCount==0U && out.ipv6.sourceAddress[0]==0U);
    }else if(!strcmp(mode,"no-option")){
        Seal(frame,24U);CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,78U,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(!out.hasLinkLayerAddress && out.optionCount==0U);
    }else if(!strcmp(mode,"padding")){
        memset(frame+sizeof golden,0xff,1514U-sizeof golden);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,1514U,&out)==UMICOM_NETWORK_PACKET_OK && out.optionCount==1U);
    }else if(!strcmp(mode,"maximum")){
        for(unsigned at=78U;at<1510U;at+=8U){memset(frame+at,0,8U);frame[at]=250U;frame[at+1U]=1U;}
        Seal(frame,1456U);CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,1514U,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.optionCount==179U && out.ignoredOptionCount==179U && !out.hasLinkLayerAddress);
    }else if(!strcmp(mode,"unknown-option")||!strcmp(mode,"irrelevant-option")||!strcmp(mode,"unknown-long")){
        frame[78]=!strcmp(mode,"irrelevant-option")?2U:250U;
        unsigned length=!strcmp(mode,"unknown-long")?40U:32U;
        frame[79]=(unsigned char)(!strcmp(mode,"unknown-long")?2U:1U);Seal(frame,length);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,54U+length,&out)==UMICOM_NETWORK_PACKET_OK);
        CHECK(out.optionCount==1U && out.ignoredOptionCount==1U && !out.hasLinkLayerAddress);
    }else if(!strcmp(mode,"relevant-length")){
        frame[79]=2U;Seal(frame,40U);CHECK(!Refused(frame,94U,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(mode,"duplicate-option")){
        memcpy(frame+86U,frame+78U,8U);Seal(frame,40U);CHECK(!Refused(frame,94U,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(mode,"zero-option")){
        frame[79]=0U;Seal(frame,32U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(mode,"short-option")){
        Seal(frame,25U);CHECK(!Refused(frame,79U,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"option-overrun")){
        frame[79]=255U;Seal(frame,32U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"target-multicast")){
        frame[62]=0xffU;Seal(frame,32U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(mode,"dad-option")){
        memset(frame+22U,0,16U);Seal(frame,32U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(mode,"dad-destination")){
        memcpy(frame,dad,sizeof dad);memcpy(frame+38U,golden+22U,16U);Seal(frame,24U);
        CHECK(!Refused(frame,sizeof dad,UMICOM_NETWORK_PACKET_HEADER));
    }else if(!strcmp(mode,"dad-prefix")){
        for(unsigned i=0U;i<13U;++i){memcpy(frame,dad,sizeof dad);frame[38U+i]^=1U;Seal(frame,24U);
            CHECK(!Refused(frame,sizeof dad,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(mode,"advertisement-multicast")){
        memcpy(frame,advert,sizeof advert);memcpy(frame+38U,golden+38U,16U);Seal(frame,32U);
        CHECK(!Refused(frame,sizeof advert,UMICOM_NETWORK_PACKET_HEADER));
        frame[58]&=(unsigned char)~0x40U;Seal(frame,32U);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof advert,&out)==UMICOM_NETWORK_PACKET_OK && !out.solicited);
    }else if(!strcmp(mode,"flags")){
        for(unsigned f=0U;f<8U;++f){memcpy(frame,advert,sizeof advert);frame[58]=(unsigned char)(f<<5U);Seal(frame,32U);
            CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof advert,&out)==UMICOM_NETWORK_PACKET_OK);
            CHECK(out.router==((f&4U)?UMICOM_TRUE:UMICOM_FALSE));
            CHECK(out.solicited==((f&2U)?UMICOM_TRUE:UMICOM_FALSE));CHECK(out.override==((f&1U)?UMICOM_TRUE:UMICOM_FALSE));}
    }else if(!strcmp(mode,"reserved")){
        memset(frame+58U,0xff,4U);Seal(frame,32U);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK && !out.router && !out.solicited && !out.override);
        memcpy(frame,advert,sizeof advert);frame[58]|=31U;memset(frame+59U,0xff,3U);Seal(frame,32U);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof advert,&out)==UMICOM_NETWORK_PACKET_OK && out.router && out.solicited && out.override);
    }else if(!strcmp(mode,"hop")){
        for(unsigned n=0U;n<255U;++n){frame[21]=(unsigned char)n;CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(mode,"code")){
        for(unsigned n=1U;n<256U;++n){frame[55]=(unsigned char)n;Seal(frame,32U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_HEADER));}
    }else if(!strcmp(mode,"type")){
        for(unsigned n=0U;n<256U;++n)if(n!=135U && n!=136U){frame[54]=(unsigned char)n;Seal(frame,32U);CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_UNSUPPORTED));}
    }else if(!strcmp(mode,"protocol")||!strcmp(mode,"fragment")||!strcmp(mode,"extension")){
        frame[20]=(unsigned char)(!strcmp(mode,"protocol")?17U:(!strcmp(mode,"fragment")?44U:0U));
        CHECK(!Refused(frame,sizeof golden,!strcmp(mode,"fragment")?UMICOM_NETWORK_PACKET_FRAGMENT:UMICOM_NETWORK_PACKET_UNSUPPORTED));
    }else if(!strcmp(mode,"checksum")){
        for(unsigned i=56U;i<sizeof golden;++i)for(unsigned bit=0U;bit<8U;++bit){memcpy(frame,golden,sizeof golden);frame[i]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));}
    }else if(!strcmp(mode,"pseudo-addresses")){
        for(unsigned i=22U;i<54U;++i)for(unsigned bit=0U;bit<8U;++bit){memcpy(frame,golden,sizeof golden);frame[i]^=(unsigned char)(1U<<bit);
            CHECK(!Refused(frame,sizeof golden,UMICOM_NETWORK_PACKET_CHECKSUM));}
    }else if(!strcmp(mode,"truncation")){
        for(UmicomSize n=0U;n<sizeof golden;++n)CHECK(!Refused(frame,n,UMICOM_NETWORK_PACKET_LENGTH));
    }else if(!strcmp(mode,"short-header")){
        for(unsigned n=0U;n<24U;++n){Seal(frame,n);CHECK(!Refused(frame,54U+n,UMICOM_NETWORK_PACKET_LENGTH));}
    }else if(!strcmp(mode,"unaligned")){
        _Alignas(16) unsigned char storage[sizeof golden+1U];memcpy(storage+1U,golden,sizeof golden);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(storage+1U,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
    }else if(!strcmp(mode,"overlap")){
        union{unsigned char bytes[256];UmicomEthernetIpv6NeighborView view;} storage;
        unsigned char prior[sizeof storage];memset(&storage,0,sizeof storage);memcpy(storage.bytes,golden,sizeof golden);memcpy(prior,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(storage.bytes,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT && !memcmp(prior,&storage,sizeof storage));
        memcpy(storage.bytes+8U,golden,sizeof golden);memcpy(prior,&storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(storage.bytes+8U,sizeof golden,&storage.view)==UMICOM_NETWORK_PACKET_ARGUMENT && !memcmp(prior,&storage,sizeof storage));
        CHECK(UmicomKernelEthernetIpv6NeighborRead(storage.bytes,256U,(UmicomEthernetIpv6NeighborView *)(void *)(storage.bytes+128U))==UMICOM_NETWORK_PACKET_ARGUMENT && !memcmp(prior,&storage,sizeof storage));
    }else if(!strcmp(mode,"output-alignment")){
        _Alignas(16) unsigned char storage[sizeof out+16U],prior[sizeof out+16U];memset(storage,0xa5,sizeof storage);memcpy(prior,storage,sizeof storage);
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof golden,(UmicomEthernetIpv6NeighborView *)(void *)(storage+1U))==UMICOM_NETWORK_PACKET_ARGUMENT && !memcmp(prior,storage,sizeof storage));
    }else if(!strcmp(mode,"address-wrap")){
        const UmicomAddress maximum=~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum-15U),sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof golden,(UmicomEthernetIpv6NeighborView *)(maximum-(_Alignof(UmicomEthernetIpv6NeighborView)-1U)))==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else if(!strcmp(mode,"lifetime")){
        CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof golden,&out)==UMICOM_NETWORK_PACKET_OK);
        memset(frame,0,sizeof frame);CHECK(out.targetAddress[15]==2U && out.linkLayerAddress[5]==1U && out.messageType==135U);
    }else if(!strcmp(mode,"capture-limit")){
        CHECK(!Refused(frame,1515U,UMICOM_NETWORK_PACKET_LIMIT));CHECK(!Refused(frame,~(UmicomSize)0U,UMICOM_NETWORK_PACKET_LIMIT));
    }else if(!strcmp(mode,"arguments")){
        CHECK(!Refused(NULL,sizeof golden,UMICOM_NETWORK_PACKET_ARGUMENT));CHECK(UmicomKernelEthernetIpv6NeighborRead(frame,sizeof golden,NULL)==UMICOM_NETWORK_PACKET_ARGUMENT);
    }else return 2;
    return 0;
}
