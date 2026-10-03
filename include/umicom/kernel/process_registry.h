/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/process_registry.h
 *
 * PURPOSE:
 *   Keep loaded process objects at stable addresses and expose their operations
 *   through owner-scoped handles rather than lending out mutable pointers.
 *
 * EDUCATIONAL OVERVIEW:
 *   A process identity answers "which program instance?" A handle answers
 *   "which reference may this caller use, and what may it do?" They are not
 *   interchangeable. Several handles can refer to the same process, each with
 *   fewer rights than the original. Closing one reference must not destroy the
 *   object while another reference still exists.
 *
 *   This is a bounded, single-hart Kernel service, not a new user syscall ABI
 *   or a scheduler. The caller supplies an authenticated owner identity from
 *   Kernel state; a future syscall must never accept that identity from user
 *   arguments. A handle is an identifier, not a secret or a Kernel address.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_PROCESS_REGISTRY_H
#define UMICOM_KERNEL_PROCESS_REGISTRY_H
#include "umicom/kernel/process.h"

/* Fixed limits avoid a dependency on a heap before its ownership rules exist.
 * Exhaustion returns a result; it must not evict an object or recycle authority. */
#define UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT 8U
#define UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT 32U
#define UMICOM_PROCESS_REGISTRY_OWNER_LIMIT 8U

/* Low bits select a slot; high bits distinguish successive uses of that slot.
 * Zero is never issued. Exhausted generations retire instead of wrapping. */
typedef UmicomU64 UmicomKernelProcessHandle;
typedef UmicomU32 UmicomKernelProcessRights;
#define UMICOM_PROCESS_RIGHT_QUERY ((UmicomKernelProcessRights)1U)
#define UMICOM_PROCESS_RIGHT_RUN ((UmicomKernelProcessRights)2U)
#define UMICOM_PROCESS_RIGHT_DUPLICATE ((UmicomKernelProcessRights)4U)
#define UMICOM_PROCESS_RIGHT_TRANSFER ((UmicomKernelProcessRights)8U)
#define UMICOM_PROCESS_RIGHT_ALL ((UmicomKernelProcessRights)15U)

typedef enum UmicomKernelRegistryStatus {
    UMICOM_REGISTRY_OK,
    UMICOM_REGISTRY_INVALID_ARGUMENT,
    UMICOM_REGISTRY_NOT_INITIALISED,
    UMICOM_REGISTRY_BAD_STATE,
    UMICOM_REGISTRY_BUSY,
    UMICOM_REGISTRY_INVALID_HANDLE,
    UMICOM_REGISTRY_WRONG_OWNER,
    UMICOM_REGISTRY_ACCESS_DENIED,
    UMICOM_REGISTRY_PROCESS_LIMIT,
    UMICOM_REGISTRY_HANDLE_LIMIT,
    UMICOM_REGISTRY_OWNER_LIMIT,
    UMICOM_REGISTRY_IDENTITY_EXHAUSTED,
    UMICOM_REGISTRY_LOAD_FAILED,
    UMICOM_REGISTRY_RUN_FAILED,
    UMICOM_REGISTRY_CLEANUP_FAILED,
    UMICOM_REGISTRY_CORRUPT_STATE
} UmicomKernelRegistryStatus;

/* These records are visible only to allow static Kernel allocation. Their
 * fields are private to process_registry.c; do not edit or copy a live owner. */
typedef struct UmicomKernelProcessRecord {
    UmicomKernelProcess process; /* Stable storage required by the existing loader. */
    UmicomSize references; /* Number of issued handles, not the running state. */
    UmicomBoolean occupied; /* Also true for a failed load retained for cleanup. */
} UmicomKernelProcessRecord;

typedef struct UmicomKernelProcessHandleRecord {
    UmicomU64 owner; /* Principal chosen by Kernel admission, not program identity. */
    UmicomU32 generation;
    UmicomU32 objectIndex;
    UmicomKernelProcessRights rights;
    UmicomBoolean occupied;
    UmicomBoolean retired; /* A maximum-generation slot will never be reused. */
} UmicomKernelProcessHandleRecord;

