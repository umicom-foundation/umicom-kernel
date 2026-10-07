/*-----------------------------------------------------------------------------
 * Umicom Kernel native block-protocol qualification
 *
 * The model implements register side effects, selector banks and descriptor
 * consumption. It does not replace the driver, physical allocator or parser.
 * A DMA/reset failure is injectable so retained memory can be checked without
 * manufacturing a real device that continues corrupting a developer's machine.
 * These tests are not evidence of executing RISC-V fences or QEMU itself.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/virtio_block_protocol.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "fixture_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define RAM_FRAMES 32U
_Alignas(4096) static UmicomU8 ram[RAM_FRAMES * 4096U];
static UmicomKernelBlockDomain domain;
static UmicomU8 resultBytes[4096];
static UmicomKernelHardwareCatalogue catalogue;
static char transcript[32768];
static UmicomSize transcriptBytes;

typedef struct BlockModel {
    UmicomU32 registers[128];
    UmicomU32 magic, revision, device;
    UmicomU32 featuresLow, featuresHigh, selectedFeatures, selectedDriver;
    UmicomU32 driverLow, driverHigh, queueMax;
    UmicomU64 capacity, now, step;
    UmicomAddress desc, avail, used;
    UmicomU32 notifications, reads, writes, barriers, lastNotifyBarrier, resets, delay, resetDelay, resetRemaining;
    UmicomU32 usedId, usedLength, usedDelta, result;
    UmicomU32 allocateCalls, freeCalls, failAllocate, failFree;
    UmicomBoolean allowed, noCompletion, unstableConfig, changeConfig, needsReset;
    UmicomBoolean refuseFeatures, refuseQueue, refuseDriver, stuckReset, omitResult;
    UmicomBoolean backwardClock, pending, reenter, partialData, stickAfterDriver;
    UmicomBoolean observedReadOnlyRequest, observedSafeRelease;
    UmicomU32 reentryResult;
    /* Zero-default extension points preserve this original read-only model.
     * A separate writable suite supplies its own media completion only. */
    UmicomU32 expectedDriverLow;
    void (*completeRequest)(void);
} BlockModel;
static BlockModel model;

