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

/*-----------------------------------------------------------------------------
 * Ordered commits use the existing interpretation, deadline and lease helpers.
 * The original metadata-preserving updater above remains intact. This separate
 * outer owner adds persistent dirty guards without reopening an inspector over
 * intentionally dirty media or changing the read-only parser's admission rules.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_commit.h"

static UmicomBoolean UmicomCommitEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *const a = (const UmicomU8 *)left;
    const UmicomU8 *const b = (const UmicomU8 *)right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomCommitIndependent(const UmicomKernelFat16Committer *owner,
    UmicomAddress address, UmicomSize bytes)
{
    return UmicomUpdateDomainIndependent(owner->updater.domain, address, bytes) &&
        !UmicomUpdateOverlap(address, bytes, (UmicomAddress)owner, sizeof(*owner));
}
static UmicomKernelFat16UpdateStatus UmicomCommitLive(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitState expected)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16Committer) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->self != owner) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->busy) return UMICOM_FAT16_UPDATE_BUSY;
    const UmicomKernelFat16UpdateStatus status = UmicomUpdateLive(&owner->updater);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    if (owner->state != expected ||
        !UmicomUpdateDomainIndependent(owner->updater.domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    return UMICOM_FAT16_UPDATE_OK;
}
static UmicomKernelFat16UpdateStatus UmicomCommitResultBuffer(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitResult *outResult)
{
    return outResult && !((UmicomAddress)outResult % alignof(UmicomKernelFat16CommitResult)) &&
        UmicomCommitIndependent(owner, (UmicomAddress)outResult, sizeof(*outResult)) ?
        UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
}
static UmicomKernelFat16UpdateStatus UmicomCommitBuffers(UmicomKernelFat16Committer *owner,
    const char *path, const void *input, UmicomSize bytes, UmicomKernelFat16CommitResult *outResult)
{
    if (UmicomCommitResultBuffer(owner, outResult) != UMICOM_FAT16_UPDATE_OK ||
        !input || !bytes || bytes > UMICOM_FAT16_UPDATE_BYTES || !path ||
        !UmicomCommitIndependent(owner, (UmicomAddress)input, bytes) ||
        UmicomUpdateOverlap((UmicomAddress)input, bytes, (UmicomAddress)outResult, sizeof(*outResult)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    const UmicomAddress address = (UmicomAddress)path;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_PATH_BYTES; ++i) {
        /* Check each candidate byte before dereferencing: an unterminated path
         * inside result/input storage must not scan beyond the aliased object. */
        if (address > ~(UmicomAddress)0U - i ||
            !UmicomCommitIndependent(owner, address + i, 1U) ||
            UmicomUpdateOverlap(address + i, 1U, (UmicomAddress)input, bytes) ||
            UmicomUpdateOverlap(address + i, 1U, (UmicomAddress)outResult, sizeof(*outResult)))
            return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
        if (!path[i]) return UMICOM_FAT16_UPDATE_OK;
    }
    return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
}
static UmicomKernelFat16UpdateStatus UmicomCommitEnter(UmicomKernelFat16Committer *owner)
{
    owner->busy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomUpdateEnter(&owner->updater);
    if (status != UMICOM_FAT16_UPDATE_OK) owner->busy = UMICOM_FALSE;
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomCommitPublish(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitResult *result, UmicomKernelFat16CommitResult *outResult,
    UmicomKernelFat16UpdateStatus status)
{
    result->status = status;
    result->diskStatus = owner->updater.lastDiskStatus;
    result->blockStatus = owner->updater.lastBlockStatus;
    result->needsFlush = owner->updater.needsFlush;
    result->writeUncertain = owner->updater.writeUncertain;
    owner->updater.lastStatus = status;
    UmicomUpdateCopy(&owner->lastResult, result, sizeof(*result));
    UmicomUpdateCopy(outResult, result, sizeof(*result));
    owner->updater.busy = UMICOM_FALSE;
    owner->busy = UMICOM_FALSE;
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomCommitRead(UmicomKernelFat16Committer *owner,
    UmicomU64 sector, UmicomU8 *output)
{
    if (UmicomUpdateReadSector(&owner->updater, sector, output)) return UMICOM_FAT16_UPDATE_OK;
    owner->updater.lastDiskStatus = UMICOM_DISK_IO_ERROR;
    return UmicomUpdateFromDisk(&owner->updater, UMICOM_DISK_IO_ERROR);
}
static UmicomKernelFat16UpdateStatus UmicomCommitVerify(UmicomKernelFat16Committer *owner,
    UmicomU64 sector, const UmicomU8 *expected)
{
    const UmicomKernelFat16UpdateStatus status = UmicomCommitRead(owner, sector, owner->readback);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    if (!UmicomCommitEqual(owner->readback, expected, UMICOM_DISK_SECTOR_BYTES)) {
        owner->updater.lastDiskStatus = UMICOM_DISK_CORRUPT;
        return UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR;
    }
    return UMICOM_FAT16_UPDATE_OK;
}
static UmicomKernelFat16UpdateStatus UmicomCommitVerifyHeaders(UmicomKernelFat16Committer *owner,
    const UmicomU8 *expected)
{
    for (UmicomSize i = 0U; i < 2U; ++i) {
        const UmicomKernelFat16UpdateStatus status =
            UmicomCommitVerify(owner, owner->headerSectors[i], expected);
        if (status != UMICOM_FAT16_UPDATE_OK) return status;
    }
    return UMICOM_FAT16_UPDATE_OK;
}
static UmicomKernelFat16UpdateStatus UmicomCommitVerifyData(UmicomKernelFat16Committer *owner)
{
    for (UmicomSize i = 0U; i < owner->updater.plan.count; ++i) {
        const UmicomKernelFat16UpdateSector *const sector = &owner->updater.plan.sectors[i];
        const UmicomKernelFat16UpdateStatus status = UmicomCommitVerify(owner, sector->sector, sector->data);
        if (status != UMICOM_FAT16_UPDATE_OK) return status;
    }
    return UMICOM_FAT16_UPDATE_OK;
}
static UmicomKernelFat16UpdateStatus UmicomCommitWriteSector(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitResult *result, UmicomKernelFat16CommitPhase phase,
    UmicomU64 lba, const UmicomU8 *data, const UmicomKernelFat16UpdateSector *fileSector)
{
    UmicomKernelFat16Updater *const base = &owner->updater;
    result->phase = phase;
    result->lastBlockOutcome = UMICOM_BLOCK_NOT_SUBMITTED;
    if (!UmicomUpdateClock(base)) return UmicomUpdateFromBlock(base->lastBlockStatus);
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_NOT_SUBMITTED;
    base->lastBlockStatus = UmicomKernelBlockWrite(base->domain, base->handle,
        lba, 1U, data, UMICOM_DISK_SECTOR_BYTES, &outcome);
    result->lastBlockOutcome = outcome;
    if (outcome != UMICOM_BLOCK_NOT_SUBMITTED) {
        result->mediaTouched = UMICOM_TRUE;
        base->needsFlush = UMICOM_TRUE;
        if (fileSector) {
            ++result->submittedDataSectors;
            result->submittedBytes += fileSector->bytes;
        } else {
            ++result->submittedMetadataSectors;
            if (phase == UMICOM_FAT16_COMMIT_CLEAN_MIRROR || phase == UMICOM_FAT16_COMMIT_CLEAN_PRIMARY)
                result->cleanFinalisationStarted = UMICOM_TRUE;
        }
    }
    if (outcome == UMICOM_BLOCK_COMPLETED) {
        if (fileSector) {
            ++result->completedDataSectors;
            result->confirmedBytes += fileSector->bytes;
        } else ++result->completedMetadataSectors;
    } else if (outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED) {
        result->uncertainSector = lba;
        result->uncertainSectorValid = UMICOM_TRUE;
        base->writeUncertain = UMICOM_TRUE;
        if (fileSector) result->dataOutcome = UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED;
    }
    if (fileSector && result->dataOutcome != UMICOM_FAT16_UPDATE_SUBMITTED_UNCONFIRMED) {
        result->dataOutcome = result->confirmedBytes == result->requestedBytes ?
            UMICOM_FAT16_UPDATE_COMPLETED : result->confirmedBytes ?
            UMICOM_FAT16_UPDATE_PARTIAL_CONFIRMED : UMICOM_FAT16_UPDATE_NOT_SUBMITTED;
    }
    UmicomKernelFat16UpdateStatus status = UmicomUpdateFromBlock(base->lastBlockStatus);
    if (status == UMICOM_FAT16_UPDATE_OK && outcome != UMICOM_BLOCK_COMPLETED) {
        base->lastBlockStatus = UMICOM_BLOCK_MALFORMED_COMPLETION;
        status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
    }
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(base))
        status = UmicomUpdateFromBlock(base->lastBlockStatus);
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomCommitFlush(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitResult *result, UmicomKernelFat16CommitPhase phase)
{
    UmicomKernelFat16Updater *const base = &owner->updater;
    result->phase = phase;
    result->lastBlockOutcome = UMICOM_BLOCK_NOT_SUBMITTED;
    if (!UmicomUpdateClock(base)) return UmicomUpdateFromBlock(base->lastBlockStatus);
    UmicomKernelBlockMutationOutcome outcome = UMICOM_BLOCK_NOT_SUBMITTED;
    base->lastBlockStatus = UmicomKernelBlockFlush(base->domain, base->handle, &outcome);
    base->lastFlush = outcome;
    result->lastBlockOutcome = outcome;
    if (base->lastBlockStatus == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED) {
        ++result->completedFlushes;
        base->needsFlush = UMICOM_FALSE;
        if (phase == UMICOM_FAT16_COMMIT_DIRTY_PRIMARY_FLUSH) result->dirtyDurable = UMICOM_TRUE;
        if (phase == UMICOM_FAT16_COMMIT_DATA_FLUSH) result->dataDurable = UMICOM_TRUE;
        if (phase == UMICOM_FAT16_COMMIT_CLEAN_PRIMARY_FLUSH) result->cleanDurable = UMICOM_TRUE;
    }
    UmicomKernelFat16UpdateStatus status = UmicomUpdateFromBlock(base->lastBlockStatus);
    if (status == UMICOM_FAT16_UPDATE_OK && outcome != UMICOM_BLOCK_COMPLETED) {
        base->lastBlockStatus = UMICOM_BLOCK_MALFORMED_COMPLETION;
        status = UMICOM_FAT16_UPDATE_TRANSPORT_ERROR;
    }
    /* Preserve each successful barrier even if the enclosing acceptance clock
     * fails now. The caller can see persistence acknowledgement without an
     * invented successful high-level commit or permission to retry. */
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(base))
        status = UmicomUpdateFromBlock(base->lastBlockStatus);
    return status;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitOpen(UmicomKernelFat16Committer *owner,
    UmicomKernelBlockDomain *domain, UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16Committer) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (!UmicomUpdateDomainValid(domain)) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (!UmicomUpdateDomainIndependent(domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->busy || owner->updater.busy || owner->updater.volume.busy ||
        owner->updater.workspace.busy || domain->busy) return UMICOM_FAT16_UPDATE_BUSY;
    if ((!owner->self && !UmicomUpdateZero(owner, sizeof(*owner))) ||
        (owner->self && owner->self != owner) || owner->state != UMICOM_FAT16_COMMIT_UNUSED)
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    /* The outer guard covers policy and open callbacks before the embedded
     * helper establishes its own guard. Every alias check covers both owners. */
    owner->busy = UMICOM_TRUE;
    owner->self = owner;
    const UmicomKernelFat16UpdateStatus status =
        UmicomKernelFat16UpdateOpen(&owner->updater, domain, slot, partition, timeoutTicks);
    owner->state = status == UMICOM_FAT16_UPDATE_OK ? UMICOM_FAT16_COMMIT_READY :
        owner->updater.handle ? UMICOM_FAT16_COMMIT_CLOSING : UMICOM_FAT16_COMMIT_UNUSED;
    owner->busy = UMICOM_FALSE;
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomCommitPlan(UmicomKernelFat16Committer *owner,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes)
{
    UmicomKernelFat16Updater *const base = &owner->updater;
    const UmicomKernelDiskReader reader = {base->sectors, UmicomUpdateReadSector, base};
    base->lastDiskStatus = UmicomKernelFat16Open(&base->volume, &reader, base->partition);
    UmicomKernelFat16UpdateStatus status = UmicomUpdateFromDisk(base, base->lastDiskStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        base->lastDiskStatus = UmicomKernelFat16PlanUpdate(&base->volume, path, offset,
            input, bytes, &base->workspace, &base->plan);
        status = UmicomUpdateFromDisk(base, base->lastDiskStatus);
    }
    if (status == UMICOM_FAT16_UPDATE_OK) {
        owner->headerSectors[0] = base->volume.info.firstSector + base->volume.fatStart;
        owner->headerSectors[1] = owner->headerSectors[0] + base->volume.info.sectorsPerFat;
        status = UmicomCommitRead(owner, owner->headerSectors[0], owner->cleanHeader);
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitVerify(owner, owner->headerSectors[1], owner->cleanHeader);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        if (owner->cleanHeader[2] != 0xffU || owner->cleanHeader[3] != 0xffU) {
            base->lastDiskStatus = UMICOM_DISK_DIRTY;
            status = UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR;
        } else {
            UmicomUpdateCopy(owner->dirtyHeader, owner->cleanHeader, sizeof(owner->dirtyHeader));
            /* Clear only the clean-shutdown bit. The no-I/O-error bit is not
             * an in-progress marker and must not be repurposed as one. */
            owner->dirtyHeader[3] &= (UmicomU8)~(UMICOM_FAT16_CLEAN_MASK >> 8U);
        }
    }
    /* Every address and payload is now a private qualified snapshot. */
    return UmicomUpdateInspectorClose(base, status);
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitStage(UmicomKernelFat16Committer *owner,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    UmicomKernelFat16CommitResult *outResult)
{
    UmicomKernelFat16UpdateStatus status = UmicomCommitLive(owner, UMICOM_FAT16_COMMIT_READY);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomCommitBuffers(owner, path, input, bytes, outResult);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomCommitEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomKernelFat16CommitResult result;
    UmicomUpdateClear(&result, sizeof(result));
    result.phase = UMICOM_FAT16_COMMIT_PREFLIGHT;
    result.offset = offset;
    result.requestedBytes = bytes;
    UmicomUpdateBeginIo(&owner->updater);
    status = UmicomCommitPlan(owner, path, offset, input, bytes);
    /* Verification has a known remaining read cost. Reserve that budget before
     * changing either FAT header, so an already predictable admission limit
     * cannot unnecessarily leave an otherwise valid volume dirty. */
    if (status == UMICOM_FAT16_UPDATE_OK && owner->updater.operationReads >
        UMICOM_FAT16_IO_LIMIT - 2U - owner->updater.plan.count) {
        owner->updater.lastDiskStatus = UMICOM_DISK_LIMIT;
        status = UMICOM_FAT16_UPDATE_INSPECTION_LIMIT;
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(owner, &result, UMICOM_FAT16_COMMIT_DIRTY_MIRROR,
            owner->headerSectors[1], owner->dirtyHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(owner, &result, UMICOM_FAT16_COMMIT_DIRTY_MIRROR_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(owner, &result, UMICOM_FAT16_COMMIT_DIRTY_PRIMARY,
            owner->headerSectors[0], owner->dirtyHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(owner, &result, UMICOM_FAT16_COMMIT_DIRTY_PRIMARY_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.phase = UMICOM_FAT16_COMMIT_DIRTY_VERIFY;
        status = UmicomCommitVerifyHeaders(owner, owner->dirtyHeader);
        if (status == UMICOM_FAT16_UPDATE_OK) result.dirtyVerified = UMICOM_TRUE;
    }
    for (UmicomSize i = 0U; status == UMICOM_FAT16_UPDATE_OK && i < owner->updater.plan.count; ++i) {
        const UmicomKernelFat16UpdateSector *const sector = &owner->updater.plan.sectors[i];
        status = UmicomCommitWriteSector(owner, &result, UMICOM_FAT16_COMMIT_DATA_WRITE,
            sector->sector, sector->data, sector);
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(owner, &result, UMICOM_FAT16_COMMIT_DATA_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.phase = UMICOM_FAT16_COMMIT_DATA_VERIFY;
        status = UmicomCommitVerifyData(owner);
        if (status == UMICOM_FAT16_UPDATE_OK) result.dataVerified = UMICOM_TRUE;
    }
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(&owner->updater))
        status = UmicomUpdateFromBlock(owner->updater.lastBlockStatus);
    owner->state = status == UMICOM_FAT16_UPDATE_OK ? UMICOM_FAT16_COMMIT_STAGED :
        result.mediaTouched ? UMICOM_FAT16_COMMIT_FAILED : UMICOM_FAT16_COMMIT_READY;
    if (owner->state == UMICOM_FAT16_COMMIT_READY)
        UmicomUpdateClear(&owner->updater.plan, sizeof(owner->updater.plan));
    return UmicomCommitPublish(owner, &result, outResult, status);
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitFinish(UmicomKernelFat16Committer *owner,
    UmicomKernelFat16CommitResult *outResult)
{
    UmicomKernelFat16UpdateStatus status = UmicomCommitLive(owner, UMICOM_FAT16_COMMIT_STAGED);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomCommitResultBuffer(owner, outResult);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    if (!owner->lastResult.dirtyDurable || !owner->lastResult.dirtyVerified ||
        !owner->lastResult.dataDurable || !owner->lastResult.dataVerified ||
        owner->updater.writeUncertain || !owner->updater.plan.count ||
        owner->updater.plan.count > UMICOM_FAT16_UPDATE_SECTOR_LIMIT)
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    status = UmicomCommitEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomKernelFat16CommitResult result;
    UmicomUpdateCopy(&result, &owner->lastResult, sizeof(result));
    UmicomUpdateBeginIo(&owner->updater);
    result.phase = UMICOM_FAT16_COMMIT_FINISH_VERIFY;
    status = UmicomCommitVerifyHeaders(owner, owner->dirtyHeader);
    if (status == UMICOM_FAT16_UPDATE_OK) status = UmicomCommitVerifyData(owner);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(owner, &result, UMICOM_FAT16_COMMIT_CLEAN_MIRROR,
            owner->headerSectors[1], owner->cleanHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(owner, &result, UMICOM_FAT16_COMMIT_CLEAN_MIRROR_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(owner, &result, UMICOM_FAT16_COMMIT_CLEAN_PRIMARY,
            owner->headerSectors[0], owner->cleanHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(owner, &result, UMICOM_FAT16_COMMIT_CLEAN_PRIMARY_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.phase = UMICOM_FAT16_COMMIT_CLEAN_VERIFY;
        status = UmicomCommitVerifyHeaders(owner, owner->cleanHeader);
        if (status == UMICOM_FAT16_UPDATE_OK) result.cleanVerified = UMICOM_TRUE;
    }
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(&owner->updater))
        status = UmicomUpdateFromBlock(owner->updater.lastBlockStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.phase = UMICOM_FAT16_COMMIT_COMPLETE;
        result.commitAccepted = UMICOM_TRUE;
        owner->state = UMICOM_FAT16_COMMIT_COMMITTED;
    } else owner->state = UMICOM_FAT16_COMMIT_FAILED;
    return UmicomCommitPublish(owner, &result, outResult, status);
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16CommitClose(UmicomKernelFat16Committer *owner)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16Committer) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->busy || owner->updater.busy || owner->updater.volume.busy ||
        owner->updater.workspace.busy) return UMICOM_FAT16_UPDATE_BUSY;
    if (!owner->self) return UmicomUpdateZero(owner, sizeof(*owner)) ?
        UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->self != owner) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->state == UMICOM_FAT16_COMMIT_UNUSED || owner->state == UMICOM_FAT16_COMMIT_CLOSED)
        return owner->updater.handle ? UMICOM_FAT16_UPDATE_BAD_STATE : UMICOM_FAT16_UPDATE_OK;
    if (owner->state < UMICOM_FAT16_COMMIT_READY || owner->state > UMICOM_FAT16_COMMIT_CLOSING ||
        !UmicomUpdateDomainValid(owner->updater.domain) ||
        !UmicomUpdateDomainIndependent(owner->updater.domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    owner->busy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16UpdateClose(&owner->updater);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        owner->state = owner->updater.admitted ? UMICOM_FAT16_COMMIT_CLOSED : UMICOM_FAT16_COMMIT_UNUSED;
        UmicomUpdateClear(owner->cleanHeader, sizeof(owner->cleanHeader));
        UmicomUpdateClear(owner->dirtyHeader, sizeof(owner->dirtyHeader));
        UmicomUpdateClear(owner->readback, sizeof(owner->readback));
    } else if (owner->updater.state == UMICOM_FAT16_UPDATER_CLOSING)
        owner->state = UMICOM_FAT16_COMMIT_CLOSING;
    owner->busy = UMICOM_FALSE;
    return status;
}
const char *UmicomKernelFat16CommitStateName(UmicomKernelFat16CommitState state)
{
    switch (state) {
    case UMICOM_FAT16_COMMIT_UNUSED: return "unused";
    case UMICOM_FAT16_COMMIT_READY: return "ready";
    case UMICOM_FAT16_COMMIT_STAGED: return "staged";
    case UMICOM_FAT16_COMMIT_FAILED: return "failed";
    case UMICOM_FAT16_COMMIT_COMMITTED: return "committed";
    case UMICOM_FAT16_COMMIT_CLOSING: return "closing";
    case UMICOM_FAT16_COMMIT_CLOSED: return "closed";
    default: return "unknown-fat16-commit-state";
    }
}
const char *UmicomKernelFat16CommitPhaseName(UmicomKernelFat16CommitPhase phase)
{
    switch (phase) {
    case UMICOM_FAT16_COMMIT_NONE: return "none";
    case UMICOM_FAT16_COMMIT_PREFLIGHT: return "preflight";
    case UMICOM_FAT16_COMMIT_DIRTY_MIRROR: return "dirty-mirror";
    case UMICOM_FAT16_COMMIT_DIRTY_MIRROR_FLUSH: return "dirty-mirror-flush";
    case UMICOM_FAT16_COMMIT_DIRTY_PRIMARY: return "dirty-primary";
    case UMICOM_FAT16_COMMIT_DIRTY_PRIMARY_FLUSH: return "dirty-primary-flush";
    case UMICOM_FAT16_COMMIT_DIRTY_VERIFY: return "dirty-verify";
    case UMICOM_FAT16_COMMIT_DATA_WRITE: return "data-write";
    case UMICOM_FAT16_COMMIT_DATA_FLUSH: return "data-flush";
    case UMICOM_FAT16_COMMIT_DATA_VERIFY: return "data-verify";
    case UMICOM_FAT16_COMMIT_FINISH_VERIFY: return "finish-verify";
    case UMICOM_FAT16_COMMIT_CLEAN_MIRROR: return "clean-mirror";
    case UMICOM_FAT16_COMMIT_CLEAN_MIRROR_FLUSH: return "clean-mirror-flush";
    case UMICOM_FAT16_COMMIT_CLEAN_PRIMARY: return "clean-primary";
    case UMICOM_FAT16_COMMIT_CLEAN_PRIMARY_FLUSH: return "clean-primary-flush";
    case UMICOM_FAT16_COMMIT_CLEAN_VERIFY: return "clean-verify";
    case UMICOM_FAT16_COMMIT_COMPLETE: return "complete";
    case UMICOM_FAT16_COMMIT_DIRECTORY_WRITE: return "directory-write";
    case UMICOM_FAT16_COMMIT_DIRECTORY_FLUSH: return "directory-flush";
    case UMICOM_FAT16_COMMIT_DIRECTORY_VERIFY: return "directory-verify";
    default: return "unknown-fat16-commit-phase";
    }
}

/*-----------------------------------------------------------------------------
 * File commits extend the ordered transport with one checked directory sector.
 * The established data-only APIs above retain their original admission rules.
 * No caller-supplied disk address reaches a mutation: all sectors and complete
 * payloads come from the read-only file planner while both FAT copies are clean.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/fat16_file_commit.h"

static UmicomBoolean UmicomFileCommitIndependent(const UmicomKernelFat16FileCommitter *owner,
    UmicomAddress address, UmicomSize bytes)
{
    return UmicomUpdateDomainIndependent(owner->commit.updater.domain, address, bytes) &&
        !UmicomUpdateOverlap(address, bytes, (UmicomAddress)owner, sizeof(*owner));
}
static UmicomKernelFat16UpdateStatus UmicomFileCommitLive(UmicomKernelFat16FileCommitter *owner,
    UmicomKernelFat16CommitState expected)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16FileCommitter) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->self != owner) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->busy || owner->fileWorkspace.busy) return UMICOM_FAT16_UPDATE_BUSY;
    const UmicomKernelFat16UpdateStatus status = UmicomCommitLive(&owner->commit, expected);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    return UmicomUpdateDomainIndependent(owner->commit.updater.domain,
        (UmicomAddress)owner, sizeof(*owner)) ? UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_BAD_STATE;
}
static UmicomKernelFat16UpdateStatus UmicomFileCommitResultBuffer(
    UmicomKernelFat16FileCommitter *owner, UmicomKernelFat16FileCommitResult *outResult)
{
    return outResult && !((UmicomAddress)outResult % alignof(UmicomKernelFat16FileCommitResult)) &&
        UmicomFileCommitIndependent(owner, (UmicomAddress)outResult, sizeof(*outResult)) ?
        UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
}
static UmicomKernelFat16UpdateStatus UmicomFileCommitBuffers(UmicomKernelFat16FileCommitter *owner,
    const char *path, const void *input, UmicomSize bytes, const UmicomKernelFat16FileTime *time,
    UmicomKernelFat16FileCommitResult *outResult)
{
    if (UmicomFileCommitResultBuffer(owner, outResult) != UMICOM_FAT16_UPDATE_OK ||
        !input || !bytes || bytes > UMICOM_FAT16_UPDATE_BYTES || !path || !time ||
        (UmicomAddress)time % alignof(UmicomKernelFat16FileTime) ||
        !UmicomFileCommitIndependent(owner, (UmicomAddress)time, sizeof(*time)) ||
        !UmicomFileCommitIndependent(owner, (UmicomAddress)input, bytes) ||
        UmicomUpdateOverlap((UmicomAddress)input, bytes, (UmicomAddress)outResult, sizeof(*outResult)) ||
        UmicomUpdateOverlap((UmicomAddress)time, sizeof(*time), (UmicomAddress)outResult, sizeof(*outResult)) ||
        UmicomUpdateOverlap((UmicomAddress)time, sizeof(*time), (UmicomAddress)input, bytes))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    const UmicomAddress address = (UmicomAddress)path;
    UmicomBoolean terminated = UMICOM_FALSE;
    for (UmicomSize i = 0U; i < UMICOM_FAT16_PATH_BYTES; ++i) {
        /* Check each candidate byte before reading it, including intersections
         * with the new calendar object and every byte of the outer owner. */
        if (address > ~(UmicomAddress)0U - i ||
            !UmicomFileCommitIndependent(owner, address + i, 1U) ||
            UmicomUpdateOverlap(address + i, 1U, (UmicomAddress)input, bytes) ||
            UmicomUpdateOverlap(address + i, 1U, (UmicomAddress)time, sizeof(*time)) ||
            UmicomUpdateOverlap(address + i, 1U, (UmicomAddress)outResult, sizeof(*outResult)))
            return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
        if (!path[i]) { terminated = UMICOM_TRUE; break; }
    }
    if (!terminated) return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    UmicomKernelFat16FileTimeEncoding encoded;
    /* Bad calendar input is a pre-admission refusal: no policy, clock or disk
     * callback is needed, and the caller's previous result stays untouched. */
    return UmicomKernelFat16FileTimeEncode(time, &encoded) == UMICOM_DISK_OK ?
        UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
}
static UmicomKernelFat16UpdateStatus UmicomFileCommitEnter(UmicomKernelFat16FileCommitter *owner)
{
    owner->busy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomCommitEnter(&owner->commit);
    if (status != UMICOM_FAT16_UPDATE_OK) owner->busy = UMICOM_FALSE;
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomFileCommitPublish(UmicomKernelFat16FileCommitter *owner,
    UmicomKernelFat16FileCommitResult *result, UmicomKernelFat16FileCommitResult *outResult,
    UmicomKernelFat16UpdateStatus status)
{
    UmicomKernelFat16Updater *const base = &owner->commit.updater;
    result->commit.status = status;
    result->commit.diskStatus = base->lastDiskStatus;
    result->commit.blockStatus = base->lastBlockStatus;
    result->commit.needsFlush = base->needsFlush;
    result->commit.writeUncertain = base->writeUncertain;
    base->lastStatus = status;
    UmicomUpdateCopy(&owner->commit.lastResult, &result->commit, sizeof(result->commit));
    UmicomUpdateCopy(&owner->lastResult, result, sizeof(*result));
    UmicomUpdateCopy(outResult, result, sizeof(*result));
    base->busy = UMICOM_FALSE;
    owner->commit.busy = UMICOM_FALSE;
    owner->busy = UMICOM_FALSE;
    return status;
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitOpen(
    UmicomKernelFat16FileCommitter *owner, UmicomKernelBlockDomain *domain,
    UmicomSize slot, UmicomSize partition, UmicomU64 timeoutTicks)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16FileCommitter) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (!UmicomUpdateDomainValid(domain)) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (!UmicomUpdateDomainIndependent(domain, (UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->busy || owner->commit.busy || owner->commit.updater.busy ||
        owner->commit.updater.volume.busy || owner->commit.updater.workspace.busy ||
        owner->fileWorkspace.busy || domain->busy) return UMICOM_FAT16_UPDATE_BUSY;
    if ((!owner->self && !UmicomUpdateZero(owner, sizeof(*owner))) ||
        (owner->self && owner->self != owner) || owner->commit.state != UMICOM_FAT16_COMMIT_UNUSED)
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    owner->busy = UMICOM_TRUE;
    owner->self = owner;
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16CommitOpen(&owner->commit,
        domain, slot, partition, timeoutTicks);
    owner->busy = UMICOM_FALSE;
    return status;
}
static UmicomKernelFat16UpdateStatus UmicomFileCommitPlan(UmicomKernelFat16FileCommitter *owner,
    const char *path, UmicomU64 offset, const void *input, UmicomSize bytes,
    const UmicomKernelFat16FileTime *time, UmicomKernelFat16FileCommitResult *result)
{
    UmicomKernelFat16Committer *const commit = &owner->commit;
    UmicomKernelFat16Updater *const base = &commit->updater;
    const UmicomKernelDiskReader reader = {base->sectors, UmicomUpdateReadSector, base};
    base->lastDiskStatus = UmicomKernelFat16Open(&base->volume, &reader, base->partition);
    UmicomKernelFat16UpdateStatus status = UmicomUpdateFromDisk(base, base->lastDiskStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        base->lastDiskStatus = UmicomKernelFat16PlanFileUpdate(&base->volume, path, offset,
            input, bytes, time, &base->workspace, &owner->fileWorkspace, &base->plan, &owner->filePlan);
        status = UmicomUpdateFromDisk(base, base->lastDiskStatus);
    }
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result->directorySector = owner->filePlan.directorySector;
        result->entryOffset = owner->filePlan.entryOffset;
        UmicomUpdateCopy(&result->requestedTime, &owner->filePlan.requestedTime, sizeof(result->requestedTime));
        UmicomUpdateCopy(&result->encodedTime, &owner->filePlan.encodedTime, sizeof(result->encodedTime));
        result->originalAttributes = owner->filePlan.original[owner->filePlan.entryOffset + 11U];
        result->updatedAttributes = owner->filePlan.data[owner->filePlan.entryOffset + 11U];
        result->directoryPlanned = UMICOM_TRUE;
        commit->headerSectors[0] = base->volume.info.firstSector + base->volume.fatStart;
        commit->headerSectors[1] = commit->headerSectors[0] + base->volume.info.sectorsPerFat;
        status = UmicomCommitRead(commit, commit->headerSectors[0], commit->cleanHeader);
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitVerify(commit, commit->headerSectors[1], commit->cleanHeader);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        if (commit->cleanHeader[2] != 0xffU || commit->cleanHeader[3] != 0xffU) {
            base->lastDiskStatus = UMICOM_DISK_DIRTY;
            status = UMICOM_FAT16_UPDATE_FILESYSTEM_ERROR;
        } else {
            UmicomUpdateCopy(commit->dirtyHeader, commit->cleanHeader, sizeof(commit->dirtyHeader));
            commit->dirtyHeader[3] &= (UmicomU8)~(UMICOM_FAT16_CLEAN_MASK >> 8U);
        }
    }
    return UmicomUpdateInspectorClose(base, status);
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitStage(
    UmicomKernelFat16FileCommitter *owner, const char *path, UmicomU64 offset,
    const void *input, UmicomSize bytes, const UmicomKernelFat16FileTime *time,
    UmicomKernelFat16FileCommitResult *outResult)
{
    UmicomKernelFat16UpdateStatus status = UmicomFileCommitLive(owner, UMICOM_FAT16_COMMIT_READY);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomFileCommitBuffers(owner, path, input, bytes, time, outResult);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomFileCommitEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomKernelFat16FileCommitResult result;
    UmicomUpdateClear(&result, sizeof(result));
    result.commit.phase = UMICOM_FAT16_COMMIT_PREFLIGHT;
    result.commit.offset = offset;
    result.commit.requestedBytes = bytes;
    UmicomKernelFat16Committer *const commit = &owner->commit;
    UmicomKernelFat16Updater *const base = &commit->updater;
    UmicomUpdateBeginIo(base);
    status = UmicomFileCommitPlan(owner, path, offset, input, bytes, time, &result);
    /* Reserve all predictable post-mutation reads while the volume is clean:
     * two dirty FAT headers, each data sector and the directory sector. */
    if (status == UMICOM_FAT16_UPDATE_OK &&
        base->operationReads > UMICOM_FAT16_IO_LIMIT - (3U + base->plan.count)) {
        base->lastDiskStatus = UMICOM_DISK_LIMIT;
        status = UMICOM_FAT16_UPDATE_INSPECTION_LIMIT;
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(commit, &result.commit, UMICOM_FAT16_COMMIT_DIRTY_MIRROR,
            commit->headerSectors[1], commit->dirtyHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(commit, &result.commit, UMICOM_FAT16_COMMIT_DIRTY_MIRROR_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(commit, &result.commit, UMICOM_FAT16_COMMIT_DIRTY_PRIMARY,
            commit->headerSectors[0], commit->dirtyHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(commit, &result.commit, UMICOM_FAT16_COMMIT_DIRTY_PRIMARY_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.commit.phase = UMICOM_FAT16_COMMIT_DIRTY_VERIFY;
        status = UmicomCommitVerifyHeaders(commit, commit->dirtyHeader);
        if (status == UMICOM_FAT16_UPDATE_OK) result.commit.dirtyVerified = UMICOM_TRUE;
    }
    for (UmicomSize i = 0U; status == UMICOM_FAT16_UPDATE_OK && i < base->plan.count; ++i) {
        const UmicomKernelFat16UpdateSector *const sector = &base->plan.sectors[i];
        status = UmicomCommitWriteSector(commit, &result.commit, UMICOM_FAT16_COMMIT_DATA_WRITE,
            sector->sector, sector->data, sector);
    }
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(commit, &result.commit, UMICOM_FAT16_COMMIT_DATA_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.commit.phase = UMICOM_FAT16_COMMIT_DATA_VERIFY;
        status = UmicomCommitVerifyData(commit);
        if (status == UMICOM_FAT16_UPDATE_OK) result.commit.dataVerified = UMICOM_TRUE;
    }
    if (status == UMICOM_FAT16_UPDATE_OK) {
        const UmicomSize submitted = result.commit.submittedMetadataSectors;
        const UmicomSize completed = result.commit.completedMetadataSectors;
        status = UmicomCommitWriteSector(commit, &result.commit, UMICOM_FAT16_COMMIT_DIRECTORY_WRITE,
            owner->filePlan.directorySector, owner->filePlan.data, 0);
        result.directorySubmitted = result.commit.submittedMetadataSectors > submitted;
        result.directoryCompleted = result.commit.completedMetadataSectors > completed;
    }
    if (status == UMICOM_FAT16_UPDATE_OK) {
        const UmicomSize completed = result.commit.completedFlushes;
        status = UmicomCommitFlush(commit, &result.commit, UMICOM_FAT16_COMMIT_DIRECTORY_FLUSH);
        /* Preserve a successful barrier even if its final deadline check fails. */
        result.directoryDurable = result.commit.completedFlushes > completed;
    }
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.commit.phase = UMICOM_FAT16_COMMIT_DIRECTORY_VERIFY;
        status = UmicomCommitVerify(commit, owner->filePlan.directorySector, owner->filePlan.data);
        if (status == UMICOM_FAT16_UPDATE_OK) result.directoryVerified = UMICOM_TRUE;
    }
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(base))
        status = UmicomUpdateFromBlock(base->lastBlockStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) commit->state = UMICOM_FAT16_COMMIT_STAGED;
    else if (result.commit.mediaTouched) commit->state = UMICOM_FAT16_COMMIT_FAILED;
    else {
        UmicomUpdateClear(&base->plan, sizeof(base->plan));
        UmicomUpdateClear(&owner->filePlan, sizeof(owner->filePlan));
    }
    return UmicomFileCommitPublish(owner, &result, outResult, status);
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitFinish(
    UmicomKernelFat16FileCommitter *owner, UmicomKernelFat16FileCommitResult *outResult)
{
    UmicomKernelFat16UpdateStatus status = UmicomFileCommitLive(owner, UMICOM_FAT16_COMMIT_STAGED);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    status = UmicomFileCommitResultBuffer(owner, outResult);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomKernelFat16Committer *const commit = &owner->commit;
    UmicomKernelFat16Updater *const base = &commit->updater;
    if (!owner->lastResult.commit.dirtyDurable || !owner->lastResult.commit.dirtyVerified ||
        !owner->lastResult.commit.dataDurable || !owner->lastResult.commit.dataVerified ||
        !owner->lastResult.directoryPlanned || !owner->lastResult.directoryCompleted ||
        !owner->lastResult.directoryDurable || !owner->lastResult.directoryVerified ||
        base->writeUncertain || !base->plan.count || base->plan.count > UMICOM_FAT16_UPDATE_SECTOR_LIMIT ||
        owner->filePlan.entryOffset > UMICOM_DISK_SECTOR_BYTES - 32U || owner->filePlan.entryOffset % 32U ||
        owner->filePlan.directorySector >= base->sectors)
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    status = UmicomFileCommitEnter(owner);
    if (status != UMICOM_FAT16_UPDATE_OK) return status;
    UmicomKernelFat16FileCommitResult result;
    UmicomUpdateCopy(&result, &owner->lastResult, sizeof(result));
    UmicomUpdateBeginIo(base);
    result.commit.phase = UMICOM_FAT16_COMMIT_FINISH_VERIFY;
    status = UmicomCommitVerifyHeaders(commit, commit->dirtyHeader);
    if (status == UMICOM_FAT16_UPDATE_OK) status = UmicomCommitVerifyData(commit);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitVerify(commit, owner->filePlan.directorySector, owner->filePlan.data);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(commit, &result.commit, UMICOM_FAT16_COMMIT_CLEAN_MIRROR,
            commit->headerSectors[1], commit->cleanHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(commit, &result.commit, UMICOM_FAT16_COMMIT_CLEAN_MIRROR_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitWriteSector(commit, &result.commit, UMICOM_FAT16_COMMIT_CLEAN_PRIMARY,
            commit->headerSectors[0], commit->cleanHeader, 0);
    if (status == UMICOM_FAT16_UPDATE_OK)
        status = UmicomCommitFlush(commit, &result.commit, UMICOM_FAT16_COMMIT_CLEAN_PRIMARY_FLUSH);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.commit.phase = UMICOM_FAT16_COMMIT_CLEAN_VERIFY;
        status = UmicomCommitVerifyHeaders(commit, commit->cleanHeader);
        if (status == UMICOM_FAT16_UPDATE_OK) result.commit.cleanVerified = UMICOM_TRUE;
    }
    if (status == UMICOM_FAT16_UPDATE_OK && !UmicomUpdateClock(base))
        status = UmicomUpdateFromBlock(base->lastBlockStatus);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        result.commit.phase = UMICOM_FAT16_COMMIT_COMPLETE;
        result.commit.commitAccepted = UMICOM_TRUE;
        commit->state = UMICOM_FAT16_COMMIT_COMMITTED;
    } else commit->state = UMICOM_FAT16_COMMIT_FAILED;
    return UmicomFileCommitPublish(owner, &result, outResult, status);
}
UmicomKernelFat16UpdateStatus UmicomKernelFat16FileCommitClose(UmicomKernelFat16FileCommitter *owner)
{
    if (!owner || (UmicomAddress)owner % alignof(UmicomKernelFat16FileCommitter) ||
        !UmicomUpdateSpan((UmicomAddress)owner, sizeof(*owner)))
        return UMICOM_FAT16_UPDATE_INVALID_ARGUMENT;
    if (owner->busy || owner->fileWorkspace.busy || owner->commit.busy || owner->commit.updater.busy ||
        owner->commit.updater.volume.busy || owner->commit.updater.workspace.busy)
        return UMICOM_FAT16_UPDATE_BUSY;
    if (!owner->self) return UmicomUpdateZero(owner, sizeof(*owner)) ?
        UMICOM_FAT16_UPDATE_OK : UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->self != owner) return UMICOM_FAT16_UPDATE_BAD_STATE;
    if (owner->commit.state != UMICOM_FAT16_COMMIT_UNUSED && owner->commit.state != UMICOM_FAT16_COMMIT_CLOSED &&
        (!UmicomUpdateDomainValid(owner->commit.updater.domain) ||
         !UmicomUpdateDomainIndependent(owner->commit.updater.domain, (UmicomAddress)owner, sizeof(*owner))))
        return UMICOM_FAT16_UPDATE_BAD_STATE;
    owner->busy = UMICOM_TRUE;
    const UmicomKernelFat16UpdateStatus status = UmicomKernelFat16CommitClose(&owner->commit);
    if (status == UMICOM_FAT16_UPDATE_OK) {
        UmicomUpdateClear(&owner->filePlan, sizeof(owner->filePlan));
        UmicomUpdateClear(owner->fileWorkspace.path, sizeof(owner->fileWorkspace.path));
        UmicomUpdateClear(&owner->fileWorkspace.stage, sizeof(owner->fileWorkspace.stage));
    }
    owner->busy = UMICOM_FALSE;
    return status;
}
