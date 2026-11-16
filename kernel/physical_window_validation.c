/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/physical_window_validation.c
 *
 * PURPOSE:
 *   Qualify constrained physical-frame ownership in the real diagnostic RISC-V image without touching selected RAM.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/physical_window_report.h"
#include "umicom/kernel/physical_boundary_report.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#define PAGE UMICOM_KERNEL_PAGE_SIZE
#define OK UMICOM_KERNEL_MEMORY_OK
static void WindowRequire(int condition,const char *reason)
{
    if(condition)return;
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelConsoleWrite("physical-window.reason=");UmicomKernelConsoleWriteLine(reason);
    UmicomPlatformFinishFailure(0xd5U);UmicomPlatformHalt();
}
/* The normal boot path never invokes this acceptance helper. It borrows a
 * known free run through the real allocator, returns it, then qualifies window
 * selection while preserving all permanent Kernel and firmware reservations. */
void UmicomKernelPhysicalWindowsValidateExecution(void)
{
    UmicomKernelPhysicalMemorySnapshot before,after;
    WindowRequire(UmicomKernelPhysicalMemorySnapshotRead(&before)==OK,"baseline");
    UmicomAddress first=0U,chosen=0U;
    WindowRequire(UmicomKernelPhysicalMemoryAllocateFrames(4U,4U,&first)==OK,"borrow-four");
    WindowRequire(UmicomKernelPhysicalMemoryFreeFrames(first,4U)==OK,"return-four");
    WindowRequire(UmicomKernelPhysicalMemoryAllocateFramesInWindow(2U,2U,first+PAGE,first+4U*PAGE,&chosen)==OK,"aligned-window");
    WindowRequire(chosen==first+2U*PAGE,"absolute-alignment");
    UmicomKernelConsoleWriteLine("physical-window.alignment=pass");
    /* Query the same acquired run without changing the ownership fixture. */
    UmicomKernelPhysicalWindowReport report;
    WindowRequire(UmicomKernelPhysicalWindowInspect(first,first+4U*PAGE,1U,&report)==OK,"window-report");
    WindowRequire(report.totalFrames==4U && report.freeFrames==2U && report.allocatedFrames==2U &&
        report.reservedFrames==0U && report.freeRuns==1U && report.largestFirstFrame==first &&
        report.largestRunFrames==2U,"report-geometry");
    UmicomKernelConsoleWriteLine("physical-window.report=pass");

    UmicomAddress refused=(UmicomAddress)0x1234U;
    WindowRequire(UmicomKernelPhysicalMemoryAllocateFramesInWindow(2U,1U,first+PAGE,first+3U*PAGE,&refused)==UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,"no-fallback");
    WindowRequire(refused==(UmicomAddress)0x1234U,"output-preserved");
    WindowRequire(UmicomKernelPhysicalMemoryAllocateFramesInWindow(1U,1U,first+1U,first+PAGE,&refused)==UMICOM_KERNEL_MEMORY_INVALID_ALIGNMENT,"unaligned-window");
    WindowRequire(refused==(UmicomAddress)0x1234U,"unaligned-output");
    UmicomKernelPhysicalFrameState state;
    WindowRequire(UmicomKernelPhysicalMemoryFrameQuery(first+PAGE,&state)==OK && state==UMICOM_PHYSICAL_FRAME_FREE,"no-partial-allocation");
    UmicomKernelConsoleWriteLine("physical-window.refusal=pass");
    WindowRequire(UmicomKernelPhysicalMemoryFreeFrames(chosen,2U)==OK,"release-window");

    /* Alignment alone cannot prevent crossing a smaller physical boundary.
     * Reuse the returned four-page fixture and leave all boot reservations
     * untouched. No selected address is dereferenced by this diagnostic. */
    WindowRequire(UmicomKernelPhysicalMemoryAllocateFramesInWindowWithBoundary(
        2U,1U,2U,first+PAGE,first+4U*PAGE,&chosen)==OK,"boundary-window");
    WindowRequire(chosen==first+2U*PAGE,"boundary-skips-crossing-prefix");
    WindowRequire(UmicomKernelPhysicalMemoryFreeFrames(chosen,2U)==OK,"release-boundary");
    refused=(UmicomAddress)0x1234U;
    WindowRequire(UmicomKernelPhysicalMemoryAllocateFramesInWindowWithBoundary(
        2U,1U,2U,first+PAGE,first+3U*PAGE,&refused)==UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY,"boundary-no-fit");
    WindowRequire(refused==(UmicomAddress)0x1234U,"boundary-output-preserved");
    for(UmicomSize offset=0U;offset<4U;++offset)
        WindowRequire(UmicomKernelPhysicalMemoryFrameQuery(first+offset*PAGE,&state)==OK &&
            state==UMICOM_PHYSICAL_FRAME_FREE,"boundary-no-partial-allocation");
    UmicomKernelConsoleWriteLine("physical-window.boundary=pass");

    /* The observer uses the same absolute boundary geometry but acquires no
     * frames. Free-region count remains raw even when a boundary divides it. */
    UmicomKernelPhysicalBoundaryReport constrained;
    WindowRequire(UmicomKernelPhysicalBoundaryInspect(first+PAGE,first+4U*PAGE,1U,2U,&constrained)==OK,"boundary-report");
    WindowRequire(constrained.boundaryFrames==2U && constrained.window.totalFrames==3U &&
        constrained.window.freeFrames==3U && constrained.window.freeRuns==1U &&
        constrained.window.largestFirstFrame==first+2U*PAGE &&
        constrained.window.largestRunFrames==2U,"boundary-report-geometry");
    WindowRequire(UmicomKernelPhysicalBoundaryInspect(first+PAGE,first+3U*PAGE,1U,2U,&constrained)==OK,"boundary-report-partial");
    WindowRequire(constrained.window.freeFrames==2U && constrained.window.freeRuns==1U &&
        constrained.window.largestFirstFrame==first+PAGE &&
        constrained.window.largestRunFrames==1U,"boundary-report-split");
    UmicomKernelConsoleWriteLine("physical-window.boundary-report=pass");

    WindowRequire(UmicomKernelPhysicalMemoryValidate()==OK,"invariants");
    WindowRequire(UmicomKernelPhysicalMemorySnapshotRead(&after)==OK,"final-snapshot");
    WindowRequire(before.ramBase==after.ramBase && before.ramBytes==after.ramBytes &&
        before.allocatedFrames==after.allocatedFrames && before.reservedFrames==after.reservedFrames &&
        before.freeFrames==after.freeFrames,"ownership-restored");
    UmicomKernelConsoleWriteLine("physical-window.ownership=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PHYSICAL_WINDOWS_READY");
}
