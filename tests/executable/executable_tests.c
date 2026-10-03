/*-----------------------------------------------------------------------------
 * Umicom Kernel executable-loading host tests
 * File: tests/executable/executable_tests.c
 *
 * PURPOSE:
 *   Test malformed ELF data, eager loading and allocation rollback using the
 *   actual Kernel parser, allocator, mapper and process-image ownership code.
 *
 * EDUCATIONAL NOTE:
 *   Aligned host storage stands in for physical RAM. No test here claims to
 *   execute a RISC-V instruction. Privilege transitions, timer recovery and
 *   the separately linked program's execution still require the QEMU tests.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/process.h"

alignas(4096) static UmicomU8 umicomTestRam[4096U * 64U];
static UmicomU8 umicomTestFile[16384U];
static UmicomKernelProcess umicomTestProcess;
static UmicomKernelProcess umicomTestOther;
static unsigned umicomTestChecks;

static void UmicomTestRequire(int condition, const char *description)
{
    ++umicomTestChecks;
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", description);
        exit(EXIT_FAILURE);
    }
}
/* Fixture encoding uses bytes just like a real disk file. It does not share
 * parser decoding helpers, so a common structure packing mistake cannot hide. */
static void UmicomTestPut(UmicomSize offset, UmicomU64 value, UmicomSize bytes)
{
    for (UmicomSize index = 0U; index < bytes; ++index) {
        umicomTestFile[offset + index] = (UmicomU8)(value >> (UmicomU32)(index * 8U));
    }
}
static UmicomSize UmicomTestHeader(UmicomSize index) { return 64U + index * 56U; }
static void UmicomTestSegment(UmicomSize index, UmicomU32 flags, UmicomSize file,
    UmicomAddress address, UmicomSize fileBytes, UmicomSize memoryBytes)
{
    const UmicomSize at = UmicomTestHeader(index);
    UmicomTestPut(at, 1U, 4U);
    UmicomTestPut(at + 4U, flags, 4U);
    UmicomTestPut(at + 8U, file, 8U);
    UmicomTestPut(at + 16U, address, 8U);
    UmicomTestPut(at + 24U, 0xdead0000U, 8U); /* Must never become a physical mapping. */
    UmicomTestPut(at + 32U, fileBytes, 8U);
    UmicomTestPut(at + 40U, memoryBytes, 8U);
    UmicomTestPut(at + 48U, 4096U, 8U);
}
static void UmicomTestFixture(void)
{
    memset(umicomTestFile, 0, sizeof(umicomTestFile));
    umicomTestFile[0] = 0x7fU;
    umicomTestFile[1] = 'E'; umicomTestFile[2] = 'L'; umicomTestFile[3] = 'F';
    umicomTestFile[4] = 2U; umicomTestFile[5] = 1U; umicomTestFile[6] = 1U;
    UmicomTestPut(16U, 2U, 2U); /* ET_EXEC. */
    UmicomTestPut(18U, 243U, 2U); /* EM_RISCV. */
    UmicomTestPut(20U, 1U, 4U);
    UmicomTestPut(24U, 0x400000U, 8U);
    UmicomTestPut(32U, 64U, 8U);
    UmicomTestPut(48U, 1U, 4U); /* RVC, integer ABI. */
    UmicomTestPut(52U, 64U, 2U);
    UmicomTestPut(54U, 56U, 2U);
    UmicomTestPut(56U, 3U, 2U);
    UmicomTestSegment(0U, 5U, 0x1000U, 0x400000U, 16U, 16U);
    UmicomTestSegment(1U, 4U, 0x2000U, 0x401000U, 8U, 8U);
    /* The third segment begins partway through a page. Prefix, BSS and tail
     * bytes must all be zero, not data left by the previous frame owner. */
    UmicomTestSegment(2U, 6U, 0x3003U, 0x402003U, 10U, 5000U);
    for (UmicomSize index = 0U; index < 16U; ++index) umicomTestFile[0x1000U + index] = (UmicomU8)(0x10U + index);
    for (UmicomSize index = 0U; index < 8U; ++index) umicomTestFile[0x2000U + index] = (UmicomU8)(0x30U + index);
    for (UmicomSize index = 0U; index < 10U; ++index) umicomTestFile[0x3003U + index] = (UmicomU8)(0x50U + index);
}
static void UmicomTestRam(void)
{
    memset(umicomTestRam, 0xa5, sizeof(umicomTestRam));
    memset(&umicomTestProcess, 0, sizeof(umicomTestProcess));
    memset(&umicomTestOther, 0, sizeof(umicomTestOther));
    UmicomTestRequire(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomTestRam,
        sizeof(umicomTestRam)) == UMICOM_KERNEL_MEMORY_OK, "initialise simulated RAM");
}
static UmicomKernelPhysicalMemorySnapshot UmicomTestSnapshot(void)
{
    UmicomKernelPhysicalMemorySnapshot snapshot;
    UmicomTestRequire(UmicomKernelPhysicalMemorySnapshotRead(&snapshot) == UMICOM_KERNEL_MEMORY_OK,
        "read physical accounting");
    return snapshot;
}
static void UmicomTestAccounting(const UmicomKernelPhysicalMemorySnapshot *before)
{
    const UmicomKernelPhysicalMemorySnapshot after = UmicomTestSnapshot();
    UmicomTestRequire(before->allocatedFrames == after.allocatedFrames &&
        before->reservedFrames == after.reservedFrames && before->freeFrames == after.freeFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "allocator accounting unchanged");
}
static void UmicomTestLoad(UmicomKernelProcess *process, const UmicomU8 *bytes, UmicomSize count)
{
    UmicomKernelExecutableStatus elf = UMICOM_EXECUTABLE_BAD_MAGIC;
    const UmicomKernelProcessStatus result = UmicomKernelProcessCreate(process, bytes, count, 601U, &elf);
    if (result != UMICOM_PROCESS_OK) {
        fprintf(stderr, "load status: %s / %s\n", UmicomKernelProcessStatusName(result),
            UmicomKernelExecutableStatusName(elf));
    }
    UmicomTestRequire(result == UMICOM_PROCESS_OK && process->state == UMICOM_PROCESS_READY &&
        process->quiesced != UMICOM_FALSE, "load a ready, inactive process image");
}
static UmicomAddress UmicomTestTranslate(const UmicomKernelProcess *process, UmicomAddress virtualAddress)
{
    UmicomAddress physical = 0U;
    UmicomTestRequire(UmicomKernelVirtualMemoryTranslate(&process->space, virtualAddress,
        &physical, (UmicomKernelVirtualMemoryPermissions *)0) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK,
        "translate loaded address");
    return physical;
}

