/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/physical_window_report.c
 *
 * PURPOSE:
 *   Compose public frame classification and checked geometry into an atomic read-only window report.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/physical_window_report.h"
#include "umicom/kernel/physical_boundary_report.h"
#include "umicom/kernel/address.h"
#include "boot_memory.h"
/* The original scanner reports only aligned window capacity. The shared scanner below adds optional absolute boundaries while the existing public wrapper supplies zero; retain the original complete implementation and no-libc history for review. The former implementation is retained for owner review. */
#if 0
UmicomKernelMemoryStatus UmicomKernelPhysicalWindowInspect(
    UmicomAddress minimum,UmicomAddress maximum,UmicomSize alignment,
    UmicomKernelPhysicalWindowReport *out)
{
    if(out==(UmicomKernelPhysicalWindowReport *)0)return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    if(UmicomKernelIsPowerOfTwo(alignment)==UMICOM_FALSE)return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    UmicomKernelPhysicalMemorySnapshot memory;
    UmicomKernelMemoryStatus status=UmicomKernelPhysicalMemorySnapshotRead(&memory);
    if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
    if(minimum>=maximum)return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    if(minimum%UMICOM_KERNEL_PAGE_SIZE!=0U || maximum%UMICOM_KERNEL_PAGE_SIZE!=0U)
        return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    UmicomAddress ramEnd=0U;
    if(UmicomKernelAddressAddChecked(memory.ramBase,memory.ramBytes,&ramEnd)==UMICOM_FALSE)
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    if(minimum<memory.ramBase || maximum>ramEnd)return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    status=UmicomKernelPhysicalMemoryValidate();
    if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
/* Freestanding Debug lowering inserted memset for aggregate initialization. The existing reviewed boot-memory primitive clears the same record without a hosted runtime. The former implementation is retained for owner review. */
#if 0
    UmicomKernelPhysicalWindowReport candidate={0};
#endif
    UmicomKernelPhysicalWindowReport candidate;
    UmicomBootMemoryClear(&candidate,sizeof candidate);
    candidate.minimumAddress=minimum;candidate.maximumEnd=maximum;candidate.alignmentFrames=alignment;
    candidate.totalFrames=(UmicomSize)((maximum-minimum)/UMICOM_KERNEL_PAGE_SIZE);
    UmicomSize alignedLength=0U;UmicomAddress alignedStart=0U;UmicomBoolean previousFree=UMICOM_FALSE;
    for(UmicomAddress address=minimum;address<maximum;address+=UMICOM_KERNEL_PAGE_SIZE){
        UmicomKernelPhysicalFrameState state;
        status=UmicomKernelPhysicalMemoryFrameQuery(address,&state);
        if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
        if(state==UMICOM_PHYSICAL_FRAME_FREE){
            ++candidate.freeFrames;
            if(previousFree==UMICOM_FALSE)++candidate.freeRuns;
            previousFree=UMICOM_TRUE;
            /* The earliest eligible frame gives the longest aligned suffix of
             * this free region. Keep the raw region count separate from it. */
            if(alignedLength==0U){
                const UmicomU64 number=(UmicomU64)address/(UmicomU64)UMICOM_KERNEL_PAGE_SIZE;
                if((number&(alignment-1U))!=0U)continue;
                alignedStart=address;
            }
            ++alignedLength;
            if(alignedLength>candidate.largestRunFrames){
                candidate.largestRunFrames=alignedLength;candidate.largestFirstFrame=alignedStart;
            }
        }else{
            alignedLength=0U;previousFree=UMICOM_FALSE;
            if(state==UMICOM_PHYSICAL_FRAME_RESERVED)++candidate.reservedFrames;
            else if(state==UMICOM_PHYSICAL_FRAME_ALLOCATED)++candidate.allocatedFrames;
            else return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
        }
    }
/* Aggregate assignment inserted memcpy in the no-libc image. Publish the complete candidate with the shared nonoverlapping boot-memory copy after every failure point. The former implementation is retained for owner review. */
#if 0
    *out=candidate;return UMICOM_KERNEL_MEMORY_OK;
#endif
    UmicomBootMemoryCopy(out,&candidate,sizeof candidate);
    return UMICOM_KERNEL_MEMORY_OK;
}

