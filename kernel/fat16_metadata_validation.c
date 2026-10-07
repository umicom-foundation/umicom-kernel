/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_metadata_validation.c
 *
 * Read persisted metadata in three independent read-only guest lifetimes.
 * The initial fixture has no recorded write calendar; the completed writer's
 * image carries its exact saved ARCHIVE bit and even-second timestamp. Dirty
 * admission is refused before any metadata is published. Entire disk images,
 * output guards, physical accounting and machine controls are also checked.
 * No writer owner or retained writer result exists in these guests.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_metadata.h"
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/fat16_file_commit/guest_fixture.h"

#if (defined(UMICOM_KERNEL_FAT16_METADATA_TEST) + \
     defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_METADATA_DIRTY_TEST)) != 1
#error "Select exactly one dedicated FAT16 metadata qualification image"
#endif

typedef struct UmicomFatMetadataReader {
    UmicomKernelBlockDomain *domain;
    UmicomKernelBlockHandle handle;
} UmicomFatMetadataReader;
typedef struct UmicomFatMetadataOutput {
    UmicomU8 before[16];
    UmicomKernelFat16Metadata value;
    UmicomU8 after[16];
} UmicomFatMetadataOutput;
static UmicomKernelFat16 umicomMetadataVolume;
static UmicomFatMetadataOutput umicomMetadataOutput;
static UmicomU8 umicomMetadataDisk[UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES];
static UmicomU8 umicomMetadataExpected[UMICOM_BLOCK_SECTOR_BYTES];