/* Each malformed case starts from a valid file and changes one relevant field.
 * This keeps the refusal reason meaningful rather than failing at the magic
 * check before reaching the guard under test. */
static UmicomKernelExecutableStatus UmicomTestMutate(const char *name, UmicomSize *count)
{
    const UmicomSize first = UmicomTestHeader(0U);
    const UmicomSize data = UmicomTestHeader(2U);
    if (strcmp(name, "truncated-header") == 0) { *count = 63U; return UMICOM_EXECUTABLE_TRUNCATED; }
    if (strcmp(name, "too-large") == 0) { *count = UMICOM_EXECUTABLE_MAX_FILE_BYTES + 1U; return UMICOM_EXECUTABLE_TOO_LARGE; }
    if (strcmp(name, "bad-magic") == 0) { umicomTestFile[1] = 'X'; return UMICOM_EXECUTABLE_BAD_MAGIC; }
    if (strcmp(name, "wrong-class") == 0) { umicomTestFile[4] = 1U; return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "wrong-endian") == 0) { umicomTestFile[5] = 2U; return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "ident-format") == 0) { umicomTestFile[6] = 0U; return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "wrong-machine") == 0) { UmicomTestPut(18U, 62U, 2U); return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "shared-object") == 0) { UmicomTestPut(16U, 3U, 2U); return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "header-format") == 0) { UmicomTestPut(20U, 0U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "header-size") == 0) { UmicomTestPut(52U, 63U, 2U); return UMICOM_EXECUTABLE_UNSUPPORTED_FORMAT; }
    if (strcmp(name, "foreign-os-abi") == 0) { umicomTestFile[7] = 3U; return UMICOM_EXECUTABLE_UNSUPPORTED_ABI; }
    if (strcmp(name, "foreign-abi-tag") == 0) { umicomTestFile[8] = 1U; return UMICOM_EXECUTABLE_UNSUPPORTED_ABI; }
    if (strcmp(name, "float-abi") == 0) { UmicomTestPut(48U, 5U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_ABI; }
    if (strcmp(name, "unknown-abi-flags") == 0) { UmicomTestPut(48U, 0x80000001U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_ABI; }
    if (strcmp(name, "empty-header-table") == 0) { UmicomTestPut(56U, 0U, 2U); return UMICOM_EXECUTABLE_BAD_HEADER_TABLE; }
    if (strcmp(name, "oversized-header-table") == 0) { UmicomTestPut(56U, 17U, 2U); return UMICOM_EXECUTABLE_BAD_HEADER_TABLE; }
    if (strcmp(name, "header-entry-size") == 0) { UmicomTestPut(54U, 55U, 2U); return UMICOM_EXECUTABLE_BAD_HEADER_TABLE; }
    if (strcmp(name, "header-overlap") == 0) { UmicomTestPut(32U, 63U, 8U); return UMICOM_EXECUTABLE_BAD_HEADER_TABLE; }
    if (strcmp(name, "header-offset-wrap") == 0) { UmicomTestPut(32U, ~(UmicomU64)0U - 8U, 8U); return UMICOM_EXECUTABLE_BAD_HEADER_TABLE; }
    if (strcmp(name, "truncated-table") == 0) { *count = 64U + 3U * 56U - 1U; return UMICOM_EXECUTABLE_BAD_HEADER_TABLE; }
    if (strcmp(name, "dynamic-segment") == 0) { UmicomTestPut(first, 2U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT; }
    if (strcmp(name, "interpreter-segment") == 0) { UmicomTestPut(first, 3U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT; }
    if (strcmp(name, "tls-segment") == 0) { UmicomTestPut(first, 7U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT; }
    if (strcmp(name, "unknown-segment") == 0) { UmicomTestPut(first, 0x6ffffff0U, 4U); return UMICOM_EXECUTABLE_UNSUPPORTED_SEGMENT; }
    if (strcmp(name, "executable-stack") == 0) { UmicomTestPut(first, 0x6474e551U, 4U); return UMICOM_EXECUTABLE_BAD_PERMISSIONS; }
    if (strcmp(name, "metadata-outside-file") == 0) { UmicomTestPut(first, 4U, 4U); UmicomTestPut(first + 8U, *count, 8U); return UMICOM_EXECUTABLE_BAD_FILE_RANGE; }
    if (strcmp(name, "file-larger-than-memory") == 0) { UmicomTestPut(first + 40U, 8U, 8U); return UMICOM_EXECUTABLE_BAD_FILE_RANGE; }
    if (strcmp(name, "segment-offset-wrap") == 0) { UmicomTestPut(first + 8U, ~(UmicomU64)0U - 8U, 8U); return UMICOM_EXECUTABLE_BAD_FILE_RANGE; }
    if (strcmp(name, "segment-truncated") == 0) { *count = 0x3008U; return UMICOM_EXECUTABLE_BAD_FILE_RANGE; }
    if (strcmp(name, "empty-load") == 0) { UmicomTestPut(first + 32U, 0U, 8U); UmicomTestPut(first + 40U, 0U, 8U); return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE; }
    if (strcmp(name, "null-page") == 0) { UmicomTestPut(first + 16U, 0U, 8U); return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE; }
    if (strcmp(name, "upper-canonical") == 0) { UmicomTestPut(first + 16U, 0xffffffc000001000ULL, 8U); return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE; }
    if (strcmp(name, "virtual-range-wrap") == 0) { UmicomTestPut(first + 40U, ~(UmicomU64)0U, 8U); return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE; }
    if (strcmp(name, "canonical-hole") == 0) { UmicomTestPut(first + 16U, 0x4000000000ULL, 8U); return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE; }
    if (strcmp(name, "cross-canonical-limit") == 0) { UmicomTestPut(first + 16U, 0x3ffffff000ULL, 8U); UmicomTestPut(first + 40U, 4097U, 8U); return UMICOM_EXECUTABLE_BAD_MEMORY_RANGE; }
    if (strcmp(name, "alignment-not-power-two") == 0) { UmicomTestPut(first + 48U, 3U, 8U); return UMICOM_EXECUTABLE_BAD_ALIGNMENT; }
    if (strcmp(name, "file-virtual-misaligned") == 0) { UmicomTestPut(first + 8U, 0x1001U, 8U); return UMICOM_EXECUTABLE_BAD_ALIGNMENT; }
    if (strcmp(name, "writable-code") == 0) { UmicomTestPut(first + 4U, 7U, 4U); return UMICOM_EXECUTABLE_BAD_PERMISSIONS; }
    if (strcmp(name, "write-only") == 0) { UmicomTestPut(first + 4U, 2U, 4U); return UMICOM_EXECUTABLE_BAD_PERMISSIONS; }
    if (strcmp(name, "execute-only-profile") == 0) { UmicomTestPut(first + 4U, 1U, 4U); return UMICOM_EXECUTABLE_BAD_PERMISSIONS; }
    if (strcmp(name, "unknown-permissions") == 0) { UmicomTestPut(first + 4U, 0x80000005U, 4U); return UMICOM_EXECUTABLE_BAD_PERMISSIONS; }
    if (strcmp(name, "overlap-pages") == 0) { UmicomTestPut(UmicomTestHeader(1U) + 16U, 0x400000U, 8U); return UMICOM_EXECUTABLE_SEGMENT_OVERLAP; }
    if (strcmp(name, "disjoint-bytes-shared-page") == 0) { UmicomTestPut(UmicomTestHeader(1U) + 16U, 0x400020U, 8U); UmicomTestPut(UmicomTestHeader(1U) + 8U, 0x2020U, 8U); return UMICOM_EXECUTABLE_SEGMENT_OVERLAP; }
    if (strcmp(name, "unsorted-loads") == 0) { UmicomTestPut(data + 16U, 0x300003U, 8U); return UMICOM_EXECUTABLE_SEGMENT_OVERLAP; }
    if (strcmp(name, "stack-collision") == 0) { UmicomTestPut(data + 16U, UMICOM_EXECUTABLE_STACK_BASE + 3U, 8U); return UMICOM_EXECUTABLE_STACK_CONFLICT; }
    if (strcmp(name, "stack-guard-collision") == 0) { UmicomTestPut(data + 16U, UMICOM_EXECUTABLE_STACK_BASE - 4096U + 3U, 8U); return UMICOM_EXECUTABLE_STACK_CONFLICT; }
    if (strcmp(name, "page-budget") == 0) { UmicomTestPut(data + 40U, 4096U * 14U, 8U); return UMICOM_EXECUTABLE_RESOURCE_LIMIT; }
    if (strcmp(name, "entry-in-bss") == 0) { UmicomTestPut(first + 40U, 32U, 8U); UmicomTestPut(24U, 0x400010U, 8U); return UMICOM_EXECUTABLE_BAD_ENTRY; }
    if (strcmp(name, "entry-read-only") == 0) { UmicomTestPut(24U, 0x401000U, 8U); return UMICOM_EXECUTABLE_BAD_ENTRY; }
    if (strcmp(name, "entry-odd") == 0) { UmicomTestPut(24U, 0x400001U, 8U); return UMICOM_EXECUTABLE_BAD_ENTRY; }
    if (strcmp(name, "entry-without-rvc") == 0) { UmicomTestPut(48U, 0U, 4U); UmicomTestPut(24U, 0x400002U, 8U); return UMICOM_EXECUTABLE_BAD_ENTRY; }
    if (strcmp(name, "no-loads") == 0) { for (UmicomSize i = 0U; i < 3U; ++i) UmicomTestPut(UmicomTestHeader(i), 0U, 4U); return UMICOM_EXECUTABLE_NO_LOAD_SEGMENTS; }
    if (strcmp(name, "too-many-loads") == 0) {
        UmicomTestPut(56U, 9U, 2U);
        for (UmicomSize i = 0U; i < 9U; ++i) UmicomTestSegment(i, 5U, 0x1000U, 0x400000U + i * 4096U, 16U, 16U);
        return UMICOM_EXECUTABLE_RESOURCE_LIMIT;
    }
    return UMICOM_EXECUTABLE_OK; /* The caller treats an unknown test name as failure. */
}

static void UmicomTestLoadedBytes(const UmicomU8 *image, UmicomSize count)
{
    UmicomKernelExecutablePlan plan;
    UmicomTestRequire(UmicomKernelExecutableInspect(image, count, &plan) == UMICOM_EXECUTABLE_OK,
        "inspect before independent byte comparison");
    UmicomTestLoad(&umicomTestProcess, image, count);
    UmicomTestRequire(umicomTestProcess.pageCount == plan.imagePages + UMICOM_EXECUTABLE_STACK_PAGES,
        "backing catalogue includes stack pages");
    for (UmicomSize i = 0U; i < plan.segmentCount; ++i) {
        const UmicomKernelExecutableSegment *const segment = &plan.segments[i];
        for (UmicomSize j = 0U; j < segment->pageCount * 4096U; ++j) {
            const UmicomAddress address = segment->pageBase + j;
            const UmicomAddress physical = UmicomTestTranslate(&umicomTestProcess, address);
            UmicomU8 expected = 0U;
            if (address >= segment->virtualBase && address - segment->virtualBase < segment->fileBytes) {
                expected = image[segment->fileOffset + address - segment->virtualBase];
            }
            UmicomTestRequire(*(const UmicomU8 *)physical == expected, "initialisers, BSS and page padding");
            UmicomTestRequire(physical != address && physical != 0xdead0000U, "file cannot choose physical backing");
        }
    }
    UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "destroy loaded sample");
}

int main(int argc, char **argv)
{
    UmicomTestRequire(argc >= 2, "one named case required");
    const char *const name = argv[1];
    UmicomTestFixture();
    UmicomTestRam();
    const UmicomKernelPhysicalMemorySnapshot baseline = UmicomTestSnapshot();
    UmicomKernelExecutablePlan plan;
    memset(&plan, 0xa5, sizeof(plan));
    if (strcmp(name, "valid-inspect") == 0) {
        UmicomTestRequire(UmicomKernelExecutableInspect(umicomTestFile, sizeof(umicomTestFile), &plan) == UMICOM_EXECUTABLE_OK &&
            plan.entry == 0x400000U && plan.segmentCount == 3U && plan.imagePages == 4U, "valid segmented plan");
    } else if (strcmp(name, "null-input") == 0) {
        UmicomTestRequire(UmicomKernelExecutableInspect(NULL, 64U, &plan) == UMICOM_EXECUTABLE_INVALID_ARGUMENT, "null file");
    } else if (strcmp(name, "null-output") == 0) {
        UmicomTestRequire(UmicomKernelExecutableInspect(umicomTestFile, sizeof(umicomTestFile), NULL) == UMICOM_EXECUTABLE_INVALID_ARGUMENT, "null plan");
    } else if (strcmp(name, "unaligned-input-buffer") == 0) {
        UmicomU8 *const shifted = malloc(sizeof(umicomTestFile) + 1U);
        UmicomTestRequire(shifted != NULL, "allocate unaligned fixture");
        memcpy(shifted + 1U, umicomTestFile, sizeof(umicomTestFile));
        UmicomTestRequire(UmicomKernelExecutableInspect(shifted + 1U, sizeof(umicomTestFile), &plan) == UMICOM_EXECUTABLE_OK, "bytewise unaligned input");
        free(shifted);
    } else if (strcmp(name, "ignored-section-table") == 0) {
        UmicomTestPut(40U, ~(UmicomU64)0U, 8U); UmicomTestPut(60U, 0xffffU, 2U);
        UmicomTestRequire(UmicomKernelExecutableInspect(umicomTestFile, sizeof(umicomTestFile), &plan) == UMICOM_EXECUTABLE_OK,
            "section metadata is not required to load PT_LOAD");
    } else if (strcmp(name, "readable-metadata") == 0) {
        UmicomTestPut(56U, 4U, 2U);
        UmicomTestPut(UmicomTestHeader(3U), 0x70000003U, 4U);
        UmicomTestPut(UmicomTestHeader(3U) + 8U, 0x100U, 8U);
        UmicomTestPut(UmicomTestHeader(3U) + 32U, 8U, 8U);
        UmicomTestRequire(UmicomKernelExecutableInspect(umicomTestFile, sizeof(umicomTestFile), &plan) == UMICOM_EXECUTABLE_OK,
            "RISC-V attributes remain bounded inert metadata");
    } else if (strcmp(name, "copy-and-zero-fill") == 0) {
        UmicomTestLoadedBytes(umicomTestFile, sizeof(umicomTestFile));
    } else if (strcmp(name, "real-linked-elf") == 0) {
        UmicomTestRequire(argc == 3, "real ELF path required");
        FILE *const file = fopen(argv[2], "rb");
        UmicomTestRequire(file != NULL, "open separately linked executable");
        UmicomTestRequire(fseek(file, 0, SEEK_END) == 0, "measure executable");
        const long size = ftell(file);
        UmicomTestRequire(size > 0 && size <= (long)UMICOM_EXECUTABLE_MAX_FILE_BYTES, "bounded real ELF");
        rewind(file);
        UmicomU8 *const image = malloc((size_t)size);
        UmicomTestRequire(image != NULL && fread(image, 1U, (size_t)size, file) == (size_t)size, "read real ELF");
        fclose(file);
        UmicomTestLoadedBytes(image, (UmicomSize)size);
        free(image);
    } else if (strcmp(name, "source-independence") == 0) {
        UmicomTestLoad(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile));
        memset(umicomTestFile, 0xcc, sizeof(umicomTestFile));
        UmicomTestRequire(*(UmicomU8 *)UmicomTestTranslate(&umicomTestProcess, 0x402003U) == 0x50U,
            "loaded bytes do not borrow input storage");
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "destroy independent image");
    } else if (strcmp(name, "two-independent-images") == 0) {
        UmicomTestLoad(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile));
        UmicomTestLoad(&umicomTestOther, umicomTestFile, sizeof(umicomTestFile));
        UmicomTestRequire(umicomTestProcess.space.rootTablePhysicalAddress != umicomTestOther.space.rootTablePhysicalAddress, "distinct roots");
        for (UmicomSize i = 0U; i < umicomTestProcess.pageCount; ++i)
            for (UmicomSize j = 0U; j < umicomTestOther.pageCount; ++j)
                UmicomTestRequire(umicomTestProcess.pages[i].physicalBase != umicomTestOther.pages[j].physicalBase, "no shared backing");
        *(UmicomU8 *)UmicomTestTranslate(&umicomTestProcess, 0x402003U) = 0xffU;
        UmicomTestRequire(*(UmicomU8 *)UmicomTestTranslate(&umicomTestOther, 0x402003U) == 0x50U, "independent writable data");
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK &&
            UmicomKernelProcessDestroy(&umicomTestOther) == UMICOM_PROCESS_OK, "destroy both owners");
    } else if (strcmp(name, "page-permissions-and-guards") == 0) {
        UmicomTestLoad(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile));
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, 0x400000U, 4U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) == UMICOM_USER_RESULT_OK, "code executes");
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, 0x400000U, 1U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) == UMICOM_USER_RESULT_DENIED, "code is not writable");
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, 0x402003U, 1U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE) == UMICOM_USER_RESULT_DENIED, "data is not executable");
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, 0x401000U, 1U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) == UMICOM_USER_RESULT_DENIED, "read-only data is protected");
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, UMICOM_EXECUTABLE_STACK_BASE, 8192U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) == UMICOM_USER_RESULT_OK, "whole stack writable");
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, UMICOM_EXECUTABLE_STACK_BASE - 1U, 1U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == UMICOM_USER_RESULT_BAD_ADDRESS, "lower guard absent");
        UmicomTestRequire(UmicomKernelUserMemoryCheck(&umicomTestProcess.report.memory, UMICOM_EXECUTABLE_STACK_TOP, 1U,
            UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == UMICOM_USER_RESULT_BAD_ADDRESS, "upper guard absent");
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "destroy permissions fixture");
    } else if (strcmp(name, "owner-lifecycle") == 0) {
        UmicomTestRequire(UmicomKernelProcessCreate(NULL, umicomTestFile, sizeof(umicomTestFile), 1U, NULL) == UMICOM_PROCESS_INVALID_ARGUMENT, "null owner");
        UmicomTestRequire(UmicomKernelProcessCreate(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile), 0U, NULL) == UMICOM_PROCESS_INVALID_ARGUMENT, "zero identity");
        UmicomTestLoad(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile));
        const UmicomAddress root = umicomTestProcess.space.rootTablePhysicalAddress;
        UmicomTestRequire(UmicomKernelProcessCreate(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile), 7U, NULL) == UMICOM_PROCESS_BAD_STATE &&
            umicomTestProcess.space.rootTablePhysicalAddress == root, "do not overwrite live image");
        umicomTestProcess.state = UMICOM_PROCESS_RUNNING; /* Model the reclamation guard only. */
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_BAD_STATE, "running image is not reclaimable");
        umicomTestProcess.state = UMICOM_PROCESS_MONITOR_ERROR;
        umicomTestProcess.quiesced = UMICOM_FALSE;
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_BAD_STATE, "unproved hardware return is not reclaimable");
        umicomTestProcess.quiesced = UMICOM_TRUE;
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK &&
            UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_BAD_STATE, "destroy exactly once");
        UmicomTestLoad(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile));
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "reuse empty owner after reload");
    } else if (strcmp(name, "scrub-released-pages") == 0) {
        UmicomTestLoad(&umicomTestProcess, umicomTestFile, sizeof(umicomTestFile));
        UmicomAddress frames[UMICOM_USER_MEMORY_MAX_PAGES];
        const UmicomSize count = umicomTestProcess.pageCount;
        for (UmicomSize i = 0U; i < count; ++i) frames[i] = umicomTestProcess.pages[i].physicalBase;
        UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "destroy before observing scrub");
        for (UmicomSize i = 0U; i < count; ++i)
            for (UmicomSize j = 0U; j < 4096U; ++j)
                UmicomTestRequire(((UmicomU8 *)frames[i])[j] == 0U, "released backing frame is scrubbed");
    } else if (strcmp(name, "allocation-failure-sweep") == 0) {
        /* Let each allocation point fail in turn by varying genuinely available
         * RAM. No mock allocator claims success or guesses about page tables. */
        unsigned refused = 0U, loaded = 0U;
        for (UmicomSize available = 0U; available <= 20U; ++available) {
            UmicomTestRam();
            UmicomTestRequire(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)umicomTestRam + available * 4096U,
                sizeof(umicomTestRam) - available * 4096U) == UMICOM_KERNEL_MEMORY_OK, "reserve unavailable frames");
            const UmicomKernelPhysicalMemorySnapshot before = UmicomTestSnapshot();
            const UmicomKernelProcessStatus status = UmicomKernelProcessCreate(&umicomTestProcess,
                umicomTestFile, sizeof(umicomTestFile), 601U, NULL);
            if (status == UMICOM_PROCESS_OK) {
                ++loaded;
                UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "release successful bounded image");
            } else {
                ++refused;
                UmicomTestRequire(status == UMICOM_PROCESS_OUT_OF_MEMORY && umicomTestProcess.state == UMICOM_PROCESS_EMPTY,
                    "failed load rolls back to empty");
            }
            UmicomTestAccounting(&before);
        }
        UmicomTestRequire(refused != 0U && loaded != 0U, "sweep exercised failure and success");
        UmicomTestRam(); /* Restore the original full-RAM baseline. */
    } else if (strcmp(name, "mutated-input-sweep") == 0) {
        UmicomU32 seed = 0x137a931U;
        for (unsigned trial = 0U; trial < 4000U; ++trial) {
            UmicomTestFixture();
            for (unsigned edit = 0U; edit < 8U; ++edit) {
                seed = seed * 1664525U + 1013904223U;
                const UmicomSize offset = seed % 256U;
                seed = seed * 1664525U + 1013904223U;
                umicomTestFile[offset] ^= (UmicomU8)(seed >> 24U);
            }
            const UmicomSize count = (trial % 4U == 0U) ? (UmicomSize)(seed % sizeof(umicomTestFile)) : sizeof(umicomTestFile);
            const UmicomKernelProcessStatus status = UmicomKernelProcessCreate(&umicomTestProcess,
                umicomTestFile, count, 601U, NULL);
            if (status == UMICOM_PROCESS_OK) {
                UmicomTestRequire(UmicomKernelProcessDestroy(&umicomTestProcess) == UMICOM_PROCESS_OK, "release accepted mutation without execution");
            } else {
                UmicomTestRequire(status == UMICOM_PROCESS_EXECUTABLE_REFUSED && umicomTestProcess.state == UMICOM_PROCESS_EMPTY,
                    "mutated headers fail closed");
            }
            UmicomTestAccounting(&baseline);
        }
    } else {
        UmicomSize count = sizeof(umicomTestFile);
        const UmicomKernelExecutableStatus expected = UmicomTestMutate(name, &count);
        UmicomTestRequire(expected != UMICOM_EXECUTABLE_OK, "unknown test case");
        UmicomKernelExecutablePlan unchanged;
        memcpy(&unchanged, &plan, sizeof(plan));
        const UmicomKernelExecutableStatus actual = UmicomKernelExecutableInspect(umicomTestFile, count, &plan);
        if (actual != expected) fprintf(stderr, "expected %s; got %s\n", UmicomKernelExecutableStatusName(expected), UmicomKernelExecutableStatusName(actual));
        UmicomTestRequire(actual == expected && memcmp(&plan, &unchanged, sizeof(plan)) == 0, "precise refusal preserves output");
        UmicomTestRequire(UmicomKernelProcessCreate(&umicomTestProcess, umicomTestFile, count, 601U, NULL) == UMICOM_PROCESS_EXECUTABLE_REFUSED &&
            umicomTestProcess.state == UMICOM_PROCESS_EMPTY, "invalid executable takes no ownership");
    }
    UmicomTestAccounting(&baseline);
    printf("PASS %s (%u checks)\n", name, umicomTestChecks);
    return EXIT_SUCCESS;
}