UmicomKernelMemoryStatus __real_UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *out);
UmicomKernelMemoryStatus __real_UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress frame);
UmicomKernelMemoryStatus __wrap_UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *out)
{
    ++model.allocateCalls;
    if (model.failAllocate && model.allocateCalls == model.failAllocate) return UMICOM_KERNEL_MEMORY_OUT_OF_MEMORY;
    return __real_UmicomKernelPhysicalMemoryAllocateFrame(out);
}
UmicomKernelMemoryStatus __wrap_UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress frame)
{
    ++model.freeCalls;
    /* Pages may be scrubbed/freed only after the device acknowledges reset.
     * In this harness the allocator is used solely for this transport. */
    CHECK(model.registers[UMICOM_VIRTIO_STATUS / 4U] == 0U);
    for (UmicomSize i = 0U; i < 4096U; ++i) CHECK(((const UmicomU8 *)frame)[i] == 0U);
    model.observedSafeRelease = UMICOM_TRUE;
    if (model.failFree && model.freeCalls == model.failFree) return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    return __real_UmicomKernelPhysicalMemoryFreeFrame(frame);
}
static UmicomSize Allocated(void)
{
    UmicomKernelPhysicalMemorySnapshot snapshot = {0};
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot) == UMICOM_KERNEL_MEMORY_OK);
    return snapshot.allocatedFrames;
}
static UmicomU32 Offset(UmicomAddress address)
{
    CHECK(address >= 0x10001000U && address < 0x10002000U && address % 4U == 0U);
    return (UmicomU32)(address - 0x10001000U);
}
static void Complete(void)
{
    if (model.completeRequest) { model.completeRequest(); return; }
    CHECK(model.desc && model.avail && model.used);
    const UmicomU64 *headerDescriptor = (const UmicomU64 *)model.desc;
    const UmicomAddress header = (UmicomAddress)headerDescriptor[0];
    const UmicomU32 *request = (const UmicomU32 *)header;
    CHECK(request[0] == UMICOM_VIRTIO_REQUEST_READ && request[1] == 0U);
    model.observedReadOnlyRequest = UMICOM_TRUE;
    const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
    const UmicomAddress data = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
    const UmicomU32 length = *(const UmicomU32 *)(model.desc + 24U);
    const UmicomAddress status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 32U);
    CHECK(*(const UmicomU32 *)(model.desc + 8U) == 16U);
    CHECK(*(const UmicomU16 *)(model.desc + 12U) == 1U && *(const UmicomU16 *)(model.desc + 14U) == 1U);
    CHECK(*(const UmicomU16 *)(model.desc + 28U) == 3U && *(const UmicomU16 *)(model.desc + 30U) == 2U);
    CHECK(*(const UmicomU32 *)(model.desc + 40U) == 1U && *(const UmicomU16 *)(model.desc + 44U) == 2U);
    CHECK(length && length <= 4096U && length % 512U == 0U);
    CHECK(sector < model.capacity && length / 512U <= model.capacity - sector);
    CHECK(model.desc >= (UmicomAddress)ram && model.desc + 4096U <= (UmicomAddress)ram + sizeof(ram));
    CHECK(data >= (UmicomAddress)ram && data + 4096U <= (UmicomAddress)ram + sizeof(ram));
    CHECK(data != model.desc && model.used == model.desc + UMICOM_VIRTIO_USED_OFFSET);
    CHECK(model.avail == model.desc + UMICOM_VIRTIO_AVAIL_OFFSET);
    CHECK(status == model.desc + UMICOM_VIRTIO_RESULT_OFFSET);
    CHECK(*(const UmicomU16 *)model.avail == 1U);
    volatile UmicomU16 *index = (volatile UmicomU16 *)(model.used + 2U);
    const UmicomU16 used = *index;
    CHECK(*(const UmicomU16 *)(model.avail + 2U) == (UmicomU16)(used + 1U));
    CHECK(*(const UmicomU16 *)(model.avail + 4U + 2U * (used % 8U)) == 0U);
    const UmicomU32 written = model.partialData ? length / 2U : length;
    for (UmicomU32 i = 0U; i < written; ++i)
        ((UmicomU8 *)data)[i] = UmicomBlockFixtureByte(sector + i / 512U, i % 512U);
    if (!model.omitResult) *(UmicomU8 *)status = (UmicomU8)model.result;
    const UmicomAddress element = model.used + 4U + 8U * (used % 8U);
    *(UmicomU32 *)element = model.usedId;
    *(UmicomU32 *)(element + 4U) = model.usedLength == 0xffffffffU ? length + 1U : model.usedLength;
    atomic_thread_fence(memory_order_seq_cst);
    *index = (UmicomU16)(used + model.usedDelta);
    model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] |= 1U;
    model.pending = UMICOM_FALSE;
}
static UmicomU32 ReadRegister(void *context, UmicomAddress address)
{
    CHECK(context == &model);
    ++model.reads;
    const UmicomU32 offset = Offset(address);
    if (offset == UMICOM_VIRTIO_MAGIC_OFFSET) return model.magic;
    if (offset == UMICOM_VIRTIO_VERSION_OFFSET) return model.revision;
    if (offset == UMICOM_VIRTIO_DEVICE_ID) return model.device;
    CHECK(model.device != 0U); /* Empty transport means stop after DeviceID. */
    switch (offset) {
    case UMICOM_VIRTIO_VENDOR_ID: return 0x554d4943U;
    case UMICOM_VIRTIO_STATUS:
        if (model.resetRemaining && --model.resetRemaining == 0U) {
            model.registers[offset / 4U] = 0U;
            model.registers[UMICOM_VIRTIO_QUEUE_READY / 4U] = 0U;
            model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] = 0U;
            model.pending = UMICOM_FALSE;
        }
        return model.registers[offset / 4U];
    case UMICOM_VIRTIO_DEVICE_FEATURES: return model.selectedFeatures ? model.featuresHigh : model.featuresLow;
    case UMICOM_VIRTIO_QUEUE_MAX: return model.queueMax;
    case UMICOM_VIRTIO_QUEUE_READY: return model.registers[offset / 4U];
    case UMICOM_VIRTIO_CONFIG_GENERATION:
        if (model.unstableConfig) ++model.registers[offset / 4U];
        return model.registers[offset / 4U];
    case UMICOM_VIRTIO_CAPACITY_LOW: return (UmicomU32)model.capacity;
    case UMICOM_VIRTIO_CAPACITY_HIGH: return (UmicomU32)(model.capacity >> 32U);
    case UMICOM_VIRTIO_INTERRUPT_STATUS: return model.registers[offset / 4U];
    default: CHECK(0); return 0U; /* Reject reads of write-only or unused registers. */
    }
}
static void WriteRegister(void *context, UmicomAddress address, UmicomU32 value)
{
    CHECK(context == &model);
    ++model.writes;
    const UmicomU32 offset = Offset(address);
    CHECK(model.device == 2U && model.revision == 2U && model.magic == UMICOM_VIRTIO_MAGIC);
    switch (offset) {
    case UMICOM_VIRTIO_STATUS:
        if (value == 0U) {
            ++model.resets;
            if (model.stuckReset) { model.registers[offset / 4U] = 64U; return; }
            if (model.resetDelay) {
                model.resetRemaining = model.resetDelay;
                model.registers[offset / 4U] = 64U;
                return;
            }
            model.registers[UMICOM_VIRTIO_QUEUE_READY / 4U] = 0U;
            model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] = 0U;
            model.pending = UMICOM_FALSE;
        } else {
            CHECK((value | model.registers[offset / 4U]) == value); /* Status bits are monotonic until reset. */
            if (value == 11U && model.refuseFeatures) value = 3U;
            if (value == 15U && model.refuseDriver) {
                value = 128U;
                if (model.stickAfterDriver) model.stuckReset = UMICOM_TRUE;
            }
        }
        model.registers[offset / 4U] = value;
        return;
    case UMICOM_VIRTIO_DEVICE_FEATURES_SELECT: CHECK(value <= 1U); model.selectedFeatures = value; return;
    case UMICOM_VIRTIO_DRIVER_FEATURES_SELECT: CHECK(value <= 1U); model.selectedDriver = value; return;
    case UMICOM_VIRTIO_DRIVER_FEATURES:
        if (model.selectedDriver) model.driverHigh = value; else model.driverLow = value;
        return;
    case UMICOM_VIRTIO_QUEUE_SELECT: CHECK(value == 0U); return;
    case UMICOM_VIRTIO_QUEUE_SIZE: CHECK(value == 8U); return;
    case UMICOM_VIRTIO_QUEUE_DESC_LOW: model.desc = (model.desc & ~((UmicomAddress)0xffffffffU)) | value; return;
    case UMICOM_VIRTIO_QUEUE_DESC_HIGH: model.desc = (model.desc & 0xffffffffU) | ((UmicomAddress)value << 32U); return;
    case UMICOM_VIRTIO_QUEUE_AVAIL_LOW: model.avail = (model.avail & ~((UmicomAddress)0xffffffffU)) | value; return;
    case UMICOM_VIRTIO_QUEUE_AVAIL_HIGH: model.avail = (model.avail & 0xffffffffU) | ((UmicomAddress)value << 32U); return;
    case UMICOM_VIRTIO_QUEUE_USED_LOW: model.used = (model.used & ~((UmicomAddress)0xffffffffU)) | value; return;
    case UMICOM_VIRTIO_QUEUE_USED_HIGH: model.used = (model.used & 0xffffffffU) | ((UmicomAddress)value << 32U); return;
    case UMICOM_VIRTIO_QUEUE_READY:
        CHECK(value == 1U && model.desc && model.avail && model.used);
        model.registers[offset / 4U] = model.refuseQueue ? 0U : value;
        return;
    case UMICOM_VIRTIO_QUEUE_NOTIFY:
        CHECK(value == 0U && model.registers[UMICOM_VIRTIO_STATUS / 4U] == 15U);
        CHECK(model.registers[UMICOM_VIRTIO_QUEUE_READY / 4U] == 1U);
        if (model.expectedDriverLow) {
            CHECK(model.driverLow == model.expectedDriverLow && model.driverHigh == 1U);
        } else {
        CHECK(model.driverLow == UMICOM_VIRTIO_READ_ONLY && model.driverHigh == 1U);
        }
        CHECK(model.barriers >= model.lastNotifyBarrier + 2U);
        model.lastNotifyBarrier = model.barriers;
        ++model.notifications;
        if (model.reenter) {
            UmicomKernelBlockInfo ignored = {0};
            model.reentryResult = (UmicomU32)UmicomKernelBlockProbe(&domain, 0U, &ignored);
        }
        if (model.changeConfig) ++model.registers[UMICOM_VIRTIO_CONFIG_GENERATION / 4U];
        if (model.needsReset) model.registers[UMICOM_VIRTIO_STATUS / 4U] |= 64U;
        model.pending = UMICOM_TRUE;
        if (!model.noCompletion && !model.delay) Complete();
        return;
    case UMICOM_VIRTIO_INTERRUPT_ACK:
        CHECK(value == 1U && (model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] & 1U));
        model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] &= ~value;
        return;
    default: CHECK(0); return; /* No legacy PFN, read-only or invented register writes. */
    }
}
static void Barrier(void *context)
{
    CHECK(context == &model);
    ++model.barriers;
    atomic_thread_fence(memory_order_seq_cst);
}
static UmicomU64 Clock(void *context)
{
    CHECK(context == &model);
    if (model.backwardClock) { if (model.now) --model.now; return model.now; }
    model.now += model.step;
    if (model.pending && !model.noCompletion && model.delay && --model.delay == 0U) Complete();
    return model.now;
}
static UmicomBoolean Allowed(void *context) { CHECK(context == &model); return model.allowed; }
static UmicomKernelBlockOperations Operations(void)
{
    const UmicomKernelBlockOperations ops = {ReadRegister, WriteRegister, Barrier, Clock, Allowed, &model};
    return ops;
}
static void Start(void)
{
    memset(&model, 0, sizeof(model));
    memset(&domain, 0, sizeof(domain));
    memset(resultBytes, 0xa5, sizeof(resultBytes));
    memset(transcript, 0, sizeof(transcript)); transcriptBytes = 0U;
    memset(ram, 0xa7, sizeof(ram));
    CHECK(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)ram, sizeof(ram)) == UMICOM_KERNEL_MEMORY_OK);
    model.magic = UMICOM_VIRTIO_MAGIC; model.revision = 2U; model.device = 2U;
    model.featuresLow = UMICOM_VIRTIO_READ_ONLY | 0x70000656U;
    model.featuresHigh = 0xffffffffU;
    model.queueMax = 256U; model.capacity = 128U; model.step = 1U; model.allowed = UMICOM_TRUE;
    model.usedLength = 0xffffffffU; model.usedDelta = 1U;
    const UmicomKernelBlockTransport transport = {0x10001000U, 4096U};
    const UmicomKernelBlockOperations ops = Operations();
    CHECK(UmicomKernelBlockDomainInitialize(&domain, &transport, 1U, &ops) == UMICOM_BLOCK_OK);
}
static UmicomKernelBlockHandle Open(void)
{
    UmicomKernelBlockHandle handle = 0U;
    CHECK(UmicomKernelBlockOpen(&domain, 0U, 32U, &handle) == UMICOM_BLOCK_OK && handle != 0U);
    CHECK(Allocated() == 2U);
    return handle;
}
static void Close(UmicomKernelBlockHandle handle)
{
    CHECK(UmicomKernelBlockClose(&domain, handle) == UMICOM_BLOCK_OK);
    CHECK(Allocated() == 0U && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
}
static void Unchanged(void)
{
    for (UmicomSize i = 0U; i < sizeof(resultBytes); ++i) CHECK(resultBytes[i] == 0xa5U);
}
static void Verify(UmicomU64 first, UmicomSize sectors)
{
    for (UmicomSize i = 0U; i < sectors * 512U; ++i)
        CHECK(resultBytes[i] == UmicomBlockFixtureByte(first + i / 512U, i % 512U));
}
static void Output(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    CHECK(bytes < sizeof(transcript) - transcriptBytes);
    memcpy(transcript + transcriptBytes, text, (size_t)bytes);
    transcriptBytes += bytes; transcript[transcriptBytes] = '\0';
}
/* Adapter substitutions used only by the actual console and guest-C tests. */
UmicomKernelBlockStatus UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **out)
{
    if (!out) return UMICOM_BLOCK_INVALID_ARGUMENT;
    *out = &domain; return UMICOM_BLOCK_OK;
}
void UmicomKernelConsoleWrite(const char *text) { Output(0, text, strlen(text)); }
void UmicomKernelConsoleWriteLine(const char *text) { UmicomKernelConsoleWrite(text); Output(0, "\n", 1U); }
void UmicomRiscvSupervisorMachineStateRead(UmicomRiscvSupervisorMachineState *out) { memset(out, 0, sizeof(*out)); }
void UmicomPlatformFinishFailure(UmicomU32 code) { fprintf(stderr, "guest failure %u\n%s\n", code, transcript); exit(1); }
void UmicomPlatformHalt(void) { abort(); }

