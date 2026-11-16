/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/physical_window_report.h
 *
 * PURPOSE:
 *   Describe current constrained frame capacity and fragmentation without acquiring ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_PHYSICAL_WINDOW_REPORT_H
#define UMICOM_KERNEL_PHYSICAL_WINDOW_REPORT_H
#include "umicom/kernel/physical_memory.h"
typedef struct UmicomKernelPhysicalWindowReport {
    UmicomAddress minimumAddress,maximumEnd;
    UmicomSize alignmentFrames,totalFrames,freeFrames,reservedFrames,allocatedFrames;
    /* Count all contiguous free regions, including those with no aligned start. */
    UmicomSize freeRuns;
    /* Longest free run beginning at an absolutely aligned frame. Equal lengths
     * choose the lowest start. Both fields are zero when no aligned run exists;
     * an address of zero is valid when largestRunFrames is nonzero. */
    UmicomAddress largestFirstFrame;
    UmicomSize largestRunFrames;
} UmicomKernelPhysicalWindowReport;
/* Inspect exactly [minimumAddress,maximumEnd) through the existing allocator's
 * public ownership queries. Require a nonempty page-aligned window wholly in
 * managed RAM and a nonzero power-of-two alignment in frames. Never clip a range
 * or extend a reported run outside the window. No physical frames are acquired
 * and no described RAM contents are read.
 *
 * Validate allocator accounting before inspection. Failure preserves the entire
 * caller report and does not alter ownership. Success with no free/aligned run
 * still returns OK with a zero run length. Alignment uses absolute frame numbers
 * and never multiplies an unchecked alignment by page size.
 *
 * Serialize the complete call with allocator mutation. This is a current
 * observation, not a reservation, transferable owner or guarantee that a later
 * allocation succeeds. The bounded scan is O(managed frames + window frames),
 * uses constant stack space, and does not access the described RAM contents. */
UmicomKernelMemoryStatus UmicomKernelPhysicalWindowInspect(
    UmicomAddress minimumAddress,UmicomAddress maximumEnd,UmicomSize alignmentFrames,
    UmicomKernelPhysicalWindowReport *out);
#endif
