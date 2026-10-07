/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/virtio_block.h
 *
 * Own a modern VirtIO MMIO block transport and two DMA frames for read-only
 * requests. The caller supplies a previously qualified transport catalogue;
 * addresses in an arbitrary device tree are not, by themselves, MMIO authority.
 *
 * A timeout ends the caller's wait, not the device's access to RAM. Queue and
 * bounce pages therefore remain owned until reset completion is observed.
 * This distinction is more important than recovering a failed command quickly.
 *
 * All calls are trusted, serial Kernel work on hart zero in the platform's Bare,
 * interrupts-disabled context. Domains and callback contexts must stay alive at
 * their original addresses. Never copy or reinitialise an established domain.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_VIRTIO_BLOCK_H
#define UMICOM_KERNEL_VIRTIO_BLOCK_H
#include "umicom/kernel/types.h"

#define UMICOM_BLOCK_SLOT_LIMIT 8U
#define UMICOM_BLOCK_SECTOR_BYTES 512U
#define UMICOM_BLOCK_MAX_SECTORS 8U
#define UMICOM_BLOCK_QUEUE_SIZE 8U
#define UMICOM_BLOCK_POLL_LIMIT 1000000U
#define UMICOM_BLOCK_CONFIG_RETRIES 16U
#define UMICOM_BLOCK_MAX_TIMEOUT_TICKS 100000000U

typedef enum UmicomKernelBlockStatus {
    UMICOM_BLOCK_OK,
    UMICOM_BLOCK_INVALID_ARGUMENT,
    UMICOM_BLOCK_BAD_STATE,
    UMICOM_BLOCK_UNSAFE_CONTEXT,
    UMICOM_BLOCK_BUSY,
    UMICOM_BLOCK_NO_DEVICE,
    UMICOM_BLOCK_NOT_BLOCK,
    UMICOM_BLOCK_BAD_MAGIC,
    UMICOM_BLOCK_UNSUPPORTED_TRANSPORT,
    UMICOM_BLOCK_ALREADY_ACTIVE,
    UMICOM_BLOCK_REQUIRED_FEATURE,
    UMICOM_BLOCK_WRITABLE_DEVICE,
    UMICOM_BLOCK_FEATURE_REFUSED,
    UMICOM_BLOCK_QUEUE_UNAVAILABLE,
    UMICOM_BLOCK_CONFIG_UNSTABLE,
    UMICOM_BLOCK_CONFIG_CHANGED,
    UMICOM_BLOCK_NO_MEMORY,
    UMICOM_BLOCK_RANGE,
    UMICOM_BLOCK_TIMEOUT,
    UMICOM_BLOCK_CLOCK_ERROR,
    UMICOM_BLOCK_DEVICE_ERROR,
    UMICOM_BLOCK_IO_ERROR,
    UMICOM_BLOCK_UNSUPPORTED_REQUEST,
    UMICOM_BLOCK_MALFORMED_COMPLETION,
    UMICOM_BLOCK_INVALID_HANDLE,
    UMICOM_BLOCK_RESET_PENDING,
    UMICOM_BLOCK_RELEASE_FAILED,
    UMICOM_BLOCK_CORRUPT_OWNER,
    UMICOM_BLOCK_NO_CATALOGUE,
    UMICOM_BLOCK_UNQUALIFIED_PLATFORM
} UmicomKernelBlockStatus;

typedef enum UmicomKernelBlockState {
    UMICOM_BLOCK_AVAILABLE,
    UMICOM_BLOCK_OPENING,
    UMICOM_BLOCK_READY,
    UMICOM_BLOCK_FAULTED,
    UMICOM_BLOCK_CLOSING,
    UMICOM_BLOCK_RETIRED
} UmicomKernelBlockState;

typedef UmicomU64 UmicomKernelBlockHandle;
typedef struct UmicomKernelBlockTransport {
    UmicomAddress base;
    UmicomSize bytes;
} UmicomKernelBlockTransport;

/* One narrow I/O boundary permits protocol testing without pretending that
 * ordinary host RAM implements MMIO selector, notify or reset semantics.
 * Barrier orders both DMA memory and device I/O. Clock is monotonic; a backward
 * value is an error. Callbacks must not re-enter this domain or throw faults.
 * Real hardware lacking coherent DMA needs a different, reviewed adapter. */
typedef struct UmicomKernelBlockOperations {
    UmicomU32 (*read32)(void *context, UmicomAddress address);
    void (*write32)(void *context, UmicomAddress address, UmicomU32 value);
    void (*barrier)(void *context);
    UmicomU64 (*clock)(void *context);
    UmicomBoolean (*allowed)(void *context);
    void *context;
} UmicomKernelBlockOperations;

