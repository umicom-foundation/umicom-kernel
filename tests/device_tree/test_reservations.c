/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/device_tree/test_reservations.c
 *
 * PURPOSE:
 *   Verify page rounding, RAM clipping and reservation merging before any allocator mutation.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_reservations.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    const UmicomAddress base=0x80000000U;
    UmicomAddress ram=base, end=base+16384U,dtb=base+32768U;
    UmicomSize bytes=1024U*1024U;
    UmicomKernelDeviceTreeLayout tree={0};tree.totalBytes=72U;
    UmicomKernelBootReservationPlan plan;memset(&plan,0x5a,sizeof plan);
    unsigned char before[sizeof plan];memcpy(before,&plan,sizeof plan);
    UmicomBoolean expected=UMICOM_TRUE;
    const char *name=argv[1];
    if(!strcmp(name,"base")){}
    else if(!strcmp(name,"inside")){tree.reservationCount=1;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){base+65536U,4096U};}
    else if(!strcmp(name,"kernel-overlap")){tree.reservationCount=1;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){base+8192U,4096U};}
    else if(!strcmp(name,"shared-page")){tree.reservationCount=2;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){base+65536U,1U};tree.reservations[1]=(UmicomKernelDeviceTreeReservation){base+65538U,1U};}
    else if(!strcmp(name,"clip-low")){tree.reservationCount=1;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){base-4096U,8192U};}
    else if(!strcmp(name,"clip-high")){tree.reservationCount=1;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){base+bytes-1U,8192U};}
    else if(!strcmp(name,"outside")){tree.reservationCount=2;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){0U,4096U};tree.reservations[1]=(UmicomKernelDeviceTreeReservation){base+bytes,4096U};}
    else if(!strcmp(name,"bridge")){tree.reservationCount=1;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){end,dtb-end};}
    else if(!strcmp(name,"ram-wrap")){ram=~(UmicomAddress)0U-4095U;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"ram-alignment")){ram++;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"tree-alignment")){dtb++;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"tree-outside")){dtb=base+bytes;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"tree-overlap")){dtb=end-8U;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"reservation-wrap")){tree.reservationCount=1;tree.reservations[0]=(UmicomKernelDeviceTreeReservation){~(UmicomU64)0U-8U,16U};expected=UMICOM_FALSE;}
    else if(!strcmp(name,"reservation-empty")){tree.reservationCount=1;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"capacity")){tree.reservationCount=65;expected=UMICOM_FALSE;}
    else if(!strcmp(name,"arguments")){
        CHECK(!UmicomKernelBootReservationsPlan(base,bytes,end,dtb,NULL,&plan));
        CHECK(!UmicomKernelBootReservationsPlan(base,bytes,end,dtb,&tree,NULL));
        CHECK(!memcmp(before,&plan,sizeof plan));return 0;
    }else return 2;
    CHECK(UmicomKernelBootReservationsPlan(ram,bytes,end,dtb,&tree,&plan)==expected);
    if(!expected){CHECK(!memcmp(before,&plan,sizeof plan));return 0;}
    /* A successful staged copy replaces every byte, including unused slots.
     * The caller started with a nonzero sentinel; stale reservations must not
     * escape merely because only the live prefix is consumed today. */
    for (UmicomSize i = plan.count; i < UMICOM_KERNEL_BOOT_RESERVATION_LIMIT; ++i) {
        CHECK(plan.ranges[i].base == 0U);
        CHECK(plan.ranges[i].bytes == 0U);
    }
    for(UmicomSize i=0;i<plan.count;++i){
        CHECK(plan.ranges[i].base>=base&&plan.ranges[i].bytes>0U);
        CHECK(plan.ranges[i].base+plan.ranges[i].bytes<=base+bytes);
        CHECK(!(plan.ranges[i].base%4096U)&&!(plan.ranges[i].bytes%4096U));
        if(i)CHECK(plan.ranges[i-1U].base+plan.ranges[i-1U].bytes<plan.ranges[i].base);
    }
    if(!strcmp(name,"inside")||!strcmp(name,"shared-page")){
        CHECK(plan.count==3U&&plan.ranges[2].base==base+65536U&&plan.ranges[2].bytes==4096U);
    }else if(!strcmp(name,"clip-high")){
        CHECK(plan.count==3U&&plan.ranges[2].base==base+bytes-4096U&&plan.ranges[2].bytes==4096U);
    }else if(!strcmp(name,"bridge")){
        CHECK(plan.count==1U&&plan.ranges[0].base==base&&plan.ranges[0].bytes==36864U);
    }else CHECK(plan.count==2U);
    return 0;
}
