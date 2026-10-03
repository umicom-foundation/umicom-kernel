/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/executable.c
 *
 * PURPOSE:
 *   Decode and validate the supported ELF program headers without trusting
 *   their offsets, sizes, addresses or alignment.
 *
 * EDUCATIONAL OVERVIEW:
 *   Do not cast a file buffer to a C structure. The buffer may be unaligned,
 *   host byte order may differ, and an attacker may have supplied only part of
 *   a header. Byte-wise decoding makes those assumptions explicit. Every read
 *   below follows a successful range check; addition is never used to prove
 *   that an untrusted range fits, because that addition could wrap.
 *
 *   This is an eager, static executable profile, not a dynamic linker. Refusing
 *   an interpreter, TLS template or unsupported header is safer than silently
 *   starting a program whose runtime requirements we have not implemented.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/executable.h"

/* Read little-endian integers only after the surrounding byte span was checked.
 * Explicit shifts avoid unaligned loads and compiler packing dependencies. */
static UmicomU64 UmicomExecutableRead(const UmicomU8 *bytes, UmicomSize count)
{
    UmicomU64 value = 0U;
    for (UmicomSize index = 0U; index < count; ++index) {
        value |= (UmicomU64)bytes[index] << (UmicomU32)(index * 8U);
    }
    return value;
}

/* Subtraction after offset <= total makes this immune to offset+length wrap. */
static UmicomBoolean UmicomExecutableSpan(
    UmicomSize offset, UmicomSize length, UmicomSize total)
{
    return offset <= total && length <= total - offset ? UMICOM_TRUE : UMICOM_FALSE;
}

/* Copy fields explicitly so the freestanding compiler need not emit memcpy.
 * The destination is published only once the entire input has passed review. */
static void UmicomExecutablePublish(
    UmicomKernelExecutablePlan *destination, const UmicomKernelExecutablePlan *source)
{
    destination->entry = source->entry;
    destination->segmentCount = source->segmentCount;
    destination->imagePages = source->imagePages;
    for (UmicomSize index = 0U; index < source->segmentCount; ++index) {
        destination->segments[index].fileOffset = source->segments[index].fileOffset;
        destination->segments[index].fileBytes = source->segments[index].fileBytes;
        destination->segments[index].virtualBase = source->segments[index].virtualBase;
        destination->segments[index].memoryBytes = source->segments[index].memoryBytes;
        destination->segments[index].pageBase = source->segments[index].pageBase;
        destination->segments[index].pageCount = source->segments[index].pageCount;
        destination->segments[index].flags = source->segments[index].flags;
    }
}

