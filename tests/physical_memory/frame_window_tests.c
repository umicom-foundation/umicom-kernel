/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/physical_memory/frame_window_tests.c
 *
 * PURPOSE:
 *   Qualify constrained allocation, shared frame ownership and refusal atomicity against production Kernel code.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/physical_memory.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define OK UMICOM_KERNEL_MEMORY_OK
#define PAGE UMICOM_KERNEL_PAGE_SIZE
#define BASE ((UmicomAddress)0x1000U)
#define MARKER ((UmicomAddress)0x12345678U)
typedef struct Saved {
    UmicomKernelPhysicalMemorySnapshot counts;
    UmicomKernelPhysicalFrameState frames[32];
} Saved;
static int Save(Saved *out)
{
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&out->counts)==OK);CHECK(out->counts.totalFrames<=32U);
    for(UmicomSize i=0U;i<out->counts.totalFrames;++i)
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(out->counts.ramBase+i*PAGE,&out->frames[i])==OK);
    return 0;
}
static int Same(const Saved *before)
{
    Saved after;CHECK(Save(&after)==0);
    CHECK(after.counts.ramBase==before->counts.ramBase && after.counts.ramBytes==before->counts.ramBytes);
    CHECK(after.counts.allocatedFrames==before->counts.allocatedFrames && after.counts.reservedFrames==before->counts.reservedFrames);
    CHECK(after.counts.freeFrames==before->counts.freeFrames);
    for(UmicomSize i=0U;i<before->counts.totalFrames;++i)CHECK(after.frames[i]==before->frames[i]);
    CHECK(UmicomKernelPhysicalMemoryValidate()==OK);return 0;
}
static int Refuse(UmicomSize count,UmicomSize alignment,UmicomAddress start,UmicomAddress end,
    UmicomKernelMemoryStatus expected,int nullOutput)
{
    Saved before;CHECK(Save(&before)==0);UmicomAddress out=MARKER;
    CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(count,alignment,start,end,nullOutput?NULL:&out)==expected);
    CHECK(out==MARKER);return Same(&before);
}
static int Allocate(UmicomSize count,UmicomSize alignment,UmicomAddress start,UmicomAddress end,UmicomAddress expected)
{
    Saved before;CHECK(Save(&before)==0);UmicomAddress out=MARKER;
    CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(count,alignment,start,end,&out)==OK);CHECK(out==expected);
    UmicomKernelPhysicalMemorySnapshot after;CHECK(UmicomKernelPhysicalMemorySnapshotRead(&after)==OK);
    CHECK(after.allocatedFrames==before.counts.allocatedFrames+count && after.reservedFrames==before.counts.reservedFrames);
    for(UmicomSize i=0U;i<before.counts.totalFrames;++i){
        UmicomAddress address=before.counts.ramBase+i*PAGE;UmicomKernelPhysicalFrameState state;
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(address,&state)==OK);
        CHECK(state==(address>=out && (address-out)/PAGE<count?UMICOM_PHYSICAL_FRAME_ALLOCATED:before.frames[i]));
    }
    CHECK(UmicomKernelPhysicalMemoryValidate()==OK);return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *name=argv[1];UmicomAddress out=MARKER;
    if(strcmp(name,"uninitialised")==0){CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(1U,1U,BASE,BASE+PAGE,&out)==UMICOM_KERNEL_MEMORY_NOT_INITIALISED);CHECK(out==MARKER);return 0;}
    CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,32U*PAGE)==OK);
    if(strcmp(name,"null-output")==0)return Refuse(1U,1U,BASE,BASE+PAGE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,1);
    if(strcmp(name,"zero-count")==0)return Refuse(0U,1U,BASE,BASE+PAGE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(name,"zero-alignment")==0)return Refuse(1U,0U,BASE,BASE+PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(name,"nonpower-alignment")==0)return Refuse(1U,3U,BASE,BASE+PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(name,"empty-window")==0)return Refuse(1U,1U,BASE,BASE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(name,"reverse-window")==0)return Refuse(1U,1U,BASE+PAGE,BASE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(name,"unaligned-start")==0)return Refuse(1U,1U,BASE+1U,BASE+PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(name,"unaligned-end")==0)return Refuse(1U,1U,BASE,BASE+PAGE+1U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(name,"below-ram")==0 || strcmp(name,"disjoint-low")==0)return Refuse(1U,1U,0U,strcmp(name,"below-ram")==0?BASE+PAGE:BASE,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0);
    if(strcmp(name,"above-ram")==0 || strcmp(name,"disjoint-high")==0)return Refuse(1U,1U,strcmp(name,"above-ram")==0?BASE:BASE+32U*PAGE,BASE+33U*PAGE,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0);
    if(strcmp(name,"whole")==0)return Allocate(32U,1U,BASE,BASE+32U*PAGE,BASE);
    if(strcmp(name,"single")==0)return Allocate(1U,1U,BASE+9U*PAGE,BASE+10U*PAGE,BASE+9U*PAGE);
    if(strcmp(name,"middle")==0)return Allocate(3U,1U,BASE+4U*PAGE,BASE+12U*PAGE,BASE+4U*PAGE);
    if(strcmp(name,"exact-end")==0)return Allocate(2U,1U,BASE+30U*PAGE,BASE+32U*PAGE,BASE+30U*PAGE);
    if(strcmp(name,"count-too-large")==0)return Refuse(~(UmicomSize)0U,1U,BASE,BASE+32U*PAGE,UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,0);
    if(strcmp(name,"no-fallback")==0 || strcmp(name,"output-preserved")==0)return Refuse(3U,1U,BASE+4U*PAGE,BASE+6U*PAGE,UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,0);
    if(strcmp(name,"absolute-alignment")==0)return Allocate(2U,4U,BASE+4U*PAGE,BASE+10U*PAGE,8U*PAGE);
    if(strcmp(name,"alignment-no-fit")==0)return Refuse(2U,4U,BASE+4U*PAGE,BASE+7U*PAGE,UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,0);
    if(strcmp(name,"largest-alignment")==0)return Refuse(1U,(UmicomSize)1U<<63U,BASE,BASE+32U*PAGE,UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,0);
    if(strcmp(name,"largest-alignment-zero")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(0U,32U*PAGE)==OK);return Allocate(1U,(UmicomSize)1U<<63U,0U,PAGE,0U);}
    if(strcmp(name,"reserved-start")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+4U*PAGE,PAGE)==OK);return Allocate(2U,1U,BASE+4U*PAGE,BASE+8U*PAGE,BASE+5U*PAGE);}
    if(strcmp(name,"reserved-middle")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+6U*PAGE,PAGE)==OK);return Allocate(3U,1U,BASE+4U*PAGE,BASE+10U*PAGE,BASE+7U*PAGE);}
    if(strcmp(name,"allocated-start")==0 || strcmp(name,"allocated-middle")==0){UmicomAddress block=BASE+(strcmp(name,"allocated-start")==0?4U:6U)*PAGE;CHECK(Allocate(1U,1U,block,block+PAGE,block)==0);return Allocate(3U,1U,BASE+4U*PAGE,BASE+10U*PAGE,strcmp(name,"allocated-start")==0?BASE+5U*PAGE:BASE+7U*PAGE);}
    if(strcmp(name,"fragmented")==0){for(UmicomSize i=4U;i<12U;i+=2U)CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+i*PAGE,PAGE)==OK);return Refuse(2U,1U,BASE+4U*PAGE,BASE+12U*PAGE,UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,0);}
    if(strcmp(name,"reuse")==0){CHECK(Allocate(2U,1U,BASE+4U*PAGE,BASE+10U*PAGE,BASE+4U*PAGE)==0);CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE+4U*PAGE,2U)==OK);return Allocate(2U,1U,BASE+4U*PAGE,BASE+10U*PAGE,BASE+4U*PAGE);}
    if(strcmp(name,"mixed-whole")==0){CHECK(Allocate(2U,1U,BASE,BASE+4U*PAGE,BASE)==0);CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&out)==OK && out==BASE+2U*PAGE);CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE,2U)==OK);return Allocate(2U,1U,BASE,BASE+4U*PAGE,BASE);}
    if(strcmp(name,"partial-release")==0){CHECK(Allocate(4U,1U,BASE,BASE+8U*PAGE,BASE)==0);CHECK(UmicomKernelPhysicalMemoryFreeFrames(BASE+PAGE,2U)==OK);return Allocate(2U,1U,BASE,BASE+8U*PAGE,BASE+PAGE);}
    if(strcmp(name,"top-address")==0){UmicomAddress end=(~(UmicomAddress)0U)&~((UmicomAddress)PAGE-1U);CHECK(UmicomKernelPhysicalMemoryInitialize(end-32U*PAGE,32U*PAGE)==OK);return Allocate(2U,1U,end-2U*PAGE,end,end-2U*PAGE);}
    if(strcmp(name,"maximum-capacity")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE)==OK);CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(UMICOM_KERNEL_PHYSICAL_MAX_FRAMES,1U,BASE,BASE+UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE,&out)==OK);CHECK(out==BASE);CHECK(UmicomKernelPhysicalMemoryValidate()==OK);CHECK(UmicomKernelPhysicalMemoryFreeFrames(out,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES)==OK);return 0;}
    if(strcmp(name,"bitmap-boundary")==0)return Allocate(5U,1U,BASE+6U*PAGE,BASE+13U*PAGE,BASE+6U*PAGE);
    if(strcmp(name,"exhausted")==0){CHECK(Allocate(32U,1U,BASE,BASE+32U*PAGE,BASE)==0);return Refuse(1U,1U,BASE+PAGE,BASE+2U*PAGE,UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,0);}
    return 2;
}