static void UmicomMetadataFill(void *target, UmicomSize bytes, UmicomU8 value)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = value;
}
static UmicomBoolean UmicomMetadataEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomMetadataZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *const input = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (input[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static void UmicomMetadataRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("fat16-metadata.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x8bU); UmicomPlatformHalt();
    for (;;) {}
}
static UmicomBoolean UmicomMetadataRead(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomFatMetadataReader *const source = (UmicomFatMetadataReader *)context;
    return UmicomKernelBlockRead(source->domain, source->handle, sector, 1U,
        output, UMICOM_DISK_SECTOR_BYTES) == UMICOM_BLOCK_OK ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomFat16FileCommitFixtureState UmicomMetadataFixtureState(void)
{
#if defined(UMICOM_KERNEL_FAT16_METADATA_TEST)
    return UMICOM_FAT16_FILE_FIXTURE_ORIGINAL;
#elif defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST)
    return UMICOM_FAT16_FILE_FIXTURE_COMMITTED;
#else
    return UMICOM_FAT16_FILE_FIXTURE_STAGED;
#endif
}
static void UmicomMetadataWholeDisk(const UmicomFatMetadataReader *source)
{
    for (UmicomU64 first = 0U; first < UMICOM_DISK_FIXTURE_SECTORS; first += UMICOM_BLOCK_MAX_SECTORS) {
        UmicomMetadataRequire(UmicomKernelBlockRead(source->domain, source->handle, first,
            UMICOM_BLOCK_MAX_SECTORS, umicomMetadataDisk, sizeof(umicomMetadataDisk)) == UMICOM_BLOCK_OK,
            "complete read-only backing image remains readable");
        for (UmicomSize sector = 0U; sector < UMICOM_BLOCK_MAX_SECTORS; ++sector) {
            UmicomFat16FileCommitFixtureSector(first + sector, umicomMetadataExpected, UmicomMetadataFixtureState());
            UmicomMetadataRequire(UmicomMetadataEqual(umicomMetadataExpected,
                umicomMetadataDisk + sector * UMICOM_BLOCK_SECTOR_BYTES, UMICOM_BLOCK_SECTOR_BYTES),
                "every disk byte agrees with the exact independent fixture expectation");
        }
    }
}
static void UmicomMetadataGuards(void)
{
    for (UmicomSize i = 0U; i < sizeof(umicomMetadataOutput.before); ++i)
        UmicomMetadataRequire(umicomMetadataOutput.before[i] == 0xa5U &&
            umicomMetadataOutput.after[i] == 0xa5U, "metadata result stays within both caller guards");
}
static void UmicomMetadataRefused(const char *path, UmicomKernelDiskStatus expected)
{
    UmicomMetadataFill(&umicomMetadataOutput, sizeof(umicomMetadataOutput), 0xa5U);
    UmicomMetadataRequire(UmicomKernelFat16MetadataRead(&umicomMetadataVolume, path,
        &umicomMetadataOutput.value) == expected, "refused metadata query returns its exact status");
    UmicomMetadataGuards();
    const UmicomU8 *const bytes = (const UmicomU8 *)&umicomMetadataOutput;
    for (UmicomSize i = 0U; i < sizeof(umicomMetadataOutput); ++i)
        UmicomMetadataRequire(bytes[i] == 0xa5U, "refusal preserves every caller output and padding byte");
}

#if !defined(UMICOM_KERNEL_FAT16_METADATA_DIRTY_TEST)
static void UmicomMetadataQuery(const char *path, const char *name, UmicomU32 bytes,
    UmicomU16 cluster, UmicomU8 attributes, UmicomBoolean directory,
    UmicomBoolean entryPresent, UmicomBoolean timestampPresent)
{
    UmicomMetadataFill(&umicomMetadataOutput, sizeof(umicomMetadataOutput), 0xa5U);
    UmicomMetadataRequire(UmicomKernelFat16MetadataRead(&umicomMetadataVolume, path,
        &umicomMetadataOutput.value) == UMICOM_DISK_OK, "persisted metadata query succeeds");
    UmicomMetadataGuards();
    const UmicomKernelFat16Metadata *const result = &umicomMetadataOutput.value;
    UmicomSize nameBytes = 0U;
    while (name[nameBytes]) ++nameBytes;
    UmicomMetadataRequire(UmicomMetadataEqual(result->entry.name, name, nameBytes + 1U) &&
        result->entry.bytes == bytes && result->entry.firstCluster == cluster &&
        result->entry.attributes == attributes && result->entry.directory == directory &&
        result->directoryEntryPresent == entryPresent, "independent entry fields and physical-entry distinction");
    const UmicomKernelFat16Timestamp *const stamp = &result->writeTimestamp;
    if (timestampPresent) {
        UmicomMetadataRequire(stamp->state == UMICOM_FAT16_TIMESTAMP_VALID &&
            stamp->rawTime == 0x747dU && stamp->rawDate == 0x7377U &&
            stamp->value.year == 2037U && stamp->value.month == 11U && stamp->value.day == 23U &&
            stamp->value.hour == 14U && stamp->value.minute == 35U && stamp->value.second == 58U,
            "saved calendar is decoded from media after the writer process has exited");
    } else {
        UmicomMetadataRequire(stamp->state == UMICOM_FAT16_TIMESTAMP_ABSENT &&
            !stamp->rawTime && !stamp->rawDate && UmicomMetadataZero(&stamp->value, sizeof(stamp->value)),
            "zero timestamp words are absent, without an invented epoch");
    }
}
static void UmicomMetadataAccepted(void)
{
    UmicomMetadataQuery("/", "/", 0U, 0U, 0U, UMICOM_TRUE, UMICOM_FALSE, UMICOM_FALSE);
    UmicomMetadataQuery("/README.TXT", "README.TXT", sizeof(UMICOM_DISK_FIXTURE_README) - 1U,
        2U, 0x21U, UMICOM_FALSE, UMICOM_TRUE, UMICOM_FALSE);
    UmicomMetadataQuery("/docs", "DOCS", 0U, 3U, 0x10U, UMICOM_TRUE, UMICOM_TRUE, UMICOM_FALSE);
    UmicomMetadataQuery("/docs/guide.txt", "GUIDE.TXT", sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U,
        7U, 0x21U, UMICOM_FALSE, UMICOM_TRUE, UMICOM_FALSE);
    UmicomMetadataQuery("/EMPTY.TXT", "EMPTY.TXT", 0U, 0U, 0x20U, UMICOM_FALSE, UMICOM_TRUE, UMICOM_FALSE);
#if defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST)
    UmicomMetadataQuery("/frag.bin", "FRAG.BIN", 1300U, 4U, 0x20U, UMICOM_FALSE, UMICOM_TRUE, UMICOM_TRUE);
    UmicomKernelConsoleWriteLine("fat16-metadata.persisted-write-time=2037-11-23T14:35:58 archive=1");
#else
    UmicomMetadataQuery("/frag.bin", "FRAG.BIN", 1300U, 4U, 0U, UMICOM_FALSE, UMICOM_TRUE, UMICOM_FALSE);
    UmicomKernelConsoleWriteLine("fat16-metadata.persisted-write-time=absent archive=0");
#endif
    const UmicomKernelFat16Metadata retained = umicomMetadataOutput.value;
    UmicomMetadataRefused("/MISSING.TXT", UMICOM_DISK_NOT_FOUND);
    UmicomMetadataRefused("/FRAG.BIN/CHILD.TXT", UMICOM_DISK_NOT_DIRECTORY);
    UmicomMetadataRefused("/../README.TXT", UMICOM_DISK_INVALID_ARGUMENT);
    UmicomMetadataRequire(UmicomKernelFat16Close(&umicomMetadataVolume) == UMICOM_DISK_OK,
        "close retires the inspector without altering the copied metadata snapshot");
    UmicomMetadataRequire(retained.entry.bytes == 1300U && retained.entry.firstCluster == 4U &&
        UmicomMetadataEqual(retained.entry.name, "FRAG.BIN", 9U), "metadata remains a stable value after Close");
    UmicomMetadataRefused("/FRAG.BIN", UMICOM_DISK_BAD_STATE);
    UmicomKernelConsoleWriteLine("fat16-metadata.root-file-directory-and-refusal-outputs=verified");
}
#endif

static void UmicomMetadataValidate(void)
{
    UmicomKernelConsoleWriteLine("fat16-metadata-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomMetadataFill(&before, sizeof(before), 0U); UmicomMetadataFill(&after, sizeof(after), 0U);
    UmicomMetadataFill(&machineBefore, sizeof(machineBefore), 0U);
    UmicomMetadataFill(&machineAfter, sizeof(machineAfter), 0U);
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomMetadataRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "physical allocator is available");
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomMetadataRequire(UmicomPlatformBlockDomainGet(&domain) == UMICOM_BLOCK_OK,
        "qualified platform block catalogue");
    UmicomSize found = 0U, slot = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo probe;
        UmicomMetadataFill(&probe, sizeof(probe), 0U);
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &probe);
        if (status == UMICOM_BLOCK_OK) { ++found; slot = i; }
        else UmicomMetadataRequire(status == UMICOM_BLOCK_NO_DEVICE || status == UMICOM_BLOCK_NOT_BLOCK ||
            status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT, "safe transport identity probing");
    }
    UmicomMetadataRequire(found == 1U, "exactly one read-only metadata fixture device");
    UmicomFatMetadataReader source = {domain, 0U};
    UmicomMetadataRequire(UmicomKernelBlockOpen(domain, slot, 10000000U, &source.handle) == UMICOM_BLOCK_OK,
        "read-only block lease admitted");
    UmicomKernelBlockInfo info;
    UmicomMetadataFill(&info, sizeof(info), 0U);
    UmicomMetadataRequire(UmicomKernelBlockProbe(domain, slot, &info) == UMICOM_BLOCK_OK &&
        !info.writable && info.heldFrames == 2U && info.sectors == UMICOM_DISK_FIXTURE_SECTORS,
        "exact fixture capacity and read-only transport ownership");
    UmicomMetadataWholeDisk(&source);
    const UmicomKernelDiskReader reader = {info.sectors, UmicomMetadataRead, &source};
#if defined(UMICOM_KERNEL_FAT16_METADATA_DIRTY_TEST)
    UmicomMetadataRequire(UmicomKernelFat16Open(&umicomMetadataVolume, &reader, 0U) == UMICOM_DISK_DIRTY &&
        UmicomMetadataZero(&umicomMetadataVolume, sizeof(umicomMetadataVolume)),
        "dirty admission preserves the unopened inspector and publishes no metadata");
    UmicomMetadataRefused("/FRAG.BIN", UMICOM_DISK_BAD_STATE);
    UmicomMetadataRequire(UmicomKernelFat16Close(&umicomMetadataVolume) == UMICOM_DISK_BAD_STATE &&
        UmicomMetadataZero(&umicomMetadataVolume, sizeof(umicomMetadataVolume)),
        "unopened inspector Close is refused without acquiring or repairing anything");
    UmicomKernelConsoleWriteLine("fat16-metadata.filesystem=dirty-refused output=unchanged repair=none");
#else
    UmicomMetadataRequire(UmicomKernelFat16Open(&umicomMetadataVolume, &reader, 0U) == UMICOM_DISK_OK,
        "clean filesystem admission in a fresh metadata reader");
    UmicomMetadataAccepted();
#endif
    UmicomMetadataWholeDisk(&source);
    UmicomMetadataRequire(UmicomKernelBlockClose(domain, source.handle) == UMICOM_BLOCK_OK,
        "transport reset completes before both DMA frames are released");
    UmicomMetadataRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "physical frame accounting returns to its baseline");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomMetadataRequire(UmicomMetadataEqual(&machineBefore, &machineAfter, sizeof(machineBefore)),
        "machine controls and privilege state remain unchanged");
    UmicomMetadataFill(umicomMetadataDisk, sizeof(umicomMetadataDisk), 0U);
    UmicomMetadataFill(umicomMetadataExpected, sizeof(umicomMetadataExpected), 0U);
    UmicomMetadataFill(&umicomMetadataOutput, sizeof(umicomMetadataOutput), 0U);
    UmicomKernelConsoleWriteLine("fat16-metadata.disk-bytes=8388608-verified-before-and-after-query");
    UmicomKernelConsoleWriteLine("fat16-metadata.frame-accounting=restored machine-state=unchanged writes=none");
    UmicomKernelConsoleWriteLine("fat16-metadata-test=pass");
#if defined(UMICOM_KERNEL_FAT16_METADATA_TEST)
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_METADATA_READY");
#elif defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST)
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_METADATA_COMMITTED_READY");
#else
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAT16_METADATA_DIRTY_READY");
#endif
}
#if defined(UMICOM_KERNEL_FAT16_METADATA_TEST)
void UmicomKernelFat16MetadataValidate(void) { UmicomMetadataValidate(); }
#elif defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST)
void UmicomKernelFat16MetadataCommittedValidate(void) { UmicomMetadataValidate(); }
#else
void UmicomKernelFat16MetadataDirtyValidate(void) { UmicomMetadataValidate(); }
#endif