UmicomKernelExecutableStatus UmicomKernelExecutableInspect(
    const UmicomU8 *image, UmicomSize imageBytes, UmicomKernelExecutablePlan *outPlan)
{
    if (image == (const UmicomU8 *)0 || outPlan == (UmicomKernelExecutablePlan *)0) {
        return UMICOM_EXECUTABLE_INVALID_ARGUMENT;
    }
    if (imageBytes > UMICOM_EXECUTABLE_MAX_FILE_BYTES) {
        return UMICOM_EXECUTABLE_TOO_LARGE;
    }
    /* The fixed ELF64 header occupies 64 bytes, regardless of section count. */
    if (imageBytes < 64U) {
        return UMICOM_EXECUTABLE_TRUNCATED;
    }
    if (image[0] != 0x7fU || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
        return UMICOM_EXECUTABLE_BAD_MAGIC;
    }
    /* Require ELF64, little endian, current ELF format, System V identification,
     * and a fixed-address RV64 executable. No Linux process ABI is implied. */
    if (image[4] != 2U || image[5] != 1U || image[6] != 1U ||
        UmicomExecutableRead(image + 16U, 2U) != 2U ||
        UmicomExecutableRead(image + 18U, 2U) != 243U ||
        UmicomExecutableRead(image + 20U, 4U) != 1U ||
        UmicomExecutableRead(image + 52U, 2U) != 64U) {
        return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT;
    }
    /* Only the RVC flag is accepted. Floating-register, RVE, TSO and other ABI
     * variants are not silently accepted by our integer-only execution monitor. */
    const UmicomU64 flags = UmicomExecutableRead(image + 48U, 4U);
    if (image[7] != 0U || image[8] != 0U || (flags & ~(UmicomU64)1U) != 0U) {
        return UMICOM_EXECUTABLE_UNSUPPORTED_ABI;
    }
    const UmicomSize headerOffset = UmicomExecutableRead(image + 32U, 8U);
    const UmicomSize headerBytes = UmicomExecutableRead(image + 54U, 2U);
    const UmicomSize headerCount = UmicomExecutableRead(image + 56U, 2U);
    /* Bounding count before multiplication also bounds the loop and stack plan. */
    if (headerOffset < 64U || headerBytes != 56U || headerCount == 0U ||
        headerCount > UMICOM_EXECUTABLE_MAX_HEADERS ||
        UmicomExecutableSpan(headerOffset, headerCount * 56U, imageBytes) == UMICOM_FALSE) {
        return UMICOM_EXECUTABLE_BAD_HEADER_TABLE;
    }

    UmicomKernelExecutablePlan plan;
    plan.entry = (UmicomAddress)UmicomExecutableRead(image + 24U, 8U);
    plan.segmentCount = 0U;
    plan.imagePages = 0U;
    /* This profile uses only the lower canonical Sv39 region. Reserving the
     * high half for later Kernel designs avoids admitting ambiguous layouts. */
    const UmicomAddress userLimit = (UmicomAddress)1ULL << 38U;
    const UmicomAddress pageMask = UMICOM_EXECUTABLE_PAGE_BYTES - 1U;
    const UmicomAddress guardStart = UMICOM_EXECUTABLE_STACK_BASE - UMICOM_EXECUTABLE_PAGE_BYTES;
    const UmicomAddress guardEnd = UMICOM_EXECUTABLE_STACK_TOP + UMICOM_EXECUTABLE_PAGE_BYTES;
    UmicomBoolean entryFound = UMICOM_FALSE;

    for (UmicomSize index = 0U; index < headerCount; ++index) {
        /* The complete table was range checked above; this header is inside it. */
        const UmicomU8 *const header = image + headerOffset + index * 56U;
        const UmicomU64 kind = UmicomExecutableRead(header, 4U);
        if (kind == 0U) {
            continue; /* PT_NULL explicitly declares an unused entry. */
        }
        const UmicomU64 permissions = UmicomExecutableRead(header + 4U, 4U);
        const UmicomSize offset = UmicomExecutableRead(header + 8U, 8U);
        const UmicomAddress base = (UmicomAddress)UmicomExecutableRead(header + 16U, 8U);
        /* p_paddr is deliberately not read: a file cannot select physical RAM. */
        const UmicomSize fileBytes = UmicomExecutableRead(header + 32U, 8U);
        const UmicomSize memoryBytes = UmicomExecutableRead(header + 40U, 8U);
        const UmicomSize alignment = UmicomExecutableRead(header + 48U, 8U);

        if (kind != 1U) {
            /* Notes and RISC-V attributes remain inert metadata. Their byte
             * ranges must still fit the file. They are not ISA verification. */
            if (kind == 4U || kind == 0x70000003U) {
                if (UmicomExecutableSpan(offset, fileBytes, imageBytes) == UMICOM_FALSE) {
                    return UMICOM_EXECUTABLE_BAD_FILE_RANGE;
                }
                continue;
            }
            /* Honour the non-executable stack request only. This loader owns
             * the stack size and will not use a header to widen its rights. */
            if (kind == 0x6474e551U) {
                if ((permissions & ~(UmicomU64)6U) != 0U || fileBytes != 0U) {
                    return UMICOM_EXECUTABLE_BAD_PERMISSIONS;
                }
                continue;
            }
            return UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT;
        }
        if (plan.segmentCount == UMICOM_EXECUTABLE_MAX_SEGMENTS) {
            return UMICOM_EXECUTABLE_RESOURCE_LIMIT;
        }
        if (fileBytes > memoryBytes ||
            UmicomExecutableSpan(offset, fileBytes, imageBytes) == UMICOM_FALSE) {
            return UMICOM_EXECUTABLE_BAD_FILE_RANGE;
        }
        if (memoryBytes == 0U || base < UMICOM_EXECUTABLE_PAGE_BYTES ||
            base >= userLimit || memoryBytes > userLimit - base) {
            return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE;
        }
        /* ELF permits alignment zero/one. Larger values must be powers of two;
         * the virtual/file offsets must agree for both it and our page size. */
        if ((alignment > 1U && ((alignment & (alignment - 1U)) != 0U ||
             ((base ^ offset) & (alignment - 1U)) != 0U)) ||
            ((base ^ offset) & pageMask) != 0U) {
            return UMICOM_EXECUTABLE_BAD_ALIGNMENT;
        }
        /* Requiring READ is this profile's policy. WRITE+EXECUTE is forbidden;
         * we do not combine neighbouring segments to create a wider mapping. */
        if ((permissions & ~(UmicomU64)7U) != 0U || (permissions & 4U) == 0U ||
            (permissions & 3U) == 3U) {
            return UMICOM_EXECUTABLE_BAD_PERMISSIONS;
        }
        const UmicomAddress pageBase = base & ~pageMask;
        const UmicomAddress end = base + (UmicomAddress)memoryBytes;
        /* end <= userLimit leaves room for this rounding addition. */
        const UmicomAddress pageEnd = (end + pageMask) & ~pageMask;
        const UmicomSize pages = (pageEnd - pageBase) / UMICOM_EXECUTABLE_PAGE_BYTES;
        if (pages > UMICOM_EXECUTABLE_MAX_IMAGE_PAGES - plan.imagePages) {
            return UMICOM_EXECUTABLE_RESOURCE_LIMIT;
        }
        if (pageBase < guardEnd && pageEnd > guardStart) {
            return UMICOM_EXECUTABLE_STACK_CONFLICT;
        }
        /* PT_LOAD entries are required in increasing address order. Comparing
         * with the last accepted page range catches both disorder and overlap. */
        if (plan.segmentCount != 0U) {
            const UmicomKernelExecutableSegment *const prior =
                &plan.segments[plan.segmentCount - 1U];
            if (pageBase < prior->pageBase + prior->pageCount * UMICOM_EXECUTABLE_PAGE_BYTES) {
                return UMICOM_EXECUTABLE_SEGMENT_OVERLAP;
            }
        }
        /* Entry must name copied instructions, not zero-fill or page padding.
         * Two-byte alignment is sufficient only when RVC is advertised. */
        if ((permissions & 1U) != 0U && plan.entry >= base &&
            plan.entry - base < fileBytes &&
            (plan.entry & ((flags & 1U) != 0U ? 1U : 3U)) == 0U) {
            entryFound = UMICOM_TRUE;
        }
        UmicomKernelExecutableSegment *const segment = &plan.segments[plan.segmentCount];
        segment->fileOffset = offset;
        segment->fileBytes = fileBytes;
        segment->virtualBase = base;
        segment->memoryBytes = memoryBytes;
        segment->pageBase = pageBase;
        segment->pageCount = pages;
        segment->flags = (UmicomU32)permissions;
        ++plan.segmentCount;
        plan.imagePages += pages;
    }
    if (plan.segmentCount == 0U) {
        return UMICOM_EXECUTABLE_NO_LOAD_SEGMENTS;
    }
    if (entryFound == UMICOM_FALSE) {
        return UMICOM_EXECUTABLE_BAD_ENTRY;
    }
    UmicomExecutablePublish(outPlan, &plan);
    return UMICOM_EXECUTABLE_OK;
}

