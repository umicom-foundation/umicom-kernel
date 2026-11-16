/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/physical_memory/window_report_tests.c
 *
 * PURPOSE:
 *   Verify read-only frame reports against exact ownership states and independent expected run geometry.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/physical_window_report.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define PAGE UMICOM_KERNEL_PAGE_SIZE
#define BASE ((UmicomAddress)0x1000U)
#define END (BASE+16U*PAGE)
#define OK UMICOM_KERNEL_MEMORY_OK
static UmicomKernelPhysicalFrameState saved[UMICOM_KERNEL_PHYSICAL_MAX_FRAMES];
static UmicomKernelPhysicalMemorySnapshot before;
static int Save(void)
{
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&before)==OK);
    for(UmicomSize i=0U;i<before.totalFrames;++i)CHECK(UmicomKernelPhysicalMemoryFrameQuery(before.ramBase+i*PAGE,&saved[i])==OK);
    return 0;
}
static int Unchanged(void)
{
    UmicomKernelPhysicalMemorySnapshot after;CHECK(UmicomKernelPhysicalMemorySnapshotRead(&after)==OK);
    CHECK(before.ramBase==after.ramBase && before.ramBytes==after.ramBytes && before.totalFrames==after.totalFrames);
    CHECK(before.freeFrames==after.freeFrames && before.reservedFrames==after.reservedFrames && before.allocatedFrames==after.allocatedFrames);
    for(UmicomSize i=0U;i<before.totalFrames;++i){UmicomKernelPhysicalFrameState current;
        CHECK(UmicomKernelPhysicalMemoryFrameQuery(before.ramBase+i*PAGE,&current)==OK && current==saved[i]);}
    CHECK(UmicomKernelPhysicalMemoryValidate()==OK);return 0;
}
static int Report(UmicomAddress start,UmicomAddress end,UmicomSize alignment,
    UmicomSize freeCount,UmicomSize reserved,UmicomSize allocated,UmicomSize runs,
    UmicomAddress first,UmicomSize longest)
{
    CHECK(Save()==0);UmicomKernelPhysicalWindowReport value;
    CHECK(UmicomKernelPhysicalWindowInspect(start,end,alignment,&value)==OK);
    CHECK(value.minimumAddress==start && value.maximumEnd==end && value.alignmentFrames==alignment);
    CHECK(value.totalFrames==(end-start)/PAGE && value.totalFrames==freeCount+reserved+allocated);
    CHECK(value.freeFrames==freeCount && value.reservedFrames==reserved && value.allocatedFrames==allocated);
    CHECK(value.freeRuns==runs && value.largestFirstFrame==first && value.largestRunFrames==longest);
    return Unchanged();
}
static int Refuse(UmicomAddress start,UmicomAddress end,UmicomSize alignment,UmicomKernelMemoryStatus expected,int null_output)
{
    CHECK(Save()==0);UmicomKernelPhysicalWindowReport value,prior;memset(&value,0x5a,sizeof value);memcpy(&prior,&value,sizeof value);
    CHECK(UmicomKernelPhysicalWindowInspect(start,end,alignment,null_output?NULL:&value)==expected);
    CHECK(memcmp(&value,&prior,sizeof value)==0);return Unchanged();
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];UmicomAddress owned=0U;
    if(strcmp(mode,"uninitialised")==0){UmicomKernelPhysicalWindowReport value,prior;memset(&value,0x5a,sizeof value);memcpy(&prior,&value,sizeof value);
        CHECK(UmicomKernelPhysicalWindowInspect(BASE,END,1U,&value)==UMICOM_KERNEL_MEMORY_NOT_INITIALISED);CHECK(memcmp(&value,&prior,sizeof value)==0);return 0;}
    CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,16U*PAGE)==OK);
    if(strcmp(mode,"null-output")==0)return Refuse(BASE,END,1U,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,1);
    if(strcmp(mode,"zero-alignment")==0)return Refuse(BASE,END,0U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"nonpower-alignment")==0)return Refuse(BASE,END,3U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"empty")==0)return Refuse(BASE,BASE,1U,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(mode,"reverse")==0)return Refuse(END,BASE,1U,UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT,0);
    if(strcmp(mode,"unaligned-start")==0)return Refuse(BASE+1U,END,1U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"unaligned-end")==0)return Refuse(BASE,END-1U,1U,UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,0);
    if(strcmp(mode,"below-ram")==0)return Refuse(0U,END,1U,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0);
    if(strcmp(mode,"above-ram")==0)return Refuse(BASE,END+PAGE,1U,UMICOM_KERNEL_MEMORY_OUTSIDE_RAM,0);
    if(strcmp(mode,"whole")==0)return Report(BASE,END,1U,16U,0U,0U,1U,BASE,16U);
    if(strcmp(mode,"single")==0)return Report(BASE+3U*PAGE,BASE+4U*PAGE,1U,1U,0U,0U,1U,BASE+3U*PAGE,1U);
    if(strcmp(mode,"subwindow")==0)return Report(BASE+3U*PAGE,BASE+9U*PAGE,1U,6U,0U,0U,1U,BASE+3U*PAGE,6U);
    if(strcmp(mode,"reserved")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+4U*PAGE,2U*PAGE)==OK);return Report(BASE,END,1U,14U,2U,0U,2U,BASE+6U*PAGE,10U);}
    if(strcmp(mode,"allocated")==0){CHECK(UmicomKernelPhysicalMemoryAllocateFrames(3U,1U,&owned)==OK);CHECK(owned==BASE);return Report(BASE,END,1U,13U,0U,3U,1U,BASE+3U*PAGE,13U);}
    if(strcmp(mode,"mixed")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+4U*PAGE,2U*PAGE)==OK);CHECK(UmicomKernelPhysicalMemoryAllocateFrames(3U,1U,&owned)==OK);return Report(BASE,END,1U,11U,2U,3U,2U,BASE+6U*PAGE,10U);}
    if(strcmp(mode,"fragmented")==0){for(UmicomSize i=1U;i<16U;i+=2U)CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+i*PAGE,PAGE)==OK);return Report(BASE,END,1U,8U,8U,0U,8U,BASE,1U);}
    if(strcmp(mode,"alignment-prefix")==0)return Report(BASE,END,4U,16U,0U,0U,1U,BASE+3U*PAGE,13U);
    if(strcmp(mode,"alignment-none")==0)return Report(BASE,BASE+2U*PAGE,4U,2U,0U,0U,1U,0U,0U);
    if(strcmp(mode,"first-tie")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+4U*PAGE,PAGE)==OK);return Report(BASE,BASE+9U*PAGE,1U,8U,1U,0U,2U,BASE,4U);}
    if(strcmp(mode,"aligned-tie")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+7U*PAGE,4U*PAGE)==OK);return Report(BASE,BASE+15U*PAGE,4U,11U,4U,0U,2U,BASE+3U*PAGE,4U);}
    if(strcmp(mode,"largest-alignment")==0)return Report(BASE,END,(UmicomSize)1U<<63U,16U,0U,0U,1U,0U,0U);
    if(strcmp(mode,"zero-address")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(0U,16U*PAGE)==OK);return Report(0U,16U*PAGE,(UmicomSize)1U<<63U,16U,0U,0U,1U,0U,16U);}
    if(strcmp(mode,"top-address")==0){UmicomAddress end=(~(UmicomAddress)0U)&~((UmicomAddress)PAGE-1U);CHECK(UmicomKernelPhysicalMemoryInitialize(end-16U*PAGE,16U*PAGE)==OK);return Report(end-3U*PAGE,end,1U,3U,0U,0U,1U,end-3U*PAGE,3U);}
    if(strcmp(mode,"maximum-capacity")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE)==OK);return Report(BASE,BASE+UMICOM_KERNEL_PHYSICAL_MAX_FRAMES*PAGE,1U,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES,0U,0U,1U,BASE,UMICOM_KERNEL_PHYSICAL_MAX_FRAMES);}
    if(strcmp(mode,"after-free")==0){CHECK(UmicomKernelPhysicalMemoryAllocateFrames(3U,1U,&owned)==OK);CHECK(UmicomKernelPhysicalMemoryFreeFrames(owned,3U)==OK);return Report(BASE,END,1U,16U,0U,0U,1U,BASE,16U);}
    if(strcmp(mode,"after-unreserve")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+4U*PAGE,2U*PAGE)==OK);CHECK(UmicomKernelPhysicalMemoryReleaseReservedRange(BASE+4U*PAGE,2U*PAGE)==OK);return Report(BASE,END,1U,16U,0U,0U,1U,BASE,16U);}
    if(strcmp(mode,"window-exclusion")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+4U*PAGE,2U*PAGE)==OK);return Report(BASE+6U*PAGE,BASE+10U*PAGE,1U,4U,0U,0U,1U,BASE+6U*PAGE,4U);}
    if(strcmp(mode,"no-reservation")==0){CHECK(Report(BASE,END,1U,16U,0U,0U,1U,BASE,16U)==0);CHECK(UmicomKernelPhysicalMemoryAllocateFrames(16U,1U,&owned)==OK);CHECK(owned==BASE);return Report(BASE,END,1U,0U,0U,16U,0U,0U,0U);}
    if(strcmp(mode,"bitmap-boundary")==0){CHECK(UmicomKernelPhysicalMemoryInitialize(BASE,128U*PAGE)==OK);CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE+63U*PAGE,2U*PAGE)==OK);return Report(BASE,BASE+128U*PAGE,1U,126U,2U,0U,2U,BASE,63U);}
    if(strcmp(mode,"repeat")==0){CHECK(Report(BASE,END,1U,16U,0U,0U,1U,BASE,16U)==0);return Report(BASE,END,1U,16U,0U,0U,1U,BASE,16U);}
    if(strcmp(mode,"all-reserved")==0){CHECK(UmicomKernelPhysicalMemoryReserveRange(BASE,16U*PAGE)==OK);return Report(BASE,END,1U,0U,16U,0U,0U,0U,0U);}
    if(strcmp(mode,"all-allocated")==0){CHECK(UmicomKernelPhysicalMemoryAllocateFrames(16U,1U,&owned)==OK);return Report(BASE,END,1U,0U,0U,16U,0U,0U,0U);}
    fprintf(stderr,"Unknown mode: %s\n",mode);return 2;
}
