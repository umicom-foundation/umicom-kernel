/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/block_platform_policy.c
 *
 * Firmware observations are not permission to touch arbitrary MMIO. This gate
 * recognises only the already selected single-hart, 128 MiB QEMU virt profile
 * and its eight fixed VirtIO windows. No PCI BAR or user-supplied address enters
 * the driver. Discovery remains read-only until Open explicitly claims a slot.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/virtio_block.h"
#include "umicom/kernel/hardware_catalogue.h"

/* Volatile stores keep freestanding clearing independent of a hosted memset. */
static void UmicomBlockLocalClear(void *memory, UmicomSize bytes)
{
    volatile UmicomU8 *p = (volatile UmicomU8 *)memory;
    for (UmicomSize i = 0U; i < bytes; ++i) p[i] = 0U;
}

UmicomKernelBlockStatus UmicomKernelBlockSelectQemuTransports(const UmicomKernelHardwareCatalogue *catalogue,
    UmicomKernelBlockTransport *outTransports, UmicomSize *outCount)
{
    if (!catalogue || !outTransports || !outCount) return UMICOM_BLOCK_INVALID_ARGUMENT;
    if (!catalogue->ready || catalogue->devices > UMICOM_HARDWARE_DEVICE_LIMIT)
        return UMICOM_BLOCK_NO_CATALOGUE;
    /* The current boot allocator has not yet learned arbitrary firmware
     * reservations. Refuse such a profile rather than DMA into unreserved RAM.
     * This leaves reservation-policy expansion as a visible prerequisite. */
    if (catalogue->firmwareReservations || catalogue->timebaseFrequency != 10000000U)
        return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
    UmicomBoolean ram = UMICOM_FALSE;
    UmicomSize cpus = 0U;
    UmicomKernelBlockTransport candidates[UMICOM_BLOCK_SLOT_LIMIT];
    UmicomBlockLocalClear(&candidates, sizeof(candidates));
    UmicomSize count = 0U;
    for (UmicomSize i = 0U; i < catalogue->devices; ++i) {
        const UmicomKernelHardwareDevice *device = &catalogue->entries[i];
        if (!device->enabled) continue;
        if (device->kind == UMICOM_HARDWARE_RESERVED_MEMORY) return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
        if (device->kind == UMICOM_HARDWARE_CPU) {
            if (device->registerCount != 1U || device->registers[0].busAddress != 0U)
                return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
            ++cpus;
        }
        if (device->kind == UMICOM_HARDWARE_MEMORY) {
            if (ram || device->registerStatus != UMICOM_TREE_OK || device->registerCount != 1U ||
                device->registers[0].translation != UMICOM_TREE_OK ||
                device->registers[0].physicalAddress != 0x80000000ULL ||
                device->registers[0].bytes != 128U * 1024U * 1024U)
                return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
            ram = UMICOM_TRUE;
        }
        if (device->kind != UMICOM_HARDWARE_VIRTIO_MMIO) continue;
        if (device->registerStatus != UMICOM_TREE_OK || device->registerCount != 1U ||
            device->registers[0].translation != UMICOM_TREE_OK || count == UMICOM_BLOCK_SLOT_LIMIT)
            return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
        const UmicomU64 base = device->registers[0].physicalAddress;
        if (base < 0x10001000ULL || base > 0x10008000ULL || base % 4096U ||
            device->registers[0].bytes != 4096U) return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
        for (UmicomSize j = 0U; j < count; ++j)
            if (candidates[j].base == (UmicomAddress)base) return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
        /* Stable ascending slot numbering is independent of DT node order.
         * It is still a boot-local slot index, not a persistent disk identity. */
        UmicomSize position = count;
        while (position && candidates[position - 1U].base > (UmicomAddress)base) {
            candidates[position] = candidates[position - 1U];
            --position;
        }
        candidates[position].base = (UmicomAddress)base;
        candidates[position].bytes = 4096U;
        ++count;
    }
    if (!ram || cpus != 1U || !count) return UMICOM_BLOCK_UNQUALIFIED_PLATFORM;
    /* Publish only after all candidate windows have been checked. No MMIO or
     * allocator call is made by this policy function, including on failure. */
    for (UmicomSize i = 0U; i < count; ++i) outTransports[i] = candidates[i];
    *outCount = count;
    return UMICOM_BLOCK_OK;
}
