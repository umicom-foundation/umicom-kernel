/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_inspection_validation.c
 *
 * Exercise actual disk-derived names and file bytes through the existing block
 * transport. This runs only in the separate partition-fixture image; the older
 * raw-sector fixture and its exact acceptance criteria remain untouched.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_console.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/disk_inspection/fixture_layout.h"

static UmicomKernelFat16 umicomFixtureVolume;
static UmicomKernelFat16Directory umicomFixtureDirectory;
static UmicomU8 umicomFixtureOutput[UMICOM_FAT16_READ_BYTES];
typedef struct UmicomDiskValidationReader {
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
} UmicomDiskValidationReader;
static void UmicomDiskValidationClear(void *p, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)p;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static void UmicomDiskRequire(UmicomBoolean test, const char *why)
{
    if (test) return;
    UmicomKernelConsoleWrite("disk-inspection.failure="); UmicomKernelConsoleWriteLine(why);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x86U); UmicomPlatformHalt();
}
static UmicomBoolean UmicomDiskValidationRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomDiskValidationReader *reader = (UmicomDiskValidationReader *)context;
    return UmicomKernelBlockRead(reader->domain, reader->handle, sector, 1U,
        output, UMICOM_DISK_SECTOR_BYTES) == UMICOM_BLOCK_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomDiskBytesEqual(const void *a, const void *b, UmicomSize bytes)
{
    const UmicomU8 *left = (const UmicomU8 *)a, *right = (const UmicomU8 *)b;
    for (UmicomSize i = 0U; i < bytes; ++i) if (left[i] != right[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
void UmicomKernelDiskInspectionValidate(void)
{
    UmicomKernelConsoleWriteLine("disk-inspection-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomDiskValidationClear(&before, sizeof(before)); UmicomDiskValidationClear(&after, sizeof(after));
    UmicomDiskValidationClear(&machineBefore, sizeof(machineBefore)); UmicomDiskValidationClear(&machineAfter, sizeof(machineAfter));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomDiskRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "allocator available");
    UmicomKernelBlockDomain *domain = 0;
    UmicomDiskRequire(UmicomPlatformBlockDomainGet(&domain) == UMICOM_BLOCK_OK, "qualified MMIO domain");
    UmicomSize found = 0U, slot = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info; UmicomDiskValidationClear(&info, sizeof(info));
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { ++found; slot = i; }
        else UmicomDiskRequire(status == UMICOM_BLOCK_NO_DEVICE || status == UMICOM_BLOCK_NOT_BLOCK ||
            status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT, "safe transport identity");
    }
    UmicomDiskRequire(found == 1U, "exactly one synthetic partitioned device required");
    UmicomDiskValidationReader source = {domain, 0U};
    UmicomDiskRequire(UmicomKernelBlockOpen(domain, slot, 10000000U, &source.handle) == UMICOM_BLOCK_OK,
        "modern read-only block open");
    UmicomKernelBlockInfo info; UmicomDiskValidationClear(&info, sizeof(info));
    UmicomDiskRequire(UmicomKernelBlockProbe(domain, slot, &info) == UMICOM_BLOCK_OK &&
        info.sectors == UMICOM_DISK_FIXTURE_SECTORS, "fixture capacity");
    const UmicomKernelDiskReader reader = {info.sectors, UmicomDiskValidationRead, &source};
    UmicomKernelPartitionTable table; UmicomDiskValidationClear(&table, sizeof(table));
    UmicomDiskRequire(UmicomKernelDiskPartitionsInspect(&reader, &table) == UMICOM_DISK_OK && table.count == 1U &&
        table.entries[0].firstSector == UMICOM_DISK_FIXTURE_FIRST &&
        table.entries[0].sectors == UMICOM_DISK_FIXTURE_LENGTH, "MBR extent decoded from disk");
    UmicomKernelConsoleWriteLine("disk-inspection.partition-bounds=verified");
    UmicomDiskRequire(UmicomKernelFat16Open(&umicomFixtureVolume, &reader, 0U) == UMICOM_DISK_OK &&
        umicomFixtureVolume.info.clusters == 12159U, "FAT16 geometry derived from cluster count");
    UmicomDiskRequire(UmicomKernelFat16List(&umicomFixtureVolume, "/", &umicomFixtureDirectory) == UMICOM_DISK_OK &&
        umicomFixtureDirectory.count == 4U, "root names copied");
    UmicomDiskRequire(UmicomKernelFat16List(&umicomFixtureVolume, "/DOCS", &umicomFixtureDirectory) == UMICOM_DISK_OK &&
        umicomFixtureDirectory.count == 1U &&
        UmicomDiskBytesEqual(umicomFixtureDirectory.entries[0].name, "GUIDE.TXT", 10U), "subdirectory short name");
    UmicomSize bytes = 0U;
    UmicomDiskRequire(UmicomKernelFat16Read(&umicomFixtureVolume, "/readme.txt", 0U, umicomFixtureOutput,
        sizeof(umicomFixtureOutput), &bytes) == UMICOM_DISK_OK && bytes == sizeof(UMICOM_DISK_FIXTURE_README) - 1U &&
        UmicomDiskBytesEqual(umicomFixtureOutput, UMICOM_DISK_FIXTURE_README, bytes), "disk README contents");
    UmicomDiskRequire(UmicomKernelFat16Read(&umicomFixtureVolume, "/docs/guide.txt", 0U, umicomFixtureOutput,
        sizeof(umicomFixtureOutput), &bytes) == UMICOM_DISK_OK && bytes == sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U &&
        UmicomDiskBytesEqual(umicomFixtureOutput, UMICOM_DISK_FIXTURE_GUIDE, bytes), "nested file contents");
    UmicomKernelConsoleWriteLine("disk-inspection.directory-and-file-bytes=verified");
    UmicomDiskRequire(UmicomKernelFat16Read(&umicomFixtureVolume, "/FRAG.BIN", 0U, umicomFixtureOutput,
        sizeof(umicomFixtureOutput), &bytes) == UMICOM_DISK_OK && bytes == UMICOM_DISK_FIXTURE_FRAGMENT_BYTES,
        "fragmented file length");
    for (UmicomSize i = 0U; i < bytes; ++i)
        UmicomDiskRequire(umicomFixtureOutput[i] == UmicomDiskFixturePattern(i), "fragmented cluster contents");
    UmicomDiskRequire(UmicomKernelFat16Read(&umicomFixtureVolume, "/FRAG.BIN", 511U, umicomFixtureOutput,
        700U, &bytes) == UMICOM_DISK_OK && bytes == 700U, "unaligned range crosses discontiguous clusters");
    for (UmicomSize i = 0U; i < bytes; ++i)
        UmicomDiskRequire(umicomFixtureOutput[i] == UmicomDiskFixturePattern(i + 511U), "range contents");
    UmicomKernelConsoleWriteLine("disk-inspection.fragmented-read=verified");
    bytes = 99U; umicomFixtureOutput[0] = 0xa5U;
    UmicomDiskRequire(UmicomKernelFat16Read(&umicomFixtureVolume, "/FRAG.BIN", UMICOM_DISK_FIXTURE_FRAGMENT_BYTES,
        umicomFixtureOutput, 1U, &bytes) == UMICOM_DISK_OK && bytes == 0U && umicomFixtureOutput[0] == 0xa5U,
        "EOF never exposes cluster slack");
    bytes = 99U;
    UmicomDiskRequire(UmicomKernelFat16Read(&umicomFixtureVolume, "/../README.TXT", 0U,
        umicomFixtureOutput, 1U, &bytes) == UMICOM_DISK_INVALID_ARGUMENT && bytes == 99U &&
        umicomFixtureOutput[0] == 0xa5U, "unsupported path leaves output unchanged");
    UmicomDiskRequire(UmicomKernelFat16Close(&umicomFixtureVolume) == UMICOM_DISK_OK, "interpretation retired");
    UmicomDiskRequire(UmicomKernelBlockClose(domain, source.handle) == UMICOM_BLOCK_OK, "device reset before DMA release");
    UmicomDiskRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "physical accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomDiskRequire(UmicomDiskBytesEqual(&machineBefore, &machineAfter, sizeof(machineBefore)), "machine controls unchanged");
    UmicomKernelConsoleWriteLine("disk-inspection.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("disk-inspection.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("disk-inspection.disk-writes=none");
    UmicomKernelConsoleWriteLine("disk-inspection-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_DISK_INSPECTION_READY");
}
