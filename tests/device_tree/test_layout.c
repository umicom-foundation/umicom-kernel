/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/device_tree/test_layout.c
 *
 * PURPOSE:
 *   Verify bounded FDT layout, byte order, reservation termination and failure-atomic results.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/device_tree_layout.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static void Put32(unsigned char *p, UmicomU32 v)
{
    p[0]=(unsigned char)(v>>24U);p[1]=(unsigned char)(v>>16U);
    p[2]=(unsigned char)(v>>8U);p[3]=(unsigned char)v;
}
static void Put64(unsigned char *p, UmicomU64 v)
{
    Put32(p,(UmicomU32)(v>>32U));Put32(p+4U,(UmicomU32)v);
}
static size_t Tree(unsigned char *p, unsigned reservations)
{
    memset(p,0,2048);
    UmicomU32 structure=40U+16U*(reservations+1U);
    Put32(p,0xd00dfeedU);Put32(p+4U,structure+16U);
    Put32(p+8U,structure);Put32(p+12U,structure+16U);Put32(p+16U,40U);
    Put32(p+20U,17U);Put32(p+24U,16U);Put32(p+28U,3U);Put32(p+36U,16U);
    for(unsigned i=0;i<reservations;++i){Put64(p+40U+i*16U,0x80000000ULL+i*8192ULL);Put64(p+48U+i*16U,4096U);}
    Put32(p+structure,1U);Put32(p+structure+8U,2U);Put32(p+structure+12U,9U);
    return (size_t)structure+16U;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    unsigned char storage[2049];
    unsigned char *p=storage+1; /* Byte-wise decoding must not require alignment. */
    size_t size=Tree(p,1);
    UmicomKernelDeviceTreeLayout out;
    memset(&out,0x5a,sizeof out);
    unsigned char before[sizeof out];memcpy(before,&out,sizeof out);
    UmicomKernelDeviceTreeLayoutStatus expected=UMICOM_FDT_LAYOUT_OK;
    const char *name=argv[1];
    if(!strcmp(name,"valid")){
        CHECK(UmicomKernelDeviceTreeLayoutRead(p,size,&out)==UMICOM_FDT_LAYOUT_OK);
        CHECK(out.version==17U&&out.bootCpu==3U&&out.reservationCount==1U);
        CHECK(out.reservations[0].address==0x80000000ULL&&out.reservations[0].bytes==4096U);
        CHECK(out.totalBytes==size&&out.structureBytes==16U);
        return 0;
    }else if(!strcmp(name,"empty"))size=Tree(p,0);
    else if(!strcmp(name,"future-compatible"))Put32(p+20U,18U);
    else if(!strcmp(name,"maximum"))size=Tree(p,UMICOM_TREE_RESERVATION_LIMIT);
    else if(!strcmp(name,"arguments")){
        CHECK(UmicomKernelDeviceTreeLayoutRead(NULL,size,&out)==UMICOM_FDT_LAYOUT_ARGUMENT);
        CHECK(UmicomKernelDeviceTreeLayoutRead(p,size,NULL)==UMICOM_FDT_LAYOUT_ARGUMENT);
        CHECK(!memcmp(before,&out,sizeof out));return 0;
    }else if(!strcmp(name,"truncation")){
        for(size_t n=0;n<size;++n){
            CHECK(UmicomKernelDeviceTreeLayoutRead(p,n,&out)!=UMICOM_FDT_LAYOUT_OK);
            CHECK(!memcmp(before,&out,sizeof out));
        }return 0;
    }else if(!strcmp(name,"magic")){p[0]=0;expected=UMICOM_FDT_LAYOUT_HEADER;}
    else if(!strcmp(name,"version")){Put32(p+20U,16U);expected=UMICOM_FDT_LAYOUT_VERSION;}
    else if(!strcmp(name,"incompatible")){Put32(p+24U,18U);expected=UMICOM_FDT_LAYOUT_VERSION;}
    else if(!strcmp(name,"structure-range")){Put32(p+8U,0xfffffff0U);expected=UMICOM_FDT_LAYOUT_RANGE;}
    else if(!strcmp(name,"strings-range")){Put32(p+32U,0xffffffffU);expected=UMICOM_FDT_LAYOUT_RANGE;}
    else if(!strcmp(name,"structure-alignment")){Put32(p+8U,73U);Put32(p+36U,12U);expected=UMICOM_FDT_LAYOUT_HEADER;}
    else if(!strcmp(name,"reservation-alignment")){Put32(p+16U,41U);expected=UMICOM_FDT_LAYOUT_HEADER;}
    else if(!strcmp(name,"section-overlap")){Put32(p+12U,80U);Put32(p+32U,8U);expected=UMICOM_FDT_LAYOUT_HEADER;}
    else if(!strcmp(name,"unterminated")){
        Put64(p+56U,0x90000000ULL);Put64(p+64U,4096U);expected=UMICOM_FDT_LAYOUT_HEADER;
    }else if(!strcmp(name,"reservation-zero")){Put64(p+48U,0U);expected=UMICOM_FDT_LAYOUT_RESERVATION;}
    else if(!strcmp(name,"reservation-wrap")){Put64(p+40U,~(UmicomU64)0U-8U);Put64(p+48U,16U);expected=UMICOM_FDT_LAYOUT_RESERVATION;}
    else if(!strcmp(name,"reservation-overlap")){
        size=Tree(p,2);Put64(p+56U,0x80000001ULL);expected=UMICOM_FDT_LAYOUT_RESERVATION;
    }else if(!strcmp(name,"capacity")){size=Tree(p,UMICOM_TREE_RESERVATION_LIMIT+1U);expected=UMICOM_FDT_LAYOUT_CAPACITY;}
    else if(!strcmp(name,"names")){
        for(int i=UMICOM_FDT_LAYOUT_OK;i<=UMICOM_FDT_LAYOUT_CAPACITY;++i)
            CHECK(strlen(UmicomKernelDeviceTreeLayoutStatusName((UmicomKernelDeviceTreeLayoutStatus)i))>0);
        CHECK(!strcmp(UmicomKernelDeviceTreeLayoutStatusName((UmicomKernelDeviceTreeLayoutStatus)100),"unknown-layout-status"));
        return 0;
    }else return 2;
    CHECK(UmicomKernelDeviceTreeLayoutRead(p,size,&out)==expected);
    if(expected!=UMICOM_FDT_LAYOUT_OK)CHECK(!memcmp(before,&out,sizeof out));
    if(!strcmp(name,"maximum"))CHECK(out.reservationCount==UMICOM_TREE_RESERVATION_LIMIT);
    if(!strcmp(name,"empty"))CHECK(out.reservationCount==0U);
    return 0;
}
