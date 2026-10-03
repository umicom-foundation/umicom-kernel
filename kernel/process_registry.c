/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/process_registry.c
 *
 * PURPOSE:
 *   Add bounded, owner-scoped process handles above the existing executable
 *   loader. Keep rights checks, reference lifetime and failure recovery here,
 *   rather than teaching the ELF parser or architecture monitor about handles.
 *
 * EDUCATIONAL OVERVIEW:
 *   Publishing a handle is a commit point. Before that point a failed load must
 *   either roll back completely or leave a named recovery owner in this table.
 *   Closing the final handle has the opposite ordering: release the process
 *   safely first, then invalidate the token. Never lose the only reference to
 *   memory merely because a cleanup operation returned an error.
 *
 *   The registry has a single-hart, serialised caller contract. Its busy flag
 *   catches accidental recursive use across ProcessRun; it does not make these
 *   operations safe for multiple harts. Objects and inputs are Kernel-owned.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/process_registry.h"

/* Use the actual type width, not an assumption about unsigned long on a host. */
#define UMICOM_REGISTRY_MAX_GENERATION ((UmicomU32)0xffffffffU)

static UmicomKernelRegistryStatus UmicomRegistryEnter(UmicomKernelProcessRegistry *registry)
{
    /* Checking self also rejects a byte-for-byte copy of a live registry: its
     * process objects contain interior pointers and cannot be relocated. */
    if (registry == (UmicomKernelProcessRegistry *)0) {
        return UMICOM_REGISTRY_INVALID_ARGUMENT;
    }
    if (registry->initialised == UMICOM_FALSE || registry->self != registry) {
        return UMICOM_REGISTRY_NOT_INITIALISED;
    }
    if (registry->busy != UMICOM_FALSE) {
        return UMICOM_REGISTRY_BUSY;
    }
    registry->busy = UMICOM_TRUE;
    return UMICOM_REGISTRY_OK;
}

static UmicomKernelRegistryStatus UmicomRegistryLeave(
    UmicomKernelProcessRegistry *registry, UmicomKernelRegistryStatus status)
{
    /* Every operation that acquired the reentry guard releases it, including
     * failure paths. A refused operation must not permanently wedge the table. */
    registry->busy = UMICOM_FALSE;
    return status;
}

static UmicomBoolean UmicomRegistryRightsKnown(UmicomKernelProcessRights rights)
{
    /* Reject unknown bits instead of silently granting a future operation. */
    return (rights & ~UMICOM_PROCESS_RIGHT_ALL) == 0U ? UMICOM_TRUE : UMICOM_FALSE;
}

static UmicomKernelProcessHandle UmicomRegistryEncode(
    UmicomSize index, const UmicomKernelProcessHandleRecord *record)
{
    /* The one-based slot ensures zero is invalid even before checking generation. */
    return ((UmicomU64)record->generation << 32U) | (UmicomU64)(index + 1U);
}

static UmicomKernelRegistryStatus UmicomRegistryResolve(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessRights required,
    UmicomKernelProcessHandleRecord **outHandle,
    UmicomKernelProcessRecord **outObject)
{
    if (owner == 0U) {
        return UMICOM_REGISTRY_INVALID_ARGUMENT;
    }
    const UmicomU32 slot = (UmicomU32)(handle & 0xffffffffULL);
    const UmicomU32 generation = (UmicomU32)(handle >> 32U);
    /* Bounds precede every table access. Caller-supplied integers are not pointers. */
    if (slot == 0U || slot > UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT || generation == 0U) {
        return UMICOM_REGISTRY_INVALID_HANDLE;
    }
    UmicomKernelProcessHandleRecord *const record = &registry->handles[slot - 1U];
    if (record->occupied == UMICOM_FALSE || record->retired != UMICOM_FALSE ||
        record->generation != generation) {
        return UMICOM_REGISTRY_INVALID_HANDLE;
    }
    /* A token copied from another owner conveys no authority in this namespace. */
    if (record->owner != owner) {
        return UMICOM_REGISTRY_WRONG_OWNER;
    }
    if ((record->rights & required) != required) {
        return UMICOM_REGISTRY_ACCESS_DENIED;
    }
    if (record->objectIndex >= UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT) {
        return UMICOM_REGISTRY_CORRUPT_STATE;
    }
    UmicomKernelProcessRecord *const object = &registry->objects[record->objectIndex];
    if (object->occupied == UMICOM_FALSE || object->references == 0U ||
        object->process.state == UMICOM_PROCESS_EMPTY) {
        return UMICOM_REGISTRY_CORRUPT_STATE;
    }
    *outHandle = record;
    *outObject = object;
    return UMICOM_REGISTRY_OK;
}

