/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/physical_memory/frame_boundary_tests.c
 *
 * PURPOSE:
 *   Qualify boundary-constrained ownership against exact cases and an independent exhaustive small-memory model.
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
#define PAGE UMICOM_KERNEL_PAGE_SIZE
#define OK UMICOM_KERNEL_MEMORY_OK
#define OOM UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY
#define MARKER ((UmicomAddress)0x12345678U)
typedef struct Snapshot {
    UmicomKernelPhysicalMemorySnapshot memory;
    UmicomKernelPhysicalFrameState frames[32];
} Snapshot;
static int Save(Snapshot *out)
{
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&out->memory)==OK);
    CHECK(out->memory.totalFrames<=32U);
    for(UmicomSize i=0U;i<out->memory.totalFrames;++i)
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(out->memory.ramBase+i*PAGE,&out->frames[i])==OK);
    return 0;
}
/* Check every frame, including pages outside the request, against the observed
 * pre-call model. The expected geometry is supplied independently by each case. */
static int Request(UmicomSize count,UmicomSize alignment,UmicomSize boundary,
    UmicomAddress minimum,UmicomAddress maximum,UmicomKernelMemoryStatus expected,
    UmicomAddress expectedAddress,int nullOutput)
{
    Snapshot before,after;CHECK(Save(&before)==0);UmicomAddress out=MARKER;
    CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindowWithBoundary(
        count,alignment,boundary,minimum,maximum,nullOutput?NULL:&out)==expected);
    CHECK(out==(expected==OK?expectedAddress:MARKER));CHECK(Save(&after)==0);
    UmicomSize acquired=expected==OK?count:0U;
    CHECK(after.memory.ramBase==before.memory.ramBase && after.memory.ramBytes==before.memory.ramBytes);
    CHECK(after.memory.reservedFrames==before.memory.reservedFrames);
    CHECK(after.memory.allocatedFrames==before.memory.allocatedFrames+acquired);
    CHECK(after.memory.freeFrames==before.memory.freeFrames-acquired);
    for(UmicomSize i=0U;i<before.memory.totalFrames;++i){
        UmicomAddress address=before.memory.ramBase+i*PAGE;
        int selected=expected==OK && address>=out && (address-out)/PAGE<count;
        CHECK(after.frames[i]==(selected?UMICOM_PHYSICAL_FRAME_ALLOCATED:before.frames[i]));
    }
    CHECK(UmicomKernelPhysicalMemoryValidate()==OK);
    return 0;
}
static int Exhaustive(UmicomSize baseFrame)
{
    /* Six frames keep all 64 ownership masks affordable. Alternating occupied
     * frames use reservations and allocations. The oracle checks each complete
     * candidate with division, independently of the allocator's running scan. */
    const UmicomSize alignments[]={1U,2U,4U},boundaries[]={0U,1U,2U,4U,8U};
    for(unsigned mask=0U;mask<64U;++mask){
        for(UmicomSize count=1U;count<=4U;++count){
            for(size_t a=0U;a<3U;++a){
                for(size_t b=0U;b<5U;++b){
                    for(UmicomSize window=0U;window<3U;++window){
                        CHECK(UmicomKernelPhysicalMemoryInitialize(baseFrame*PAGE,6U*PAGE)==OK);
                        for(UmicomSize i=0U;i<6U;++i){
                            if((mask&(1U<<(unsigned)i))==0U)continue;
                            UmicomAddress address=(baseFrame+i)*PAGE,owned=MARKER;
                            if((i&1U)==0U)CHECK(UmicomKernelPhysicalMemoryReserveRange(address,PAGE)==OK);
                            else CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(1U,1U,address,address+PAGE,&owned)==OK && owned==address);
                        }
                        UmicomSize begin=window,end=6U-window;
                        UmicomAddress expected=MARKER;UmicomKernelMemoryStatus status=OOM;
                        for(UmicomSize candidate=begin;candidate<end;++candidate){
                            UmicomSize absolute=baseFrame+candidate;
                            if(count>end-candidate || absolute%alignments[a]!=0U)continue;
                            if(boundaries[b]!=0U && absolute/boundaries[b]!=(absolute+count-1U)/boundaries[b])continue;
                            int available=1;
                            for(UmicomSize offset=0U;offset<count;++offset)
                                if((mask&(1U<<(unsigned)(candidate+offset)))!=0U)available=0;
                            if(available){status=OK;expected=absolute*PAGE;break;}
                        }
                        CHECK(Request(count,alignments[a],boundaries[b],(baseFrame+begin)*PAGE,
                            (baseFrame+end)*PAGE,status,expected,0)==0);
                    }
                }
            }
        }
    }
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];UmicomAddress out=MARKER;
    if(!strcmp(mode,"uninitialised")){
        CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindowWithBoundary(1U,1U,4U,PAGE,2U*PAGE,&out)==UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
        CHECK(out==MARKER);return 0;
    }
    if(!strcmp(mode,"exhaustive-zero"))return Exhaustive(0U);
    if(!strcmp(mode,"exhaustive-one"))return Exhaustive(1U);
    if(!strcmp(mode,"exhaustive-three"))return Exhaustive(3U);
    if(!strcmp(mode,"exhaustive-five"))return Exhaustive(5U);
    CHECK(UmicomKernelPhysicalMemoryInitialize(PAGE,32U*PAGE)==OK);
    if(!strcmp(mode,"null-output"))return Request(1U,1U,4U,PAGE,2U*PAGE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0U,1);
    if(!strcmp(mode,"zero-count"))return Request(0U,1U,4U,PAGE,2U*PAGE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0U,0);
    if(!strcmp(mode,"zero-alignment"))return Request(1U,0U,4U,PAGE,2U*PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0U,0);
    if(!strcmp(mode,"nonpower-alignment"))return Request(1U,3U,4U,PAGE,5U*PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0U,0);
    if(!strcmp(mode,"nonpower-boundary"))return Request(1U,1U,3U,PAGE,5U*PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0U,0);
    if(!strcmp(mode,"empty-window"))return Request(1U,1U,4U,PAGE,PAGE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0U,0);
    if(!strcmp(mode,"reverse-window"))return Request(1U,1U,4U,2U*PAGE,PAGE,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0U,0);
    if(!strcmp(mode,"unaligned-start"))return Request(1U,1U,4U,PAGE+1U,4U*PAGE,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0U,0);
    if(!strcmp(mode,"unaligned-end"))return Request(1U,1U,4U,PAGE,4U*PAGE+1U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0U,0);
    if(!strcmp(mode,"below-ram"))return Request(1U,1U,4U,0U,PAGE,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0U,0);
    if(!strcmp(mode,"above-ram"))return Request(1U,1U,4U,PAGE,34U*PAGE,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0U,0);
    if(!strcmp(mode,"disabled"))return Request(8U,1U,0U,PAGE,9U*PAGE,OK,PAGE,0);
    if(!strcmp(mode,"single-boundary"))return Request(1U,1U,1U,3U*PAGE,4U*PAGE,OK,3U*PAGE,0);
    if(!strcmp(mode,"crossing-prefix"))return Request(3U,1U,4U,3U*PAGE,8U*PAGE,OK,4U*PAGE,0);
    if(!strcmp(mode,"exact-end"))return Request(3U,1U,4U,PAGE,4U*PAGE,OK,PAGE,0);
    if(!strcmp(mode,"exact-bucket"))return Request(4U,1U,4U,PAGE,8U*PAGE,OK,4U*PAGE,0);
    if(!strcmp(mode,"count-too-large"))return Request(5U,1U,4U,PAGE,16U*PAGE,OOM,0U,0);
    if(!strcmp(mode,"window-no-fit"))return Request(3U,1U,4U,3U*PAGE,6U*PAGE,OOM,0U,0);
    if(!strcmp(mode,"absolute-origin"))return Request(4U,1U,4U,5U*PAGE,13U*PAGE,OK,8U*PAGE,0);
    if(!strcmp(mode,"alignment-and-boundary"))return Request(3U,2U,4U,5U*PAGE,12U*PAGE,OK,8U*PAGE,0);
    if(!strcmp(mode,"alignment-larger"))return Request(2U,8U,4U,PAGE,12U*PAGE,OK,8U*PAGE,0);
    if(!strcmp(mode,"largest-boundary"))return Request(3U,1U,(UmicomSize)1U<<63U,PAGE,4U*PAGE,OK,PAGE,0);
    if(!strcmp(mode,"largest-alignment"))return Request(1U,(UmicomSize)1U<<63U,4U,PAGE,33U*PAGE,OOM,0U,0);
    if(!strcmp(mode,"zero-address")){
        CHECK(UmicomKernelPhysicalMemoryInitialize(0U,32U*PAGE)==OK);
        return Request(4U,(UmicomSize)1U<<63U,4U,0U,4U*PAGE,OK,0U,0);
    }
    if(!strcmp(mode,"top-address")){
        const UmicomAddress end=(~(UmicomAddress)0U)&~((UmicomAddress)PAGE-1U),base=end-8U*PAGE;
        CHECK(UmicomKernelPhysicalMemoryInitialize(base,8U*PAGE)==OK);
        return Request(4U,1U,4U,base,end,OK,base+PAGE,0);
    }
    if(!strcmp(mode,"reserved-prefix")){
        CHECK(UmicomKernelPhysicalMemoryReserveRange(4U*PAGE,PAGE)==OK);
        return Request(4U,1U,4U,3U*PAGE,13U*PAGE,OK,8U*PAGE,0);
    }
    if(!strcmp(mode,"allocated-prefix")){
        CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(1U,1U,4U*PAGE,5U*PAGE,&out)==OK);
        return Request(3U,1U,4U,3U*PAGE,12U*PAGE,OK,5U*PAGE,0);
    }
    if(!strcmp(mode,"fragmented")){
        for(UmicomSize frame=1U;frame<16U;frame+=4U)
            CHECK(UmicomKernelPhysicalMemoryReserveRange(frame*PAGE,PAGE)==OK);
        return Request(4U,1U,4U,PAGE,16U*PAGE,OOM,0U,0);
    }
    if(!strcmp(mode,"reuse")){
        CHECK(Request(4U,1U,4U,PAGE,12U*PAGE,OK,4U*PAGE,0)==0);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(4U*PAGE,4U)==OK);
        return Request(4U,1U,4U,PAGE,12U*PAGE,OK,4U*PAGE,0);
    }
    if(!strcmp(mode,"bitmap-boundary"))return Request(3U,1U,8U,7U*PAGE,16U*PAGE,OK,8U*PAGE,0);
    if(!strcmp(mode,"maximum-capacity")){
        CHECK(UmicomKernelPhysicalMemoryInitialize(0U,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE)==OK);
        CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindowWithBoundary(UMICOM_KERNEL_PHYSICAL_MAX_FRAMES,1U,
            UMICOM_KERNEL_PHYSICAL_MAX_FRAMES,0U,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE,&out)==OK && out==0U);
        UmicomKernelPhysicalMemorySnapshot snapshot;
        CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot)==OK && snapshot.freeFrames==0U);
        CHECK(UmicomKernelPhysicalMemoryFreeFrames(out,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES)==OK);
        CHECK(UmicomKernelPhysicalMemoryValidate()==OK);return 0;
    }
    CHECK(0);return 1;
}
