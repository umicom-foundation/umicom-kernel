/*-----------------------------------------------------------------------------
 * Umicom Kernel mounted-filesystem transport and cleanup qualification.
 *
 * The earlier register/DMA model is included without changing its implementation.
 * Only the byte source changes from its raw-sector fixture to the existing FAT16
 * disk. Production VFS, provider, mount lifecycle, block driver, physical allocator
 * and the guest C acceptance sequence are linked. This does not execute RISC-V
 * instructions or constitute a real QEMU run.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/types.h"
#include "../disk_inspection/fixture_layout.h"
#include <stdio.h>
#include <stdlib.h>

static UmicomU8 *umicomFilesystemImage;
#define UMICOM_BLOCK_FIXTURE_FORMAT_H
#define UMICOM_BLOCK_FIXTURE_SECTORS 128U
static UmicomU8 UmicomBlockFixtureByte(UmicomU64 sector, UmicomSize offset)
{
    if (sector >= UMICOM_DISK_FIXTURE_SECTORS || offset >= 512U) abort();
    return umicomFilesystemImage[sector * 512U + offset];
}
#define UmicomPlatformBlockDomainGet UmicomFilesystemModelDomainGet
#define main UmicomBlockModelRegressionEntry
#include "../virtio_block/virtio_block_tests.c"
#undef main
#undef UmicomPlatformBlockDomainGet
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/disk_filesystem_console.h"

static UmicomKernelDiskMount mountOwner;
static UmicomKernelVfsClient mountClient;
static UmicomKernelVfsClient extraClient;
static UmicomKernelConsoleShell mountShell;
static UmicomKernelConsoleShell foreignShell;
static UmicomKernelBlockStatus filesystemPlatformStatus = UMICOM_BLOCK_OK;
static UmicomU32 finalNotification;
static UmicomBoolean jumpAtFinalSector, rollBackAtFinalSector, clockInjected;

/* Only discovery's model boundary is fallible here. The real provider, mount
 * and console still have to preserve an untouched owner after that refusal. */
UmicomKernelBlockStatus UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **out)
{
    if (filesystemPlatformStatus != UMICOM_BLOCK_OK) return filesystemPlatformStatus;
    return UmicomFilesystemModelDomainGet(out);
}
static UmicomU64 FilesystemClock(void *context)
{
    const UmicomU64 now = Clock(context);
    if ((jumpAtFinalSector || rollBackAtFinalSector) && model.notifications == finalNotification) {
        /* Notification has completed DMA synchronously in the inherited model.
         * An immediate completion satisfies the per-request wait, but the final
         * sector still has to satisfy the enclosing filesystem operation bound. */
        if (jumpAtFinalSector) model.now += UMICOM_DISK_MOUNT_OPERATION_TICKS;
        else {
            CHECK(mountOwner.operationClock > 3U);
            model.now = mountOwner.operationClock - 3U;
        }
        jumpAtFinalSector = UMICOM_FALSE;
        rollBackAtFinalSector = UMICOM_FALSE;
        clockInjected = UMICOM_TRUE;
        return model.now;
    }
    return now;
}
static void ClearTranscript(void)
{
    memset(transcript, 0, sizeof(transcript)); transcriptBytes = 0U;
}
static UmicomKernelShellStatus Command(const char *line)
{
    UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_FALSE;
    CHECK(UmicomKernelShellParse(line, strlen(line), &command) == UMICOM_SHELL_OK);
    const UmicomKernelShellStatus status = UmicomKernelDiskFilesystemCommand(&mountShell, &command, &handled);
    CHECK(handled);
    return status;
}
static void ConsoleMount(void)
{
    CHECK(Command("mountdisk 0 0") == UMICOM_SHELL_OK);
    CHECK(Allocated() == 2U && domain.slots[0].claimed);
    CHECK(strstr(transcript, "disk.mount=ok") != 0);
}

