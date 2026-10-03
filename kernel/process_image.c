/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/process_image.c
 *
 * PURPOSE:
 *   Materialise a checked ELF plan into separately owned user frames, and
 *   unwind that ownership safely if allocation or mapping cannot finish.
 *
 * EDUCATIONAL OVERVIEW:
 *   The order is deliberate: inspect the whole file, allocate a root, allocate
 *   and zero each backing frame, copy only its initialised bytes, then map the
 *   page with its final rights. Executable pages are never writable to U-mode.
 *   Machine mode fills physical frames before execution; the architecture run
 *   layer later synchronises the instruction stream with those new bytes.
 *
 *   A page is recorded immediately after allocation, before mapping. Otherwise
 *   a failed map would leave a frame which the rollback path did not know about.
 *   No existing allocator or mapper is replaced by this owner.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process.h"

/* Volatile byte stores keep this freestanding operation independent of libc.
 * The same helper clears newly allocated pages and scrubs them before release. */
static void UmicomProcessZero(void *storage, UmicomSize bytes)
{
    volatile UmicomU8 *const destination = (volatile UmicomU8 *)storage;
    for (UmicomSize index = 0U; index < bytes; ++index) {
        destination[index] = 0U;
    }
}

/* Append ownership before publishing the leaf. If mapping fails, Destroy can
 * still release this page even though it was never reachable by the user. */
static UmicomKernelProcessStatus UmicomProcessAddPage(
    UmicomKernelProcess *process, UmicomAddress virtualBase,
    UmicomKernelVirtualMemoryPermissions permissions,
    const UmicomU8 *source, UmicomSize sourceBytes, UmicomSize destinationOffset)
{
    if (process->pageCount >= UMICOM_USER_MEMORY_MAX_PAGES ||
        destinationOffset > UMICOM_EXECUTABLE_PAGE_BYTES ||
        sourceBytes > UMICOM_EXECUTABLE_PAGE_BYTES - destinationOffset) {
        return UMICOM_PROCESS_INVALID_ARGUMENT;
    }
    UmicomAddress frame = 0U;
    if (UmicomKernelPhysicalMemoryAllocateFrame(&frame) != UMICOM_KERNEL_MEMORY_OK) {
        return UMICOM_PROCESS_OUT_OF_MEMORY;
    }
    UmicomKernelUserPage *const page = &process->pages[process->pageCount];
    page->virtualBase = virtualBase;
    page->physicalBase = frame;
    page->permissions = permissions;
    ++process->pageCount;
    /* Clearing the whole page protects BSS, alignment gaps and trailing padding
     * from bytes left by a previous owner, not only the named ELF memory span. */
    UmicomProcessZero((void *)frame, UMICOM_EXECUTABLE_PAGE_BYTES);
    volatile UmicomU8 *const destination = (volatile UmicomU8 *)frame;
    for (UmicomSize index = 0U; index < sourceBytes; ++index) {
        destination[destinationOffset + index] = source[index];
    }
    const UmicomKernelVirtualMemoryStatus mapped = UmicomKernelVirtualMemoryMapPage(
        &process->space, virtualBase, frame, permissions);
    if (mapped == UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY) {
        return UMICOM_PROCESS_OUT_OF_MEMORY;
    }
    return mapped == UMICOM_KERNEL_VIRTUAL_MEMORY_OK
        ? UMICOM_PROCESS_OK : UMICOM_PROCESS_MAPPING_ERROR;
}