static UmicomKernelRegistryStatus UmicomRegistryFindHandle(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner, UmicomSize *outIndex)
{
    UmicomSize owned = 0U;
    UmicomSize available = UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT;
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT; ++index) {
        const UmicomKernelProcessHandleRecord *const record = &registry->handles[index];
        if (record->occupied != UMICOM_FALSE && record->owner == owner) {
            ++owned;
        }
        /* An exhausted generation is never eligible even when its slot is empty. */
        if (record->occupied == UMICOM_FALSE && record->retired == UMICOM_FALSE &&
            available == UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT) {
            available = index;
        }
    }
    if (owned >= UMICOM_PROCESS_REGISTRY_OWNER_LIMIT) {
        return UMICOM_REGISTRY_OWNER_LIMIT;
    }
    if (available == UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT) {
        return UMICOM_REGISTRY_HANDLE_LIMIT;
    }
    *outIndex = available;
    return UMICOM_REGISTRY_OK;
}

static void UmicomRegistryPublish(UmicomKernelProcessRegistry *registry,
    UmicomSize handleIndex, UmicomSize objectIndex, UmicomU64 owner,
    UmicomKernelProcessRights rights, UmicomKernelProcessHandle *outHandle)
{
    UmicomKernelProcessHandleRecord *const record = &registry->handles[handleIndex];
    record->owner = owner;
    record->objectIndex = (UmicomU32)objectIndex;
    record->rights = rights;
    /* No fallible operation remains between adding the reference and exposing
     * its token. Failed admissions never consume a visible handle slot. */
    ++registry->objects[objectIndex].references;
    record->occupied = UMICOM_TRUE;
    *outHandle = UmicomRegistryEncode(handleIndex, record);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryInitialize(UmicomKernelProcessRegistry *registry)
{
    if (registry == (UmicomKernelProcessRegistry *)0) {
        return UMICOM_REGISTRY_INVALID_ARGUMENT;
    }
    /* Require untouched zero-filled storage. Reinitialising even an empty live
     * table resets generations and could give an old token a second life. */
    const UmicomU8 *const bytes = (const UmicomU8 *)registry;
    for (UmicomSize index = 0U; index < sizeof(*registry); ++index) {
        if (bytes[index] != 0U) {
            return UMICOM_REGISTRY_BAD_STATE;
        }
    }
    registry->self = registry;
    registry->nextIdentity = 1U;
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT; ++index) {
        registry->handles[index].generation = 1U;
    }
    registry->initialised = UMICOM_TRUE;
    return UMICOM_REGISTRY_OK;
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryCreate(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    const UmicomU8 *image, UmicomSize imageBytes, UmicomKernelProcessRights rights,
    UmicomKernelProcessHandle *outHandle)
{
    if (owner == 0U || image == (const UmicomU8 *)0 || imageBytes == 0U ||
        outHandle == (UmicomKernelProcessHandle *)0 || UmicomRegistryRightsKnown(rights) == UMICOM_FALSE) {
        return UMICOM_REGISTRY_INVALID_ARGUMENT;
    }
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    if (registry->nextIdentity == 0U) {
        return UmicomRegistryLeave(registry, UMICOM_REGISTRY_IDENTITY_EXHAUSTED);
    }
    /* Reserve logical capacity before the loader takes any physical frames. */
    UmicomSize handleIndex = 0U;
    status = UmicomRegistryFindHandle(registry, owner, &handleIndex);
    if (status != UMICOM_REGISTRY_OK) return UmicomRegistryLeave(registry, status);
    UmicomSize objectIndex = 0U;
    while (objectIndex < UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT &&
        registry->objects[objectIndex].occupied != UMICOM_FALSE) {
        ++objectIndex;
    }
    if (objectIndex == UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT) {
        return UmicomRegistryLeave(registry, UMICOM_REGISTRY_PROCESS_LIMIT);
    }
    UmicomKernelProcessRecord *const object = &registry->objects[objectIndex];
    if (object->process.state != UMICOM_PROCESS_EMPTY || object->references != 0U) {
        return UmicomRegistryLeave(registry, UMICOM_REGISTRY_CORRUPT_STATE);
    }
    const UmicomKernelProcessStatus loaded = UmicomKernelProcessCreate(
        &object->process, image, imageBytes, registry->nextIdentity,
        (UmicomKernelExecutableStatus *)0);
    if (loaded != UMICOM_PROCESS_OK) {
        /* Ordinary rollback leaves EMPTY. A cleanup failure remains reachable
         * through the registry's maintenance path even though no handle exists. */
        if (object->process.state != UMICOM_PROCESS_EMPTY) {
            object->occupied = UMICOM_TRUE;
            /* This identity now belongs to retained state; never issue it again. */
            registry->nextIdentity = registry->nextIdentity == ~(UmicomU64)0U
                ? 0U : registry->nextIdentity + 1U;
            return UmicomRegistryLeave(registry, UMICOM_REGISTRY_CLEANUP_FAILED);
        }
        return UmicomRegistryLeave(registry, UMICOM_REGISTRY_LOAD_FAILED);
    }
    object->occupied = UMICOM_TRUE;
    registry->nextIdentity = registry->nextIdentity == ~(UmicomU64)0U
        ? 0U : registry->nextIdentity + 1U;
    UmicomRegistryPublish(registry, handleIndex, objectIndex, owner, rights, outHandle);
    return UmicomRegistryLeave(registry, UMICOM_REGISTRY_OK);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryQuery(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessInfo *outInfo)
{
    if (outInfo == (UmicomKernelProcessInfo *)0) return UMICOM_REGISTRY_INVALID_ARGUMENT;
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    UmicomKernelProcessHandleRecord *record;
    UmicomKernelProcessRecord *object;
    status = UmicomRegistryResolve(registry, owner, handle, UMICOM_PROCESS_RIGHT_QUERY, &record, &object);
    if (status == UMICOM_REGISTRY_OK) {
        /* A snapshot grants observation, not a mutable pointer to the owner. */
        outInfo->identity = object->process.identity;
        outInfo->state = object->process.state;
        outInfo->rights = record->rights;
        outInfo->references = object->references;
        outInfo->backingPages = object->process.pageCount;
        outInfo->quiesced = object->process.quiesced;
        outInfo->exitValue = object->process.report.exitValue;
        outInfo->trapCause = object->process.report.trapCause;
        outInfo->systemCalls = object->process.report.callCount;
    }
    return UmicomRegistryLeave(registry, status);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryRun(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomU64 argument, UmicomU64 deadlineTicks)
{
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    UmicomKernelProcessHandleRecord *record;
    UmicomKernelProcessRecord *object;
    status = UmicomRegistryResolve(registry, owner, handle, UMICOM_PROCESS_RIGHT_RUN, &record, &object);
    if (status == UMICOM_REGISTRY_OK) {
        /* Keep the guard held throughout execution. No callback or trap-side
         * service may close the last reference while this invocation uses it. */
        status = UmicomKernelProcessRun(&object->process, argument, deadlineTicks) == UMICOM_PROCESS_OK
            ? UMICOM_REGISTRY_OK : UMICOM_REGISTRY_RUN_FAILED;
    }
    return UmicomRegistryLeave(registry, status);
}

static UmicomKernelRegistryStatus UmicomRegistryCopyHandle(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomU64 receivingOwner,
    UmicomKernelProcessRights rights, UmicomBoolean transfer,
    UmicomKernelProcessHandle *outHandle)
{
    if (receivingOwner == 0U || outHandle == (UmicomKernelProcessHandle *)0 ||
        UmicomRegistryRightsKnown(rights) == UMICOM_FALSE) return UMICOM_REGISTRY_INVALID_ARGUMENT;
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    const UmicomKernelProcessRights required = UMICOM_PROCESS_RIGHT_DUPLICATE |
        (transfer != UMICOM_FALSE ? UMICOM_PROCESS_RIGHT_TRANSFER : 0U);
    UmicomKernelProcessHandleRecord *record;
    UmicomKernelProcessRecord *object;
    status = UmicomRegistryResolve(registry, owner, handle, required, &record, &object);
    if (status != UMICOM_REGISTRY_OK) return UmicomRegistryLeave(registry, status);
    /* A new reference can preserve or reduce existing authority, never invent it. */
    if ((rights & record->rights) != rights) {
        return UmicomRegistryLeave(registry, UMICOM_REGISTRY_ACCESS_DENIED);
    }
    UmicomSize available = 0U;
    status = UmicomRegistryFindHandle(registry, receivingOwner, &available);
    if (status != UMICOM_REGISTRY_OK) return UmicomRegistryLeave(registry, status);
    if (object->references >= UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT) {
        return UmicomRegistryLeave(registry, UMICOM_REGISTRY_CORRUPT_STATE);
    }
    UmicomRegistryPublish(registry, available, record->objectIndex, receivingOwner, rights, outHandle);
    return UmicomRegistryLeave(registry, UMICOM_REGISTRY_OK);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryDuplicate(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessRights rights,
    UmicomKernelProcessHandle *outHandle)
{
    return UmicomRegistryCopyHandle(registry, owner, handle, owner, rights, UMICOM_FALSE, outHandle);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryGrant(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomU64 receivingOwner,
    UmicomKernelProcessRights rights, UmicomKernelProcessHandle *outHandle)
{
    /* Grant creates a separate owner-bound token. It is not a raw-token transfer
     * and does not revoke the sender's existing reference. */
    return UmicomRegistryCopyHandle(registry, owner, handle, receivingOwner, rights, UMICOM_TRUE, outHandle);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryRestrict(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessRights rights)
{
    if (UmicomRegistryRightsKnown(rights) == UMICOM_FALSE) return UMICOM_REGISTRY_INVALID_ARGUMENT;
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    UmicomKernelProcessHandleRecord *record;
    UmicomKernelProcessRecord *object;
    status = UmicomRegistryResolve(registry, owner, handle, 0U, &record, &object);
    if (status == UMICOM_REGISTRY_OK) {
        if ((rights & record->rights) != rights) status = UMICOM_REGISTRY_ACCESS_DENIED;
        else record->rights = rights;
    }
    return UmicomRegistryLeave(registry, status);
}

static UmicomKernelRegistryStatus UmicomRegistryCloseHeld(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomKernelProcessHandleRecord *record;
    UmicomKernelProcessRecord *object;
    UmicomKernelRegistryStatus status = UmicomRegistryResolve(registry, owner, handle, 0U, &record, &object);
    if (status != UMICOM_REGISTRY_OK) return status;
    if (object->references == 1U) {
        /* Do not invalidate the final reference before destruction succeeds.
         * A failed cleanup must remain reachable for query and a later retry. */
        if (object->process.quiesced == UMICOM_FALSE || object->process.state == UMICOM_PROCESS_RUNNING) {
            return UMICOM_REGISTRY_BAD_STATE;
        }
        if (UmicomKernelProcessDestroy(&object->process) != UMICOM_PROCESS_OK) {
            return UMICOM_REGISTRY_CLEANUP_FAILED;
        }
        object->occupied = UMICOM_FALSE;
    }
    --object->references;
    record->occupied = UMICOM_FALSE;
    record->owner = 0U;
    record->objectIndex = 0U;
    record->rights = 0U;
    /* A wrapped counter would resurrect an old handle. Spend the slot instead;
     * the bound is explicit, and every older token remains invalid forever. */
    if (record->generation == UMICOM_REGISTRY_MAX_GENERATION) record->retired = UMICOM_TRUE;
    else ++record->generation;
    return UMICOM_REGISTRY_OK;
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryClose(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner, UmicomKernelProcessHandle handle)
{
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    status = UmicomRegistryCloseHeld(registry, owner, handle);
    return UmicomRegistryLeave(registry, status);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryCloseOwner(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner, UmicomSize *outClosed)
{
    if (owner == 0U || outClosed == (UmicomSize *)0) return UMICOM_REGISTRY_INVALID_ARGUMENT;
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    *outClosed = 0U;
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT; ++index) {
        UmicomKernelProcessHandleRecord *const record = &registry->handles[index];
        if (record->occupied == UMICOM_FALSE || record->owner != owner) continue;
        status = UmicomRegistryCloseHeld(registry, owner, UmicomRegistryEncode(index, record));
        if (status != UMICOM_REGISTRY_OK) break;
        ++*outClosed;
    }
    /* Partial success is reported honestly; no earlier close is undone. */
    return UmicomRegistryLeave(registry, status);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryReap(
    UmicomKernelProcessRegistry *registry, UmicomSize *outReaped)
{
    if (outReaped == (UmicomSize *)0) return UMICOM_REGISTRY_INVALID_ARGUMENT;
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    *outReaped = 0U;
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT; ++index) {
        UmicomKernelProcessRecord *const object = &registry->objects[index];
        if (object->occupied == UMICOM_FALSE || object->references != 0U) continue;
        /* Only failed unpublished loads enter this path. Live handles, including
         * other owners' grants, always protect their referenced process. */
        if (object->process.quiesced == UMICOM_FALSE ||
            UmicomKernelProcessDestroy(&object->process) != UMICOM_PROCESS_OK) {
            return UmicomRegistryLeave(registry, UMICOM_REGISTRY_CLEANUP_FAILED);
        }
        object->occupied = UMICOM_FALSE;
        ++*outReaped;
    }
    return UmicomRegistryLeave(registry, UMICOM_REGISTRY_OK);
}

static UmicomKernelRegistryStatus UmicomRegistryInspect(
    UmicomKernelProcessRegistry *registry, UmicomKernelRegistrySnapshot *outSnapshot)
{
    UmicomSize references[UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT] = {0};
    UmicomKernelRegistrySnapshot snapshot = {0};
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT; ++index) {
        const UmicomKernelProcessHandleRecord *const record = &registry->handles[index];
        if (record->generation == 0U || (record->retired != UMICOM_FALSE &&
            (record->occupied != UMICOM_FALSE || record->generation != UMICOM_REGISTRY_MAX_GENERATION))) {
            return UMICOM_REGISTRY_CORRUPT_STATE;
        }
        if (record->retired != UMICOM_FALSE) ++snapshot.retiredHandles;
        if (record->occupied == UMICOM_FALSE) {
            if (record->owner != 0U || record->rights != 0U || record->objectIndex != 0U) {
                return UMICOM_REGISTRY_CORRUPT_STATE;
            }
            continue;
        }
        if (record->owner == 0U || record->objectIndex >= UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT ||
            UmicomRegistryRightsKnown(record->rights) == UMICOM_FALSE) return UMICOM_REGISTRY_CORRUPT_STATE;
        ++references[record->objectIndex];
        ++snapshot.handles;
        /* Independently recount the owner's quota instead of trusting a cached
         * counter which could drift away from the actual handle records. */
        UmicomSize sameOwner = 0U;
        for (UmicomSize other = 0U; other < UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT; ++other) {
            if (registry->handles[other].occupied != UMICOM_FALSE &&
                registry->handles[other].owner == record->owner) ++sameOwner;
        }
        if (sameOwner > UMICOM_PROCESS_REGISTRY_OWNER_LIMIT) return UMICOM_REGISTRY_CORRUPT_STATE;
    }
    for (UmicomSize index = 0U; index < UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT; ++index) {
        const UmicomKernelProcessRecord *const object = &registry->objects[index];
        if (object->references != references[index]) return UMICOM_REGISTRY_CORRUPT_STATE;
        if (object->occupied == UMICOM_FALSE) {
            if (object->references != 0U || object->process.state != UMICOM_PROCESS_EMPTY ||
                object->process.pageCount != 0U || object->process.space.initialised != UMICOM_FALSE) {
                return UMICOM_REGISTRY_CORRUPT_STATE;
            }
            continue;
        }
        if (object->process.state == UMICOM_PROCESS_EMPTY || object->process.identity == 0U ||
            object->process.pageCount > UMICOM_USER_MEMORY_MAX_PAGES) return UMICOM_REGISTRY_CORRUPT_STATE;
        if (object->references == 0U && object->process.state != UMICOM_PROCESS_CLEANUP_REQUIRED) {
            return UMICOM_REGISTRY_CORRUPT_STATE;
        }
        for (UmicomSize other = index + 1U; other < UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT; ++other) {
            if (registry->objects[other].occupied != UMICOM_FALSE &&
                registry->objects[other].process.identity == object->process.identity) {
                return UMICOM_REGISTRY_CORRUPT_STATE;
            }
        }
        ++snapshot.objects;
        if (object->references == 0U) ++snapshot.retainedWithoutHandles;
    }
    /* This validator checks registry ownership, not arbitrary corrupt page
     * tables. The existing mapper/loader remain their own validation authority. */
    if (outSnapshot != (UmicomKernelRegistrySnapshot *)0) *outSnapshot = snapshot;
    return UMICOM_REGISTRY_OK;
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistrySnapshotRead(
    UmicomKernelProcessRegistry *registry, UmicomKernelRegistrySnapshot *outSnapshot)
{
    if (outSnapshot == (UmicomKernelRegistrySnapshot *)0) return UMICOM_REGISTRY_INVALID_ARGUMENT;
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    status = UmicomRegistryInspect(registry, outSnapshot);
    return UmicomRegistryLeave(registry, status);
}

UmicomKernelRegistryStatus UmicomKernelProcessRegistryValidate(UmicomKernelProcessRegistry *registry)
{
    UmicomKernelRegistryStatus status = UmicomRegistryEnter(registry);
    if (status != UMICOM_REGISTRY_OK) return status;
    status = UmicomRegistryInspect(registry, (UmicomKernelRegistrySnapshot *)0);
    return UmicomRegistryLeave(registry, status);
}

const char *UmicomKernelRegistryStatusName(UmicomKernelRegistryStatus status)
{
    switch (status) {
        case UMICOM_REGISTRY_OK: return "ok";
        case UMICOM_REGISTRY_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_REGISTRY_NOT_INITIALISED: return "not-initialised";
        case UMICOM_REGISTRY_BAD_STATE: return "bad-state";
        case UMICOM_REGISTRY_BUSY: return "busy";
        case UMICOM_REGISTRY_INVALID_HANDLE: return "invalid-handle";
        case UMICOM_REGISTRY_WRONG_OWNER: return "wrong-owner";
        case UMICOM_REGISTRY_ACCESS_DENIED: return "access-denied";
        case UMICOM_REGISTRY_PROCESS_LIMIT: return "process-limit";
        case UMICOM_REGISTRY_HANDLE_LIMIT: return "handle-limit";
        case UMICOM_REGISTRY_OWNER_LIMIT: return "owner-limit";
        case UMICOM_REGISTRY_IDENTITY_EXHAUSTED: return "identity-exhausted";
        case UMICOM_REGISTRY_LOAD_FAILED: return "load-failed";
        case UMICOM_REGISTRY_RUN_FAILED: return "run-failed";
        case UMICOM_REGISTRY_CLEANUP_FAILED: return "cleanup-failed";
        case UMICOM_REGISTRY_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-registry-status";
    }
}
