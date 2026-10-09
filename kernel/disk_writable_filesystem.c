/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_writable_filesystem.c
 *
 * Publish the VFS only after the exclusive writable/FLUSH lease and provider
 * are ready. Teardown follows the reverse dependency order and never submits
 * a filesystem mutation. A retained close obligation remains in this owner.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_writable_filesystem.h"
#include "umicom/kernel/physical_memory.h"

static UmicomBoolean UmicomKernelDiskWritableZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *const input = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (input[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomKernelDiskWritableSpan(UmicomAddress address, UmicomSize bytes)
{
    return address && bytes && address <= ~(UmicomAddress)0U - bytes ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelDiskWritableOverlap(UmicomAddress left, UmicomSize leftBytes,
    UmicomAddress right, UmicomSize rightBytes)
{
    if (!leftBytes || !rightBytes) return UMICOM_FALSE;
    return left <= right ? right - left < leftBytes : left - right < rightBytes;
}
static UmicomBoolean UmicomKernelDiskWritableStorage(const UmicomKernelDiskWritableMount *mount)
{
    return mount && !((UmicomAddress)mount % alignof(UmicomKernelDiskWritableMount)) &&
        UmicomKernelDiskWritableSpan((UmicomAddress)mount, sizeof(*mount)) ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelDiskWritableDomainValid(const UmicomKernelBlockDomain *domain)
{
    return domain && !((UmicomAddress)domain % alignof(UmicomKernelBlockDomain)) &&
        UmicomKernelDiskWritableSpan((UmicomAddress)domain, sizeof(*domain)) &&
        domain->self == domain && domain->ready == UMICOM_TRUE && domain->count &&
        domain->count <= UMICOM_BLOCK_SLOT_LIMIT && domain->operations.allowed &&
        domain->operations.clock && domain->operations.read32 && domain->operations.write32 &&
        domain->operations.barrier ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomKernelDiskWritableDomainIndependent(const UmicomKernelBlockDomain *domain,
    UmicomAddress address, UmicomSize bytes)
{
    if (!UmicomKernelDiskWritableSpan(address, bytes) ||
        UmicomKernelDiskWritableOverlap(address, bytes, (UmicomAddress)domain, sizeof(*domain)))
        return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        const UmicomKernelBlockSlot *const slot = &domain->slots[i];
        if ((slot->queueFrame && UmicomKernelDiskWritableOverlap(address, bytes,
                slot->queueFrame, UMICOM_KERNEL_PAGE_SIZE)) ||
            (slot->dataFrame && UmicomKernelDiskWritableOverlap(address, bytes,
                slot->dataFrame, UMICOM_KERNEL_PAGE_SIZE))) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
static UmicomKernelVfsStatus UmicomKernelDiskWritableResult(
    const UmicomKernelDiskWritableMount *mount, UmicomKernelFat16UpdateStatus status)
{
    switch (status) {
    case UMICOM_FAT16_UPDATE_OK: return UMICOM_VFS_OK;
    case UMICOM_FAT16_UPDATE_INVALID_ARGUMENT: return UMICOM_VFS_INVALID_ARGUMENT;
    case UMICOM_FAT16_UPDATE_BAD_STATE: return UMICOM_VFS_BAD_STATE;
    case UMICOM_FAT16_UPDATE_BUSY: return UMICOM_VFS_BUSY;
    case UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT: return UMICOM_VFS_UNSAFE_CONTEXT;
    case UMICOM_FAT16_UPDATE_READ_ONLY: return UMICOM_VFS_READ_ONLY;
    case UMICOM_FAT16_UPDATE_RANGE: return UMICOM_VFS_RANGE;
    case UMICOM_FAT16_UPDATE_INSPECTION_LIMIT: return UMICOM_VFS_INSPECTION_LIMIT;
    case UMICOM_FAT16_UPDATE_RELEASE_FAILED: return UMICOM_VFS_RELEASE_FAILED;
    case UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR:
        switch (mount->lifecycle.commit.updater.lastDiskStatus) {
        case UMICOM_DISK_NOT_FOUND: return UMICOM_VFS_NOT_FOUND;
        case UMICOM_DISK_UNSUPPORTED_TABLE:
        case UMICOM_DISK_UNSUPPORTED_FILESYSTEM: return UMICOM_VFS_UNSUPPORTED;
        case UMICOM_DISK_READ_ONLY: return UMICOM_VFS_READ_ONLY;
        default: return UMICOM_VFS_CORRUPT_FILESYSTEM;
        }
    case UMICOM_FAT16_UPDATE_TRANSPORT_ERROR:
        switch (mount->lifecycle.commit.updater.lastBlockStatus) {
        case UMICOM_BLOCK_NO_DEVICE: return UMICOM_VFS_NOT_FOUND;
        case UMICOM_BLOCK_NO_MEMORY: return UMICOM_VFS_NO_MEMORY;
        case UMICOM_BLOCK_NOT_BLOCK:
        case UMICOM_BLOCK_UNSUPPORTED_TRANSPORT:
        case UMICOM_BLOCK_REQUIRED_FEATURE:
        case UMICOM_BLOCK_FEATURE_REFUSED:
        case UMICOM_BLOCK_UNSUPPORTED_REQUEST:
        case UMICOM_BLOCK_UNQUALIFIED_PLATFORM: return UMICOM_VFS_UNSUPPORTED;
        default: return UMICOM_VFS_IO_ERROR;
        }
    case UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN: return UMICOM_VFS_IO_ERROR;
    default: return UMICOM_VFS_CORRUPT_STATE;
    }
}
static UmicomBoolean UmicomKernelDiskWritableBusy(const UmicomKernelDiskWritableMount *mount)
{
    return mount->busy || mount->provider.busy || mount->lifecycle.busy ||
        mount->lifecycle.commit.busy || mount->lifecycle.commit.updater.busy ||
        mount->lifecycle.workspace.busy ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomKernelDiskWritableRelease(UmicomKernelDiskWritableMount *mount)
{
    if (mount->provider.state == UMICOM_VFS_OPEN) {
        const UmicomKernelVfsStatus status = UmicomKernelFat16WritableProviderClose(&mount->provider);
        if (status != UMICOM_VFS_OK) return status;
    }
    const UmicomKernelFat16UpdateStatus released = UmicomKernelFat16LifecycleClose(&mount->lifecycle);
    const UmicomKernelVfsStatus status = UmicomKernelDiskWritableResult(mount, released);
    if (status == UMICOM_VFS_OK) mount->state = UMICOM_VFS_CLOSED;
    return status;
}

UmicomKernelVfsStatus UmicomKernelDiskWritableMountOpen(
    UmicomKernelDiskWritableMount *mount, UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks,
    const UmicomKernelFat16FileTime *time)
{
    if (!UmicomKernelDiskWritableStorage(mount) || !time ||
        (UmicomAddress)time % alignof(UmicomKernelFat16FileTime) ||
        !UmicomKernelDiskWritableSpan((UmicomAddress)time, sizeof(*time)) ||
        slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS ||
        !timeoutTicks || timeoutTicks > UMICOM_BLOCK_MAX_TIMEOUT_TICKS) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!UmicomKernelDiskWritableDomainValid(domain) || slot >= domain->count) return UMICOM_VFS_BAD_STATE;
    if (!UmicomKernelDiskWritableDomainIndependent(domain, (UmicomAddress)mount, sizeof(*mount)) ||
        !UmicomKernelDiskWritableDomainIndependent(domain, (UmicomAddress)time, sizeof(*time)) ||
        UmicomKernelDiskWritableOverlap((UmicomAddress)time, sizeof(*time),
            (UmicomAddress)mount, sizeof(*mount))) return UMICOM_VFS_INVALID_ARGUMENT;
    if (UmicomKernelDiskWritableBusy(mount) || domain->busy) return UMICOM_VFS_BUSY;
    if (!UmicomKernelDiskWritableZero(mount, sizeof(*mount))) return UMICOM_VFS_BAD_STATE;
    UmicomKernelFat16FileTimeEncoding encoded;
    if (UmicomKernelFat16FileTimeEncode(time, &encoded) != UMICOM_DISK_OK) return UMICOM_VFS_INVALID_ARGUMENT;
    /* Snapshot caller intent before transport admission invokes callbacks. */
    const UmicomKernelFat16FileTime copiedTime = *time;
    mount->self = mount;
    mount->busy = UMICOM_TRUE;
    mount->admitted = UMICOM_TRUE;
    mount->state = UMICOM_VFS_CLOSING;
    const UmicomKernelFat16UpdateStatus opened = UmicomKernelFat16LifecycleOpen(&mount->lifecycle,
        domain, slot, partition, timeoutTicks);
    UmicomKernelVfsStatus status = UmicomKernelDiskWritableResult(mount, opened);
    if (status == UMICOM_VFS_OK)
        status = UmicomKernelFat16WritableProviderOpen(&mount->provider, &mount->lifecycle, &copiedTime);
    if (status == UMICOM_VFS_OK)
        status = UmicomKernelVfsMount(&mount->vfs, UmicomKernelFat16WritableProviderOperationsGet(),
            &mount->provider);
    mount->lastStatus = status;
    if (status == UMICOM_VFS_OK) mount->state = UMICOM_VFS_OPEN;
    else (void)UmicomKernelDiskWritableRelease(mount);
    mount->busy = UMICOM_FALSE;
    return status; /* Preserve the admission failure separately from cleanup. */
}
UmicomKernelVfsStatus UmicomKernelDiskWritableMountClientOpen(
    UmicomKernelDiskWritableMount *mount, UmicomKernelVfsClient *client,
    UmicomU64 principal, UmicomKernelVfsRights rights)
{
    if (!UmicomKernelDiskWritableStorage(mount) || !client ||
        (UmicomAddress)client % alignof(UmicomKernelVfsClient) || !principal ||
        (rights & ~UMICOM_VFS_RIGHT_ALL)) return UMICOM_VFS_INVALID_ARGUMENT;
    if (UmicomKernelDiskWritableBusy(mount)) return UMICOM_VFS_BUSY;
    if (mount->self != mount || mount->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    const UmicomKernelBlockDomain *const domain = mount->lifecycle.commit.updater.domain;
    if (!UmicomKernelDiskWritableDomainValid(domain)) return UMICOM_VFS_BAD_STATE;
    if (!UmicomKernelDiskWritableDomainIndependent(domain, (UmicomAddress)client, sizeof(*client)) ||
        UmicomKernelDiskWritableOverlap((UmicomAddress)client, sizeof(*client),
            (UmicomAddress)mount, sizeof(*mount))) return UMICOM_VFS_INVALID_ARGUMENT;
    if (mount->provider.mediaFailed) return UMICOM_VFS_IO_ERROR;
    mount->busy = UMICOM_TRUE;
    const UmicomKernelVfsStatus status = UmicomKernelVfsClientOpen(client, &mount->vfs, principal, rights);
    mount->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelVfsStatus UmicomKernelDiskWritableMountSetTime(
    UmicomKernelDiskWritableMount *mount, const UmicomKernelFat16FileTime *time)
{
    if (!UmicomKernelDiskWritableStorage(mount)) return UMICOM_VFS_INVALID_ARGUMENT;
    if (UmicomKernelDiskWritableBusy(mount)) return UMICOM_VFS_BUSY;
    if (mount->self != mount || mount->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    if (!time || !UmicomKernelDiskWritableSpan((UmicomAddress)time, sizeof(*time)) ||
        UmicomKernelDiskWritableOverlap((UmicomAddress)time, sizeof(*time),
            (UmicomAddress)mount, sizeof(*mount))) return UMICOM_VFS_INVALID_ARGUMENT;
    mount->busy = UMICOM_TRUE;
    const UmicomKernelVfsStatus status = UmicomKernelFat16WritableProviderSetTime(&mount->provider, time);
    mount->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelVfsStatus UmicomKernelDiskWritableMountClose(UmicomKernelDiskWritableMount *mount)
{
    if (!UmicomKernelDiskWritableStorage(mount)) return UMICOM_VFS_INVALID_ARGUMENT;
    if (UmicomKernelDiskWritableBusy(mount)) return UMICOM_VFS_BUSY;
    if (!mount->self) return UmicomKernelDiskWritableZero(mount, sizeof(*mount)) ?
        UMICOM_VFS_OK : UMICOM_VFS_BAD_STATE;
    if (mount->self != mount) return UMICOM_VFS_BAD_STATE;
    if (mount->state == UMICOM_VFS_CLOSED) return mount->lifecycle.commit.updater.handle ||
        mount->provider.pins || mount->vfs.clients ? UMICOM_VFS_CORRUPT_STATE : UMICOM_VFS_OK;
    if (mount->state != UMICOM_VFS_OPEN && mount->state != UMICOM_VFS_CLOSING) return UMICOM_VFS_BAD_STATE;
    mount->busy = UMICOM_TRUE;
    if (mount->vfs.state == UMICOM_VFS_OPEN) {
        const UmicomKernelVfsStatus valid = UmicomKernelFat16WritableProviderOperationsGet()->validate(
            &mount->provider);
        if (valid != UMICOM_VFS_OK || mount->provider.pins != 1U) {
            mount->busy = UMICOM_FALSE;
            if (valid != UMICOM_VFS_OK) return valid;
            return mount->provider.pins ? UMICOM_VFS_BUSY : UMICOM_VFS_CORRUPT_STATE;
        }
        const UmicomKernelVfsStatus status = UmicomKernelVfsUnmount(&mount->vfs);
        if (status != UMICOM_VFS_OK) {
            mount->busy = UMICOM_FALSE;
            return status;
        }
    }
    mount->state = UMICOM_VFS_CLOSING;
    const UmicomKernelVfsStatus status = UmicomKernelDiskWritableRelease(mount);
    mount->busy = UMICOM_FALSE;
    return status;
}