static void MountOpen(void)
{
    CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_OK);
    CHECK(mountOwner.state == UMICOM_VFS_OPEN && mountOwner.admitted && mountOwner.handle);
    CHECK(mountOwner.provider.pins == 1U && Allocated() == 2U);
}
static void MountClientOpen(UmicomKernelVfsRights rights)
{
    CHECK(UmicomKernelDiskMountClientOpen(&mountOwner, &mountClient, 7U, rights) == UMICOM_VFS_OK);
}
static UmicomKernelFileDescriptor MountFileOpen(void)
{
    UmicomKernelFileDescriptor descriptor = 0U;
    CHECK(UmicomKernelVfsOpen(&mountClient, "/FRAG.BIN",
        UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_QUERY | UMICOM_VFS_RIGHT_DUPLICATE,
        UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK && descriptor);
    return descriptor;
}
static void MountClientClose(void)
{
    UmicomSize closed = 0U;
    if (extraClient.state == UMICOM_VFS_OPEN)
        CHECK(UmicomKernelVfsClientClose(&extraClient, &closed) == UMICOM_VFS_OK);
    if (mountClient.state == UMICOM_VFS_OPEN)
        CHECK(UmicomKernelVfsClientClose(&mountClient, &closed) == UMICOM_VFS_OK);
}
static void MountFinish(void)
{
    MountClientClose();
    CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_OK);
    CHECK(mountOwner.handle == 0U && Allocated() == 0U);
    CHECK(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
}
static void MountPattern(UmicomSize first, UmicomSize count)
{
    for (UmicomSize i = 0U; i < count; ++i)
        CHECK(resultBytes[i] == UmicomDiskFixturePattern(first + i));
}
static void MountBusyUnchanged(void)
{
    UmicomKernelDiskMount *before = malloc(sizeof(*before)); CHECK(before);
    memcpy(before, &mountOwner, sizeof(*before));
    const UmicomU32 writes = model.writes, resets = model.resets, frees = model.freeCalls;
    const UmicomKernelBlockHandle handle = mountOwner.handle;
    CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_BUSY);
    CHECK(memcmp(before, &mountOwner, sizeof(*before)) == 0);
    CHECK(mountOwner.handle == handle && model.writes == writes && model.resets == resets && model.freeCalls == frees);
    CHECK(Allocated() == 2U && domain.slots[0].claimed && domain.slots[0].state == UMICOM_BLOCK_READY);
    free(before);
}
static void MountReadFailure(UmicomKernelFileDescriptor descriptor)
{
    memset(resultBytes, 0xa5, sizeof(resultBytes)); UmicomSize count = 99U;
    CHECK(UmicomKernelVfsRead(&mountClient, descriptor, resultBytes, 1300U, &count) == UMICOM_VFS_IO_ERROR);
    CHECK(count == 0U); Unchanged();
    CHECK(mountOwner.provider.pins == 2U && !mountOwner.provider.busy);
    UmicomKernelVfsNodeInfo info = {0};
    CHECK(UmicomKernelVfsQuery(&mountClient, descriptor, &info) == UMICOM_VFS_OK && info.bytes == 1300U);
    CHECK(UmicomKernelVfsClose(&mountClient, descriptor) == UMICOM_VFS_OK);
    CHECK(mountOwner.provider.pins == 1U);
}