UmicomKernelProcessStatus UmicomKernelProcessDestroy(UmicomKernelProcess *process)
{
    if (process == (UmicomKernelProcess *)0) {
        return UMICOM_PROCESS_INVALID_ARGUMENT;
    }
    if (process->state == UMICOM_PROCESS_EMPTY || process->state == UMICOM_PROCESS_RUNNING ||
        process->quiesced == UMICOM_FALSE) {
        return UMICOM_PROCESS_BAD_STATE;
    }
    /* Keep ownership visible on failure. Never free data that may still be
     * reachable from a hierarchy which failed its own teardown validation. */
    process->state = UMICOM_PROCESS_CLEANUP_REQUIRED;
    if (process->space.initialised != UMICOM_FALSE &&
        UmicomKernelVirtualAddressSpaceDestroy(&process->space) != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return UMICOM_PROCESS_CLEANUP_ERROR;
    }
    /* Pop records only after a successful free. A later retry cannot double-free
     * the pages already released if an unexpected allocator error interrupted us. */
    while (process->pageCount != 0U) {
        UmicomKernelUserPage *const page = &process->pages[process->pageCount - 1U];
        UmicomProcessZero((void *)page->physicalBase, UMICOM_EXECUTABLE_PAGE_BYTES);
        if (UmicomKernelPhysicalMemoryFreeFrame(page->physicalBase) != UMICOM_KERNEL_MEMORY_OK) {
            return UMICOM_PROCESS_CLEANUP_ERROR;
        }
        --process->pageCount;
    }
    /* EMPTY is published only after both page-table and data ownership end. */
    UmicomProcessZero(process, (UmicomSize)sizeof(*process));
    return UMICOM_PROCESS_OK;
}

static UmicomKernelProcessStatus UmicomProcessRollback(
    UmicomKernelProcess *process, UmicomKernelProcessStatus reason)
{
    /* A cleanup failure is more urgent than the allocation refusal that led
     * here, because the owner must remain available for diagnosis/recovery. */
    return UmicomKernelProcessDestroy(process) == UMICOM_PROCESS_OK
        ? reason : UMICOM_PROCESS_CLEANUP_ERROR;
}

UmicomKernelProcessStatus UmicomKernelProcessCreate(
    UmicomKernelProcess *process, const UmicomU8 *image, UmicomSize imageBytes,
    UmicomU64 identity, UmicomKernelExecutableStatus *outExecutableStatus)
{
    if (process == (UmicomKernelProcess *)0 || identity == 0U) {
        return UMICOM_PROCESS_INVALID_ARGUMENT;
    }
    if (process->state != UMICOM_PROCESS_EMPTY || process->space.initialised != UMICOM_FALSE ||
        process->pageCount != 0U) {
        return UMICOM_PROCESS_BAD_STATE;
    }
    /* Parse into local storage before taking a single physical frame. */
    UmicomKernelExecutablePlan plan;
    const UmicomKernelExecutableStatus inspected =
        UmicomKernelExecutableInspect(image, imageBytes, &plan);
    if (outExecutableStatus != (UmicomKernelExecutableStatus *)0) {
        *outExecutableStatus = inspected;
    }
    if (inspected != UMICOM_EXECUTABLE_OK) {
        return UMICOM_PROCESS_EXECUTABLE_REFUSED;
    }
    process->state = UMICOM_PROCESS_LOADING;
    process->quiesced = UMICOM_TRUE; /* No hart has ever entered this new image. */
    process->identity = identity;
    process->entry = plan.entry;
    const UmicomKernelVirtualMemoryStatus created = UmicomKernelVirtualAddressSpaceCreate(&process->space);
    if (created != UMICOM_KERNEL_VIRTUAL_MEMORY_OK) {
        return UmicomProcessRollback(process, created == UMICOM_KERNEL_VIRTUAL_MEMORY_OUT_OF_MEMORY
            ? UMICOM_PROCESS_OUT_OF_MEMORY : UMICOM_PROCESS_MAPPING_ERROR);
    }
    for (UmicomSize index = 0U; index < plan.segmentCount; ++index) {
        const UmicomKernelExecutableSegment *const segment = &plan.segments[index];
        UmicomKernelVirtualMemoryPermissions rights =
            UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ;
        if ((segment->flags & UMICOM_EXECUTABLE_FLAG_WRITE) != 0U) {
            rights |= UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE;
        }
        if ((segment->flags & UMICOM_EXECUTABLE_FLAG_EXECUTE) != 0U) {
            rights |= UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE;
        }
        /* The plan proved every end lies below the lower Sv39 limit. These
         * additions are consequently bounded, unlike the original file fields. */
        const UmicomAddress fileEnd = segment->virtualBase + segment->fileBytes;
        for (UmicomSize pageIndex = 0U; pageIndex < segment->pageCount; ++pageIndex) {
            const UmicomAddress pageBase = segment->pageBase + pageIndex * UMICOM_EXECUTABLE_PAGE_BYTES;
            const UmicomAddress pageEnd = pageBase + UMICOM_EXECUTABLE_PAGE_BYTES;
            const UmicomAddress copyStart = pageBase > segment->virtualBase ? pageBase : segment->virtualBase;
            const UmicomAddress copyEnd = pageEnd < fileEnd ? pageEnd : fileEnd;
            const UmicomSize bytes = copyEnd > copyStart ? copyEnd - copyStart : 0U;
            /* Do not even form an out-of-buffer pointer for a zero-fill page. */
            const UmicomU8 *const source = bytes == 0U ? (const UmicomU8 *)0 :
                image + segment->fileOffset + (copyStart - segment->virtualBase);
            const UmicomSize offset = bytes == 0U ? 0U : copyStart - pageBase;
            const UmicomKernelProcessStatus status =
                UmicomProcessAddPage(process, pageBase, rights, source, bytes, offset);
            if (status != UMICOM_PROCESS_OK) {
                return UmicomProcessRollback(process, status);
            }
        }
    }
    for (UmicomSize index = 0U; index < UMICOM_EXECUTABLE_STACK_PAGES; ++index) {
        const UmicomKernelProcessStatus status = UmicomProcessAddPage(process,
            UMICOM_EXECUTABLE_STACK_BASE + index * UMICOM_EXECUTABLE_PAGE_BYTES,
            UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ |
                UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE,
            (const UmicomU8 *)0, 0U, 0U);
        if (status != UMICOM_PROCESS_OK) {
            return UmicomProcessRollback(process, status);
        }
    }
    /* Publish the borrowed view only after every page exists. The monitor uses
     * the same ownership checks as before; loading does not bypass its rules. */
    process->report.memory.space = &process->space;
    process->report.memory.pages = process->pages;
    process->report.memory.pageCount = process->pageCount;
    if (UmicomKernelVirtualMemoryValidate(&process->space) != UMICOM_KERNEL_VIRTUAL_MEMORY_OK ||
        UmicomKernelUserMemoryCheck(&process->report.memory, process->entry, 4U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) != UMICOM_USER_RESULT_OK) {
        return UmicomProcessRollback(process, UMICOM_PROCESS_MAPPING_ERROR);
    }
    process->state = UMICOM_PROCESS_READY;
    return UMICOM_PROCESS_OK;
}