typedef struct UmicomKernelBlockSlot {
    UmicomKernelBlockTransport transport;
    UmicomKernelBlockState state;
    UmicomKernelBlockStatus lastError;
    UmicomU32 generation;
    UmicomAddress queueFrame;
    UmicomAddress dataFrame;
    UmicomU64 sectors;
    UmicomU64 timeoutTicks;
    UmicomU64 requests;
    UmicomU32 configuration;
    UmicomU16 availableIndex;
    UmicomU16 usedIndex;
    UmicomBoolean claimed;
    UmicomBoolean exposed; /* Device may still access queues: reset must precede free. */
    UmicomBoolean stopped; /* Set only after observing Status == 0 after reset. */
} UmicomKernelBlockSlot;

typedef struct UmicomKernelBlockDomain {
    const struct UmicomKernelBlockDomain *self;
    UmicomKernelBlockOperations operations;
    UmicomSize count;
    UmicomBoolean ready;
    UmicomBoolean busy; /* Serial reentry guard, not an atomic multi-hart lock. */
    UmicomKernelBlockSlot slots[UMICOM_BLOCK_SLOT_LIMIT];
} UmicomKernelBlockDomain;

typedef struct UmicomKernelBlockInfo {
    UmicomAddress base;
    UmicomKernelBlockState state;
    UmicomKernelBlockStatus lastError;
    UmicomU32 transportRevision; /* Hardware protocol revision, not a delivery label. */
    UmicomU32 deviceId;
    UmicomU32 vendorId;
    UmicomU64 sectors;
    UmicomU64 requests;
    UmicomSize heldFrames;
    UmicomBoolean deviceMayAccessMemory;
} UmicomKernelBlockInfo;

/* Initialise from a trusted, non-overlapping transport list and zero-filled
 * storage. This acquires no physical pages and does not touch any MMIO register.
 * Input records/callback storage and outputs may not overlap the owner. */
UmicomKernelBlockStatus UmicomKernelBlockDomainInitialize(UmicomKernelBlockDomain *domain,
    const UmicomKernelBlockTransport *transports, UmicomSize count,
    const UmicomKernelBlockOperations *operations);
/* Probe has no MMIO writes. DeviceID zero ends discovery immediately: even
 * VendorID must not be read for an unpopulated modern transport slot. */
UmicomKernelBlockStatus UmicomKernelBlockProbe(UmicomKernelBlockDomain *domain,
    UmicomSize index, UmicomKernelBlockInfo *outInfo);
/* Open refuses a writable backend. Set *outHandle to zero before calling.
 * Success returns an owned handle. A setup error normally resets and releases
 * everything; if cleanup is incomplete, the returned handle remains nonzero
 * and MUST be closed/retried. lastError preserves the original setup failure.
 * Device status already nonzero is refused without resetting another driver. */
UmicomKernelBlockStatus UmicomKernelBlockOpen(UmicomKernelBlockDomain *domain,
    UmicomSize index, UmicomU64 timeoutTicks, UmicomKernelBlockHandle *outHandle);
/* Read one through eight complete 512-byte sectors. On every non-OK result,
 * the caller's output is unchanged. There is no WRITE/FLUSH/DISCARD command.
 * The trusted output buffer must remain valid throughout the synchronous call. */
UmicomKernelBlockStatus UmicomKernelBlockRead(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle, UmicomU64 firstSector, UmicomSize sectors,
    void *output, UmicomSize capacity);
/* Close resets before scrubbing/releasing DMA pages. Failed reset retains the
 * lease and every potentially exposed page; failed frame release is retryable.
 * Successfully closed handles cannot refer to a subsequent owner of the slot. */
UmicomKernelBlockStatus UmicomKernelBlockClose(UmicomKernelBlockDomain *domain,
    UmicomKernelBlockHandle handle);
const char *UmicomKernelBlockStatusName(UmicomKernelBlockStatus status);

/* Fixed-profile adapter and read-only console integration. Firmware capture
 * alone never opens a device. Independent recovery does not call these APIs. */
struct UmicomKernelHardwareCatalogue;
/* outTransports supplies UMICOM_BLOCK_SLOT_LIMIT records. Only a successful
 * complete qualification publishes entries and outCount; failures leave them
 * unchanged. These arrays are trusted Kernel storage and must not overlap. */
UmicomKernelBlockStatus UmicomKernelBlockSelectQemuTransports(
    const struct UmicomKernelHardwareCatalogue *catalogue,
    UmicomKernelBlockTransport *outTransports, UmicomSize *outCount);
UmicomKernelBlockStatus UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **outDomain);
typedef void (*UmicomKernelBlockOutput)(void *context, const char *text, UmicomSize bytes);
void UmicomKernelBlockReport(void *context, UmicomKernelBlockOutput output);
UmicomKernelBlockStatus UmicomKernelBlockInspectSector(UmicomSize index, UmicomU64 sector,
    void *context, UmicomKernelBlockOutput output);
UmicomKernelBlockStatus UmicomKernelBlockRetryClose(void);
void UmicomKernelBlockValidateExecution(void);
#endif /* UMICOM_KERNEL_VIRTIO_BLOCK_H */
