/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/boot_memory.c
 *
 * PURPOSE:
 *   Initialize native physical memory only after validating Kernel, DTB and firmware reservations.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
/* Normal memory bootstrap remains separate from cumulative allocator tests.
 * Recovery has no dependency on this physical-memory owner. */
#include "umicom/kernel/startup.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/address.h"
#include "umicom/kernel/device_tree.h"
#include "umicom/kernel/physical_memory.h"

#include "umicom/kernel/boot_reservations.h"
#include "umicom/kernel/device_tree_firmware.h"

/* Bounded early-boot workspace is part of the linked Kernel image, so its
 * pages are included in the Kernel reservation. It avoids a 128 KiB copy on
 * the 64 KiB bootstrap stack. Only single-hart startup uses this owner. */
_Alignas(8) static UmicomU8 umicomBootFirmwareCopy[UMICOM_TREE_MAX_BYTES];
static UmicomKernelDeviceTreeReader umicomBootFirmwareReader;
static void ClearFirmwareWorkspace(void)
{
    volatile UmicomU8 *copy=umicomBootFirmwareCopy;
    for(UmicomSize i=0;i<sizeof umicomBootFirmwareCopy;++i)copy[i]=0U;
    volatile UmicomU8 *reader=(volatile UmicomU8 *)&umicomBootFirmwareReader;
    for(UmicomSize i=0;i<sizeof umicomBootFirmwareReader;++i)reader[i]=0U;
}


/* The original Kernel-plus-DTB-only reservation path is retained for review.
 * The active preflight also protects firmware reservation entries and merges
 * overlapping page ranges before the existing allocator receives them. */
#if 0
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
#endif

/* Validate all reservations before initializing the global frame owner.
 * Recovery can reject malformed metadata while the allocator is untouched. */
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
    if (!UmicomKernelDeviceTreeInspect(dtb, ram.base, ram.bytes, &tree) ||
        tree.address < end || (tree.address & 7U))
        return UMICOM_BOOT_MEMORY_BAD_INPUT;
    UmicomKernelDeviceTreeLayout layout;
    /* Reuse firmware compatibility handling already used by discovery. Only
     * property padding is normalized in the owned copy; source bytes and
     * reservation values remain untouched. The strict reader is still final. */
#if 0
    if (UmicomKernelDeviceTreeLayoutRead((const void *)(UmicomUIntPtr)tree.address,
        tree.totalBytes, &layout) != UMICOM_FDT_LAYOUT_OK)
        return UMICOM_BOOT_MEMORY_BAD_INPUT;
#endif
    UmicomSize changedPadding=0U;
    UmicomKernelTreeStatus opened=UmicomKernelDeviceTreeOpenFirmwareCopy(
        (const void *)(UmicomUIntPtr)tree.address,tree.totalBytes,
        umicomBootFirmwareCopy,sizeof umicomBootFirmwareCopy,
        &umicomBootFirmwareReader,&changedPadding);
    UmicomKernelDeviceTreeLayoutStatus projected=opened==UMICOM_TREE_OK?
        UmicomKernelDeviceTreeLayoutFromReader(&umicomBootFirmwareReader,&layout):UMICOM_FDT_LAYOUT_HEADER;
    ClearFirmwareWorkspace();
    if(projected!=UMICOM_FDT_LAYOUT_OK)return UMICOM_BOOT_MEMORY_BAD_INPUT;
    UmicomKernelBootReservationPlan plan;
    if (!UmicomKernelBootReservationsPlan(ram.base, ram.bytes, end, tree.address, &layout, &plan))
        return UMICOM_BOOT_MEMORY_BAD_INPUT;
    UmicomKernelPhysicalMemorySnapshot prior;
    if (UmicomKernelPhysicalMemorySnapshotRead(&prior) != UMICOM_KERNEL_MEMORY_NOT_INITIALISED)
        return UMICOM_BOOT_MEMORY_ALREADY_INITIALISED;
    if (UmicomKernelPhysicalMemoryInitialize(ram.base, ram.bytes) != UMICOM_KERNEL_MEMORY_OK)
        return UMICOM_BOOT_MEMORY_ALLOCATION_ERROR;
    for (UmicomSize i = 0; i < plan.count; ++i)
        if (UmicomKernelPhysicalMemoryReserveRange(plan.ranges[i].base, plan.ranges[i].bytes) != UMICOM_KERNEL_MEMORY_OK)
            return UMICOM_BOOT_MEMORY_ALLOCATION_ERROR;
    if (UmicomKernelPhysicalMemoryValidate() != UMICOM_KERNEL_MEMORY_OK)
        return UMICOM_BOOT_MEMORY_ALLOCATION_ERROR;
    return UMICOM_BOOT_MEMORY_OK;
}