const char *UmicomKernelProcessStateName(UmicomKernelProcessState state)
{
    switch (state) {
        case UMICOM_PROCESS_EMPTY: return "empty";
        case UMICOM_PROCESS_LOADING: return "loading";
        case UMICOM_PROCESS_READY: return "ready";
        case UMICOM_PROCESS_RUNNING: return "running";
        case UMICOM_PROCESS_EXITED: return "exited";
        case UMICOM_PROCESS_FAULTED: return "faulted";
        case UMICOM_PROCESS_TIMED_OUT: return "timed-out";
        case UMICOM_PROCESS_CALL_LIMIT: return "call-limit";
        case UMICOM_PROCESS_MONITOR_ERROR: return "monitor-error";
        case UMICOM_PROCESS_CLEANUP_REQUIRED: return "cleanup-required";
        default: return "unknown-process-state";
    }
}
const char *UmicomKernelProcessStatusName(UmicomKernelProcessStatus status)
{
    switch (status) {
        case UMICOM_PROCESS_OK: return "ok";
        case UMICOM_PROCESS_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_PROCESS_BAD_STATE: return "bad-state";
        case UMICOM_PROCESS_EXECUTABLE_REFUSED: return "executable-refused";
        case UMICOM_PROCESS_OUT_OF_MEMORY: return "out-of-memory";
        case UMICOM_PROCESS_MAPPING_ERROR: return "mapping-error";
        case UMICOM_PROCESS_CLEANUP_ERROR: return "cleanup-error";
        case UMICOM_PROCESS_ENTRY_REFUSED: return "entry-refused";
        case UMICOM_PROCESS_MACHINE_STATE_ERROR: return "machine-state-error";
        default: return "unknown-process-status";
    }
}
