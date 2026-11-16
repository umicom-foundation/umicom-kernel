/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/virtual_memory/physical_address_tests.c
 *
 * PURPOSE:
 *   Verify Sv39 physical-width admission, unchanged page tables on refusal and bounded backing-frame allocation.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/



#include "umicom/kernel/virtual_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define PAGE ((UmicomAddress)4096U)
#define ENCODING_END ((UmicomAddress)0x0100000000000000ULL)
#define LOW_VA ((UmicomAddress)0x20000000U)
alignas(4096) static unsigned char ram[4096U*32U];
static unsigned char beforeRam[sizeof ram];
static UmicomKernelVirtualAddressSpace space;
static void Initialize(UmicomSize frames)
{
    memset(ram,0xa5,sizeof ram);memset(&space,0,sizeof space);
    CHECK(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)ram,frames*4096U)==UMICOM_KERNEL_MEMORY_OK);
}
static void Create(void)
{
    CHECK(UmicomKernelVirtualAddressSpaceCreate(&space)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
}
static UmicomKernelPhysicalMemorySnapshot Snapshot(void)
{
    UmicomKernelPhysicalMemorySnapshot snapshot={0};
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot)==UMICOM_KERNEL_MEMORY_OK);return snapshot;
}
static void SameAccounting(const UmicomKernelPhysicalMemorySnapshot *before)
{
    UmicomKernelPhysicalMemorySnapshot after=Snapshot();
    CHECK(before->ramBase==after.ramBase && before->ramBytes==after.ramBytes);
    CHECK(before->pageBytes==after.pageBytes && before->totalFrames==after.totalFrames);
    CHECK(before->reservedFrames==after.reservedFrames && before->allocatedFrames==after.allocatedFrames);
    CHECK(before->freeFrames==after.freeFrames);
    CHECK(UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK);
}
static void Refuse(UmicomAddress va,UmicomAddress pa,
    UmicomKernelVirtualMemoryPermissions permissions,UmicomKernelVirtualMemoryStatus expected)
{
    UmicomKernelPhysicalMemorySnapshot before=Snapshot();
    UmicomKernelVirtualAddressSpace old=space;memcpy(beforeRam,ram,sizeof ram);
    CHECK(UmicomKernelVirtualMemoryMapPage(&space,va,pa,permissions)==expected);
    SameAccounting(&before);
    CHECK(space.rootTablePhysicalAddress==old.rootTablePhysicalAddress && space.pageTableFrames==old.pageTableFrames);
    CHECK(space.mappedPages==old.mappedPages && space.initialised==old.initialised);
    CHECK(!memcmp(beforeRam,ram,sizeof ram));
    CHECK(UmicomKernelVirtualMemoryValidate(&space)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
}
static void Destroy(void)
{
    CHECK(UmicomKernelVirtualAddressSpaceDestroy(&space)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK);
    CHECK(Snapshot().allocatedFrames==0U);
}
static void MapAndObserve(UmicomAddress va,UmicomAddress pa)
{
    const UmicomKernelVirtualMemoryPermissions requested=UMICOM_KERNEL_VIRTUAL_MEMORY_READ|
        UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE|UMICOM_KERNEL_VIRTUAL_MEMORY_USER;
    CHECK(UmicomKernelVirtualMemoryMapPage(&space,va,pa,requested)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    /* Leaf bytes are never touched. This test checks encoding, not backing RAM. */
    for(UmicomAddress offset=0U;offset<PAGE;offset+=4095U){
        UmicomAddress address=0U;UmicomKernelVirtualMemoryPermissions permissions=0U;
        CHECK(UmicomKernelVirtualMemoryTranslate(&space,va+offset,&address,&permissions)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
        CHECK(address==pa+offset && permissions==requested);
    }
    CHECK(UmicomKernelVirtualMemoryValidate(&space)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(UmicomKernelVirtualMemoryUnmapPage(&space,va)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(space.mappedPages==0U && space.pageTableFrames==1U);
}
int main(int argc,char **argv)
{
    CHECK(argc==2);const char *mode=argv[1];
    if(!strcmp(mode,"width-query")){
        CHECK(UmicomKernelVirtualMemoryIsPhysicalAddressRepresentable(0U)==UMICOM_TRUE);
        CHECK(UmicomKernelVirtualMemoryIsPhysicalAddressRepresentable(ENCODING_END-1U)==UMICOM_TRUE);
        CHECK(UmicomKernelVirtualMemoryIsPhysicalAddressRepresentable((UmicomAddress)~(UmicomU64)0U)==UMICOM_FALSE);
        for(UmicomU32 bit=0U;bit<64U;++bit){
            UmicomAddress value=(UmicomAddress)((UmicomU64)1U<<bit);
            CHECK(UmicomKernelVirtualMemoryIsPhysicalAddressRepresentable(value)==(bit<56U?UMICOM_TRUE:UMICOM_FALSE));
        }
        return 0;
    }
    if(!strcmp(mode,"status-values")){
        CHECK(UMICOM_KERNEL_VIRTUAL_MEMORY_PHYSICAL_MEMORY_ERROR==10);
        CHECK(UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS==11);
        CHECK(!strcmp(UmicomKernelVirtualMemoryStatusName(UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS),
            "invalid-physical-address"));
        CHECK(!strcmp(UmicomKernelVirtualMemoryStatusName(UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY),"out-of-memory"));
        return 0;
    }
    if(!strcmp(mode,"root-high-range") || !strcmp(mode,"root-high-reserved") || !strcmp(mode,"root-crossing-limit")){
        UmicomAddress base=ENCODING_END;
        UmicomSize frames=8U;
        if(!strcmp(mode,"root-crossing-limit"))base=ENCODING_END-4U*PAGE;
        CHECK(UmicomKernelPhysicalMemoryInitialize(base,frames*4096U)==UMICOM_KERNEL_MEMORY_OK);
        if(!strcmp(mode,"root-high-reserved"))
            CHECK(UmicomKernelPhysicalMemoryReserveRange(base,frames*4096U)==UMICOM_KERNEL_MEMORY_OK);
        if(!strcmp(mode,"root-crossing-limit"))
            CHECK(UmicomKernelPhysicalMemoryReserveRange(base,4U*4096U)==UMICOM_KERNEL_MEMORY_OK);
        UmicomKernelPhysicalMemorySnapshot before=Snapshot();
        UmicomKernelVirtualMemoryStatus expected=!strcmp(mode,"root-crossing-limit")
            ?UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY:UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS;
        CHECK(UmicomKernelVirtualAddressSpaceCreate(&space)==expected);
        CHECK(space.initialised==UMICOM_FALSE && space.pageTableFrames==0U && space.mappedPages==0U);
        CHECK(space.rootTablePhysicalAddress==0U);SameAccounting(&before);
        for(UmicomSize i=0U;i<frames;++i){
            UmicomKernelPhysicalFrameState state=UMICOM_PHYSICAL_FRAME_ALLOCATED;
            CHECK(UmicomKernelPhysicalMemoryFrameQuery(base+i*PAGE,&state)==UMICOM_KERNEL_MEMORY_OK);
            CHECK(state==((!strcmp(mode,"root-high-reserved")||(!strcmp(mode,"root-crossing-limit")&&i<4U))
                ?UMICOM_PHYSICAL_FRAME_RESERVED:UMICOM_PHYSICAL_FRAME_FREE));
        }
        return 0; /* Described high physical addresses were never dereferenced. */
    }
    Initialize(!strcmp(mode,"map-exhaustion-rollback")?2U:32U);
    if(!strcmp(mode,"root-exhausted")){
        CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)ram,sizeof ram)==UMICOM_KERNEL_MEMORY_OK);
        UmicomKernelPhysicalMemorySnapshot before=Snapshot();memcpy(beforeRam,ram,sizeof ram);
        CHECK(UmicomKernelVirtualAddressSpaceCreate(&space)==UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY);
        CHECK(space.initialised==UMICOM_FALSE && !memcmp(beforeRam,ram,sizeof ram));SameAccounting(&before);return 0;
    }
    if(!strcmp(mode,"table-allocation-first-fit")){
        CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)ram,4096U)==UMICOM_KERNEL_MEMORY_OK);
        Create();CHECK(space.rootTablePhysicalAddress==(UmicomAddress)ram+PAGE);
        CHECK(Snapshot().reservedFrames==1U);Destroy();return 0;
    }
    Create();
    if(!strcmp(mode,"highest-leaf"))MapAndObserve(LOW_VA,ENCODING_END-PAGE);
    else if(!strcmp(mode,"zero-leaf"))MapAndObserve(LOW_VA,0U);
    else if(!strcmp(mode,"high-virtual-leaf"))MapAndObserve((UmicomAddress)0xffffffc000000000ULL,ENCODING_END-PAGE);
    else if(!strcmp(mode,"high-bits-refused")){
        for(UmicomU32 bit=56U;bit<64U;++bit)
            Refuse(LOW_VA,(UmicomAddress)((UmicomU64)1U<<bit),UMICOM_KERNEL_VIRTUAL_MEMORY_READ,
                UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS);
        Refuse(LOW_VA,(UmicomAddress)0xfffffffffffff000ULL,UMICOM_KERNEL_VIRTUAL_MEMORY_READ,
            UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS);
    }else if(!strcmp(mode,"existing-map-preserved")){
        CHECK(UmicomKernelVirtualMemoryMapPage(&space,LOW_VA,0U,UMICOM_KERNEL_VIRTUAL_MEMORY_READ)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
        Refuse(LOW_VA+PAGE,ENCODING_END,UMICOM_KERNEL_VIRTUAL_MEMORY_READ,UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS);
        Refuse(LOW_VA,ENCODING_END,UMICOM_KERNEL_VIRTUAL_MEMORY_READ,UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PHYSICAL_ADDRESS);
        UmicomAddress translated=77U;UmicomKernelVirtualMemoryPermissions permissions=0U;
        CHECK(UmicomKernelVirtualMemoryTranslate(&space,LOW_VA+123U,&translated,&permissions)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
        CHECK(translated==123U && permissions==UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    }else if(!strcmp(mode,"invalid-input-order")){
        Refuse(LOW_VA+1U,ENCODING_END,UMICOM_KERNEL_VIRTUAL_MEMORY_READ,UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ALIGNMENT);
        Refuse(LOW_VA,ENCODING_END+1U,UMICOM_KERNEL_VIRTUAL_MEMORY_READ,UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_ALIGNMENT);
        Refuse((UmicomAddress)0x0000004000000000ULL,ENCODING_END,UMICOM_KERNEL_VIRTUAL_MEMORY_READ,
            UMICOM_KERNEL_VIRTUAL_MEMORY_NON_CANONICAL_ADDRESS);
        Refuse(LOW_VA,ENCODING_END,UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE,UMICOM_KERNEL_VIRTUAL_MEMORY_INVALID_PERMISSIONS);
    }else if(!strcmp(mode,"map-exhaustion-rollback")){
        UmicomKernelPhysicalMemorySnapshot before=Snapshot();
        CHECK(UmicomKernelVirtualMemoryMapPage(&space,LOW_VA,0U,UMICOM_KERNEL_VIRTUAL_MEMORY_READ)==UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY);
        SameAccounting(&before);
        CHECK(space.pageTableFrames==1U && space.mappedPages==0U);
        CHECK(UmicomKernelVirtualMemoryValidate(&space)==UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    }else CHECK(0);
    Destroy();return 0;
}
