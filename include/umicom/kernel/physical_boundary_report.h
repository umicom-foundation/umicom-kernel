/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/physical_boundary_report.h
 *
 * PURPOSE:
 *   Describe boundary-constrained frame observations while retaining the existing window-report ABI.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_PHYSICAL_BOUNDARY_REPORT_H
#define UMICOM_KERNEL_PHYSICAL_BOUNDARY_REPORT_H
#include "umicom/kernel/physical_window_report.h"
typedef struct UmicomKernelPhysicalBoundaryReport {
    /* Counts and freeRuns describe raw ownership within the window. Only
     * largestFirstFrame/largestRunFrames also apply the boundary constraint. */
    UmicomKernelPhysicalWindowReport window;
    UmicomSize boundaryFrames;
} UmicomKernelPhysicalBoundaryReport;
/* Inspect the exact window using absolute alignment and a zero (disabled) or
 * power-of-two boundary in frames. An eligible run lies entirely within one
 * absolute frame-number bucket. Among equally long runs choose the lowest
 * starting address; an aligned address zero remains valid with nonzero length.
 *
 * Reuse the same ownership scanner as PhysicalWindowInspect. Neither ownership
 * maps nor caller output change on refusal. No allocation, reservation, RAM
 * dereference or device action occurs. Keep the full call serialized with all
 * allocator mutations; a report is not a guarantee about a later allocation.
 * Geometry and output validity requirements match PhysicalWindowInspect.
 * A nonzero non-power-of-two boundary returns INVALID_ALIGNMENT. */
UmicomKernelMemoryStatus UmicomKernelPhysicalBoundaryInspect(
    UmicomAddress minimumAddress,UmicomAddress maximumEnd,
    UmicomSize alignmentFrames,UmicomSize boundaryFrames,
    UmicomKernelPhysicalBoundaryReport *out);
#endif
