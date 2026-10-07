/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_filesystem_validation.c
 *
 * Read the synthetic FAT16 medium through ordinary VFS descriptors while the
 * mount owns the actual VirtIO transport. A separate test image requires that
 * medium, so absent-device handling cannot produce a filesystem success marker.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "../tests/disk_inspection/fixture_layout.h"

static UmicomKernelDiskMount umicomFilesystemValidationMount;
static UmicomKernelVfsClient umicomFilesystemValidationClient;
static UmicomU8 umicomFilesystemValidationBytes[UMICOM_FAT16_READ_BYTES];

static void UmicomFilesystemClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static void UmicomFilesystemRequire(UmicomBoolean accepted, const char *reason)
{
    if (accepted) return;
    UmicomKernelConsoleWrite("disk-filesystem.failure="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x87U); UmicomPlatformHalt();
}
static UmicomBoolean UmicomFilesystemEqual(const void *left, const void *right, UmicomSize count)
{
    const UmicomU8 *const a = (const UmicomU8 *)left, *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < count; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static void UmicomFilesystemPattern(UmicomSize count, UmicomSize offset)
{
    for (UmicomSize i = 0U; i < count; ++i)
        UmicomFilesystemRequire(umicomFilesystemValidationBytes[i] == UmicomDiskFixturePattern(offset + i),
            "fragmented bytes through VFS");
}
void UmicomKernelDiskFilesystemValidate(void)
{
    UmicomKernelConsoleWriteLine("disk-filesystem-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomFilesystemClear(&before, sizeof(before)); UmicomFilesystemClear(&after, sizeof(after));
    UmicomFilesystemClear(&machineBefore, sizeof(machineBefore));
    UmicomFilesystemClear(&machineAfter, sizeof(machineAfter));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomFilesystemRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "allocator available");
    UmicomKernelBlockDomain *domain = (UmicomKernelBlockDomain *)0;
    UmicomFilesystemRequire(UmicomPlatformBlockDomainGet(&domain) == UMICOM_BLOCK_OK,
        "qualified MMIO domain");
    UmicomSize found = 0U, slot = 0U;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        UmicomKernelBlockInfo info;
        UmicomFilesystemClear(&info, sizeof(info));
        const UmicomKernelBlockStatus status = UmicomKernelBlockProbe(domain, i, &info);
        if (status == UMICOM_BLOCK_OK) { ++found; slot = i; }
        else UmicomFilesystemRequire(status == UMICOM_BLOCK_NO_DEVICE || status == UMICOM_BLOCK_NOT_BLOCK ||
            status == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT, "safe identity probe");
    }
    UmicomFilesystemRequire(found == 1U, "exactly one synthetic filesystem device required");
    UmicomKernelDiskMount *const mount = &umicomFilesystemValidationMount;
    UmicomKernelVfsClient *const client = &umicomFilesystemValidationClient;
    UmicomFilesystemRequire(UmicomKernelDiskMountOpen(mount, domain, slot, 0U, 10000000U) == UMICOM_VFS_OK,
        "transport and provider mounted");
    UmicomFilesystemRequire(mount->provider.volume.info.clusters == 12159U && mount->handle != 0U,
        "fixture geometry and live transport lease");
    UmicomFilesystemRequire(UmicomKernelDiskMountClientOpen(mount, client, 42U,
        UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK, "read-only client admitted");
    const UmicomKernelBlockHandle lease = mount->handle;
    UmicomFilesystemRequire(UmicomKernelDiskMountClose(mount) == UMICOM_VFS_BUSY &&
        mount->state == UMICOM_VFS_OPEN && mount->handle == lease,
        "even an empty client keeps the mount alive");
    UmicomKernelConsoleWriteLine("disk-filesystem.mount-and-client-lifetime=verified");

    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomFilesystemRequire(UmicomKernelVfsOpen(client, "/", UMICOM_VFS_RIGHT_ENUMERATE,
        UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK, "root directory descriptor");
    UmicomSize entries = 0U;
    for (;;) {
        UmicomKernelVfsDirectoryEntry entry;
        UmicomFilesystemClear(&entry, sizeof(entry));
        const UmicomKernelVfsStatus status = UmicomKernelVfsReadDirectory(client, descriptor, &entry);
        if (status == UMICOM_VFS_END) break;
        UmicomFilesystemRequire(status == UMICOM_VFS_OK && ++entries <= 4U, "bounded root iteration");
    }
    UmicomFilesystemRequire(entries == 4U && UmicomKernelVfsClose(client, descriptor) == UMICOM_VFS_OK,
        "complete root listing and pin release");
    UmicomFilesystemRequire(UmicomKernelVfsOpen(client, "/docs/guide.txt",
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK,
        "nested short alias opened through VFS");
    UmicomSize count = 0U;
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, descriptor, umicomFilesystemValidationBytes,
        sizeof(umicomFilesystemValidationBytes), &count) == UMICOM_VFS_OK &&
        count == sizeof(UMICOM_DISK_FIXTURE_GUIDE) - 1U &&
        UmicomFilesystemEqual(umicomFilesystemValidationBytes, UMICOM_DISK_FIXTURE_GUIDE, count),
        "nested file content");
    UmicomFilesystemRequire(UmicomKernelVfsClose(client, descriptor) == UMICOM_VFS_OK, "nested close");
    UmicomKernelConsoleWriteLine("disk-filesystem.directory-and-file-bytes=verified");

    UmicomKernelFileDescriptor duplicate = 0U, independent = 0U;
    UmicomFilesystemRequire(UmicomKernelVfsOpen(client, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE,
        UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK, "fragmented file opened");
    UmicomFilesystemRequire(UmicomKernelVfsDuplicate(client, descriptor,
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY, &duplicate) == UMICOM_VFS_OK, "shared description");
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, descriptor, umicomFilesystemValidationBytes,
        16U, &count) == UMICOM_VFS_OK && count == 16U, "first shared read");
    UmicomFilesystemPattern(count, 0U);
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, duplicate, umicomFilesystemValidationBytes,
        12U, &count) == UMICOM_VFS_OK && count == 12U, "duplicate shares position");
    UmicomFilesystemPattern(count, 16U);
    UmicomFilesystemRequire(UmicomKernelVfsOpen(client, "/frag.bin", UMICOM_VFS_RIGHT_READ,
        UMICOM_FALSE, &independent) == UMICOM_VFS_OK, "independent description");
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, independent, umicomFilesystemValidationBytes,
        8U, &count) == UMICOM_VFS_OK && count == 8U, "independent position");
    UmicomFilesystemPattern(count, 0U);
    UmicomFilesystemRequire(UmicomKernelVfsSeek(client, descriptor, 511U) == UMICOM_VFS_OK,
        "seek to cluster boundary");
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, duplicate, umicomFilesystemValidationBytes,
        700U, &count) == UMICOM_VFS_OK && count == 700U, "fragmented range via duplicate");
    UmicomFilesystemPattern(count, 511U);
    UmicomFilesystemRequire(UmicomKernelDiskMountClose(mount) == UMICOM_VFS_BUSY && mount->handle == lease,
        "busy unmount retains file descriptions");
    UmicomFilesystemRequire(UmicomKernelVfsSeek(client, descriptor, UMICOM_DISK_FIXTURE_FRAGMENT_BYTES) == UMICOM_VFS_OK,
        "seek to EOF");
    umicomFilesystemValidationBytes[0] = 0xa5U; count = 77U;
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, descriptor, umicomFilesystemValidationBytes,
        1U, &count) == UMICOM_VFS_OK && count == 0U && umicomFilesystemValidationBytes[0] == 0xa5U,
        "EOF preserves unused bytes");
    UmicomFilesystemRequire(UmicomKernelVfsRead(client, descriptor, (void *)0, 0U, &count) == UMICOM_VFS_OK &&
        count == 0U, "zero-length VFS read");
    UmicomFilesystemRequire(UmicomKernelVfsClose(client, duplicate) == UMICOM_VFS_OK &&
        UmicomKernelVfsClose(client, independent) == UMICOM_VFS_OK &&
        UmicomKernelVfsClose(client, descriptor) == UMICOM_VFS_OK, "all file pins released");
    UmicomFilesystemRequire(UmicomKernelVfsClose(client, descriptor) == UMICOM_VFS_INVALID_DESCRIPTOR,
        "stale descriptor refused");
    UmicomKernelConsoleWriteLine("disk-filesystem.fragmented-seek-and-duplicate=verified");

    descriptor = 0U;
    UmicomFilesystemRequire(UmicomKernelVfsOpen(client, "/README.TXT", UMICOM_VFS_RIGHT_WRITE,
        UMICOM_FALSE, &descriptor) == UMICOM_VFS_ACCESS_DENIED && !descriptor,
        "write-right open refused by client ceiling");
    UmicomFilesystemRequire(UmicomKernelVfsCreate(client, "/NEW.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED &&
        UmicomKernelVfsRemove(client, "/README.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED,
        "namespace mutation rights withheld");
    UmicomKernelConsoleWriteLine("disk-filesystem.mutation-rights=refused");
    UmicomSize closed = 0U;
    UmicomFilesystemRequire(UmicomKernelVfsClientClose(client, &closed) == UMICOM_VFS_OK && closed == 0U,
        "client lifetime ended");
    UmicomFilesystemRequire(UmicomKernelDiskMountClose(mount) == UMICOM_VFS_OK && !mount->handle &&
        mount->state == UMICOM_VFS_CLOSED && mount->provider.pins == 0U,
        "unmount before interpretation retirement and transport reset");
    UmicomFilesystemRequire(UmicomKernelDiskMountClose(mount) == UMICOM_VFS_OK,
        "completed teardown is idempotent");
    UmicomFilesystemRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "frame accounting restored");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomFilesystemRequire(UmicomFilesystemEqual(&machineBefore, &machineAfter, sizeof(machineBefore)),
        "machine state unchanged");
    UmicomKernelConsoleWriteLine("disk-filesystem.reset-before-release=verified");
    UmicomKernelConsoleWriteLine("disk-filesystem.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("disk-filesystem.machine-state=unchanged");
    UmicomKernelConsoleWriteLine("disk-filesystem.disk-writes=none");
    UmicomKernelConsoleWriteLine("disk-filesystem-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_DISK_FILESYSTEM_READY");
}
