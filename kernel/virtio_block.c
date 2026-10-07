/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/virtio_block.c
 *
 * Modern MMIO transport ownership and a deliberately small read-only block path.
 * Two independently allocated frames are sufficient: one contains the split
 * queue, request header and status; the other is a device-writable bounce page.
 * User or caller buffers are never submitted to DMA. No disk-write request can
 * be constructed by this interface.
 *
 * Reset acknowledgement, not a timeout, proves that queue memory is no longer
 * in use. Every failure path below keeps enough ownership to retry that reset
 * and release. A malicious DMA-capable device is outside this no-IOMMU model.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/virtio_block_protocol.h"
#include "umicom/kernel/physical_memory.h"

_Static_assert(UMICOM_BLOCK_QUEUE_SIZE == 8U, "Review the bounded queue layout when changing its size");
_Static_assert(UMICOM_BLOCK_MAX_SECTORS * UMICOM_BLOCK_SECTOR_BYTES == 4096U, "A request fits its bounce frame");

static void UmicomBlockClear(UmicomAddress address, UmicomSize bytes)
{
    volatile UmicomU8 *target = (volatile UmicomU8 *)address;
    for (UmicomSize i = 0U; i < bytes; ++i) target[i] = 0U;
}
static UmicomBoolean UmicomBlockZero(const void *address, UmicomSize bytes)
{
    const UmicomU8 *p = (const UmicomU8 *)address;
    for (UmicomSize i = 0U; i < bytes; ++i) if (p[i] != 0U) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomBlockOverlap(UmicomAddress first, UmicomSize firstBytes,
    UmicomAddress second, UmicomSize secondBytes)
{
    /* Subtraction avoids overflowing an exclusive end address. Inputs have
     * already been bounded, and a zero-length region overlaps nothing. */
    if (!firstBytes || !secondBytes) return UMICOM_FALSE;
    return first <= second ? second - first < firstBytes : first - second < secondBytes;
}
static UmicomKernelBlockStatus UmicomBlockEnter(UmicomKernelBlockDomain *domain)
{
    if (!domain || domain->self != domain || !domain->ready ||
        !domain->count || domain->count > UMICOM_BLOCK_SLOT_LIMIT) return UMICOM_BLOCK_BAD_STATE;
    if (domain->busy) return UMICOM_BLOCK_BUSY;
    if (!domain->operations.allowed(domain->operations.context)) return UMICOM_BLOCK_UNSAFE_CONTEXT;
    domain->busy = UMICOM_TRUE;
    return UMICOM_BLOCK_OK;
}
static UmicomKernelBlockStatus UmicomBlockLeave(UmicomKernelBlockDomain *domain, UmicomKernelBlockStatus status)
{
    domain->busy = UMICOM_FALSE;
    return status;
}
static UmicomU32 UmicomBlockReadRegister(UmicomKernelBlockDomain *d, const UmicomKernelBlockSlot *s, UmicomU32 offset)
{
    /* Selectors and configuration-generation reads must not pass one another
     * on a weakly ordered I/O bus. Keep that rule in the common access path. */
    d->operations.barrier(d->operations.context);
    const UmicomU32 value = d->operations.read32(d->operations.context, s->transport.base + offset);
    d->operations.barrier(d->operations.context);
    return value;
}
static void UmicomBlockWriteRegister(UmicomKernelBlockDomain *d, const UmicomKernelBlockSlot *s,
    UmicomU32 offset, UmicomU32 value)
{
    d->operations.barrier(d->operations.context);
    d->operations.write32(d->operations.context, s->transport.base + offset, value);
    d->operations.barrier(d->operations.context);
}
static void UmicomBlockBarrier(UmicomKernelBlockDomain *d)
{
    d->operations.barrier(d->operations.context);
}
static UmicomKernelBlockHandle UmicomBlockToken(const UmicomKernelBlockSlot *s, UmicomSize index)
{
    return ((UmicomU64)s->generation << 32U) | (UmicomU64)(index + 1U);
}
static UmicomKernelBlockSlot *UmicomBlockFind(UmicomKernelBlockDomain *d, UmicomKernelBlockHandle token)
{
    const UmicomU32 number = (UmicomU32)token;
    if (!number || number > d->count) return 0;
    UmicomKernelBlockSlot *s = &d->slots[number - 1U];
    return s->claimed && s->generation == (UmicomU32)(token >> 32U) ? s : 0;
}
static UmicomBoolean UmicomBlockOwnsFrame(UmicomAddress frame)
{
    UmicomKernelPhysicalFrameState state;
    return frame && UmicomKernelPhysicalMemoryFrameQuery(frame, &state) == UMICOM_KERNEL_MEMORY_OK &&
        state == UMICOM_PHYSICAL_FRAME_ALLOCATED;
}
static UmicomKernelBlockStatus UmicomBlockWaitZero(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s)
{
    const UmicomU64 start = d->operations.clock(d->operations.context);
    for (UmicomSize poll = 0U; poll < UMICOM_BLOCK_POLL_LIMIT; ++poll) {
        if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_STATUS) == 0U) {
            /* A compliant device can no longer touch queue memory after this
             * observation. Order that observation before any memory scrubbing. */
            UmicomBlockBarrier(d);
            s->stopped = UMICOM_TRUE;
            s->exposed = UMICOM_FALSE;
            return UMICOM_BLOCK_OK;
        }
        const UmicomU64 now = d->operations.clock(d->operations.context);
        if (now < start || now - start >= s->timeoutTicks) return UMICOM_BLOCK_RESET_PENDING;
    }
    /* A stopped clock cannot make teardown spin forever. Crucially, running
     * out of polls does not clear exposed or pretend the device was reset. */
    return UMICOM_BLOCK_RESET_PENDING;
}
static UmicomKernelBlockStatus UmicomBlockReset(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s)
{
    s->stopped = UMICOM_FALSE;
    UmicomBlockBarrier(d);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_STATUS, 0U);
    return UmicomBlockWaitZero(d, s);
}
static UmicomKernelBlockStatus UmicomBlockRelease(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s)
{
    s->state = UMICOM_BLOCK_CLOSING;
    if (!s->stopped || s->exposed) {
        const UmicomKernelBlockStatus reset = UmicomBlockReset(d, s);
        if (reset != UMICOM_BLOCK_OK) return reset;
    }
    /* Clear one ownership record only after the original allocator accepts
     * its release. A later failed release leaves a smaller, retryable owner. */
    UmicomAddress *frames[2] = {&s->dataFrame, &s->queueFrame};
    for (UmicomSize i = 0U; i < 2U; ++i) {
        if (!*frames[i]) continue;
        if (!UmicomBlockOwnsFrame(*frames[i])) return UMICOM_BLOCK_CORRUPT_OWNER;
        UmicomBlockClear(*frames[i], UMICOM_KERNEL_PAGE_SIZE);
        if (UmicomKernelPhysicalMemoryFreeFrame(*frames[i]) != UMICOM_KERNEL_MEMORY_OK)
            return UMICOM_BLOCK_RELEASE_FAILED;
        *frames[i] = 0U;
    }
    s->claimed = UMICOM_FALSE;
    s->sectors = 0U;
    s->availableIndex = 0U;
    s->usedIndex = 0U;
    s->state = s->generation == 0xffffffffU ? UMICOM_BLOCK_RETIRED : UMICOM_BLOCK_AVAILABLE;
    return UMICOM_BLOCK_OK;
}
static UmicomKernelBlockStatus UmicomBlockOpenError(UmicomKernelBlockDomain *d,
    UmicomKernelBlockSlot *s, UmicomKernelBlockStatus cause, UmicomKernelBlockHandle *outHandle)
{
    s->lastError = cause;
    if (!s->stopped && cause != UMICOM_BLOCK_RESET_PENDING) {
        /* A rejected initialisation is not a working device. Mark failure
         * without clearing existing status bits, then perform ordinary reset.
         * Never interfere with a reset that is already awaiting acknowledgement. */
        const UmicomU32 state = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_STATUS);
        if (state) UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_STATUS, state | UMICOM_VIRTIO_FAILED);
    }
    const UmicomKernelBlockStatus closed = UmicomBlockRelease(d, s);
    if (closed == UMICOM_BLOCK_OK) *outHandle = 0U;
    /* If cleanup fails, the already-published handle survives. Neither the
     * metadata slot nor a DMA page can be lent to somebody else meanwhile. */
    return UmicomBlockLeave(d, closed == UMICOM_BLOCK_OK ? cause : closed);
}
static UmicomKernelBlockStatus UmicomBlockIdentify(UmicomKernelBlockDomain *d,
    const UmicomKernelBlockSlot *s, UmicomKernelBlockInfo *info)
{
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_MAGIC_OFFSET) != UMICOM_VIRTIO_MAGIC)
        return UMICOM_BLOCK_BAD_MAGIC;
    info->transportRevision = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_VERSION_OFFSET);
    if (info->transportRevision != UMICOM_VIRTIO_MMIO_MODERN) return UMICOM_BLOCK_UNSUPPORTED_TRANSPORT;
    info->deviceId = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_DEVICE_ID);
    if (!info->deviceId) return UMICOM_BLOCK_NO_DEVICE;
    /* No register after DeviceID may be read for an empty transport. This rule
     * applies even to a supposedly harmless vendor observation. */
    info->vendorId = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_VENDOR_ID);
    return info->deviceId == UMICOM_VIRTIO_BLOCK_DEVICE ? UMICOM_BLOCK_OK : UMICOM_BLOCK_NOT_BLOCK;
}
UmicomKernelBlockStatus UmicomKernelBlockDomainInitialize(UmicomKernelBlockDomain *d,
    const UmicomKernelBlockTransport *transports, UmicomSize count, const UmicomKernelBlockOperations *ops)
{
    if (!d || !transports || !ops || !count || count > UMICOM_BLOCK_SLOT_LIMIT ||
        !ops->read32 || !ops->write32 || !ops->barrier || !ops->clock || !ops->allowed)
        return UMICOM_BLOCK_INVALID_ARGUMENT;
    if (!UmicomBlockZero(d, sizeof(*d))) return UMICOM_BLOCK_BAD_STATE;
    for (UmicomSize i = 0U; i < count; ++i) {
        const UmicomKernelBlockTransport *t = &transports[i];
        if (!t->base || t->base % 4U || t->bytes < 0x108U ||
            t->bytes > 4096U || t->base > ~(UmicomAddress)0U - t->bytes)
            return UMICOM_BLOCK_INVALID_ARGUMENT;
        for (UmicomSize j = 0U; j < i; ++j)
            if (UmicomBlockOverlap(t->base, t->bytes, transports[j].base, transports[j].bytes))
                return UMICOM_BLOCK_INVALID_ARGUMENT;
    }
    /* Commit only after the entire list is valid; initialise does no MMIO. */
    d->self = d;
    d->operations = *ops;
    d->count = count;
    for (UmicomSize i = 0U; i < count; ++i) d->slots[i].transport = transports[i];
    d->ready = UMICOM_TRUE;
    return UMICOM_BLOCK_OK;
}
UmicomKernelBlockStatus UmicomKernelBlockProbe(UmicomKernelBlockDomain *d,
    UmicomSize index, UmicomKernelBlockInfo *outInfo)
{
    if (!outInfo) return UMICOM_BLOCK_INVALID_ARGUMENT;
    UmicomKernelBlockStatus status = UmicomBlockEnter(d);
    if (status != UMICOM_BLOCK_OK) return status;
    if (index >= d->count) return UmicomBlockLeave(d, UMICOM_BLOCK_INVALID_ARGUMENT);
    const UmicomKernelBlockSlot *s = &d->slots[index];
    UmicomKernelBlockInfo info = {0};
    info.base = s->transport.base;
    info.state = s->state;
    info.lastError = s->lastError;
    info.sectors = s->sectors;
    info.requests = s->requests;
    info.heldFrames = (s->queueFrame ? 1U : 0U) + (s->dataFrame ? 1U : 0U);
    info.deviceMayAccessMemory = s->exposed;
    status = UmicomBlockIdentify(d, s, &info);
    *outInfo = info; /* Even an empty or unsupported slot has useful observations. */
    return UmicomBlockLeave(d, status);
}
static UmicomKernelBlockStatus UmicomBlockCapacity(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s)
{
    for (UmicomSize retry = 0U; retry < UMICOM_BLOCK_CONFIG_RETRIES; ++retry) {
        const UmicomU32 before = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_CONFIG_GENERATION);
        const UmicomU32 low = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_CAPACITY_LOW);
        const UmicomU32 high = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_CAPACITY_HIGH);
        const UmicomU32 after = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_CONFIG_GENERATION);
        if (before != after) continue;
        s->sectors = ((UmicomU64)high << 32U) | low;
        s->configuration = after;
        return s->sectors ? UMICOM_BLOCK_OK : UMICOM_BLOCK_RANGE;
    }
    return UMICOM_BLOCK_CONFIG_UNSTABLE;
}
static void UmicomBlockAddressRegister(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s,
    UmicomU32 lowRegister, UmicomAddress address)
{
    UmicomBlockWriteRegister(d, s, lowRegister, (UmicomU32)address);
    UmicomBlockWriteRegister(d, s, lowRegister + 4U, (UmicomU32)((UmicomU64)address >> 32U));
}
UmicomKernelBlockStatus UmicomKernelBlockOpen(UmicomKernelBlockDomain *d,
    UmicomSize index, UmicomU64 timeoutTicks, UmicomKernelBlockHandle *outHandle)
{
    if (!outHandle || !timeoutTicks || timeoutTicks > UMICOM_BLOCK_MAX_TIMEOUT_TICKS)
        return UMICOM_BLOCK_INVALID_ARGUMENT;
    UmicomKernelBlockStatus status = UmicomBlockEnter(d);
    if (status != UMICOM_BLOCK_OK) return status;
    if (index >= d->count) return UmicomBlockLeave(d, UMICOM_BLOCK_INVALID_ARGUMENT);
    UmicomKernelBlockSlot *s = &d->slots[index];
    if (s->claimed) return UmicomBlockLeave(d, UMICOM_BLOCK_BUSY);
    if (s->state != UMICOM_BLOCK_AVAILABLE || s->generation == 0xffffffffU)
        return UmicomBlockLeave(d, UMICOM_BLOCK_BAD_STATE);
    UmicomKernelBlockInfo info = {0};
    status = UmicomBlockIdentify(d, s, &info);
    if (status != UMICOM_BLOCK_OK) return UmicomBlockLeave(d, status);
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_STATUS) != 0U)
        return UmicomBlockLeave(d, UMICOM_BLOCK_ALREADY_ACTIVE);
    /* Claim before the first write. Another driver with nonzero device status
     * was refused above; another user of this domain cannot pass claimed. */
    ++s->generation;
    s->claimed = UMICOM_TRUE;
    s->state = UMICOM_BLOCK_OPENING;
    s->lastError = UMICOM_BLOCK_OK;
    s->timeoutTicks = timeoutTicks;
    s->requests = 0U;
    *outHandle = UmicomBlockToken(s, index);
    status = UmicomBlockReset(d, s);
    if (status != UMICOM_BLOCK_OK) return UmicomBlockOpenError(d, s, status, outHandle);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_STATUS, UMICOM_VIRTIO_ACKNOWLEDGE);
    s->stopped = UMICOM_FALSE;
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_STATUS, UMICOM_VIRTIO_ACKNOWLEDGE | UMICOM_VIRTIO_DRIVER);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_DEVICE_FEATURES_SELECT, 0U);
    const UmicomU32 lowFeatures = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_DEVICE_FEATURES);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_DEVICE_FEATURES_SELECT, 1U);
    const UmicomU32 highFeatures = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_DEVICE_FEATURES);
    if (!(highFeatures & UMICOM_VIRTIO_FEATURE_MODERN_HIGH))
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_REQUIRED_FEATURE, outHandle);
    if (!(lowFeatures & UMICOM_VIRTIO_READ_ONLY))
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_WRITABLE_DEVICE, outHandle);
    /* Accept exactly what is implemented: modern semantics and a read-only
     * backend. Packed rings, indirect descriptors, EVENT_IDX, IOMMUs and live
     * resize are not silently enabled simply because a device offers them. */
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_DRIVER_FEATURES_SELECT, 0U);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_DRIVER_FEATURES, UMICOM_VIRTIO_READ_ONLY);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_DRIVER_FEATURES_SELECT, 1U);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_DRIVER_FEATURES, UMICOM_VIRTIO_FEATURE_MODERN_HIGH);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_STATUS, 11U);
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_STATUS) != 11U)
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_FEATURE_REFUSED, outHandle);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_QUEUE_SELECT, 0U);
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_QUEUE_READY) != 0U ||
        UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_QUEUE_MAX) < UMICOM_BLOCK_QUEUE_SIZE)
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_QUEUE_UNAVAILABLE, outHandle);
    status = UmicomBlockCapacity(d, s);
    if (status != UMICOM_BLOCK_OK) return UmicomBlockOpenError(d, s, status, outHandle);
    if (UmicomKernelPhysicalMemoryAllocateFrame(&s->queueFrame) != UMICOM_KERNEL_MEMORY_OK ||
        UmicomKernelPhysicalMemoryAllocateFrame(&s->dataFrame) != UMICOM_KERNEL_MEMORY_OK)
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_NO_MEMORY, outHandle);
    UmicomBlockClear(s->queueFrame, UMICOM_KERNEL_PAGE_SIZE);
    UmicomBlockClear(s->dataFrame, UMICOM_KERNEL_PAGE_SIZE);
    /* All modern multibyte queue fields are little-endian. The platform adapter
     * qualifies RV64 little-endian and coherent physical addressing. Volatile
     * alone is not an ordering primitive; barriers bracket publication below. */
    *(volatile UmicomU16 *)(s->queueFrame + UMICOM_VIRTIO_AVAIL_OFFSET) = 1U; /* NO_INTERRUPT hint. */
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_QUEUE_SIZE, UMICOM_BLOCK_QUEUE_SIZE);
    UmicomBlockAddressRegister(d, s, UMICOM_VIRTIO_QUEUE_DESC_LOW, s->queueFrame);
    UmicomBlockAddressRegister(d, s, UMICOM_VIRTIO_QUEUE_AVAIL_LOW, s->queueFrame + UMICOM_VIRTIO_AVAIL_OFFSET);
    UmicomBlockAddressRegister(d, s, UMICOM_VIRTIO_QUEUE_USED_LOW, s->queueFrame + UMICOM_VIRTIO_USED_OFFSET);
    UmicomBlockBarrier(d);
    s->exposed = UMICOM_TRUE; /* Conservatively retain pages from first device publication. */
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_QUEUE_READY, 1U);
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_QUEUE_READY) != 1U)
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_QUEUE_UNAVAILABLE, outHandle);
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_STATUS, UMICOM_VIRTIO_RUNNING_STATUS);
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_STATUS) != UMICOM_VIRTIO_RUNNING_STATUS)
        return UmicomBlockOpenError(d, s, UMICOM_BLOCK_DEVICE_ERROR, outHandle);
    s->state = UMICOM_BLOCK_READY;
    return UmicomBlockLeave(d, UMICOM_BLOCK_OK);
}
static void UmicomBlockDescriptor(UmicomAddress queue, UmicomSize index, UmicomAddress address,
    UmicomU32 bytes, UmicomU16 flags, UmicomU16 next)
{
    const UmicomAddress descriptor = queue + index * UMICOM_VIRTIO_DESC_BYTES;
    *(volatile UmicomU64 *)descriptor = (UmicomU64)address;
    *(volatile UmicomU32 *)(descriptor + 8U) = bytes;
    *(volatile UmicomU16 *)(descriptor + 12U) = flags;
    *(volatile UmicomU16 *)(descriptor + 14U) = next;
}
static UmicomKernelBlockStatus UmicomBlockLive(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s)
{
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_STATUS) != UMICOM_VIRTIO_RUNNING_STATUS)
        return UMICOM_BLOCK_DEVICE_ERROR;
    if (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_CONFIG_GENERATION) != s->configuration ||
        (UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_INTERRUPT_STATUS) & 2U))
        return UMICOM_BLOCK_CONFIG_CHANGED;
    return UMICOM_BLOCK_OK;
}
static UmicomKernelBlockStatus UmicomBlockReadError(UmicomKernelBlockDomain *d, UmicomKernelBlockSlot *s,
    UmicomKernelBlockStatus cause)
{
    s->lastError = cause;
    s->state = UMICOM_BLOCK_FAULTED;
    /* Stop DMA promptly but retain the lease and pages for explicit Close.
     * On an unacknowledged reset not even the bounce buffer may be scrubbed. */
    const UmicomKernelBlockStatus reset = UmicomBlockReset(d, s);
    return UmicomBlockLeave(d, reset == UMICOM_BLOCK_OK ? cause : reset);
}
UmicomKernelBlockStatus UmicomKernelBlockRead(UmicomKernelBlockDomain *d,
    UmicomKernelBlockHandle handle, UmicomU64 firstSector, UmicomSize sectors, void *output, UmicomSize capacity)
{
    if (!output || !sectors || sectors > UMICOM_BLOCK_MAX_SECTORS) return UMICOM_BLOCK_INVALID_ARGUMENT;
    const UmicomSize bytes = sectors * UMICOM_BLOCK_SECTOR_BYTES;
    if (capacity < bytes || (UmicomAddress)output > ~(UmicomAddress)0U - bytes) return UMICOM_BLOCK_RANGE;
    UmicomKernelBlockStatus status = UmicomBlockEnter(d);
    if (status != UMICOM_BLOCK_OK) return status;
    UmicomKernelBlockSlot *s = UmicomBlockFind(d, handle);
    if (!s) return UmicomBlockLeave(d, UMICOM_BLOCK_INVALID_HANDLE);
    if (s->state != UMICOM_BLOCK_READY) return UmicomBlockLeave(d, UMICOM_BLOCK_BAD_STATE);
    if (firstSector >= s->sectors || sectors > s->sectors - firstSector)
        return UmicomBlockLeave(d, UMICOM_BLOCK_RANGE);
    if (UmicomBlockOverlap((UmicomAddress)output, bytes, (UmicomAddress)d, sizeof(*d)) ||
        UmicomBlockOverlap((UmicomAddress)output, bytes, s->queueFrame, UMICOM_KERNEL_PAGE_SIZE) ||
        UmicomBlockOverlap((UmicomAddress)output, bytes, s->dataFrame, UMICOM_KERNEL_PAGE_SIZE))
        return UmicomBlockLeave(d, UMICOM_BLOCK_INVALID_ARGUMENT);
    /* Another slot can retain live DMA after a refused reset. A caller buffer
     * must not alias any such frame, even though this request uses a different
     * device. No device receives arbitrary caller pointers as descriptors. */
    for (UmicomSize i = 0U; i < d->count; ++i) {
        const UmicomKernelBlockSlot *other = &d->slots[i];
        if ((other->queueFrame && UmicomBlockOverlap((UmicomAddress)output, bytes, other->queueFrame, 4096U)) ||
            (other->dataFrame && UmicomBlockOverlap((UmicomAddress)output, bytes, other->dataFrame, 4096U)))
            return UmicomBlockLeave(d, UMICOM_BLOCK_INVALID_ARGUMENT);
    }
    if (!UmicomBlockOwnsFrame(s->queueFrame) || !UmicomBlockOwnsFrame(s->dataFrame) || s->queueFrame == s->dataFrame)
        return UmicomBlockReadError(d, s, UMICOM_BLOCK_CORRUPT_OWNER);
    status = UmicomBlockLive(d, s);
    if (status != UMICOM_BLOCK_OK) return UmicomBlockReadError(d, s, status);
    volatile UmicomU16 *const usedIndex = (volatile UmicomU16 *)(s->queueFrame + UMICOM_VIRTIO_USED_OFFSET + 2U);
    if (*usedIndex != s->usedIndex) return UmicomBlockReadError(d, s, UMICOM_BLOCK_MALFORMED_COMPLETION);
    /* Only one chain is outstanding. Clearing the entire bounce page prevents
     * an incomplete device response from disclosing data from the last request. */
    UmicomBlockClear(s->dataFrame, UMICOM_KERNEL_PAGE_SIZE);
    const UmicomAddress header = s->queueFrame + UMICOM_VIRTIO_HEADER_OFFSET;
    *(volatile UmicomU32 *)header = UMICOM_VIRTIO_REQUEST_READ;
    *(volatile UmicomU32 *)(header + 4U) = 0U;
    *(volatile UmicomU64 *)(header + 8U) = firstSector;
    *(volatile UmicomU8 *)(s->queueFrame + UMICOM_VIRTIO_RESULT_OFFSET) = 0xffU;
    UmicomBlockDescriptor(s->queueFrame, 0U, header, 16U, UMICOM_VIRTIO_DESCRIPTOR_NEXT, 1U);
    UmicomBlockDescriptor(s->queueFrame, 1U, s->dataFrame, (UmicomU32)bytes,
        UMICOM_VIRTIO_DESCRIPTOR_NEXT | UMICOM_VIRTIO_DESCRIPTOR_WRITE, 2U);
    UmicomBlockDescriptor(s->queueFrame, 2U, s->queueFrame + UMICOM_VIRTIO_RESULT_OFFSET,
        1U, UMICOM_VIRTIO_DESCRIPTOR_WRITE, 0U);
    const UmicomAddress available = s->queueFrame + UMICOM_VIRTIO_AVAIL_OFFSET;
    *(volatile UmicomU16 *)(available + 4U + 2U * (s->availableIndex % UMICOM_BLOCK_QUEUE_SIZE)) = 0U;
    UmicomBlockBarrier(d); /* Descriptors and available entry precede the index. */
    s->availableIndex = (UmicomU16)(s->availableIndex + 1U);
    *(volatile UmicomU16 *)(available + 2U) = s->availableIndex;
    UmicomBlockBarrier(d); /* The published index precedes the doorbell. */
    UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_QUEUE_NOTIFY, 0U);
    const UmicomU64 start = d->operations.clock(d->operations.context);
    UmicomBoolean completed = UMICOM_FALSE;
    for (UmicomSize poll = 0U; poll < UMICOM_BLOCK_POLL_LIMIT; ++poll) {
        const UmicomU16 observed = *usedIndex;
        if (observed != s->usedIndex) {
            if ((UmicomU16)(observed - s->usedIndex) != 1U)
                return UmicomBlockReadError(d, s, UMICOM_BLOCK_MALFORMED_COMPLETION);
            completed = UMICOM_TRUE;
            break;
        }
        status = UmicomBlockLive(d, s);
        if (status != UMICOM_BLOCK_OK) return UmicomBlockReadError(d, s, status);
        const UmicomU64 now = d->operations.clock(d->operations.context);
        if (now < start) return UmicomBlockReadError(d, s, UMICOM_BLOCK_CLOCK_ERROR);
        if (now - start >= s->timeoutTicks) return UmicomBlockReadError(d, s, UMICOM_BLOCK_TIMEOUT);
    }
    if (!completed) return UmicomBlockReadError(d, s, UMICOM_BLOCK_TIMEOUT);
    UmicomBlockBarrier(d); /* Observe device buffers only after the used index. */
    const UmicomAddress used = s->queueFrame + UMICOM_VIRTIO_USED_OFFSET + 4U +
        8U * (s->usedIndex % UMICOM_BLOCK_QUEUE_SIZE);
    const UmicomU32 id = *(volatile UmicomU32 *)used;
    const UmicomU32 length = *(volatile UmicomU32 *)(used + 4U);
    const UmicomU8 result = *(volatile UmicomU8 *)(s->queueFrame + UMICOM_VIRTIO_RESULT_OFFSET);
    if (id != 0U || !length || length > bytes + 1U || result > UMICOM_VIRTIO_RESULT_UNSUPPORTED)
        return UmicomBlockReadError(d, s, UMICOM_BLOCK_MALFORMED_COMPLETION);
    status = UmicomBlockLive(d, s);
    if (status != UMICOM_BLOCK_OK) return UmicomBlockReadError(d, s, status);
    /* A valid error completion returns no caller data, but the consumed chain
     * can be reused. Success requires the full payload plus status to be used. */
    if (result == UMICOM_VIRTIO_RESULT_OK && length != bytes + 1U)
        return UmicomBlockReadError(d, s, UMICOM_BLOCK_MALFORMED_COMPLETION);
    s->usedIndex = (UmicomU16)(s->usedIndex + 1U);
    if (s->requests != ~(UmicomU64)0U) ++s->requests;
    const UmicomU32 pending = UmicomBlockReadRegister(d, s, UMICOM_VIRTIO_INTERRUPT_STATUS);
    if (pending & ~1U) return UmicomBlockReadError(d, s, UMICOM_BLOCK_CONFIG_CHANGED);
    if (pending & 1U) UmicomBlockWriteRegister(d, s, UMICOM_VIRTIO_INTERRUPT_ACK, 1U);
    status = result == UMICOM_VIRTIO_RESULT_OK ? UMICOM_BLOCK_OK :
        (result == UMICOM_VIRTIO_RESULT_IO_ERROR ? UMICOM_BLOCK_IO_ERROR : UMICOM_BLOCK_UNSUPPORTED_REQUEST);
    if (status == UMICOM_BLOCK_OK) {
        UmicomU8 *const destination = (UmicomU8 *)output;
        const volatile UmicomU8 *const source = (const volatile UmicomU8 *)s->dataFrame;
        for (UmicomSize i = 0U; i < bytes; ++i) destination[i] = source[i];
    }
    UmicomBlockClear(s->dataFrame, UMICOM_KERNEL_PAGE_SIZE);
    s->lastError = status;
    return UmicomBlockLeave(d, status);
}
UmicomKernelBlockStatus UmicomKernelBlockClose(UmicomKernelBlockDomain *d, UmicomKernelBlockHandle handle)
{
    UmicomKernelBlockStatus status = UmicomBlockEnter(d);
    if (status != UMICOM_BLOCK_OK) return status;
    UmicomKernelBlockSlot *s = UmicomBlockFind(d, handle);
    if (!s) return UmicomBlockLeave(d, UMICOM_BLOCK_INVALID_HANDLE);
    status = UmicomBlockRelease(d, s);
    return UmicomBlockLeave(d, status);
}
const char *UmicomKernelBlockStatusName(UmicomKernelBlockStatus status)
{
    static const char *const names[] = {
        "ok", "invalid-argument", "bad-state", "unsafe-context", "busy", "no-device", "not-block",
        "bad-magic", "unsupported-transport", "already-active", "required-feature-missing", "writable-device-refused",
        "features-refused", "queue-unavailable", "configuration-unstable", "configuration-changed", "out-of-memory",
        "range", "timeout", "clock-error", "device-error", "io-error", "unsupported-request", "malformed-completion",
        "invalid-handle", "reset-pending-memory-retained", "frame-release-failed", "corrupt-owner", "no-catalogue",
        "unqualified-platform"
    };
    return (UmicomSize)status < sizeof(names) / sizeof(names[0]) ? names[status] : "unknown-block-status";
}
