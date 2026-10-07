/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/hardware_validation.c
 *
 * Qualify the observed description against the existing QEMU test command,
 * without turning description bytes into MMIO accesses. A future driver must
 * negotiate and validate a real device separately; this check proves no such
 * device was needed just to enumerate its advertised transport window.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

static void UmicomHardwareRequire(UmicomBoolean condition, const char *operation)
{
    if (condition) return;
    UmicomKernelConsoleWrite("hardware.check="); UmicomKernelConsoleWriteLine(operation);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0xc1U); UmicomPlatformHalt();
}
void UmicomKernelHardwareValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("hardware-discovery-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after; /* SnapshotRead fills them on success. */
    UmicomHardwareRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "baseline-accounting");
    /* Expose the parser result before stopping, so a rejected future firmware
     * description is distinguishable from a mismatched device address. */
    UmicomKernelConsoleWrite("hardware.catalogue-status=");
    UmicomKernelConsoleWriteLine(UmicomKernelTreeStatusName(UmicomKernelHardwareCaptureStatus()));
    const UmicomKernelHardwareCatalogue *c = UmicomKernelHardwareCatalogueRead();
    UmicomHardwareRequire(c && UmicomKernelHardwareCaptureStatus() == UMICOM_TREE_OK, "complete-tree");
    if (!c) return; /* The platform stop is non-returning, but its old declaration is not. */
    UmicomHardwareRequire(c->nodes > 0U && c->timebaseFrequency > 0U, "timebase");
    UmicomPlatformPhysicalMemoryInfo ram = {0};
    UmicomPlatformPhysicalMemoryDescribe(&ram);
    UmicomBoolean memory = UMICOM_FALSE, hart = UMICOM_FALSE, serial = UMICOM_FALSE, timer = UMICOM_FALSE;
    UmicomU64 slots = 0U;
    for (UmicomU32 i = 0U; i < c->devices; ++i) {
        const UmicomKernelHardwareDevice *d = &c->entries[i];
        if (!d->enabled) continue;
        for (UmicomU32 j = 0U; j < d->registerCount; ++j) {
            const UmicomKernelHardwareRegister *reg = &d->registers[j];
            if (reg->translation != UMICOM_TREE_OK) continue;
            if (d->kind == UMICOM_HARDWARE_MEMORY && reg->physicalAddress == ram.base && reg->bytes == ram.bytes)
                memory = UMICOM_TRUE;
            if (d->kind == UMICOM_HARDWARE_CPU && reg->busAddress == 0U) hart = UMICOM_TRUE;
            if (d->kind == UMICOM_HARDWARE_UART && reg->physicalAddress == 0x10000000ULL) serial = UMICOM_TRUE;
            if (d->kind == UMICOM_HARDWARE_TIMER && reg->physicalAddress == 0x02000000ULL) timer = UMICOM_TRUE;
            if (d->kind == UMICOM_HARDWARE_VIRTIO_MMIO) ++slots;
        }
    }
    UmicomHardwareRequire(memory && hart && serial && timer && slots > 0U, "qemu-profile-description");
    UmicomKernelConsoleWriteLine("hardware.fixed-profile-description=matched");
    UmicomKernelConsoleWrite("hardware.virtio-transport-slots=");
    UmicomKernelConsoleWriteUnsigned(slots); UmicomKernelConsoleWriteLine("");
    UmicomHardwareRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        before.allocatedFrames == after.allocatedFrames && before.freeFrames == after.freeFrames &&
        before.reservedFrames == after.reservedFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "final-accounting");
    UmicomKernelConsoleWriteLine("hardware.mmio-probes=none");
    UmicomKernelConsoleWriteLine("hardware.frame-accounting=unchanged");
    UmicomKernelConsoleWriteLine("hardware-discovery-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_HARDWARE_DISCOVERY_READY");
}
