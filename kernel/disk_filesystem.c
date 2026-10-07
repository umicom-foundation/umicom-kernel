/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/disk_filesystem.c
 *
 * Mount publication follows checked transport and filesystem admission.
 * Teardown follows the reverse dependency order: clients, VFS root pin,
 * interpretation, then device reset and DMA pages. A failed read must never
 * require a successful later read merely to release those dependencies.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/disk_filesystem.h"

static void UmicomDiskMountClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static UmicomBoolean UmicomDiskMountZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *const input = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (input[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomDiskMountDomainValid(const UmicomKernelBlockDomain *domain)
{
    return domain && domain->self == domain && domain->ready &&
        domain->operations.clock && domain->operations.allowed ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomDiskMountBlockStatus(UmicomKernelBlockStatus status)
{
    switch (status) {
    case UMICOM_BLOCK_OK: return UMICOM_VFS_OK;
    case UMICOM_BLOCK_INVALID_ARGUMENT: return UMICOM_VFS_INVALID_ARGUMENT;
    case UMICOM_BLOCK_UNSAFE_CONTEXT: return UMICOM_VFS_UNSAFE_CONTEXT;
    case UMICOM_BLOCK_BUSY: case UMICOM_BLOCK_ALREADY_ACTIVE: return UMICOM_VFS_BUSY;
    case UMICOM_BLOCK_NO_DEVICE: return UMICOM_VFS_NOT_FOUND;
    case UMICOM_BLOCK_NO_MEMORY: return UMICOM_VFS_NO_MEMORY;
    case UMICOM_BLOCK_RANGE: return UMICOM_VFS_RANGE;
    case UMICOM_BLOCK_WRITABLE_DEVICE: return UMICOM_VFS_READ_ONLY;
    case UMICOM_BLOCK_NOT_BLOCK: case UMICOM_BLOCK_UNSUPPORTED_TRANSPORT:
    case UMICOM_BLOCK_REQUIRED_FEATURE: case UMICOM_BLOCK_FEATURE_REFUSED:
    case UMICOM_BLOCK_UNSUPPORTED_REQUEST: case UMICOM_BLOCK_UNQUALIFIED_PLATFORM:
        return UMICOM_VFS_UNSUPPORTED;
    case UMICOM_BLOCK_CORRUPT_OWNER: return UMICOM_VFS_CORRUPT_STATE;
    case UMICOM_BLOCK_RELEASE_FAILED: case UMICOM_BLOCK_RESET_PENDING:
        return UMICOM_VFS_RELEASE_FAILED;
    default: return UMICOM_VFS_IO_ERROR;
    }
}
static UmicomKernelVfsStatus UmicomDiskMountBeginIo(void *context)
{
    UmicomKernelDiskMount *const mount = (UmicomKernelDiskMount *)context;
    if (!mount || mount->self != mount || !mount->handle ||
        (mount->state != UMICOM_VFS_UNUSED && mount->state != UMICOM_VFS_OPEN) ||
        !UmicomDiskMountDomainValid(mount->domain)) return UMICOM_VFS_BAD_STATE;
    if (!mount->domain->operations.allowed(mount->domain->operations.context)) {
        mount->lastBlockStatus = UMICOM_BLOCK_UNSAFE_CONTEXT;
        return UMICOM_VFS_UNSAFE_CONTEXT;
    }
    /* Reset at each inspector operation, never at mount time alone. A long
     * idle interval cannot consume the following read's time budget. */
    mount->operationStarted = mount->domain->operations.clock(mount->domain->operations.context);
    mount->operationClock = mount->operationStarted;
    mount->operationReads = 0U;
    mount->lastBlockStatus = UMICOM_BLOCK_OK;
    return UMICOM_VFS_OK;
}
static UmicomBoolean UmicomDiskMountReadSector(void *context, UmicomU64 sector,
    UmicomU8 *output)
{
    UmicomKernelDiskMount *const mount = (UmicomKernelDiskMount *)context;
    if (!mount || mount->self != mount || !mount->handle || !mount->provider.busy ||
        !output || !UmicomDiskMountDomainValid(mount->domain)) return UMICOM_FALSE;
    const UmicomU64 now = mount->domain->operations.clock(mount->domain->operations.context);
    if (now < mount->operationClock) {
        mount->lastBlockStatus = UMICOM_BLOCK_CLOCK_ERROR;
        return UMICOM_FALSE;
    }
    mount->operationClock = now;
    if (now - mount->operationStarted >= UMICOM_DISK_MOUNT_OPERATION_TICKS ||
        mount->operationReads >= UMICOM_FAT16_IO_LIMIT) {
        mount->lastBlockStatus = UMICOM_BLOCK_TIMEOUT;
        return UMICOM_FALSE;
    }
    ++mount->operationReads;
    mount->lastBlockStatus = UmicomKernelBlockRead(mount->domain, mount->handle,
        sector, 1U, output, UMICOM_DISK_SECTOR_BYTES);
    if (mount->lastBlockStatus != UMICOM_BLOCK_OK) return UMICOM_FALSE;
    /* The final sector has no following request to check its elapsed time.
     * Check completion too; bytes are still private inspector scratch here. */
    const UmicomU64 completed = mount->domain->operations.clock(mount->domain->operations.context);
    if (completed < mount->operationClock) {
        mount->lastBlockStatus = UMICOM_BLOCK_CLOCK_ERROR;
        return UMICOM_FALSE;
    }
    mount->operationClock = completed;
    if (completed - mount->operationStarted >= UMICOM_DISK_MOUNT_OPERATION_TICKS) {
        mount->lastBlockStatus = UMICOM_BLOCK_TIMEOUT;
        return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
static UmicomKernelVfsStatus UmicomDiskMountRelease(UmicomKernelDiskMount *mount)
{
    /* The caller already owns the mount reentry guard. Never enter this path
     * after a BUSY VFS unmount: that result preserves an active namespace. */
    if (mount->provider.state == UMICOM_VFS_OPEN) {
        const UmicomKernelVfsStatus status = UmicomKernelFat16ProviderClose(&mount->provider);
        if (status != UMICOM_VFS_OK) return status;
    }
    if (mount->handle) {
        mount->lastCleanupStatus = UmicomKernelBlockClose(mount->domain, mount->handle);
        if (mount->lastCleanupStatus != UMICOM_BLOCK_OK)
            return UmicomDiskMountBlockStatus(mount->lastCleanupStatus);
        mount->handle = 0U; /* Clear ownership only after acknowledged release. */
    }
    mount->state = mount->admitted ? UMICOM_VFS_CLOSED : UMICOM_VFS_UNUSED;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelDiskMountOpen(UmicomKernelDiskMount *mount,
    UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize partition,
    UmicomU64 timeoutTicks)
{
    if (!mount || !domain || slot >= UMICOM_BLOCK_SLOT_LIMIT ||
        partition >= UMICOM_DISK_PRIMARY_PARTITIONS || !timeoutTicks ||
        timeoutTicks > UMICOM_BLOCK_MAX_TIMEOUT_TICKS) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!UmicomDiskMountDomainValid(domain) || slot >= domain->count) return UMICOM_VFS_BAD_STATE;
    if (mount->busy || mount->provider.busy) return UMICOM_VFS_BUSY;
    if (!mount->self) {
        if (!UmicomDiskMountZero(mount, sizeof(*mount))) return UMICOM_VFS_BAD_STATE;
    } else if (mount->self != mount) return UMICOM_VFS_BAD_STATE;
    if (mount->state != UMICOM_VFS_UNUSED || mount->handle || mount->admitted ||
        !UmicomDiskMountZero(&mount->vfs, sizeof(mount->vfs)) ||
        mount->provider.self || mount->provider.state != UMICOM_VFS_UNUSED)
        return UMICOM_VFS_BAD_STATE;
    mount->busy = UMICOM_TRUE;
    if (!domain->operations.allowed(domain->operations.context)) {
        mount->busy = UMICOM_FALSE;
        return UMICOM_VFS_UNSAFE_CONTEXT;
    }
    mount->self = mount;
    mount->domain = domain;
    mount->slot = slot;
    mount->partition = partition;
    mount->timeoutTicks = timeoutTicks;
    mount->lastCleanupStatus = UMICOM_BLOCK_OK;
    mount->lastBlockStatus = UmicomKernelBlockOpen(domain, slot, timeoutTicks, &mount->handle);
    UmicomKernelVfsStatus status = UmicomDiskMountBlockStatus(mount->lastBlockStatus);
    /* Even failed BlockOpen can return a real cleanup lease. Its output lives
     * inside the final owner throughout admission, never in temporary storage. */
    if (status == UMICOM_VFS_OK) {
        UmicomKernelBlockInfo info;
        UmicomDiskMountClear(&info, sizeof(info));
        mount->lastBlockStatus = UmicomKernelBlockProbe(domain, slot, &info);
        status = UmicomDiskMountBlockStatus(mount->lastBlockStatus);
        if (status == UMICOM_VFS_OK) {
            const UmicomKernelDiskReader reader = {info.sectors, UmicomDiskMountReadSector, mount};
            const UmicomKernelFat16ProviderIoPolicy policy = {UmicomDiskMountBeginIo, mount};
            status = UmicomKernelFat16ProviderOpen(&mount->provider, &reader, partition, &policy);
            if (status == UMICOM_VFS_OK) {
                mount->admitted = UMICOM_TRUE;
                status = UmicomKernelVfsMount(&mount->vfs,
                    UmicomKernelFat16ProviderOperationsGet(), &mount->provider);
            }
        }
    }
    mount->lastStatus = status;
    if (status == UMICOM_VFS_OK) mount->state = UMICOM_VFS_OPEN;
    else {
        /* Retire an accepted interpretation before asking its transport to
         * reset. A later close can retry without reconstructing any parser. */
        mount->state = UMICOM_VFS_CLOSING;
        (void)UmicomDiskMountRelease(mount);
    }
    mount->busy = UMICOM_FALSE;
    return status; /* Primary admission status survives a separate close error. */
}
UmicomKernelVfsStatus UmicomKernelDiskMountClose(UmicomKernelDiskMount *mount)
{
    if (!mount) return UMICOM_VFS_INVALID_ARGUMENT;
    if (mount->busy || mount->provider.busy) return UMICOM_VFS_BUSY;
    if (!mount->self) return UmicomDiskMountZero(mount, sizeof(*mount)) ?
        UMICOM_VFS_OK : UMICOM_VFS_BAD_STATE;
    if (mount->self != mount) return UMICOM_VFS_BAD_STATE;
    if (mount->state == UMICOM_VFS_CLOSED || mount->state == UMICOM_VFS_UNUSED) {
        if (mount->handle || mount->vfs.clients) return UMICOM_VFS_CORRUPT_STATE;
        return UMICOM_VFS_OK;
    }
    if ((mount->state != UMICOM_VFS_OPEN && mount->state != UMICOM_VFS_CLOSING) ||
        !UmicomDiskMountDomainValid(mount->domain)) return UMICOM_VFS_BAD_STATE;
    mount->busy = UMICOM_TRUE;
    if (!mount->domain->operations.allowed(mount->domain->operations.context)) {
        mount->busy = UMICOM_FALSE;
        return UMICOM_VFS_UNSAFE_CONTEXT;
    }
    if (mount->vfs.state == UMICOM_VFS_OPEN) {
        /* A trusted additional VFS mount or direct node pin also depends on
         * this provider. Refuse before retiring our root, preserving the same
         * non-destructive BUSY guarantee as an attached client. */
        const UmicomKernelVfsStatus valid =
            UmicomKernelFat16ProviderOperationsGet()->validate(&mount->provider);
        if (valid != UMICOM_VFS_OK || mount->provider.pins != 1U) {
            mount->busy = UMICOM_FALSE;
            if (valid != UMICOM_VFS_OK) return valid;
            return mount->provider.pins ? UMICOM_VFS_BUSY : UMICOM_VFS_CORRUPT_STATE;
        }
        const UmicomKernelVfsStatus status = UmicomKernelVfsUnmount(&mount->vfs);
        if (status != UMICOM_VFS_OK) {
            mount->busy = UMICOM_FALSE;
            return status; /* In particular, BUSY must not retire the provider. */
        }
    }
    mount->state = UMICOM_VFS_CLOSING;
    const UmicomKernelVfsStatus status = UmicomDiskMountRelease(mount);
    mount->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelVfsStatus UmicomKernelDiskMountClientOpen(UmicomKernelDiskMount *mount,
    UmicomKernelVfsClient *client, UmicomU64 principal, UmicomKernelVfsRights rights)
{
    if (!mount || !client || !principal || (rights & ~UMICOM_VFS_RIGHT_ALL))
        return UMICOM_VFS_INVALID_ARGUMENT;
    if (mount->self != mount || mount->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    if (mount->busy || mount->provider.busy) return UMICOM_VFS_BUSY;
    if (rights & ~UMICOM_DISK_MOUNT_READ_RIGHTS) return UMICOM_VFS_ACCESS_DENIED;
    return UmicomKernelVfsClientOpen(client, &mount->vfs, principal, rights);
}
