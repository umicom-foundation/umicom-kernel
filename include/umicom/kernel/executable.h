/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/executable.h
 *
 * PURPOSE:
 *   Inspect an ELF executable as bounded data before allocating or mapping any
 *   of its contents. A valid file is not permission to choose physical memory.
 *
 * EDUCATIONAL OVERVIEW:
 *   An ELF file has two useful views. Sections help the linker and debugger;
 *   program headers tell a loader which bytes belong in a running process.
 *   This interface uses program headers only. It accepts a deliberately small
 *   native profile: little-endian RV64, integer ABI, fixed-address executable,
 *   and readable, non-overlapping load segments with no writable code pages.
 *
 *   The result describes intentions, not ownership. The process-image loader
 *   subsequently allocates its own frames and copies into them. Neither the
 *   file's physical-address fields nor its section table grant any authority.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_EXECUTABLE_H
#define UMICOM_KERNEL_EXECUTABLE_H
#include "umicom/kernel/types.h"

/* Limits bound parser work and the small user monitor's ownership catalogue.
 * They are admission policy, not claims about what the ELF format can express. */
#define UMICOM_EXECUTABLE_MAX_FILE_BYTES ((UmicomSize)1048576U)
#define UMICOM_EXECUTABLE_MAX_HEADERS 16U
#define UMICOM_EXECUTABLE_MAX_SEGMENTS 8U
#define UMICOM_EXECUTABLE_MAX_IMAGE_PAGES 14U
#define UMICOM_EXECUTABLE_PAGE_BYTES ((UmicomSize)4096U)

/* Reserve two stack pages and a guard on each side, in the lower Sv39 half.
 * Load segments may not occupy even the guards: they must stay unmapped. */
#define UMICOM_EXECUTABLE_STACK_PAGES 2U
#define UMICOM_EXECUTABLE_STACK_TOP ((UmicomAddress)0x0000003000010000ULL)
#define UMICOM_EXECUTABLE_STACK_BASE \
    (UMICOM_EXECUTABLE_STACK_TOP - UMICOM_EXECUTABLE_STACK_PAGES * UMICOM_EXECUTABLE_PAGE_BYTES)

/* ELF's permission encoding is distinct from our page-table permission API. */
#define UMICOM_EXECUTABLE_FLAG_EXECUTE 1U
#define UMICOM_EXECUTABLE_FLAG_WRITE 2U
#define UMICOM_EXECUTABLE_FLAG_READ 4U

typedef enum UmicomKernelExecutableStatus {
    UMICOM_EXECUTABLE_OK,
    UMICOM_EXECUTABLE_INVALID_ARGUMENT,
    UMICOM_EXECUTABLE_TOO_LARGE,
    UMICOM_EXECUTABLE_TRUNCATED,
    UMICOM_EXECUTABLE_BAD_MAGIC,
    UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT,
    UMICOM_EXECUTABLE_UNSUPPORTED_ABI,
    UMICOM_EXECUTABLE_BAD_HEADER_TABLE,
    UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT,
    UMICOM_EXECUTABLE_BAD_FILE_RANGE,
    UMICOM_EXECUTABLE_BAD_MEMORY_RANGE,
    UMICOM_EXECUTABLE_BAD_ALIGNMENT,
    UMICOM_EXECUTABLE_BAD_PERMISSIONS,
    UMICOM_EXECUTABLE_SEGMENT_OVERLAP,
    UMICOM_EXECUTABLE_STACK_CONFLICT,
    UMICOM_EXECUTABLE_RESOURCE_LIMIT,
    UMICOM_EXECUTABLE_BAD_ENTRY,
    UMICOM_EXECUTABLE_NO_LOAD_SEGMENTS
} UmicomKernelExecutableStatus;

typedef struct UmicomKernelExecutableSegment {
    UmicomSize fileOffset;      /* First initialised byte in the immutable input. */
    UmicomSize fileBytes;       /* Bytes to copy; the remaining memory is zeroed. */
    UmicomAddress virtualBase;  /* Requested user address, never a physical pointer. */
    UmicomSize memoryBytes;     /* Initialised bytes plus zero-filled storage. */
    UmicomAddress pageBase;     /* Rounded down to our supported 4 KiB page size. */
    UmicomSize pageCount;       /* Includes a partially occupied first/last page. */
    UmicomU32 flags;            /* Checked ELF R/W/X bits, not PTE bits. */
} UmicomKernelExecutableSegment;

typedef struct UmicomKernelExecutablePlan {
    UmicomAddress entry;       /* Must name file-backed executable instructions. */
    UmicomSize segmentCount;
    UmicomSize imagePages;     /* Stack and page-table frames are not counted here. */
    UmicomKernelExecutableSegment segments[UMICOM_EXECUTABLE_MAX_SEGMENTS];
} UmicomKernelExecutablePlan;

/* The input is a Kernel-owned, readable byte span which must remain immutable
 * throughout inspection and loading. It is not an unchecked user pointer.
 * No allocation occurs here. On refusal, the caller's output is untouched. */
UmicomKernelExecutableStatus UmicomKernelExecutableInspect(
    const UmicomU8 *image,
    UmicomSize imageBytes,
    UmicomKernelExecutablePlan *outPlan
);

const char *UmicomKernelExecutableStatusName(UmicomKernelExecutableStatus status);
#endif /* UMICOM_KERNEL_EXECUTABLE_H */
