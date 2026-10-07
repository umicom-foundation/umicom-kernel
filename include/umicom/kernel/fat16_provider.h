/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/fat16_provider.h
 *
 * PURPOSE:
 *   Adapt the bounded FAT16 inspector to the existing typed VFS contract.
 *   Disk interpretation remains in that inspector; this layer owns canonical
 *   node identities, immutable metadata snapshots and open-description pins.
 *
 * EDUCATIONAL OVERVIEW:
 *   A cache slot is storage, whereas a node ID is an identity. Unpinned cache
 *   slots may be reused, but an issued ID is never issued again. Open files and
 *   mounted roots retain pins so unrelated pathname traversal cannot overwrite
 *   their records. This fixed cache allocates no Kernel frames or heap objects.
 *
 *   The reader and its transport context are borrowed until Close. The medium
 *   must remain immutable throughout that lifetime; this is neither media-change
 *   detection nor a host snapshot service. Long names and other FAT variants
 *   retain the existing inspector's explicit limitations.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FAT16_PROVIDER_H
#define UMICOM_KERNEL_FAT16_PROVIDER_H
#include "umicom/kernel/fat16_inspector.h"
#include "umicom/kernel/vfs.h"

#define UMICOM_FAT16_PROVIDER_NODE_LIMIT 64U
#define UMICOM_FAT16_PROVIDER_ROOT_ID ((UmicomKernelVfsNodeId)1U)
#define UMICOM_FAT16_PROVIDER_DIRECTORY_EPOCH ((UmicomU64)1U)

/* A transport can renew one monotonic deadline for each complete inspector
 * operation. The provider's busy guard is already set when begin runs. The
 * callback must not re-enter the provider, yield, retain caller buffers or
 * assume that every individual sector receives a new deadline. A refusal is
 * returned verbatim without issuing any disk I/O. NULL policy means no hook;
 * the reader must still impose its own finite transport bound. */
typedef struct UmicomKernelFat16ProviderIoPolicy {
    UmicomKernelVfsStatus (*begin)(void *context);
    void *context;
} UmicomKernelFat16ProviderIoPolicy;

/* Exposed for stable static storage and deliberate fault tests. Application
 * code must not edit these fields, copy a live owner or reset a closed owner. */
typedef struct UmicomKernelFat16ProviderNode {
    UmicomKernelVfsNodeId id;
    char path[UMICOM_FAT16_PATH_BYTES];
    UmicomKernelFat16Entry entry;
    UmicomSize pins;
    UmicomBoolean occupied;
} UmicomKernelFat16ProviderNode;

typedef struct UmicomKernelFat16Provider {
    const struct UmicomKernelFat16Provider *self;
    UmicomKernelVfsLifetime state;
    UmicomBoolean busy;
    UmicomSize pins; /* Mount root plus every independent open description. */
    UmicomKernelVfsNodeId nextNodeId; /* Zero means the identity space ended. */
    UmicomKernelDiskStatus lastDiskStatus; /* Exact last inspector media result. */
    UmicomKernelFat16ProviderIoPolicy ioPolicy;
    UmicomKernelFat16 volume;
    UmicomKernelFat16ProviderNode nodes[UMICOM_FAT16_PROVIDER_NODE_LIMIT];
    UmicomKernelFat16Directory listStage;
} UmicomKernelFat16Provider;

/* Use zero-filled stable storage. A successful Open admits exactly one owner
 * lifetime; Close never permits that owner to reopen. Failed admission leaves
 * the owner retryable, retaining only lastDiskStatus when an inspector call
 * occurred. Argument and policy failures do not fabricate a DiskStatus.
 *
 * The reader/policy structures are copied; their contexts remain borrowed.
 * All calls are serial trusted-Kernel operations with separate, non-overlapping
 * caller buffers. Node IDs returned without a pin are ephemeral cache handles.
 * Lookup folds ASCII short aliases to uppercase before consulting the inspector.
 * It refuses invalid alias syntax, paths and depth before admitting a record. */
UmicomKernelVfsStatus UmicomKernelFat16ProviderOpen(
    UmicomKernelFat16Provider *provider, const UmicomKernelDiskReader *reader,
    UmicomSize partition, const UmicomKernelFat16ProviderIoPolicy *policy);

/* Close is entirely local and returns BUSY while any pin remains, including
 * the mount root. Unmount first. An unavailable disk cannot prevent descriptor
 * release, local validation, metadata queries or provider Close. */
UmicomKernelVfsStatus UmicomKernelFat16ProviderClose(UmicomKernelFat16Provider *provider);

/* All twelve callbacks have their exact VFS types. Every mutation callback
 * returns READ_ONLY, even if a trusted caller deliberately creates a broad-
 * rights VFS client. The mount adapter should grant READ, QUERY, ENUMERATE and
 * DUPLICATE only: the existing VFS has no provider capability callback with
 * which to reject writable Open itself.
 *
 * Read accepts at most UMICOM_FAT16_READ_BYTES. A valid zero-byte read is OK/0
 * without media access. On backend failure the byte count is zero and caller
 * data is unchanged. Enumeration stages one complete bounded inspector list
 * before publishing one entry; an error changes neither the entry nor cursor.
 * The fixed nonzero directory epoch reflects the immutable-medium contract. */
const UmicomKernelVfsOperations *UmicomKernelFat16ProviderOperationsGet(void);

#endif /* UMICOM_KERNEL_FAT16_PROVIDER_H */
