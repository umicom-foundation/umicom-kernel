/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/hardware_boot.c
 *
 * PURPOSE:
 *   Capture firmware descriptions once, then expose only copied observations.
 *   The established platform profile still owns boot RAM, UART and timer setup.
 *   Discovery is deliberately not allowed to reconfigure those working owners.
 *   Recovery never calls this path and gains no dependency on a valid tree.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/device_tree.h"
#include "umicom/kernel/device_tree_firmware.h"
#include "umicom/kernel/platform.h"

static UmicomKernelDeviceTreeReader umicomHardwareReader;
static UmicomKernelHardwareCatalogue umicomHardwareCatalogue;
static UmicomKernelTreeStatus umicomHardwareStatus = UMICOM_TREE_NOT_FOUND;
static UmicomBoolean umicomHardwareAttempted;
/* The boot-entry observation has its own lifetime even if the DTB is missing. */
static UmicomU64 umicomHardwareBootHartId;
static UmicomBoolean umicomHardwareBootHartObserved;

/* Keep producer compatibility inside this one boot-capture owner. The input
 * stays immutable, the temporary copy has the same bound as the strict reader,
 * and no descriptor string is treated as proof of a producer's identity. */
_Alignas(8) static UmicomU8 umicomHardwareFirmwareCopy[UMICOM_TREE_MAX_BYTES];
static UmicomSize umicomHardwareCanonicalisedPropertyPaddingBytes;

void UmicomKernelHardwareCapture(UmicomAddress address)
{
    if (umicomHardwareAttempted) return; /* A later call cannot replace the boot evidence. */
    umicomHardwareAttempted = UMICOM_TRUE;
    UmicomPlatformPhysicalMemoryInfo ram = {0};
    UmicomKernelDeviceTreeInfo tree = {0};
    UmicomPlatformPhysicalMemoryDescribe(&ram);
    /* Inspect proves the complete supplied extent is in the selected RAM
     * profile before Open reads anything beyond the minimum header. */
    if (!UmicomKernelDeviceTreeInspect(address, ram.base, ram.bytes, &tree)) {
        umicomHardwareStatus = UMICOM_TREE_OUTSIDE_BUFFER;
        return;
    }
    /* The original direct-open path is retained for review. Actual QEMU/libfdt
     * firmware can leave non-zero residue in property alignment padding. The
     * explicit owned-copy adapter below normalises only those padding bytes,
     * then calls this same strict reader with every original admission guard. */
#if 0
    umicomHardwareStatus = UmicomKernelDeviceTreeOpen((const void *)address, tree.totalBytes, &umicomHardwareReader);
#endif
    umicomHardwareStatus = UmicomKernelDeviceTreeOpenFirmwareCopy(
        (const void *)address, tree.totalBytes,
        umicomHardwareFirmwareCopy, sizeof(umicomHardwareFirmwareCopy),
        &umicomHardwareReader, &umicomHardwareCanonicalisedPropertyPaddingBytes);
    const UmicomBoolean firmwareCopyOpened = umicomHardwareStatus == UMICOM_TREE_OK ?
        UMICOM_TRUE : UMICOM_FALSE;
    if (umicomHardwareStatus == UMICOM_TREE_OK)
        umicomHardwareStatus = UmicomKernelHardwareCatalogueBuild(&umicomHardwareReader, &umicomHardwareCatalogue);
    /* No later command depends on the source blob or this temporary index. */
    volatile UmicomU8 *p = (volatile UmicomU8 *)&umicomHardwareReader;
    for (UmicomSize i = 0U; i < sizeof(umicomHardwareReader); ++i) p[i] = 0U;
    /* CatalogueBuild copies every published observation. Once extraction has
     * finished, wipe the temporary firmware bytes as well as the old index.
     * On an unsuccessful open the adapter already wipes any copied extent. */
    if (firmwareCopyOpened) {
        volatile UmicomU8 *copy = umicomHardwareFirmwareCopy;
        for (UmicomSize i = 0U; i < sizeof(umicomHardwareFirmwareCopy); ++i) copy[i] = 0U;
    }
}
UmicomKernelTreeStatus UmicomKernelHardwareCaptureStatus(void)
{
    return umicomHardwareStatus;
}
const UmicomKernelHardwareCatalogue *UmicomKernelHardwareCatalogueRead(void)
{
    return umicomHardwareStatus == UMICOM_TREE_OK && umicomHardwareCatalogue.ready ? &umicomHardwareCatalogue : 0;
}

void UmicomKernelHardwareObserveBootHart(UmicomU64 hartId)
{
    /* The serial boot boundary is authoritative for this first observation. */
    if (umicomHardwareBootHartObserved) return;
    umicomHardwareBootHartId = hartId;
    umicomHardwareBootHartObserved = UMICOM_TRUE;
}