typedef struct UmicomKernelProcessRegistry {
    const struct UmicomKernelProcessRegistry *self; /* Reject accidental struct copies. */
    UmicomU64 nextIdentity; /* Nonzero, monotonic process identity within this domain. */
    UmicomBoolean initialised;
    UmicomBoolean busy; /* Reentry guard, not an atomic lock or SMP exclusion. */
    UmicomKernelProcessRecord objects[UMICOM_PROCESS_REGISTRY_OBJECT_LIMIT];
    UmicomKernelProcessHandleRecord handles[UMICOM_PROCESS_REGISTRY_HANDLE_LIMIT];
} UmicomKernelProcessRegistry;

/* Snapshots deliberately contain values, not pointers, physical addresses or
 * page-table roots. Exit values are meaningful only when state is EXITED. */
typedef struct UmicomKernelProcessInfo {
    UmicomU64 identity;
    UmicomKernelProcessState state;
    UmicomKernelProcessRights rights;
    UmicomSize references;
    UmicomSize backingPages;
    UmicomBoolean quiesced;
    UmicomU64 exitValue;
    UmicomU64 trapCause;
    UmicomU64 systemCalls;
} UmicomKernelProcessInfo;

typedef struct UmicomKernelRegistrySnapshot {
    UmicomSize objects;
    UmicomSize handles;
    UmicomSize retiredHandles;
    UmicomSize retainedWithoutHandles;
} UmicomKernelRegistrySnapshot;

/* Initialise zero-filled, stable Kernel storage once. Even an empty live
 * registry cannot be reinitialised: resetting generations revives stale tokens.
 * All input/output pointers below must be Kernel-owned and non-overlapping
 * with the registry's storage. The caller serialises all access on one hart. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryInitialize(UmicomKernelProcessRegistry *registry);

/* Kernel-only admission: validate/load a private image, then publish its first
 * handle. Outputs are unchanged on failure. No success handle exists until the
 * loader completes. An unusual rollback failure retains an unreferenced object
 * for explicit Reap instead of losing its memory ownership. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryCreate(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    const UmicomU8 *image, UmicomSize imageBytes, UmicomKernelProcessRights rights,
    UmicomKernelProcessHandle *outHandle);

/* QUERY returns a value snapshot. RUN delegates exactly once to the existing
 * READY-image execution path; an exited or faulted image is not restartable. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryQuery(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessInfo *outInfo);
UmicomKernelRegistryStatus UmicomKernelProcessRegistryRun(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomU64 argument, UmicomU64 deadlineTicks);

/* Duplicate requires DUPLICATE and can only reduce rights. Grant also requires
 * TRANSFER and issues a different handle bound to the receiving owner. Neither
 * operation copies the process or its writable memory. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryDuplicate(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessRights rights,
    UmicomKernelProcessHandle *outHandle);
UmicomKernelRegistryStatus UmicomKernelProcessRegistryGrant(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomU64 receivingOwner,
    UmicomKernelProcessRights rights, UmicomKernelProcessHandle *outHandle);

/* Restriction is irreversible on this handle. Zero rights are valid: the
 * owner can still close it, so reducing authority never prevents cleanup. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryRestrict(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle, UmicomKernelProcessRights rights);

/* Close one reference. The final close destroys only a quiesced image. If
 * teardown refuses or fails, retain the handle and object for diagnosis/retry.
 * This operation does not silently terminate a running process. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryClose(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner,
    UmicomKernelProcessHandle handle);

/* Kernel lifecycle administration, not an authority available to user input.
 * CloseOwner can make partial progress; outClosed reports completed closes on
 * an error. Other owners' references remain valid. Reap retries only objects
 * retained without handles after a failed load. Neither forces unsafe frees. */
UmicomKernelRegistryStatus UmicomKernelProcessRegistryCloseOwner(
    UmicomKernelProcessRegistry *registry, UmicomU64 owner, UmicomSize *outClosed);
UmicomKernelRegistryStatus UmicomKernelProcessRegistryReap(
    UmicomKernelProcessRegistry *registry, UmicomSize *outReaped);
UmicomKernelRegistryStatus UmicomKernelProcessRegistrySnapshotRead(
    UmicomKernelProcessRegistry *registry, UmicomKernelRegistrySnapshot *outSnapshot);
UmicomKernelRegistryStatus UmicomKernelProcessRegistryValidate(UmicomKernelProcessRegistry *registry);
const char *UmicomKernelRegistryStatusName(UmicomKernelRegistryStatus status);
void UmicomKernelProcessRegistryValidateExecution(void);
#endif /* UMICOM_KERNEL_PROCESS_REGISTRY_H */
