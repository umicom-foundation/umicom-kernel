/*-----------------------------------------------------------------------------
 * Umicom Kernel normal memory bootstrap
 * File: kernel/boot_memory.c
 *
 * This calls the established DTB inspector and physical-frame owner. It is
 * intentionally separate from the cumulative allocation/free acceptance tests:
 * normal boot protects its own memory, but does not pretend to have run those
 * tests. No alternate bitmap, discovery parser or allocation scheme is added.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/startup.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/address.h"
#include "umicom/kernel/device_tree.h"
#include "umicom/kernel/physical_memory.h"

UmicomKernelBootMemoryStatus UmicomKernelBootMemoryInitialize(UmicomU64 hart,
    UmicomAddress dtb, UmicomAddress start, UmicomAddress end)
{
    if (hart != 0U) return UMICOM_BOOT_MEMORY_BAD_INPUT;
    UmicomPlatformPhysicalMemoryInfo ram;
    UmicomPlatformPhysicalMemoryDescribe(&ram);
    UmicomAddress ramEnd = 0U;
    if (!ram.bytes || !UmicomKernelAddressAddChecked(ram.base, ram.bytes, &ramEnd) ||
        start < ram.base || end <= start || end > ramEnd ||
        (end & (UMICOM_KERNEL_PAGE_SIZE - 1U)) != 0U ||
        (ram.base & (UMICOM_KERNEL_PAGE_SIZE - 1U)) != 0U ||
        (ram.bytes & (UMICOM_KERNEL_PAGE_SIZE - 1U)) != 0U ||
        ram.bytes / UMICOM_KERNEL_PAGE_SIZE > UMICOM_KERNEL_PHYSICAL_MAX_FRAMES)
        return UMICOM_BOOT_MEMORY_BAD_INPUT;
    UmicomKernelDeviceTreeInfo tree;
    if (!UmicomKernelDeviceTreeInspect(dtb, ram.base, ram.bytes, &tree) || tree.address < end)
        return UMICOM_BOOT_MEMORY_BAD_INPUT;
    /* No consumer may observe free Kernel pages between initialisation and the
     * reservations. Startup is single-hart and interrupts remain disabled. A
     * complete preflight also rules out overlap when ranges round to pages. */
    UmicomKernelPhysicalMemorySnapshot prior;
    if (UmicomKernelPhysicalMemorySnapshotRead(&prior) != UMICOM_KERNEL_MEMORY_NOT_INITIALISED)
        return UMICOM_BOOT_MEMORY_ALREADY_INITIALISED;
    if (UmicomKernelPhysicalMemoryInitialize(ram.base, ram.bytes) != UMICOM_KERNEL_MEMORY_OK ||
        UmicomKernelPhysicalMemoryReserveRange(ram.base, end - ram.base) != UMICOM_KERNEL_MEMORY_OK ||
        UmicomKernelPhysicalMemoryReserveRange(tree.address, tree.totalBytes) != UMICOM_KERNEL_MEMORY_OK ||
        UmicomKernelPhysicalMemoryValidate() != UMICOM_KERNEL_MEMORY_OK)
        return UMICOM_BOOT_MEMORY_ALLOCATION_ERROR;
    return UMICOM_BOOT_MEMORY_OK;
}