const char *UmicomKernelExecutableStatusName(UmicomKernelExecutableStatus status)
{
    /* Keep refusal reasons stable and readable in both host tests and serial output. */
    switch (status) {
        case UMICOM_EXECUTABLE_OK: return "ok";
        case UMICOM_EXECUTABLE_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_EXECUTABLE_TOO_LARGE: return "file-too-large";
        case UMICOM_EXECUTABLE_TRUNCATED: return "truncated-header";
        case UMICOM_EXECUTABLE_BAD_MAGIC: return "bad-magic";
        case UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT: return "unsupported-format";
        case UMICOM_EXECUTABLE_UNSUPPORTED_ABI: return "unsupported-abi";
        case UMICOM_EXECUTABLE_BAD_HEADER_TABLE: return "bad-header-table";
        case UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT: return "unsupported-segment";
        case UMICOM_EXECUTABLE_BAD_FILE_RANGE: return "bad-file-range";
        case UMICOM_EXECUTABLE_BAD_MEMORY_RANGE: return "bad-memory-range";
        case UMICOM_EXECUTABLE_BAD_ALIGNMENT: return "bad-alignment";
        case UMICOM_EXECUTABLE_BAD_PERMISSIONS: return "bad-permissions";
        case UMICOM_EXECUTABLE_SEGMENT_OVERLAP: return "segment-overlap";
        case UMICOM_EXECUTABLE_STACK_CONFLICT: return "stack-conflict";
        case UMICOM_EXECUTABLE_RESOURCE_LIMIT: return "resource-limit";
        case UMICOM_EXECUTABLE_BAD_ENTRY: return "bad-entry";
        case UMICOM_EXECUTABLE_NO_LOAD_SEGMENTS: return "no-load-segments";
        default: return "unknown-executable-status";
    }
}
