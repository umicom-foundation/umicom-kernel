/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/block_device.c
 *
 * Bind the read-only block protocol to the qualified QEMU virt MMIO windows.
 * On this little-endian coherent guest, allocator addresses are DMA physical
 * addresses. There is no IOMMU translation or noncoherent-cache maintenance.
 * A board needing either must supply a reviewed adapter, not reuse this one.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/object_cache.h"
#include "umicom/kernel/platform.h"

/* Volatile stores keep freestanding clearing independent of a hosted memset. */
static void UmicomBlockLocalClear(void *memory, UmicomSize bytes)
{
    volatile UmicomU8 *p = (volatile UmicomU8 *)memory;
    for (UmicomSize i = 0U; i < bytes; ++i) p[i] = 0U;
}

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The qualified VirtIO adapter requires a little-endian target"
#endif

/* A single physical transport cannot belong to several independently created
 * driver domains. This adapter owns the only domain for these board windows.
 * All public consumers borrow it; none may reset or copy its metadata. */
static UmicomKernelBlockDomain umicomPlatformBlockDomain;
static UmicomBoolean UmicomQemuBlockAllowed(void *context)
{
    (void)context;
    /* Reuse the existing hart, satp, MPRV, interrupt and managed-ownership gate. */
    return UmicomKernelObjectCacheAccessAllowed();
}
static UmicomU32 UmicomQemuBlockRead(void *context, UmicomAddress address)
{
    (void)context;
    return *(volatile UmicomU32 *)address; /* All admitted register accesses are aligned words. */
}
static void UmicomQemuBlockWrite(void *context, UmicomAddress address, UmicomU32 value)
{
    (void)context;
    *(volatile UmicomU32 *)address = value;
}
static void UmicomQemuBlockBarrier(void *context)
{
    (void)context;
    /* Order ordinary DMA RAM and MMIO in both directions. Volatile prevents
     * compiler elision; this architectural fence orders the actual accesses. */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}
static UmicomU64 UmicomQemuBlockClock(void *context)
{
    (void)context;
    return UmicomPlatformTimerRead(); /* Observe time; do not acquire or reprogram its interrupt. */
}
UmicomKernelBlockStatus UmicomPlatformBlockDomainGet(UmicomKernelBlockDomain **outDomain)
{
    if (!outDomain) return UMICOM_BLOCK_INVALID_ARGUMENT;
    if (!UmicomQemuBlockAllowed(0)) return UMICOM_BLOCK_UNSAFE_CONTEXT;
    if (umicomPlatformBlockDomain.ready) { *outDomain = &umicomPlatformBlockDomain; return UMICOM_BLOCK_OK; }
    UmicomKernelPhysicalMemorySnapshot memory;
    UmicomBlockLocalClear(&memory, sizeof(memory));
    if (UmicomKernelPhysicalMemorySnapshotRead(&memory) != UMICOM_KERNEL_MEMORY_OK ||
        memory.ramBase != 0x80000000ULL || memory.ramBytes != 128U * 1024U * 1024U || memory.pageBytes != 4096U)
        return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
    const UmicomKernelHardwareCatalogue *catalogue = UmicomKernelHardwareCatalogueRead();
    if (!catalogue) return UMICOM_BLOCK_NO_CATALOGUE;
    UmicomKernelBlockTransport transports[UMICOM_BLOCK_SLOT_LIMIT];
    UmicomBlockLocalClear(&transports, sizeof(transports));
    UmicomSize count = 0U;
    UmicomKernelBlockStatus status = UmicomKernelBlockSelectQemuTransports(catalogue, transports, &count);
    if (status != UMICOM_BLOCK_OK) return status;
    const UmicomKernelBlockOperations operations = {
        UmicomQemuBlockRead, UmicomQemuBlockWrite, UmicomQemuBlockBarrier,
        UmicomQemuBlockClock, UmicomQemuBlockAllowed, 0
    };
    status = UmicomKernelBlockDomainInitialize(&umicomPlatformBlockDomain, transports, count, &operations);
    if (status == UMICOM_BLOCK_OK) *outDomain = &umicomPlatformBlockDomain;
    return status;
}