UmicomKernelTreeStatus UmicomKernelHardwareCpuTopologyRead(UmicomKernelHardwareCpuTopology *topology)
{
    if (topology == 0) return UMICOM_TREE_INVALID_ARGUMENT;
    const UmicomKernelHardwareCatalogue *catalogue = UmicomKernelHardwareCatalogueRead();
    /* Separate absence of a checked description from a checked tree that
     * does not describe the hart. Both observations can be retained. */
    UmicomKernelHardwareCpuTopology observation = {0};
    observation.bootHartObserved = umicomHardwareBootHartObserved;
    observation.bootHartId = umicomHardwareBootHartId;
    observation.firmwareAvailable = catalogue != 0 ? UMICOM_TRUE : UMICOM_FALSE;
    if (observation.bootHartObserved) {
        observation.bootHartDescription = catalogue != 0 ? UMICOM_BOOT_HART_NOT_DESCRIBED :
            UMICOM_BOOT_HART_FIRMWARE_UNAVAILABLE;
    }
    if (catalogue != 0) {
        /* The catalogue rejects duplicate enabled CPU IDs but may retain
         * disabled entries. Count entries as published and let an enabled
         * match take precedence over a disabled match for the boot ID. */
        for (UmicomU32 i = 0U; i < catalogue->devices; ++i) {
            const UmicomKernelHardwareDevice *entry = &catalogue->entries[i];
            if (entry->kind != UMICOM_HARDWARE_CPU) continue;
            ++observation.describedCpuEntries;
            if (entry->enabled) ++observation.firmwareEnabledCpuEntries;
            if (observation.bootHartObserved && entry->registerCount != 0U &&
                entry->registers[0].busAddress == observation.bootHartId &&
                observation.bootHartDescription != UMICOM_BOOT_HART_FIRMWARE_ENABLED) {
                observation.bootHartDescription = entry->enabled ? UMICOM_BOOT_HART_FIRMWARE_ENABLED :
                    UMICOM_BOOT_HART_FIRMWARE_DISABLED;
            }
        }
    }
    *topology = observation;
    return UMICOM_TREE_OK;
}

static void UmicomHardwareText(void *context, UmicomKernelHardwareWrite write, const char *text)
{
    UmicomSize n = 0U;
    while (text[n]) ++n;
    write(context, text, n);
}
static void UmicomHardwareNumber(void *context, UmicomKernelHardwareWrite write, UmicomU64 value, UmicomBoolean hex)
{
    char digits[32]; /* Every emitted byte is filled before the callback reads it. */
    static const char alphabet[] = "0123456789abcdef";
    UmicomSize used = 0U;
    const UmicomU64 radix = hex ? 16U : 10U;
    do { digits[used++] = alphabet[value % radix]; value /= radix; } while (value);
    if (hex) UmicomHardwareText(context, write, "0x");
    while (used) write(context, &digits[--used], 1U);
}
static void UmicomHardwareRecord(void *context, UmicomKernelHardwareWrite write, const char *key, UmicomU64 value)
{
    UmicomHardwareText(context, write, key);
    UmicomHardwareNumber(context, write, value, UMICOM_FALSE);
    UmicomHardwareText(context, write, "\r\n");
}

/* Expose both evidence owners through the existing hardware command. Avoid
 * reporting zero firmware counts when no valid description was available. */
static void UmicomHardwareReportCpuTopology(void *context, UmicomKernelHardwareWrite write)
{
    static const char *const descriptions[] = {
        [UMICOM_BOOT_HART_UNOBSERVED] = "unobserved",
        [UMICOM_BOOT_HART_FIRMWARE_UNAVAILABLE] = "firmware-unavailable",
        [UMICOM_BOOT_HART_NOT_DESCRIBED] = "not-described",
        [UMICOM_BOOT_HART_FIRMWARE_DISABLED] = "firmware-disabled",
        [UMICOM_BOOT_HART_FIRMWARE_ENABLED] = "firmware-enabled"
    };
    UmicomKernelHardwareCpuTopology topology;
    if (UmicomKernelHardwareCpuTopologyRead(&topology) != UMICOM_TREE_OK) return;
    UmicomHardwareText(context, write, topology.firmwareAvailable ?
        "hardware.cpu.firmware=available\r\n" : "hardware.cpu.firmware=unavailable\r\n");
    if (topology.firmwareAvailable) {
        UmicomHardwareRecord(context, write, "hardware.cpu.described=", topology.describedCpuEntries);
        UmicomHardwareRecord(context, write, "hardware.cpu.firmware-enabled=", topology.firmwareEnabledCpuEntries);
    }
    if (topology.bootHartObserved)
        UmicomHardwareRecord(context, write, "hardware.cpu.boot-hart=", topology.bootHartId);
    UmicomHardwareText(context, write, "hardware.cpu.boot-description=");
    UmicomHardwareText(context, write, descriptions[topology.bootHartDescription]);
    UmicomHardwareText(context, write, "\r\n");
}

