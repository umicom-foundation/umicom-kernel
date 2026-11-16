/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/physical_memory/boundary_report_tests.c
 *
 * PURPOSE:
 *   Compare boundary-aware reports with an exhaustive independent candidate oracle while verifying every frame remains unchanged.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/physical_boundary_report.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define PAGE UMICOM_KERNEL_PAGE_SIZE
#define BASE ((UmicomAddress)PAGE)
#define END (BASE+16U*PAGE)
#define OK UMICOM_KERNEL_MEMORY_OK
static UmicomKernelPhysicalFrameState states[UMICOM_KERNEL_PHYSICAL_MAX_FRAMES];
static UmicomKernelPhysicalMemorySnapshot before;
static int Save(void)
{
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&before)==OK);
    for(UmicomSize i=0U;i<before.totalFrames;++i)
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(before.ramBase+i*PAGE,&states[i])==OK);
    return 0;
}
static int Unchanged(void)
{
    UmicomKernelPhysicalMemorySnapshot after;CHECK(UmicomKernelPhysicalMemorySnapshotRead(&after)==OK);
    CHECK(after.ramBase==before.ramBase && after.ramBytes==before.ramBytes && after.totalFrames==before.totalFrames);
    CHECK(after.freeFrames==before.freeFrames && after.reservedFrames==before.reservedFrames && after.allocatedFrames==before.allocatedFrames);
    for(UmicomSize i=0U;i<before.totalFrames;++i){UmicomKernelPhysicalFrameState state;
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(before.ramBase+i*PAGE,&state)==OK && state==states[i]);}
    CHECK(UmicomKernelPhysicalMemoryValidate()==OK);return 0;
}
static int Report(UmicomAddress minimum,UmicomAddress maximum,UmicomSize alignment,
    UmicomSize boundary,UmicomKernelMemoryStatus expected,int null_output)
{
    CHECK(Save()==0);
    UmicomKernelPhysicalBoundaryReport result,prior;memset(&result,0xa5,sizeof result);memcpy(&prior,&result,sizeof result);
    CHECK(UmicomKernelPhysicalBoundaryInspect(minimum,maximum,alignment,boundary,null_output?NULL:&result)==expected);
    if(expected!=OK){CHECK(memcmp(&result,&prior,sizeof result)==0);return Unchanged();}
    UmicomSize free_count=0U,reserved=0U,allocated=0U,runs=0U,longest=0U;UmicomAddress first=0U;
    int previous_free=0;
    for(UmicomAddress p=minimum;p<maximum;p+=PAGE){
        const UmicomSize slot=(UmicomSize)((p-before.ramBase)/PAGE);
        if(states[slot]==UMICOM_PHYSICAL_FRAME_FREE){
            ++free_count;if(!previous_free)++runs;previous_free=1;
        }else{
            previous_free=0;
            if(states[slot]==UMICOM_PHYSICAL_FRAME_RESERVED)++reserved;else ++allocated;
        }
        /* Independent oracle tries each eligible start and uses division to
         * compare first/last buckets; production keeps one masked suffix. */
        const UmicomU64 number=(UmicomU64)p/(UmicomU64)PAGE;
        if(number%alignment!=0U)continue;
        UmicomSize length=0U;
        for(UmicomAddress end=p;end<maximum;end+=PAGE){
            const UmicomSize current=(UmicomSize)((end-before.ramBase)/PAGE);
            if(states[current]!=UMICOM_PHYSICAL_FRAME_FREE)break;
            if(boundary!=0U && number/boundary!=((UmicomU64)end/(UmicomU64)PAGE)/boundary)break;
            ++length;
        }
        if(length>longest){longest=length;first=p;}
    }
    CHECK(result.boundaryFrames==boundary);
    CHECK(result.window.minimumAddress==minimum && result.window.maximumEnd==maximum && result.window.alignmentFrames==alignment);
    CHECK(result.window.totalFrames==(maximum-minimum)/PAGE && result.window.freeFrames==free_count);
    CHECK(result.window.reservedFrames==reserved && result.window.allocatedFrames==allocated && result.window.freeRuns==runs);
    CHECK(result.window.largestRunFrames==longest && result.window.largestFirstFrame==first);
    if(boundary==0U){
        UmicomKernelPhysicalWindowReport original;CHECK(UmicomKernelPhysicalWindowInspect(minimum,maximum,alignment,&original)==OK);
        CHECK(original.freeFrames==result.window.freeFrames && original.freeRuns==result.window.freeRuns);
        CHECK(original.largestRunFrames==longest && original.largestFirstFrame==first);
    }
    return Unchanged();
}
static int Exhaustive(UmicomAddress base)
{
    const UmicomSize alignments[]={1U,2U,4U},boundaries[]={0U,1U,2U,4U,8U};
    for(unsigned mask=0U;mask<64U;++mask){
        CHECK(UmicomKernelPhysicalMemoryInitialize(base,6U*PAGE)==OK);
        for(UmicomSize i=0U;i<6U;++i)if((mask&(1U<<i))!=0U){
            if((i&1U)==0U)CHECK(UmicomKernelPhysicalMemoryReserveRange(base+i*PAGE,PAGE)==OK);
            else {UmicomAddress owned=0U;CHECK(UmicomKernelPhysicalMemoryAllocateFramesInWindow(1U,1U,base+i*PAGE,base+(i+1U)*PAGE,&owned)==OK && owned==base+i*PAGE);}
        }
        for(UmicomSize a=0U;a<3U;++a)for(UmicomSize b=0U;b<5U;++b)
        for(UmicomSize lo=0U;lo<6U;++lo)for(UmicomSize hi=lo+1U;hi<=6U;++hi)
            CHECK(Report(base+lo*PAGE,base+hi*PAGE,alignments[a],boundaries[b],OK,0)==0);
    }
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];
    if(strcmp(mode,"uninitialised")==0){
        UmicomKernelPhysicalBoundaryReport out,prior;memset(&out,0xa5,sizeof out);memcpy(&prior,&out,sizeof out);
        CHECK(UmicomKernelPhysicalBoundaryInspect(BASE,END,1U,4U,&out)==UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
        CHECK(memcmp(&out,&prior,sizeof out)==0);return 0;
    }
    if(strcmp(mode,"exhaustive-zero")==0)return Exhaustive(0U);
    if(strcmp(mode,"exhaustive-one")==0)return Exhaustive(PAGE);
    if(strcmp(mode,"exhaustive-three")==0)return Exhaustive(3U*PAGE);
    if(strcmp(mode,"exhaustive-five")==0)return Exhaustive(5U*PAGE);
    CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,16U*PAGE)==OK);
    if(strcmp(mode,"null-output")==0)return Report(BASE,END,1U,4U,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,1);
    if(strcmp(mode,"zero-alignment")==0)return Report(BASE,END,0U,4U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"nonpower-alignment")==0)return Report(BASE,END,3U,4U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"nonpower-boundary")==0)return Report(BASE,END,1U,3U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"empty")==0)return Report(BASE,BASE,1U,4U,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(mode,"reverse")==0)return Report(END,BASE,1U,4U,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(mode,"unaligned-start")==0)return Report(BASE+1U,END,1U,4U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"unaligned-end")==0)return Report(BASE,END-1U,1U,4U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"below-ram")==0)return Report(0U,END,1U,4U,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0);
    if(strcmp(mode,"above-ram")==0)return Report(BASE,END+PAGE,1U,4U,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0);
    if(strcmp(mode,"disabled")==0)return Report(BASE,END,1U,0U,OK,0);
    if(strcmp(mode,"single-boundary")==0)return Report(BASE,END,1U,1U,OK,0);
    if(strcmp(mode,"crossing-prefix")==0){
        UmicomKernelPhysicalBoundaryReport report;
        CHECK(UmicomKernelPhysicalBoundaryInspect(BASE,4U*PAGE,1U,2U,&report)==OK);
        CHECK(report.window.freeRuns==1U && report.window.freeFrames==3U && report.window.largestFirstFrame==2U*PAGE && report.window.largestRunFrames==2U);
        return Report(BASE,4U*PAGE,1U,2U,OK,0);
    }
    if(strcmp(mode,"exact-bucket")==0)return Report(4U*PAGE,8U*PAGE,1U,4U,OK,0);
    if(strcmp(mode,"partial-buckets")==0)return Report(BASE,3U*PAGE,1U,2U,OK,0);
    if(strcmp(mode,"absolute-alignment")==0)return Report(BASE,END,4U,8U,OK,0);
    if(strcmp(mode,"alignment-larger")==0)return Report(BASE,END,8U,2U,OK,0);
    if(strcmp(mode,"no-aligned-start")==0)return Report(BASE,3U*PAGE,4U,2U,OK,0);
    if(strcmp(mode,"fragmented")==0){for(UmicomSize i=1U;i<16U;i+=2U)CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+i*PAGE,PAGE)==OK);return Report(BASE,END,1U,4U,OK,0);}
    if(strcmp(mode,"reserved")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(3U*PAGE,3U*PAGE)==OK);return Report(BASE,END,2U,4U,OK,0);}
    if(strcmp(mode,"allocated")==0 || strcmp(mode,"mixed")==0 || strcmp(mode,"after-release")==0){
        UmicomAddress owned=0U;CHECK(UmicomKernelPhysicalMemoryAllocateFrames(3U,1U,&owned)==OK && owned==BASE);
        if(strcmp(mode,"mixed")==0)CHECK(UmicomKernelPhysicalMemoryReserveRange(8U*PAGE,2U*PAGE)==OK);
        if(strcmp(mode,"after-release")==0)CHECK(UmicomKernelPhysicalMemoryFreeFrames(owned,3U)==OK);
        return Report(BASE,END,2U,4U,OK,0);
    }
    if(strcmp(mode,"zero-address")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(0U,16U*PAGE)==OK);return Report(0U,16U*PAGE,(UmicomSize)1U<<63U,4U,OK,0);}
    if(strcmp(mode,"top-address")==0){UmicomAddress end=(~(UmicomAddress)0U)&~((UmicomAddress)PAGE-1U);CHECK(UmicomKernelPhysicalMemoryInitialize(end-16U*PAGE,16U*PAGE)==OK);return Report(end-16U*PAGE,end,2U,8U,OK,0);}
    if(strcmp(mode,"largest-boundary")==0)return Report(BASE,END,2U,(UmicomSize)1U<<63U,OK,0);
    if(strcmp(mode,"largest-alignment")==0)return Report(BASE,END,(UmicomSize)1U<<63U,4U,OK,0);
    if(strcmp(mode,"maximum-capacity")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE)==OK);return Report(BASE,BASE+UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE,1U,64U,OK,0);}
    fprintf(stderr,"Unknown mode: %s\n",mode);return 2;
}