static void Catalogue(void)
{
    memset(&catalogue, 0, sizeof(catalogue));
    catalogue.ready = UMICOM_TRUE; catalogue.timebaseFrequency = 10000000U; catalogue.devices = 10U;
    catalogue.entries[0].enabled = UMICOM_TRUE; catalogue.entries[0].kind = UMICOM_HARDWARE_MEMORY;
    catalogue.entries[0].registerCount = 1U; catalogue.entries[0].registerStatus = UMICOM_TREE_OK;
    catalogue.entries[0].registers[0].physicalAddress = 0x80000000U;
    catalogue.entries[0].registers[0].bytes = 128U * 1024U * 1024U;
    catalogue.entries[0].registers[0].translation = UMICOM_TREE_OK;
    catalogue.entries[1].enabled = UMICOM_TRUE; catalogue.entries[1].kind = UMICOM_HARDWARE_CPU;
    catalogue.entries[1].registerCount = 1U;
    for (UmicomSize i = 0U; i < 8U; ++i) {
        UmicomKernelHardwareDevice *d = &catalogue.entries[i + 2U];
        d->enabled = UMICOM_TRUE; d->kind = UMICOM_HARDWARE_VIRTIO_MMIO;
        d->registerCount = 1U; d->registerStatus = UMICOM_TREE_OK;
        d->registers[0].physicalAddress = 0x10008000U - 4096U * i;
        d->registers[0].bytes = 4096U; d->registers[0].translation = UMICOM_TREE_OK;
    }
}
static void InitialiseTest(void)
{
    CHECK(model.writes == 0U && model.reads == 0U && Allocated() == 0U);
    const UmicomKernelBlockTransport t = {0x10001000U, 4096U};
    const UmicomKernelBlockOperations ops = Operations();
    CHECK(UmicomKernelBlockDomainInitialize(&domain, &t, 1U, &ops) == UMICOM_BLOCK_BAD_STATE);
    UmicomKernelBlockDomain copied = domain;
    UmicomKernelBlockInfo info = {0};
    CHECK(UmicomKernelBlockProbe(&copied, 0U, &info) == UMICOM_BLOCK_BAD_STATE);
}
static void InvalidInitialise(void)
{
    UmicomKernelBlockDomain fresh = {0};
    UmicomKernelBlockOperations ops = Operations();
    UmicomKernelBlockTransport list[2] = {{0x10001000U, 4096U}, {0x10001800U, 4096U}};
    CHECK(UmicomKernelBlockDomainInitialize(&fresh, list, 2U, &ops) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(!fresh.ready);
    list[0].base = ~(UmicomAddress)3U;
    CHECK(UmicomKernelBlockDomainInitialize(&fresh, list, 1U, &ops) == UMICOM_BLOCK_INVALID_ARGUMENT);
    list[0].base = 0x10001001U;
    CHECK(UmicomKernelBlockDomainInitialize(&fresh, list, 1U, &ops) == UMICOM_BLOCK_INVALID_ARGUMENT);
    list[0].base = 0x10001000U; list[0].bytes = 0x100U;
    CHECK(UmicomKernelBlockDomainInitialize(&fresh, list, 1U, &ops) == UMICOM_BLOCK_INVALID_ARGUMENT);
    list[0].bytes = 4096U; ops.barrier = 0;
    CHECK(UmicomKernelBlockDomainInitialize(&fresh, list, 1U, &ops) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(model.writes == 0U && model.reads == 0U && Allocated() == 0U);
}
static void ProbeVariant(const char *name)
{
    UmicomKernelBlockStatus expected = UMICOM_BLOCK_OK;
    if (!strcmp(name,"empty_slot")) { model.device = 0U; expected = UMICOM_BLOCK_NO_DEVICE; }
    if (!strcmp(name,"legacy_transport")) { model.revision = 1U; expected = UMICOM_BLOCK_UNSUPPORTED_TRANSPORT; }
    if (!strcmp(name,"bad_magic")) { model.magic = 0U; expected = UMICOM_BLOCK_BAD_MAGIC; }
    if (!strcmp(name,"other_device")) { model.device = 1U; expected = UMICOM_BLOCK_NOT_BLOCK; }
    UmicomKernelBlockInfo info = {0};
    CHECK(UmicomKernelBlockProbe(&domain, 0U, &info) == expected);
    const UmicomU32 reads = model.reads;
    CHECK(reads == (expected == UMICOM_BLOCK_BAD_MAGIC ? 1U : expected == UMICOM_BLOCK_UNSUPPORTED_TRANSPORT ? 2U :
        expected == UMICOM_BLOCK_NO_DEVICE ? 3U : 4U));
    CHECK(model.writes == 0U && Allocated() == 0U);
    UmicomKernelBlockHandle token = 0U;
    if (expected != UMICOM_BLOCK_OK) CHECK(UmicomKernelBlockOpen(&domain, 0U, 32U, &token) == expected);
    CHECK(token == 0U && model.writes == 0U);
}
static void OpenRefusal(const char *name)
{
    UmicomKernelBlockStatus expected = UMICOM_BLOCK_REQUIRED_FEATURE;
    if (!strcmp(name,"missing_modern_feature")) model.featuresHigh &= ~1U;
    else if (!strcmp(name,"writable_device")) { model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY; expected = UMICOM_BLOCK_WRITABLE_DEVICE; }
    else if (!strcmp(name,"features_rejected")) { model.refuseFeatures = UMICOM_TRUE; expected = UMICOM_BLOCK_FEATURE_REFUSED; }
    else if (!strcmp(name,"queue_absent")) { model.queueMax = 0U; expected = UMICOM_BLOCK_QUEUE_UNAVAILABLE; }
    else if (!strcmp(name,"queue_too_small")) { model.queueMax = 2U; expected = UMICOM_BLOCK_QUEUE_UNAVAILABLE; }
    else if (!strcmp(name,"queue_enable_refused")) { model.refuseQueue = UMICOM_TRUE; expected = UMICOM_BLOCK_QUEUE_UNAVAILABLE; }
    else if (!strcmp(name,"driver_ok_refused")) { model.refuseDriver = UMICOM_TRUE; expected = UMICOM_BLOCK_DEVICE_ERROR; }
    else if (!strcmp(name,"zero_capacity")) { model.capacity = 0U; expected = UMICOM_BLOCK_RANGE; }
    else if (!strcmp(name,"unstable_capacity")) { model.unstableConfig = UMICOM_TRUE; expected = UMICOM_BLOCK_CONFIG_UNSTABLE; }
    else if (!strcmp(name,"foreign_active_driver")) { model.registers[UMICOM_VIRTIO_STATUS / 4U] = 15U; expected = UMICOM_BLOCK_ALREADY_ACTIVE; }
    else CHECK(0);
    UmicomKernelBlockHandle token = 0U;
    CHECK(UmicomKernelBlockOpen(&domain, 0U, 32U, &token) == expected);
    CHECK(token == 0U && Allocated() == 0U && model.notifications == 0U);
    if (expected == UMICOM_BLOCK_ALREADY_ACTIVE) CHECK(model.writes == 0U && model.resets == 0U);
    else CHECK(model.registers[UMICOM_VIRTIO_STATUS / 4U] == 0U);
}
static void Success(const char *name)
{
    const UmicomKernelBlockHandle h = Open();
    CHECK(model.driverLow == UMICOM_VIRTIO_READ_ONLY && model.driverHigh == 1U);
    UmicomU64 first = 0U; UmicomSize sectors = 1U;
    if (!strcmp(name,"last_sector")) first = 127U;
    if (!strcmp(name,"multi_sector")) { first = 7U; sectors = 8U; }
    CHECK(UmicomKernelBlockRead(&domain, h, first, sectors, resultBytes, sizeof(resultBytes)) == UMICOM_BLOCK_OK);
    Verify(first, sectors);
    CHECK(model.observedReadOnlyRequest && model.notifications == 1U);
    CHECK(model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] == 0U);
    for (UmicomSize i = 0U; i < 4096U; ++i) CHECK(((const UmicomU8 *)domain.slots[0].dataFrame)[i] == 0U);
    Close(h); CHECK(model.observedSafeRelease);
}
static void Bounds(void)
{
    const UmicomKernelBlockHandle h = Open();
    CHECK(UmicomKernelBlockRead(&domain,h,128U,1U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_RANGE);
    CHECK(UmicomKernelBlockRead(&domain,h,127U,2U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_RANGE);
    CHECK(UmicomKernelBlockRead(&domain,h,~(UmicomU64)0U,2U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_RANGE);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,0U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,9U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,resultBytes,511U) == UMICOM_BLOCK_RANGE);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,0,512U) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,(void *)(~(UmicomAddress)255U),512U) == UMICOM_BLOCK_RANGE);
    CHECK(model.notifications == 0U); Unchanged(); Close(h);
}
static void Overlap(void)
{
    const UmicomKernelBlockHandle h = Open();
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,&domain,sizeof(domain)) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,(void *)domain.slots[0].queueFrame,4096U) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,(void *)domain.slots[0].dataFrame,4096U) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(model.notifications == 0U); Close(h);
}
static void ReadFailure(const char *name)
{
    const UmicomKernelBlockHandle h = Open();
    UmicomKernelBlockStatus expected = UMICOM_BLOCK_MALFORMED_COMPLETION;
    if (!strcmp(name,"used_id")) model.usedId = 7U;
    else if (!strcmp(name,"short_length")) { model.usedLength = 257U; model.partialData = UMICOM_TRUE; }
    else if (!strcmp(name,"long_length")) model.usedLength = 514U;
    else if (!strcmp(name,"zero_length")) model.usedLength = 0U;
    else if (!strcmp(name,"used_jump")) model.usedDelta = 2U;
    else if (!strcmp(name,"status_unwritten")) model.omitResult = UMICOM_TRUE;
    else if (!strcmp(name,"status_unknown")) model.result = 3U;
    else if (!strcmp(name,"device_needs_reset")) { model.needsReset = UMICOM_TRUE; expected = UMICOM_BLOCK_DEVICE_ERROR; }
    else if (!strcmp(name,"config_changed")) { model.changeConfig = UMICOM_TRUE; expected = UMICOM_BLOCK_CONFIG_CHANGED; }
    else if (!strcmp(name,"timeout")) { model.noCompletion = UMICOM_TRUE; expected = UMICOM_BLOCK_TIMEOUT; }
    else if (!strcmp(name,"stopped_clock")) { model.noCompletion = UMICOM_TRUE; model.step = 0U; expected = UMICOM_BLOCK_TIMEOUT; }
    else if (!strcmp(name,"backward_clock")) { model.noCompletion = UMICOM_TRUE; model.now = 1000U; model.backwardClock = UMICOM_TRUE; expected = UMICOM_BLOCK_CLOCK_ERROR; }
    else if (!strcmp(name,"unsolicited_used")) *(UmicomU16 *)(model.used + 2U) = 1U;
    else CHECK(0);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,resultBytes,sizeof(resultBytes)) == expected);
    Unchanged(); CHECK(domain.slots[0].state == UMICOM_BLOCK_FAULTED && !domain.slots[0].exposed);
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_BAD_STATE);
    Close(h);
}
static void IoError(UmicomBoolean unsupported)
{
    const UmicomKernelBlockHandle h = Open();
    model.result = unsupported ? 2U : 1U; model.usedLength = 1U;
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,resultBytes,sizeof(resultBytes)) ==
        (unsupported ? UMICOM_BLOCK_UNSUPPORTED_REQUEST : UMICOM_BLOCK_IO_ERROR));
    Unchanged(); CHECK(domain.slots[0].state == UMICOM_BLOCK_READY);
    model.result = 0U; model.usedLength = 0xffffffffU;
    CHECK(UmicomKernelBlockRead(&domain,h,4U,1U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_OK);
    Verify(4U,1U); Close(h);
}
static void RetainedReset(void)
{
    const UmicomKernelBlockHandle h = Open();
    model.stuckReset = UMICOM_TRUE; model.noCompletion = UMICOM_TRUE;
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,resultBytes,sizeof(resultBytes)) == UMICOM_BLOCK_RESET_PENDING);
    CHECK(domain.slots[0].exposed && Allocated() == 2U && model.freeCalls == 0U);
    CHECK(domain.slots[0].lastError == UMICOM_BLOCK_TIMEOUT);
    CHECK(UmicomKernelBlockClose(&domain,h) == UMICOM_BLOCK_RESET_PENDING);
    CHECK(Allocated() == 2U && model.freeCalls == 0U); Unchanged();
    UmicomKernelBlockHandle second = 0U;
    CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&second) == UMICOM_BLOCK_BUSY && second == 0U);
    model.stuckReset = UMICOM_FALSE; Close(h);
}
static void OpenRetained(void)
{
    /* Fail after queue addresses were published, then inject a frame-release
     * refusal after reset. The returned token must preserve that partial owner. */
    model.refuseDriver = UMICOM_TRUE;
    UmicomKernelBlockHandle h = 0U;
    model.failFree = 1U;
    CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&h) == UMICOM_BLOCK_RELEASE_FAILED && h != 0U);
    CHECK(Allocated() == 2U && domain.slots[0].lastError == UMICOM_BLOCK_DEVICE_ERROR);
    model.failFree = 0U; Close(h);
}
static void OpenResetRetained(void)
{
    model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
    UmicomKernelBlockHandle h = 0U;
    CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&h) == UMICOM_BLOCK_RESET_PENDING && h != 0U);
    CHECK(Allocated() == 2U && domain.slots[0].exposed && model.freeCalls == 0U);
    model.stuckReset = UMICOM_FALSE; Close(h);
}
static void Delayed(void)
{
    model.resetDelay = 3U;
    const UmicomKernelBlockHandle h = Open();
    model.delay = 7U;
    CHECK(UmicomKernelBlockRead(&domain,h,11U,2U,resultBytes,4096U) == UMICOM_BLOCK_OK);
    CHECK(!model.pending && model.delay == 0U); Verify(11U,2U); Close(h);
}
static void Noncontiguous(void)
{
    CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)ram + 4096U,4096U) == UMICOM_KERNEL_MEMORY_OK);
    const UmicomKernelBlockHandle h = Open();
    CHECK(domain.slots[0].dataFrame != domain.slots[0].queueFrame + 4096U);
    CHECK(UmicomKernelBlockRead(&domain,h,11U,8U,resultBytes,4096U) == UMICOM_BLOCK_OK);
    Verify(11U,8U); Close(h);
}
static void InvalidEntry(void)
{
    UmicomKernelBlockHandle h = 0U;
    CHECK(UmicomKernelBlockOpen(&domain,0U,0U,&h) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockOpen(&domain,0U,UMICOM_BLOCK_MAX_TIMEOUT_TICKS+1U,&h) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(UmicomKernelBlockOpen(&domain,1U,32U,&h) == UMICOM_BLOCK_INVALID_ARGUMENT);
    CHECK(h == 0U && model.reads == 0U && model.writes == 0U);
}
static void AllocationFailures(void)
{
    for (UmicomU32 point = 1U; point <= 2U; ++point) {
        Start(); model.failAllocate = point;
        UmicomKernelBlockHandle h = 0U;
        CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&h) == UMICOM_BLOCK_NO_MEMORY);
        CHECK(h == 0U && Allocated() == 0U && model.notifications == 0U);
    }
}
static void MemoryBudgets(void)
{
    for (UmicomSize budget = 0U; budget <= 4U; ++budget) {
        Start();
        if (budget < RAM_FRAMES)
            CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)ram + budget * 4096U,
                (RAM_FRAMES - budget) * 4096U) == UMICOM_KERNEL_MEMORY_OK);
        UmicomKernelBlockHandle h = 0U;
        const UmicomKernelBlockStatus result = UmicomKernelBlockOpen(&domain,0U,32U,&h);
        CHECK(result == (budget < 2U ? UMICOM_BLOCK_NO_MEMORY : UMICOM_BLOCK_OK));
        if (h) Close(h);
        CHECK(Allocated() == 0U);
    }
}
static void ReleaseFailures(void)
{
    for (UmicomU32 point = 1U; point <= 2U; ++point) {
        Start(); const UmicomKernelBlockHandle h = Open(); model.failFree = point;
        CHECK(UmicomKernelBlockClose(&domain,h) == UMICOM_BLOCK_RELEASE_FAILED);
        CHECK(Allocated() == 3U - point && !domain.slots[0].exposed);
        CHECK(UmicomKernelBlockClose(&domain,h) == UMICOM_BLOCK_OK && Allocated() == 0U);
        CHECK(model.freeCalls == 3U); /* One rejected release, two accepted releases. */
    }
}
static void Generations(void)
{
    const UmicomKernelBlockHandle first = Open(); Close(first);
    const UmicomKernelBlockHandle second = Open(); CHECK(first != second);
    CHECK(UmicomKernelBlockClose(&domain,first) == UMICOM_BLOCK_INVALID_HANDLE);
    CHECK(UmicomKernelBlockRead(&domain,first,0U,1U,resultBytes,4096U) == UMICOM_BLOCK_INVALID_HANDLE);
    Close(second);
    domain.slots[0].generation = 0xfffffffeU;
    const UmicomKernelBlockHandle last = Open(); CHECK(last >> 32U == 0xffffffffU); Close(last);
    UmicomKernelBlockHandle refused = 0U;
    CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&refused) == UMICOM_BLOCK_BAD_STATE);
}
static void Exclusive(void)
{
    const UmicomKernelBlockHandle first = Open();
    const UmicomU32 writes = model.writes; UmicomKernelBlockHandle second = 0U;
    CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&second) == UMICOM_BLOCK_BUSY && model.writes == writes);
    model.reenter = UMICOM_TRUE;
    CHECK(UmicomKernelBlockRead(&domain,first,0U,1U,resultBytes,4096U) == UMICOM_BLOCK_OK);
    CHECK(model.reentryResult == UMICOM_BLOCK_BUSY); Close(first);
}
static void Unsafe(void)
{
    model.allowed = UMICOM_FALSE; UmicomKernelBlockHandle h = 0U; UmicomKernelBlockInfo info = {0};
    CHECK(UmicomKernelBlockOpen(&domain,0U,32U,&h) == UMICOM_BLOCK_UNSAFE_CONTEXT);
    CHECK(UmicomKernelBlockProbe(&domain,0U,&info) == UMICOM_BLOCK_UNSAFE_CONTEXT);
    CHECK(model.reads == 0U && model.writes == 0U);
    model.allowed = UMICOM_TRUE; h = Open(); model.allowed = UMICOM_FALSE;
    CHECK(UmicomKernelBlockRead(&domain,h,0U,1U,resultBytes,4096U) == UMICOM_BLOCK_UNSAFE_CONTEXT);
    CHECK(UmicomKernelBlockClose(&domain,h) == UMICOM_BLOCK_UNSAFE_CONTEXT);
    CHECK(Allocated() == 2U); model.allowed = UMICOM_TRUE; Close(h);
}
static void WrappedIndices(void)
{
    const UmicomKernelBlockHandle h = Open();
    for (UmicomU32 i = 0U; i < 65544U; ++i) {
        CHECK(UmicomKernelBlockRead(&domain,h,i % 128U,1U,resultBytes,4096U) == UMICOM_BLOCK_OK);
        Verify(i % 128U,1U);
    }
    CHECK(domain.slots[0].usedIndex == 8U && domain.slots[0].availableIndex == 8U);
    Close(h);
}
static void Lifetimes(void)
{
    UmicomKernelBlockHandle previous = 0U;
    for (UmicomSize i = 0U; i < 1000U; ++i) {
        const UmicomKernelBlockHandle h = Open(); CHECK(h != previous);
        CHECK(UmicomKernelBlockRead(&domain,h,i % 128U,1U,resultBytes,4096U) == UMICOM_BLOCK_OK);
        Verify(i % 128U,1U); Close(h); previous = h;
    }
}
static void HighCapacity(void)
{
    model.capacity = ((UmicomU64)1U << 32U) + 11U;
    const UmicomKernelBlockHandle h = Open();
    CHECK(domain.slots[0].sectors == model.capacity);
    CHECK(UmicomKernelBlockRead(&domain,h,model.capacity - 1U,1U,resultBytes,4096U) == UMICOM_BLOCK_OK);
    Verify(model.capacity - 1U,1U); Close(h);
}
static void Policy(const char *name)
{
    Catalogue();
    UmicomKernelBlockStatus expected = UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
    if (!strcmp(name,"policy_valid")) expected = UMICOM_BLOCK_OK;
    else if (!strcmp(name,"policy_window")) catalogue.entries[2].registers[0].physicalAddress = 0x80000000U;
    else if (!strcmp(name,"policy_duplicate")) catalogue.entries[2].registers[0].physicalAddress = catalogue.entries[3].registers[0].physicalAddress;
    else if (!strcmp(name,"policy_span")) catalogue.entries[2].registers[0].bytes = 8192U;
    else if (!strcmp(name,"policy_untranslated")) catalogue.entries[2].registers[0].translation = UMICOM_TREE_NOT_FOUND;
    else if (!strcmp(name,"policy_ram")) catalogue.entries[0].registers[0].bytes /= 2U;
    else if (!strcmp(name,"policy_hart")) catalogue.entries[1].registers[0].busAddress = 1U;
    else if (!strcmp(name,"policy_reservations")) catalogue.firmwareReservations = 1U;
    else if (!strcmp(name,"policy_reserved_node")) catalogue.entries[2].kind = UMICOM_HARDWARE_RESERVED_MEMORY;
    else if (!strcmp(name,"policy_clock")) catalogue.timebaseFrequency = 0U;
    else if (!strcmp(name,"policy_count")) { catalogue.devices = UMICOM_HARDWARE_DEVICE_LIMIT + 1U; expected = UMICOM_BLOCK_NO_CATALOGUE; }
    else if (!strcmp(name,"policy_not_ready")) { catalogue.ready = UMICOM_FALSE; expected = UMICOM_BLOCK_NO_CATALOGUE; }
    else if (!strcmp(name,"policy_disabled")) { catalogue.entries[2].enabled = UMICOM_FALSE; expected = UMICOM_BLOCK_OK; }
    else CHECK(0);
    UmicomKernelBlockTransport out[8]; memset(out,0xa5,sizeof(out)); UmicomSize count = 77U;
    CHECK(UmicomKernelBlockSelectQemuTransports(&catalogue,out,&count) == expected);
    if (expected == UMICOM_BLOCK_OK) {
        CHECK(count == (!strcmp(name,"policy_disabled") ? 7U : 8U));
        for (UmicomSize i = 0U; i < count; ++i) CHECK(out[i].base == 0x10001000U + i * 4096U);
    } else {
        CHECK(count == 77U);
        for (size_t i = 0U; i < sizeof(out); ++i) CHECK(((const unsigned char *)out)[i] == 0xa5U);
    }
    CHECK(model.writes == 0U && model.reads == 0U && Allocated() == 0U);
}
static void Console(const char *name)
{
    if (!strcmp(name,"console_probe")) {
        UmicomKernelBlockReport(0,Output);
        CHECK(strstr(transcript,"block.slot=0") && model.writes == 0U && Allocated() == 0U);
    } else if (!strcmp(name,"console_sector")) {
        CHECK(UmicomKernelBlockInspectSector(0U,0U,0,Output) == UMICOM_BLOCK_OK);
        CHECK(strstr(transcript,"Umicom Kernel") && strstr(transcript,"block.read=ok close=ok"));
        CHECK(Allocated() == 0U);
    } else if (!strcmp(name,"console_retry")) {
        model.failFree = 1U;
        CHECK(UmicomKernelBlockInspectSector(0U,0U,0,Output) == UMICOM_BLOCK_RELEASE_FAILED);
        CHECK(Allocated() == 2U);
        CHECK(UmicomKernelBlockRetryClose() == UMICOM_BLOCK_OK && Allocated() == 0U);
    } else CHECK(0);
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "fixture_bytes")) {
        FILE *file = fopen(argv[2], "rb"); CHECK(file != NULL);
        for (UmicomU64 sector = 0U; sector < 128U; ++sector)
            for (UmicomSize byte = 0U; byte < 512U; ++byte)
                CHECK(fgetc(file) == UmicomBlockFixtureByte(sector,byte));
        CHECK(fgetc(file) == EOF && !ferror(file)); CHECK(fclose(file) == 0);
        puts("PASS fixture_bytes"); return 0;
    }
    CHECK(argc == 2); Start(); const char *name = argv[1];
    if (!strcmp(name,"initialise")) InitialiseTest();
    else if (!strcmp(name,"invalid_initialise")) InvalidInitialise();
    else if (!strcmp(name,"probe") || !strcmp(name,"empty_slot") || !strcmp(name,"legacy_transport") ||
        !strcmp(name,"bad_magic") || !strcmp(name,"other_device")) ProbeVariant(name);
    else if (!strcmp(name,"missing_modern_feature") || !strcmp(name,"writable_device") || !strcmp(name,"features_rejected") ||
        !strcmp(name,"queue_absent") || !strcmp(name,"queue_too_small") || !strcmp(name,"queue_enable_refused") ||
        !strcmp(name,"driver_ok_refused") || !strcmp(name,"zero_capacity") || !strcmp(name,"unstable_capacity") ||
        !strcmp(name,"foreign_active_driver")) OpenRefusal(name);
    else if (!strcmp(name,"first_sector") || !strcmp(name,"last_sector") || !strcmp(name,"multi_sector")) Success(name);
    else if (!strcmp(name,"bounds")) Bounds();
    else if (!strcmp(name,"output_overlap")) Overlap();
    else if (!strcmp(name,"used_id") || !strcmp(name,"short_length") || !strcmp(name,"long_length") ||
        !strcmp(name,"zero_length") || !strcmp(name,"used_jump") || !strcmp(name,"status_unwritten") ||
        !strcmp(name,"status_unknown") || !strcmp(name,"device_needs_reset") || !strcmp(name,"config_changed") ||
        !strcmp(name,"timeout") || !strcmp(name,"stopped_clock") || !strcmp(name,"backward_clock") ||
        !strcmp(name,"unsolicited_used")) ReadFailure(name);
    else if (!strcmp(name,"io_error_retry")) IoError(UMICOM_FALSE);
    else if (!strcmp(name,"unsupported_retry")) IoError(UMICOM_TRUE);
    else if (!strcmp(name,"reset_retains_dma")) RetainedReset();
    else if (!strcmp(name,"failed_open_retains_handle")) OpenRetained();
    else if (!strcmp(name,"open_reset_retained")) OpenResetRetained();
    else if (!strcmp(name,"delayed_completion_and_reset")) Delayed();
    else if (!strcmp(name,"noncontiguous_dma")) Noncontiguous();
    else if (!strcmp(name,"invalid_entry")) InvalidEntry();
    else if (!strcmp(name,"allocation_failures")) AllocationFailures();
    else if (!strcmp(name,"memory_budgets")) MemoryBudgets();
    else if (!strcmp(name,"release_retries")) ReleaseFailures();
    else if (!strcmp(name,"stale_and_exhausted_generations")) Generations();
    else if (!strcmp(name,"exclusive_and_reentry")) Exclusive();
    else if (!strcmp(name,"unsafe_context")) Unsafe();
    else if (!strcmp(name,"full_index_wrap")) WrappedIndices();
    else if (!strcmp(name,"repeated_lifetimes")) Lifetimes();
    else if (!strcmp(name,"high_capacity")) HighCapacity();
    else if (!strncmp(name,"policy_",7U)) Policy(name);
    else if (!strncmp(name,"console_",8U)) Console(name);
    else if (!strcmp(name,"guest_c_sequence")) {
        UmicomKernelBlockValidateExecution();
        CHECK(strstr(transcript,"UMICOM_KERNEL_READ_ONLY_BLOCK_READY") && Allocated() == 0U);
    } else if (!strcmp(name,"guest_absent")) {
        model.device = 0U; UmicomKernelBlockValidateExecution();
        CHECK(strstr(transcript,"UMICOM_KERNEL_BLOCK_ABSENT_READY") && model.writes == 0U && Allocated() == 0U);
    } else CHECK(0);
    printf("PASS %s\n",name); return 0;
}