static void RunMountCase(const char *test)
{
    if (!strcmp(test, "guest_sequence")) {
        UmicomKernelDiskFilesystemValidate();
        CHECK(strstr(transcript, "UMICOM_KERNEL_DISK_FILESYSTEM_READY") != 0 && Allocated() == 0U);
    } else if (!strcmp(test, "mounted_read")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen(); UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&mountClient, descriptor, resultBytes, sizeof(resultBytes), &count) == UMICOM_VFS_OK);
        CHECK(count == 1300U); MountPattern(0U, count);
        CHECK(domain.slots[0].claimed && model.observedReadOnlyRequest);
    } else if (!strcmp(test, "idle_mount_budget")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        model.now += (UmicomU64)UMICOM_DISK_MOUNT_OPERATION_TICKS * 4U;
        UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&mountClient, descriptor, resultBytes, 32U, &count) == UMICOM_VFS_OK && count == 32U);
        MountPattern(0U, count);
    } else if (!strcmp(test, "shared_position")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        CHECK(UmicomKernelVfsSeek(&mountClient, descriptor, 510U) == UMICOM_VFS_OK);
        UmicomKernelFileDescriptor duplicate = 0U;
        CHECK(UmicomKernelVfsDuplicate(&mountClient, descriptor, UMICOM_VFS_RIGHT_READ, &duplicate) == UMICOM_VFS_OK);
        UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&mountClient, duplicate, resultBytes, 520U, &count) == UMICOM_VFS_OK && count == 520U);
        MountPattern(510U, count);
        CHECK(UmicomKernelVfsRead(&mountClient, descriptor, resultBytes, 64U, &count) == UMICOM_VFS_OK && count == 64U);
        MountPattern(1030U, count);
    } else if (!strcmp(test, "empty_client_busy")) {
        MountOpen(); MountClientOpen(0U);
        CHECK(mountOwner.vfs.clients == 1U && mountOwner.provider.pins == 1U);
        MountBusyUnchanged();
    } else if (!strcmp(test, "open_file_busy")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen(); MountBusyUnchanged();
        UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&mountClient, descriptor, resultBytes, 19U, &count) == UMICOM_VFS_OK && count == 19U);
        MountPattern(0U, count);
    } else if (!strcmp(test, "duplicate_busy")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen(); UmicomKernelFileDescriptor duplicate = 0U;
        CHECK(UmicomKernelVfsDuplicate(&mountClient, descriptor, UMICOM_VFS_RIGHT_READ, &duplicate) == UMICOM_VFS_OK);
        CHECK(UmicomKernelVfsClose(&mountClient, descriptor) == UMICOM_VFS_OK);
        MountBusyUnchanged();
        CHECK(UmicomKernelVfsClose(&mountClient, duplicate) == UMICOM_VFS_OK);
        /* Even after the final description closes, the client is still a live
         * attachment, just as the established user-file service anchor is. */
        MountBusyUnchanged();
    } else if (!strcmp(test, "single_lifetime")) {
        MountOpen(); MountFinish(); const UmicomU32 writes = model.writes;
        CHECK(mountOwner.state == UMICOM_VFS_CLOSED);
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_BAD_STATE);
        CHECK(model.writes == writes && mountOwner.handle == 0U);
    } else if (!strcmp(test, "unsafe_admission")) {
        model.allowed = UMICOM_FALSE;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_UNSAFE_CONTEXT);
        CHECK(model.writes == 0U && model.reads == 0U && Allocated() == 0U && !mountOwner.handle);
        model.allowed = UMICOM_TRUE; MountOpen();
    } else if (!strcmp(test, "readonly_clients")) {
        MountOpen();
        const UmicomKernelVfsRights refused[] = {UMICOM_VFS_RIGHT_WRITE, UMICOM_VFS_RIGHT_CREATE,
            UMICOM_VFS_RIGHT_REMOVE, UMICOM_VFS_RIGHT_ALL};
        for (UmicomSize i = 0U; i < sizeof(refused) / sizeof(refused[0]); ++i) {
            CHECK(UmicomKernelDiskMountClientOpen(&mountOwner, &mountClient, 7U, refused[i]) == UMICOM_VFS_ACCESS_DENIED);
            CHECK(!mountClient.self && mountOwner.vfs.clients == 0U && mountOwner.provider.pins == 1U);
        }
        MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        UmicomKernelFileDescriptor descriptor = 99U;
        CHECK(UmicomKernelVfsOpen(&mountClient, "/FRAG.BIN", UMICOM_VFS_RIGHT_WRITE,
            UMICOM_FALSE, &descriptor) == UMICOM_VFS_ACCESS_DENIED && descriptor == 99U);
        CHECK(UmicomKernelVfsCreate(&mountClient, "/NEW.TXT", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(UmicomKernelVfsRemove(&mountClient, "/FRAG.BIN", UMICOM_VFS_FILE) == UMICOM_VFS_ACCESS_DENIED);
    } else if (!strcmp(test, "exclusive_slot")) {
        MountOpen(); UmicomKernelDiskMount *other = calloc(1U, sizeof(*other)); CHECK(other);
        const UmicomU32 writes = model.writes;
        CHECK(UmicomKernelDiskMountOpen(other, &domain, 0U, 0U, 32U) == UMICOM_VFS_BUSY);
        CHECK(!other->handle && model.writes == writes && Allocated() == 2U);
        CHECK(UmicomKernelDiskMountClose(other) == UMICOM_VFS_OK); free(other);
        MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        (void)MountFileOpen();
    } else if (!strcmp(test, "absent_device")) {
        model.device = 0U;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_NOT_FOUND);
        CHECK(model.writes == 0U && Allocated() == 0U && !mountOwner.admitted);
        CHECK(strstr(transcript, "UMICOM_KERNEL_DISK_FILESYSTEM_READY") == 0);
    } else if (!strcmp(test, "bad_media_cleanup")) {
        umicomFilesystemImage[510] = 0U;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_CORRUPT_FILESYSTEM);
        CHECK(mountOwner.provider.lastDiskStatus == UMICOM_DISK_SIGNATURE);
        CHECK(!mountOwner.handle && !mountOwner.admitted && Allocated() == 0U && model.observedSafeRelease);
    } else if (!strcmp(test, "failed_open_retained")) {
        model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_RELEASE_FAILED);
        CHECK(mountOwner.handle && mountOwner.state == UMICOM_VFS_CLOSING && !mountOwner.admitted);
        CHECK(mountOwner.lastCleanupStatus == UMICOM_BLOCK_RESET_PENDING && Allocated() == 2U && model.freeCalls == 0U);
        const UmicomKernelBlockHandle retained = mountOwner.handle;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_BAD_STATE);
        CHECK(mountOwner.handle == retained && Allocated() == 2U);
        model.stuckReset = UMICOM_FALSE; model.refuseDriver = UMICOM_FALSE; model.stickAfterDriver = UMICOM_FALSE;
        CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_OK && !mountOwner.handle);
        CHECK(mountOwner.state == UMICOM_VFS_UNUSED && Allocated() == 0U); MountOpen();
    } else if (!strcmp(test, "reset_retry")) {
        MountOpen(); model.stuckReset = UMICOM_TRUE;
        const UmicomKernelBlockHandle retained = mountOwner.handle;
        CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_RELEASE_FAILED);
        CHECK(mountOwner.handle == retained && mountOwner.state == UMICOM_VFS_CLOSING);
        CHECK(mountOwner.vfs.state == UMICOM_VFS_CLOSED && mountOwner.provider.state == UMICOM_VFS_CLOSED);
        CHECK(Allocated() == 2U && model.freeCalls == 0U);
        CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_RELEASE_FAILED && Allocated() == 2U);
        CHECK(UmicomKernelDiskMountClientOpen(&mountOwner, &mountClient, 7U, 0U) == UMICOM_VFS_BAD_STATE);
        model.stuckReset = UMICOM_FALSE;
    } else if (!strcmp(test, "partial_release_retry")) {
        MountOpen(); model.failFree = 2U;
        const UmicomKernelBlockHandle retained = mountOwner.handle;
        CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_RELEASE_FAILED);
        CHECK(mountOwner.handle == retained && mountOwner.state == UMICOM_VFS_CLOSING && Allocated() == 1U);
        CHECK(domain.slots[0].dataFrame == 0U && domain.slots[0].queueFrame != 0U);
        CHECK(mountOwner.provider.state == UMICOM_VFS_CLOSED && model.freeCalls == 2U);
        CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_OK);
        CHECK(model.freeCalls == 3U && Allocated() == 0U);
    } else if (!strcmp(test, "read_error_cleanup")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        model.usedId = 1U; MountReadFailure(descriptor);
        CHECK(mountOwner.lastBlockStatus == UMICOM_BLOCK_MALFORMED_COMPLETION);
        CHECK(domain.slots[0].state == UMICOM_BLOCK_FAULTED && Allocated() == 2U);
    } else if (!strcmp(test, "read_reset_pending")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        model.noCompletion = UMICOM_TRUE; model.stuckReset = UMICOM_TRUE; MountReadFailure(descriptor);
        CHECK(domain.slots[0].exposed && Allocated() == 2U && model.freeCalls == 0U);
        MountClientClose();
        CHECK(UmicomKernelDiskMountClose(&mountOwner) == UMICOM_VFS_RELEASE_FAILED);
        CHECK(mountOwner.handle && mountOwner.state == UMICOM_VFS_CLOSING && Allocated() == 2U);
        model.stuckReset = UMICOM_FALSE;
    } else if (!strcmp(test, "stopped_clock")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        model.step = 0U; model.noCompletion = UMICOM_TRUE; MountReadFailure(descriptor);
        CHECK(mountOwner.lastBlockStatus == UMICOM_BLOCK_TIMEOUT && Allocated() == 2U);
    } else if (!strcmp(test, "backward_clock")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        model.now = 1000U; model.backwardClock = UMICOM_TRUE; MountReadFailure(descriptor);
        CHECK(mountOwner.lastBlockStatus == UMICOM_BLOCK_CLOCK_ERROR);
        model.backwardClock = UMICOM_FALSE;
    } else if (!strcmp(test, "operation_timeout")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        model.step = (UmicomU64)UMICOM_DISK_MOUNT_OPERATION_TICKS + 1U;
        const UmicomU32 notifications = model.notifications; MountReadFailure(descriptor);
        CHECK(mountOwner.lastBlockStatus == UMICOM_BLOCK_TIMEOUT && model.notifications == notifications);
        model.step = 1U;
    } else if (!strcmp(test, "admission_retry")) {
        model.device = 0U;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_NOT_FOUND);
        CHECK(mountOwner.state == UMICOM_VFS_UNUSED && !mountOwner.handle && !mountOwner.provider.self);
        model.device = 2U; MountOpen();
    } else if (!strcmp(test, "allocation_failure")) {
        model.failAllocate = 2U;
        CHECK(UmicomKernelDiskMountOpen(&mountOwner, &domain, 0U, 0U, 32U) == UMICOM_VFS_NO_MEMORY);
        CHECK(mountOwner.state == UMICOM_VFS_UNUSED && !mountOwner.handle && Allocated() == 0U);
        model.failAllocate = 0U; MountOpen();
    } else if (!strcmp(test, "fresh_mount_lifetimes")) {
        enum { LIFETIMES = 32 };
        UmicomKernelDiskMount *owners = calloc(LIFETIMES, sizeof(*owners));
        UmicomKernelVfsClient *clients = calloc(LIFETIMES, sizeof(*clients)); CHECK(owners && clients);
        UmicomKernelBlockHandle previous = 0U;
        for (UmicomSize i = 0U; i < LIFETIMES; ++i) {
            CHECK(UmicomKernelDiskMountOpen(&owners[i], &domain, 0U, 0U, 32U) == UMICOM_VFS_OK);
            CHECK(Allocated() == 2U);
            if (previous) {
                CHECK(UmicomKernelBlockRead(&domain, previous, 0U, 1U, resultBytes, sizeof(resultBytes)) == UMICOM_BLOCK_INVALID_HANDLE);
                CHECK(UmicomKernelBlockClose(&domain, previous) == UMICOM_BLOCK_INVALID_HANDLE);
            }
            CHECK(UmicomKernelDiskMountClientOpen(&owners[i], &clients[i], i + 1U, UMICOM_DISK_MOUNT_READ_RIGHTS) == UMICOM_VFS_OK);
            UmicomKernelFileDescriptor descriptor = 0U;
            CHECK(UmicomKernelVfsOpen(&clients[i], "/FRAG.BIN", UMICOM_VFS_RIGHT_READ,
                UMICOM_FALSE, &descriptor) == UMICOM_VFS_OK);
            UmicomSize count = 0U;
            CHECK(UmicomKernelVfsRead(&clients[i], descriptor, resultBytes, 32U, &count) == UMICOM_VFS_OK && count == 32U);
            MountPattern(0U, count);
            CHECK(UmicomKernelVfsClientClose(&clients[i], &count) == UMICOM_VFS_OK);
            previous = owners[i].handle;
            CHECK(UmicomKernelDiskMountClose(&owners[i]) == UMICOM_VFS_OK && Allocated() == 0U);
            CHECK(UmicomKernelVfsRead(&clients[i], descriptor, resultBytes, 1U, &count) == UMICOM_VFS_BAD_STATE);
        }
        free(clients); free(owners);
    } else if (!strcmp(test, "final_sector_deadline") || !strcmp(test, "final_sector_clock_rollback")) {
        MountOpen(); MountClientOpen(UMICOM_DISK_MOUNT_READ_RIGHTS);
        const UmicomKernelFileDescriptor descriptor = MountFileOpen();
        const UmicomU32 first = model.notifications; UmicomSize count = 0U;
        CHECK(UmicomKernelVfsRead(&mountClient, descriptor, resultBytes, 1300U, &count) == UMICOM_VFS_OK && count == 1300U);
        const UmicomU32 requests = model.notifications - first; CHECK(requests > 3U);
        CHECK(UmicomKernelVfsSeek(&mountClient, descriptor, 0U) == UMICOM_VFS_OK);
        finalNotification = model.notifications + requests;
        jumpAtFinalSector = !strcmp(test, "final_sector_deadline") ? UMICOM_TRUE : UMICOM_FALSE;
        rollBackAtFinalSector = jumpAtFinalSector ? UMICOM_FALSE : UMICOM_TRUE;
        const UmicomKernelBlockStatus expected = jumpAtFinalSector ? UMICOM_BLOCK_TIMEOUT : UMICOM_BLOCK_CLOCK_ERROR;
        MountReadFailure(descriptor);
        CHECK(clockInjected && model.notifications == finalNotification);
        CHECK(mountOwner.lastBlockStatus == expected && domain.slots[0].state == UMICOM_BLOCK_READY);
    } else if (!strcmp(test, "extra_provider_pin")) {
        MountOpen(); const UmicomKernelVfsOperations *const operations = UmicomKernelFat16ProviderOperationsGet();
        UmicomKernelVfsNodeId node = 0U;
        CHECK(operations->lookup(&mountOwner.provider, 1U, "README.TXT", &node) == UMICOM_VFS_OK);
        CHECK(operations->pin(&mountOwner.provider, node) == UMICOM_VFS_OK && mountOwner.provider.pins == 2U);
        CHECK(mountOwner.vfs.clients == 0U); MountBusyUnchanged();
        UmicomSize count = 0U;
        CHECK(operations->read(&mountOwner.provider, node, 0U, resultBytes, 32U, &count) == UMICOM_VFS_OK && count == 32U);
        CHECK(memcmp(resultBytes, UMICOM_DISK_FIXTURE_README, count) == 0);
        CHECK(operations->unpin(&mountOwner.provider, node) == UMICOM_VFS_OK);
    } else if (!strcmp(test, "second_vfs_pin")) {
        MountOpen(); UmicomKernelVfs secondMount = {0};
        CHECK(UmicomKernelVfsMount(&secondMount, UmicomKernelFat16ProviderOperationsGet(),
            &mountOwner.provider) == UMICOM_VFS_OK && mountOwner.provider.pins == 2U);
        CHECK(mountOwner.vfs.clients == 0U); MountBusyUnchanged();
        CHECK(secondMount.state == UMICOM_VFS_OPEN && mountOwner.vfs.state == UMICOM_VFS_OPEN);
        CHECK(UmicomKernelVfsUnmount(&secondMount) == UMICOM_VFS_OK && mountOwner.provider.pins == 1U);
    } else if (!strcmp(test, "console_persistent_commands")) {
        ConsoleMount(); const UmicomU32 resets = model.resets;
        ClearTranscript(); CHECK(Command("mountinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "disk.mount-state=mounted slot=0 partition=0 clients=1") != 0);
        ClearTranscript(); CHECK(Command("diskls /") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "README.TXT") && strstr(transcript, "DOCS") && strstr(transcript, "disk.list=ok"));
        ClearTranscript(); CHECK(Command("diskls /DOCS") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "GUIDE.TXT") && strstr(transcript, "disk.list=ok"));
        ClearTranscript(); CHECK(Command("diskcat /README.TXT") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "Umicom Kernel read-only FAT16 fixture.") && strstr(transcript, "disk.read=ok"));
        CHECK(Allocated() == 2U && model.resets == resets && model.freeCalls == 0U);
        ClearTranscript(); CHECK(Command("unmountdisk") == UMICOM_SHELL_OK && Allocated() == 0U);
        CHECK(strstr(transcript, "disk.unmount=ok") != 0);
        ClearTranscript(); CHECK(Command("mountinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "disk.mount-state=closed") != 0);
    } else if (!strcmp(test, "console_double_mount")) {
        ConsoleMount(); const UmicomU32 writes = model.writes, resets = model.resets, requests = model.notifications;
        ClearTranscript(); CHECK(Command("mountdisk 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "disk.mount=bad-state") != 0);
        CHECK(model.writes == writes && model.resets == resets && model.notifications == requests);
        CHECK(Allocated() == 2U && model.freeCalls == 0U);
        ClearTranscript(); CHECK(Command("diskcat /README.TXT") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "Umicom Kernel read-only FAT16 fixture.") != 0);
    } else if (!strcmp(test, "console_absent_retry")) {
        model.device = 0U;
        CHECK(Command("mountdisk 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "disk.mount=not-found") != 0 && model.writes == 0U && Allocated() == 0U);
        model.device = 2U; ClearTranscript(); ConsoleMount();
        ClearTranscript(); CHECK(Command("diskls /") == UMICOM_SHELL_OK && strstr(transcript, "README.TXT"));
    } else if (!strcmp(test, "console_bad_media_retry")) {
        umicomFilesystemImage[510] = 0U;
        CHECK(Command("mountdisk 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "disk.mount=corrupt-filesystem") && strstr(transcript, "disk.media=bad-signature"));
        CHECK(Allocated() == 0U && !domain.slots[0].claimed);
        umicomFilesystemImage[510] = 0x55U; ClearTranscript(); ConsoleMount();
    } else if (!strcmp(test, "console_platform_retry")) {
        filesystemPlatformStatus = UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
        CHECK(Command("mountdisk 0 0") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "disk.transport=unqualified-platform") != 0);
        CHECK(model.reads == 0U && model.writes == 0U && Allocated() == 0U);
        filesystemPlatformStatus = UMICOM_BLOCK_OK; ClearTranscript(); ConsoleMount();
    } else if (!strcmp(test, "console_unmount_retry")) {
        ConsoleMount(); model.stuckReset = UMICOM_TRUE;
        ClearTranscript(); CHECK(Command("unmountdisk") == UMICOM_SHELL_IO_ERROR);
        CHECK(strstr(transcript, "disk.unmount=release-failed") != 0 && Allocated() == 2U && model.freeCalls == 0U);
        ClearTranscript(); CHECK(Command("mountinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "disk.mount-state=closing slot=0 partition=0 clients=0") != 0);
        CHECK(Command("diskcat /README.TXT") == UMICOM_SHELL_IO_ERROR);
        CHECK(Command("unmountdisk") == UMICOM_SHELL_IO_ERROR && Allocated() == 2U && model.freeCalls == 0U);
        model.stuckReset = UMICOM_FALSE;
        ClearTranscript(); CHECK(Command("unmountdisk") == UMICOM_SHELL_OK && Allocated() == 0U);
        CHECK(strstr(transcript, "disk.unmount=ok") != 0);
    } else if (!strcmp(test, "console_poweroff_retry")) {
        ConsoleMount(); model.failFree = 2U;
        /* This is the actual cleanup function called by shell poweroff; a full
         * native machine shutdown is neither simulated nor claimed here. */
        CHECK(UmicomKernelDiskFilesystemConsoleClose(&mountShell) == UMICOM_VFS_RELEASE_FAILED);
        CHECK(Allocated() == 1U && model.freeCalls == 2U);
        ClearTranscript(); CHECK(Command("mountinfo") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "disk.mount-state=closing") != 0);
        CHECK(UmicomKernelDiskFilesystemConsoleClose(&mountShell) == UMICOM_VFS_OK);
        CHECK(Allocated() == 0U && model.freeCalls == 3U);
    } else if (!strcmp(test, "console_binary_escape")) {
        ConsoleMount(); ClearTranscript();
        CHECK(Command("diskcat /FRAG.BIN") == UMICOM_SHELL_OK);
        CHECK(strstr(transcript, "\\x1b") && strstr(transcript, "\\x00") && strstr(transcript, "\\xff"));
        CHECK(strchr(transcript, 27) == 0 && strstr(transcript, "disk.read=ok") != 0 && Allocated() == 2U);
    } else if (!strcmp(test, "console_invalid_arguments")) {
        UmicomKernelShellCommand command = {0}; UmicomBoolean handled = UMICOM_TRUE;
        CHECK(UmicomKernelShellParse("ls /", 4U, &command) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelDiskFilesystemCommand(&mountShell, &command, &handled) == UMICOM_SHELL_OK && !handled);
        const char *const invalid[] = {"mountdisk", "mountdisk 0", "mountdisk -1 0", "mountdisk 99 0",
            "mountdisk 0 4", "mountdisk a 0", "mountinfo extra", "unmountdisk extra", "diskls", "diskcat", "diskls / extra"};
        for (UmicomSize i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
            CHECK(Command(invalid[i]) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(Command("diskcat /README.TXT") == UMICOM_SHELL_IO_ERROR);
        CHECK(model.reads == 0U && model.writes == 0U && Allocated() == 0U);
        ClearTranscript(); ConsoleMount();
        CHECK(UmicomKernelDiskFilesystemConsoleClose(&foreignShell) == UMICOM_VFS_ACCESS_DENIED);
        CHECK(UmicomKernelShellParse("unmountdisk", 11U, &command) == UMICOM_SHELL_OK);
        CHECK(UmicomKernelDiskFilesystemCommand(&foreignShell, &command, &handled) == UMICOM_SHELL_BAD_STATE);
        CHECK(Allocated() == 2U && model.freeCalls == 0U);
    } else { fprintf(stderr, "unknown mount case: %s\n", test); exit(2); }
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const UmicomSize bytes = (UmicomSize)UMICOM_DISK_FIXTURE_SECTORS * 512U;
    umicomFilesystemImage = malloc(bytes); CHECK(umicomFilesystemImage);
    FILE *fixture = fopen(argv[2], "rb"); CHECK(fixture);
    CHECK(fread(umicomFilesystemImage, 1U, bytes, fixture) == bytes && fgetc(fixture) == EOF);
    CHECK(fclose(fixture) == 0);
    Start(); model.capacity = UMICOM_DISK_FIXTURE_SECTORS;
    domain.operations.clock = FilesystemClock;
    mountShell.output = Output; foreignShell.output = Output;
    RunMountCase(argv[1]);
    CHECK(UmicomKernelDiskFilesystemConsoleClose(&mountShell) == UMICOM_VFS_OK);
    MountFinish();
    CHECK(model.observedReadOnlyRequest || model.notifications == 0U);
    free(umicomFilesystemImage); return 0;
}