#endif
static UmicomKernelMemoryStatus PhysicalWindowInspect(
    UmicomAddress minimum,UmicomAddress maximum,UmicomSize alignment,UmicomSize boundary,
    UmicomKernelPhysicalWindowReport *out)
{
    if(out==(UmicomKernelPhysicalWindowReport *)0)return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    if(UmicomKernelIsPowerOfTwo(alignment)==UMICOM_FALSE ||
        (boundary!=0U && UmicomKernelIsPowerOfTwo(boundary)==UMICOM_FALSE))return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    UmicomKernelPhysicalMemorySnapshot memory;
    UmicomKernelMemoryStatus status=UmicomKernelPhysicalMemorySnapshotRead(&memory);
    if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
    if(minimum>=maximum)return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    if(minimum%UMICOM_KERNEL_PAGE_SIZE!=0U || maximum%UMICOM_KERNEL_PAGE_SIZE!=0U)
        return UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT;
    UmicomAddress ramEnd=0U;
    if(UmicomKernelAddressAddChecked(memory.ramBase,memory.ramBytes,&ramEnd)==UMICOM_FALSE)
        return UMICOM_KERNEL_MEMORY_RANGE_OVERFLOW;
    if(minimum<memory.ramBase || maximum>ramEnd)return UMICOM_KERNEL_MEMORY_OUTSIDE_RAM;
    status=UmicomKernelPhysicalMemoryValidate();
    if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
    UmicomKernelPhysicalWindowReport candidate;
    UmicomBootMemoryClear(&candidate,sizeof candidate);
    candidate.minimumAddress=minimum;candidate.maximumEnd=maximum;candidate.alignmentFrames=alignment;
    candidate.totalFrames=(UmicomSize)((maximum-minimum)/UMICOM_KERNEL_PAGE_SIZE);
    UmicomSize alignedLength=0U;UmicomAddress alignedStart=0U;UmicomBoolean previousFree=UMICOM_FALSE;
    for(UmicomAddress address=minimum;address<maximum;address+=UMICOM_KERNEL_PAGE_SIZE){
        UmicomKernelPhysicalFrameState state;
        status=UmicomKernelPhysicalMemoryFrameQuery(address,&state);
        if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
        if(state==UMICOM_PHYSICAL_FRAME_FREE){
            ++candidate.freeFrames;
            if(previousFree==UMICOM_FALSE)++candidate.freeRuns;
            previousFree=UMICOM_TRUE;
            const UmicomU64 number=(UmicomU64)address/(UmicomU64)UMICOM_KERNEL_PAGE_SIZE;
            /* Crossing an absolute bucket ends the eligible suffix without
             * ending a raw free region. Frame units avoid boundary*page overflow. */
            if(boundary!=0U && (number&(boundary-1U))==0U)alignedLength=0U;
            if(alignedLength==0U){
                if((number&(alignment-1U))!=0U)continue;
                alignedStart=address;
            }
            ++alignedLength;
            if(alignedLength>candidate.largestRunFrames){
                candidate.largestRunFrames=alignedLength;candidate.largestFirstFrame=alignedStart;
            }
        }else{
            alignedLength=0U;previousFree=UMICOM_FALSE;
            if(state==UMICOM_PHYSICAL_FRAME_RESERVED)++candidate.reservedFrames;
            else if(state==UMICOM_PHYSICAL_FRAME_ALLOCATED)++candidate.allocatedFrames;
            else return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
        }
    }
    UmicomBootMemoryCopy(out,&candidate,sizeof candidate);
    return UMICOM_KERNEL_MEMORY_OK;
}

/* Zero boundary retains the original window semantics and ABI. Both callers
 * use one classification scan, so ownership accounting cannot diverge. */
UmicomKernelMemoryStatus UmicomKernelPhysicalWindowInspect(
    UmicomAddress minimum,UmicomAddress maximum,UmicomSize alignment,
    UmicomKernelPhysicalWindowReport *out)
{
    return PhysicalWindowInspect(minimum,maximum,alignment,0U,out);
}
UmicomKernelMemoryStatus UmicomKernelPhysicalBoundaryInspect(
    UmicomAddress minimum,UmicomAddress maximum,UmicomSize alignment,UmicomSize boundary,
    UmicomKernelPhysicalBoundaryReport *out)
{
    if(out==(UmicomKernelPhysicalBoundaryReport *)0)return UMICOM_KERNEL_MEMORY_INVALID_ARGUMENT;
    UmicomKernelPhysicalBoundaryReport candidate;
    UmicomBootMemoryClear(&candidate,sizeof candidate);
    const UmicomKernelMemoryStatus status=PhysicalWindowInspect(
        minimum,maximum,alignment,boundary,&candidate.window);
    if(status!=UMICOM_KERNEL_MEMORY_OK)return status;
    candidate.boundaryFrames=boundary;
    /* One final publication preserves the caller's complete report on refusal
     * and avoids compiler-emitted hosted memory functions in the Kernel. */
    UmicomBootMemoryCopy(out,&candidate,sizeof candidate);
    return UMICOM_KERNEL_MEMORY_OK;
}