void UmicomKernelHardwareReport(void *context, UmicomKernelHardwareWrite write)
{
    if (!write) return;
    UmicomHardwareText(context, write, "hardware.catalogue=");
    UmicomHardwareText(context, write, UmicomKernelTreeStatusName(umicomHardwareStatus));
    UmicomHardwareText(context, write, "\r\n");
    const UmicomKernelHardwareCatalogue *c = UmicomKernelHardwareCatalogueRead();
    UmicomHardwareRecord(context, write, "hardware.canonicalised-property-padding-bytes=",
        umicomHardwareCanonicalisedPropertyPaddingBytes);
    UmicomHardwareReportCpuTopology(context, write);
    if (!c) {
        UmicomHardwareText(context, write, "No complete hardware inventory is available. Existing fixed-profile drivers are unchanged.\r\n");
        return;
    }
    UmicomHardwareText(context, write, "hardware.model=");
    UmicomHardwareText(context, write, c->model[0] ? c->model : "not-supplied");
    UmicomHardwareText(context, write, "\r\nhardware.stdout-path=");
    UmicomHardwareText(context, write, c->stdoutPath[0] ? c->stdoutPath : "not-supplied");
    UmicomHardwareText(context, write, "\r\n");
    UmicomHardwareRecord(context, write, "hardware.nodes=", c->nodes);
    UmicomHardwareRecord(context, write, "hardware.properties=", c->properties);
    UmicomHardwareRecord(context, write, "hardware.timebase-hz=", c->timebaseFrequency);
    UmicomHardwareRecord(context, write, "hardware.firmware-reservations=", c->firmwareReservations);
    for (UmicomU32 i = 0U; i < c->firmwareReservations; ++i) {
        UmicomHardwareText(context, write, "  reservation.address=");
        UmicomHardwareNumber(context, write, c->reservations[i].address, UMICOM_TRUE);
        UmicomHardwareText(context, write, " bytes=");
        UmicomHardwareNumber(context, write, c->reservations[i].bytes, UMICOM_FALSE);
        UmicomHardwareText(context, write, "\r\n");
    }
    UmicomHardwareRecord(context, write, "hardware.devices=", c->devices);
    for (UmicomU32 i = 0U; i < c->devices; ++i) {
        const UmicomKernelHardwareDevice *d = &c->entries[i];
        UmicomHardwareText(context, write, "hardware.device=");
        UmicomHardwareText(context, write, d->path);
        UmicomHardwareText(context, write, " kind=");
        UmicomHardwareText(context, write, UmicomKernelHardwareKindName(d->kind));
        UmicomHardwareText(context, write, d->enabled ? " enabled=yes\r\n" : " enabled=no\r\n");
        if (d->compatible[0]) {
            UmicomHardwareText(context, write, "  compatible=");
            UmicomHardwareText(context, write, d->compatible);
            UmicomHardwareText(context, write, "\r\n");
        }
        if (d->interruptParent) UmicomHardwareRecord(context, write, "  irq-parent-reference=", d->interruptParent);
        if (d->hasInterruptSpecifiers) UmicomHardwareText(context, write, "  irq-route=not-decoded\r\n");
        if (!d->registerCount) {
            UmicomHardwareText(context, write, "  registers=");
            UmicomHardwareText(context, write, UmicomKernelTreeStatusName(d->registerStatus));
            UmicomHardwareText(context, write, "\r\n");
        }
        for (UmicomU32 j = 0U; j < d->registerCount; ++j) {
            const UmicomKernelHardwareRegister *reg = &d->registers[j];
            if (d->kind == UMICOM_HARDWARE_CPU) {
                UmicomHardwareRecord(context, write, "  hart-id=", reg->busAddress);
                continue;
            }
            UmicomHardwareText(context, write, "  bus-address=");
            UmicomHardwareNumber(context, write, reg->busAddress, UMICOM_TRUE);
            UmicomHardwareText(context, write, " bytes=");
            UmicomHardwareNumber(context, write, reg->bytes, UMICOM_FALSE);
            UmicomHardwareText(context, write, " translation=");
            UmicomHardwareText(context, write, UmicomKernelTreeStatusName(reg->translation));
            if (reg->translation == UMICOM_TREE_OK) {
                UmicomHardwareText(context, write, " physical=");
                UmicomHardwareNumber(context, write, reg->physicalAddress, UMICOM_TRUE);
            }
            UmicomHardwareText(context, write, "\r\n");
        }
    }
    UmicomHardwareText(context, write,
        "Inventory only: no MMIO probes, IRQ routing, DMA setup or driver binding.\r\n"
        "VirtIO entries are transport slots, not confirmed disks or network devices.\r\n"
        "Allocator geometry and reservations still use the existing fixed platform profile.\r\n");
}
