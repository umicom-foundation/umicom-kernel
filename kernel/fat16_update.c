/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_update.c
 *
 * Keep interpretation, mutation evidence and transport cleanup separate.
 * A complete private sector plan precedes every WRITE. Publishing a later
 * sector can fail after an earlier sector changed: no error path invents a
 * rollback, repeats file data, or treats reset as a durability acknowledgement.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_update.h"
#include "umicom/kernel/physical_memory.h"

static void UmicomUpdateClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = 0U;
}
static void UmicomUpdateCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *const output = (volatile UmicomU8 *)target;
    const volatile UmicomU8 *const input = (const volatile UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) output[i] = input[i];
}
static UmicomBoolean UmicomUpdateZero(const void *target, UmicomSize bytes)
{
    const UmicomU8 *const input = (const UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) if (input[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomUpdateSpan(UmicomAddress address, UmicomSize bytes)
{
    return address && bytes && address <= ~(UmicomAddress)0U - bytes ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomUpdateOverlap(UmicomAddress left, UmicomSize leftBytes,
    UmicomAddress right, UmicomSize rightBytes)
{
    if (!leftBytes || !rightBytes) return UMICOM_FALSE;
    return left <= right ? right - left < leftBytes : left - right < rightBytes;
}
static UmicomBoolean UmicomUpdateDomainValid(const UmicomKernelBlockDomain *domain)
{
    return domain && !((UmicomAddress)domain % alignof(UmicomKernelBlockDomain)) &&
        UmicomUpdateSpan((UmicomAddress)domain, sizeof(*domain)) &&
        domain->self == domain && domain->ready && domain->count &&
        domain->count <= UMICOM_BLOCK_SLOT_LIMIT && domain->operations.clock &&
        domain->operations.allowed && domain->operations.read32 &&
        domain->operations.write32 && domain->operations.barrier ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomUpdateDomainIndependent(const UmicomKernelBlockDomain *domain,
    UmicomAddress address, UmicomSize bytes)
{
    if (!UmicomUpdateSpan(address, bytes) ||
        UmicomUpdateOverlap(address, bytes, (UmicomAddress)domain, sizeof(*domain))) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < domain->count; ++i) {
        const UmicomKernelBlockSlot *const slot = &domain->slots[i];
        if ((slot->queueFrame && UmicomUpdateOverlap(address, bytes,
                slot->queueFrame, UMICOM_KERNEL_PAGE_SIZE)) ||
            (slot->dataFrame && UmicomUpdateOverlap(address, bytes,
                slot->dataFrame, UMICOM_KERNEL_PAGE_SIZE))) return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomUpdateIndependent(const UmicomKernelFat16Updater *owner,
    UmicomAddress address, UmicomSize bytes)
{
    return UmicomUpdateDomainIndependent(owner->domain, address, bytes) &&
        !UmicomUpdateOverlap(address, bytes, (UmicomAddress)owner, sizeof(*owner));
}
static UmicomKernelFat16UpdateStatus UmicomUpdateFromBlock(UmicomKernelBlockStatus status)
{
    switch (status) {
    case UMICOM_BLOCK_OK: return UMICOM_FAT16_UPDATE_OK;
    case UMICOM_BLOCK_INVALID_ARGUMENT: return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    case UMICOM_BLOCK_BAD_STATE: case UMICOM_BLOCK_INVALID_HANDLE: case UMICOM_BLOCK_CORRUPT_OWNER:
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    case UMICOM_BLOCK_BUSY: case UMICOM_BLOCK_ALREADY_ACTIVE: return UMICOM_FAT16_UPDATE_BUSY;
    case UMICOM_BLOCK_UNSAFE_CONTEXT: return UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT;
    case UMICOM_BLOCK_READ_ONLY: return UMICOM_FAT16_UPDATE_READ_ONLY;
    case UMICOM_BLOCK_RANGE: return UMICOM_FAT16_UPDATE_RANGE;
    case UMICOM_BLOCK_RELEASE_FAILED: case UMICOM_BLOCK_RESET_PENDING:
        return UMICOM_FAT16_UPDATE_RELEASE_FAILED;
    default: return UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
    }
}
static UmicomKernelFat16UpdateStatus UmicomUpdateFromDisk(const UmicomKernelFat16Updater *owner,
    UmicomKernelDiskStatus status)
{
    switch (status) {
    case UMICOM_DISK_OK: return UMICOM_FAT16_UPDATE_OK;
    case UMICOM_DISK_INVALID_ARGUMENT: return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    case UMICOM_DISK_BAD_STATE: return UMICOM_FAT16_UPDATE_BAD_STATE;
    case UMICOM_DISK_BUSY: return UMICOM_FAT16_UPDATE_BUSY;
    case UMICOM_DISK_READ_ONLY: return UMICOM_FAT16_UPDATE_READ_ONLY;
    case UMICOM_DISK_RANGE: return UMICOM_FAT16_UPDATE_RANGE;
    case UMICOM_DISK_LIMIT: return UMICOM_FAT16_UPDATE_INSPECTION_LIMIT;
    case UMICOM_DISK_IO_ERROR:
        return owner->lastBlockStatus == UMICOM_BLOCK_OK ? UMICOM_FAT16_UPDATE_TRANSPORT_ERROR :
            UmicomUpdateFromBlock(owner->lastBlockStatus);
    default: return UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR;
    }
}
static UmicomKernelFat16UpdateStatus UmicomUpdateLive(UmicomKernelFat16Updater *owner)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16Updater) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner))) return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->self != owner) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->busy || owner->volume.busy || owner->workspace.busy) return UMICOM_FAT16_UPDATE_BUSY;
    if (owner->state != UMICOM_FAT16_UPDATER_OPEN || !owner->handle || !owner->admitted ||
        !UmicomUpdateDomainValid(owner->domain) || owner->slot >= owner->domain->count ||
        !UmicomUpdateDomainIndependent(owner->domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    return owner->domain->busy ? UMICOM_FAT16_UPDATE_BUSY : UMICOM_FAT16_UPDATE_OK;
}
static UmicomKernelFat16UpdateStatus UmicomUpdateEnter(UmicomKernelFat16Updater *owner)
{
    /* Establish ownership before invoking even the policy callback. A callback
     * can observe BUSY, but cannot close or overwrite this active operation. */
    owner->busy = UMICOM_TRUE;
    if (!owner->domain->operations.allowed(owner->domain->operations.context)) {
        owner->busy = UMICOM_FALSE;
        return UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT;
    }
    return UMICOM_FAT16_UPDATE_OK;
}
static void UmicomUpdateBeginIo(UmicomKernelFat16Updater *owner)
{
    owner->operationStarted = owner->domain->operations.clock(owner->domain->operations.context);
    owner->operationClock = owner->operationStarted;
    owner->operationReads = 0U;
    owner->lastBlockStatus = UMICOM_BLOCK_OK;
    owner->lastDiskStatus = UMICOM_DISK_OK;
}
static UmicomBoolean UmicomUpdateClock(UmicomKernelFat16Updater *owner)
{
    const UmicomU64 now = owner->domain->operations.clock(owner->domain->operations.context);
    if (now < owner->operationClock) {
        owner->lastBlockStatus = UMICOM_BLOCK_CLOCK_ERROR;
        return UMICOM_FALSE;
    }
    owner->operationClock = now;
    if (now - owner->operationStarted >= UMICOM_FAT16_UPDATE_OPERATION_TICKS) {
        owner->lastBlockStatus = UMICOM_BLOCK_TIMEOUT;
        return UMICOM_FALSE;
    }
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomUpdateReadSector(void *context, UmicomU64 sector, UmicomU8 *output)
{
    UmicomKernelFat16Updater *const owner = (UmicomKernelFat16Updater *)context;
    if (!owner || owner->self != owner || !owner->busy || !owner->handle || !output ||
        !UmicomUpdateDomainValid(owner->domain)) return UMICOM_FALSE;
    if (!UmicomUpdateClock(owner)) return UMICOM_FALSE;
    if (owner->operationReads >= UMICOM_FAT16_IO_LIMIT) {
        owner->lastBlockStatus = UMICOM_BLOCK_TIMEOUT;
        return UMICOM_FALSE;
    }
    ++owner->operationReads;
    owner->lastBlockStatus = UmicomKernelBlockRead(owner->domain, owner->handle,
        sector, 1U, output, UMICOM_DISK_SECTOR_BYTES);
    if (owner->lastBlockStatus != UMICOM_BLOCK_OK) return UMICOM_FALSE;
    /* All bytes remain private scratch until the final completion deadline has
     * also passed. A last-sector timeout therefore cannot start a file write. */
    return UmicomUpdateClock(owner);
}
static UmicomKernelFat16UpdateStatus UmicomUpdateInspectorClose(UmicomKernelFat16Updater *owner,
    UmicomKernelFat16UpdateStatus status)
{
    if (owner->volume.open) {
        const UmicomKernelDiskStatus closed = UmicomKernelFat16Close(&owner->volume);
        if (status == UMICOM_FAT16_UPDATE_OK && closed != UMICOM_DISK_OK) {
            owner->lastDiskStatus = closed;
            return UmicomUpdateFromDisk(owner, closed);
        }
    }
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomUpdateRelease(UmicomKernelFat16Updater *owner)
{
    const UmicomKernelFat16UpdateStatus inspected =
        UmicomUpdateInspectorClose(owner, UMICOM_FAT16_UPDATE_OK);
    if (inspected != UMICOM_FAT16_UPDATE_OK) return inspected;
    UmicomUpdateClear(&owner->plan, sizeof(owner->plan));
    if (owner->handle) {
        owner->lastCleanupStatus = UmicomKernelBlockClose(owner->domain, owner->handle);
        if (owner->lastCleanupStatus != UMICOM_BLOCK_OK)
            return UmicomUpdateFromBlock(owner->lastCleanupStatus);
        owner->handle = 0U;
    }
    owner->state = owner->admitted ? UMICOM_FAT16_UPDATER_CLOSED : UMICOM_FAT16_UPDATER_UNUSED;
    return UMICOM_FAT16_UPDATE_OK;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateOpen(UmicomKernelFat16Updater *owner,
    UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16Updater) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)) ||
        slot >= UMICOM_BLOCK_SLOT_LIMIT || partition >= UMICOM_DISK_PRIMARY_PARTITIONS ||
        !timeoutTicks || timeoutTicks > UMICOM_BLOCK_MAX_TIMEOUT_TICKS)
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (!UmicomUpdateDomainValid(domain) || slot >= domain->count) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (!UmicomUpdateDomainIndependent(domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->busy || owner->volume.busy || owner->workspace.busy || domain->busy)
        return UMICOM_FAT16_UPDATE_BUSY;
    if ((!owner->self && !UmicomUpdateZero(owner, sizeof(*owner))) ||
        (owner->self && owner->self != owner) || owner->state != UMICOM_FAT16_UPDATER_UNUSED ||
        owner->handle || owner->admitted) return UMICOM_FAT16_UPDATE_BAD_STATE;
    owner->busy = UMICOM_TRUE;
    if (!domain->operations.allowed(domain->operations.context)) {
        owner->busy = UMICOM_FALSE;
        return UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT;
    }
    owner->self = owner;
    owner->domain = domain;
    owner->slot = slot;
    owner->partition = partition;
    owner->timeoutTicks = timeoutTicks;
    owner->lastCleanupStatus = UMICOM_BLOCK_OK;
    UmicomUpdateBeginIo(owner);
    owner->lastBlockStatus = UmicomKernelBlockOpenWritable(domain, slot, timeoutTicks, &owner->handle);
    UmicomKernelFat16UpdateStatus status = UmicomUpdateFromBlock(owner->lastBlockStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        UmicomKernelBlockInfo info;
        UmicomUpdateClear(&info, sizeof(info));
        owner->lastBlockStatus = UmicomKernelBlockProbe(domain, slot, &info);
        status = UmicomUpdateFromBlock(owner->lastBlockStatus);
        if (status == UMICOM_FAT16_UPDATE_OK) {
            owner->sectors = info.sectors;
            const UmicomKernelDiskReader reader = {info.sectors, UmicomUpdateReadSector, owner};
            owner->lastDiskStatus = UmicomKernelFat16Open(&owner->volume, &reader, partition);
            status = UmicomUpdateFromDisk(owner, owner->lastDiskStatus);
            if (status == UMICOM_FAT16_UPDATE_OK)
                UmicomUpdateCopy(&owner->info, &owner->volume.info, sizeof(owner->info));
        }
    }
    status = UmicomUpdateInspectorClose(owner, status);
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(owner))
        status = UmicomUpdateFromBlock(owner->lastBlockStatus);
    owner->lastStatus = status;
    if (status == UMICOM_FAT16_UPDATE_OK) {
        owner->admitted = UMICOM_TRUE;
        owner->state = UMICOM_FAT16_UPDATER_OPEN;
    } else {
        owner->state = UMICOM_FAT16_UPDATER_CLOSING;
        (void)UmicomUpdateRelease(owner);
    }
    owner->busy = UMICOM_FALSE;
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomUpdateBuffers(UmicomKernelFat16Updater *owner,
    const char *path, const void *input, UmicomSize bytes, UmicomKernelFat16UpdateResult *outResult)
{
    if (!path || !input || !bytes || bytes > UMICOM_FAT16_UPDATE_BYTES || !outResult ||
        (UmicomAddress)outResult % alignof(UmicomKernelFat16UpdateResult) ||
        !UmicomUpdateIndependent(owner, (UmicomAddress)input, bytes) ||
        !UmicomUpdateIndependent(owner, (UmicomAddress)outResult, sizeof(*outResult)) ||
        UmicomUpdateOverlap((UmicomAddress)input, bytes, (UmicomAddress)outResult, sizeof(*outResult)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    UmicomSize pathBytes = 0U;
    for (; pathBytes < UMICOM_FAT16_PATH_BYTES; ++pathBytes) {
        const UmicomAddress address = (UmicomAddress)path;
        if (address > ~(UmicomAddress)0U - pathBytes ||
            !UmicomUpdateIndependent(owner, address + pathBytes, 1U) ||
            UmicomUpdateOverlap(address + pathBytes, 1U, (UmicomAddress)input, bytes) ||
            UmicomUpdateOverlap(address + pathBytes, 1U, (UmicomAddress)outResult, sizeof(*outResult)))
            return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
        /* Refuse each protected byte before looking for NUL. An invalid path
         * into a non-terminated result must not scan past that result object. */
        if (!path[pathBytes]) { ++pathBytes; break; }
    }
    if (!pathBytes || pathBytes > UMICOM_FAT16_PATH_BYTES || path[pathBytes - 1U])
        return UMICOM_FAT16_UPDATE_INSPECTION_LIMIT;
    if (UmicomUpdateOverlap((UmicomAddress)path, pathBytes, (UmicomAddress)input, bytes) ||
        UmicomUpdateOverlap((UmicomAddress)path, pathBytes, (UmicomAddress)outResult, sizeof(*outResult)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    return UMICOM_FAT16_UPDATE_OK;
}
static void UmicomUpdateObserve(UmicomKernelFat16Updater *owner, UmicomKernelFat16UpdateResult *result,
    const UmicomKernelFat16UpdateSector *sector, UmicomKernelBlockMutationOutcome outcome)
{
    result->lastBlockOutcome = outcome;
    if (outcome == UMICOM_BLOCK_NOT_SUBMITTED) return;
    ++result->submittedSectors;
    result->submittedBytes += sector->bytes;
    owner->needsFlush = UMICOM_TRUE;
    if (outcome == UMICOM_BLOCK_COMPLETED) {
        ++result->completedSectors;
        result->confirmedBytes += sector->bytes;
    } else {
        owner->writeUncertain = UMICOM_TRUE;
        result->uncertainOffset = result->offset + sector->inputOffset;
        result->uncertainBytes = sector->bytes;
        result->uncertainSector = sector->sector;
    }
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateWrite(UmicomKernelFat16Updater *owner,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    UmicomKernelFat16UpdateResult *outResult)
{
    UmicomKernelFat16UpdateStatus status = UmicomUpdateLive(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomUpdateBuffers(owner, path, input, bytes, outResult);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    /* Preserve the precise earlier uncertainty record when refusing a retry. */
    if (owner->writeUncertain) return UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN;
    status = UmicomUpdateEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomKernelFat16UpdateResult result;
    UmicomUpdateClear(&result, sizeof(result));
    result.offset = offset;
    result.requestedBytes = bytes;
    UmicomUpdateBeginIo(owner);
    const UmicomKernelDiskReader reader = {owner->sectors, UmicomUpdateReadSector, owner};
    owner->lastDiskStatus = UmicomKernelFat16Open(&owner->volume, &reader, owner->partition);
    status = UmicomUpdateFromDisk(owner, owner->lastDiskStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        owner->lastDiskStatus = UmicomKernelFat16PlanUpdate(&owner->volume, path, offset, input,
            bytes, &owner->workspace, &owner->plan);
        status = UmicomUpdateFromDisk(owner, owner->lastDiskStatus);
    }
    /* The inspector's lifetime ends before any data changes. It is reopened
     * and the complete allocation proof repeated for a later admitted write. */
    status = UmicomUpdateInspectorClose(owner, status);
    for (UmicomSize i = 0U; status == UMICOM_FAT16_UPDATE_OK && i < owner->plan.count; ++i) {
        if (!UmicomUpdateClock(owner)) {
            status = UmicomUpdateFromBlock(owner->lastBlockStatus);
            break;
        }
        const UmicomKernelFat16UpdateSector *const sector = &owner->plan.sectors[i];
        UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_NOT_SUBMITTED;
        owner->lastBlockStatus = UmicomKernelBlockWrite(owner->domain, owner->handle,
            sector->sector, 1U, sector->data, sizeof(sector->data), &outcome);
        UmicomUpdateObserve(owner, &result, sector, outcome);
        status = UmicomUpdateFromBlock(owner->lastBlockStatus);
        if (status == UMICOM_FAT16_UPDATE_OK && outcome != UMICOM_BLOCK_COMPLETED) {
            owner->lastBlockStatus = UMICOM_BLOCK_MALFORMED_COMPLETION;
            status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
        }
        if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(owner))
            status = UmicomUpdateFromBlock(owner->lastBlockStatus);
    }
    if (result.uncertainBytes) result.outcome = UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED;
    else if (result.confirmedBytes == bytes) result.outcome = UMICOM_FAT16_UPDATE_COMPLETED;
    else if (result.confirmedBytes) result.outcome = UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED;
    else result.outcome = UMICOM_FAT16_UPDATE_NOT_SUBMITTED;
    result.status = status;
    result.diskStatus = owner->lastDiskStatus;
    result.blockStatus = owner->lastBlockStatus;
    result.needsFlush = owner->needsFlush;
    result.writeUncertain = owner->writeUncertain;
    owner->lastStatus = status;
    UmicomUpdateCopy(&owner->lastWrite, &result, sizeof(result));
    UmicomUpdateCopy(outResult, &result, sizeof(result));
    UmicomUpdateClear(&owner->plan, sizeof(owner->plan));
    owner->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateFlush(UmicomKernelFat16Updater *owner,
    UmicomKernelBlockMutationOutcome *outOutcome)
{
    UmicomKernelFat16UpdateStatus status = UmicomUpdateLive(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    if (!outOutcome || (UmicomAddress)outOutcome % alignof(UmicomKernelBlockMutationOutcome) ||
        !UmicomUpdateIndependent(owner, (UmicomAddress)outOutcome, sizeof(*outOutcome)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    status = UmicomUpdateEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomUpdateBeginIo(owner);
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_NOT_SUBMITTED;
    if (UmicomUpdateClock(owner)) {
        owner->lastBlockStatus = UmicomKernelBlockFlush(owner->domain, owner->handle, &outcome);
        if (owner->lastBlockStatus == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED)
            owner->needsFlush = UMICOM_FALSE;
        status = UmicomUpdateFromBlock(owner->lastBlockStatus);
        if (status == UMICOM_FAT16_UPDATE_OK && outcome != UMICOM_BLOCK_COMPLETED) {
            owner->lastBlockStatus = UMICOM_BLOCK_MALFORMED_COMPLETION;
            status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
        }
        if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(owner))
            status = UmicomUpdateFromBlock(owner->lastBlockStatus);
    } else status = UmicomUpdateFromBlock(owner->lastBlockStatus);
    owner->lastFlush = outcome;
    owner->lastStatus = status;
    *outOutcome = outcome;
    owner->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16UpdateClose(UmicomKernelFat16Updater *owner)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16Updater) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner))) return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->busy || owner->volume.busy || owner->workspace.busy) return UMICOM_FAT16_UPDATE_BUSY;
    if (!owner->self) return UmicomUpdateZero(owner, sizeof(*owner)) ?
        UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->self != owner) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->state == UMICOM_FAT16_UPDATER_UNUSED || owner->state == UMICOM_FAT16_UPDATER_CLOSED)
        return owner->handle ? UMICOM_FAT16_UPDATE_BAD_STATE : UMICOM_FAT16_UPDATE_OK;
    if ((owner->state != UMICOM_FAT16_UPDATER_OPEN && owner->state != UMICOM_FAT16_UPDATER_CLOSING) ||
        !UmicomUpdateDomainValid(owner->domain) || owner->slot >= owner->domain->count ||
        !UmicomUpdateDomainIndependent(owner->domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->domain->busy) return UMICOM_FAT16_UPDATE_BUSY;
    UmicomKernelFat16UpdateStatus status = UmicomUpdateEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    owner->state = UMICOM_FAT16_UPDATER_CLOSING;
    status = UmicomUpdateRelease(owner);
    owner->busy = UMICOM_FALSE;
    return status;
}
const char *UmicomKernelFat16UpdateStatusName(UmicomKernelFat16UpdateStatus status)
{
    switch (status) {
    case UMICOM_FAT16_UPDATE_OK: return "ok";
    case UMICOM_FAT16_UPDATE_INVALID_ARGUMENT: return "invalid-argument";
    case UMICOM_FAT16_UPDATE_BAD_STATE: return "bad-state";
    case UMICOM_FAT16_UPDATE_BUSY: return "busy";
    case UMICOM_FAT16_UPDATE_UNSAFE_CONTEXT: return "unsafe-context";
    case UMICOM_FAT16_UPDATE_READ_ONLY: return "read-only";
    case UMICOM_FAT16_UPDATE_RANGE: return "range";
    case UMICOM_FAT16_UPDATE_INSPECTION_LIMIT: return "inspection-limit";
    case UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR: return "filesystem-error";
    case UMICOM_FAT16_UPDATE_TRANSPORT_ERROR: return "transport-error";
    case UMICOM_FAT16_UPDATE_RELEASE_FAILED: return "release-failed";
    case UMICOM_FAT16_UPDATE_WRITE_UNCERTAIN: return "write-uncertain";
    default: return "unknown-fat16-update-status";
    }
}
