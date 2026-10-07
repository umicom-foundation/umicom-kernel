/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/hardware_boot.c
 *
 * Capture firmware descriptions once, then expose only copied observations.
 * The established platform profile still owns boot RAM, UART and timer setup.
 * Discovery is deliberately not allowed to reconfigure those working owners.
 * Recovery never calls this path and gains no dependency on a valid tree.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/device_tree.h"
#include "umicom/kernel/platform.h"

static UmicomKernelDeviceTreeReader umicomHardwareReader;
static UmicomKernelHardwareCatalogue umicomHardwareCatalogue;
static UmicomKernelTreeStatus umicomHardwareStatus = UMICOM_TREE_NOT_FOUND;
static UmicomBoolean umicomHardwareAttempted;

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
    umicomHardwareStatus = UmicomKernelDeviceTreeOpen((const void *)address, tree.totalBytes, &umicomHardwareReader);
    if (umicomHardwareStatus == UMICOM_TREE_OK)
        umicomHardwareStatus = UmicomKernelHardwareCatalogueBuild(&umicomHardwareReader, &umicomHardwareCatalogue);
    /* No later command depends on the source blob or this temporary index. */
    volatile UmicomU8 *p = (volatile UmicomU8 *)&umicomHardwareReader;
    for (UmicomSize i = 0U; i < sizeof(umicomHardwareReader); ++i) p[i] = 0U;
}
UmicomKernelTreeStatus UmicomKernelHardwareCaptureStatus(void)
{
    return umicomHardwareStatus;
}
const UmicomKernelHardwareCatalogue *UmicomKernelHardwareCatalogueRead(void)
{
    return umicomHardwareStatus == UMICOM_TREE_OK && umicomHardwareCatalogue.ready ? &umicomHardwareCatalogue : 0;
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
void UmicomKernelHardwareReport(void *context, UmicomKernelHardwareWrite write)
{
    if (!write) return;
    UmicomHardwareText(context, write, "hardware.catalogue=");
    UmicomHardwareText(context, write, UmicomKernelTreeStatusName(umicomHardwareStatus));
    UmicomHardwareText(context, write, "\r\n");
    const UmicomKernelHardwareCatalogue *c = UmicomKernelHardwareCatalogueRead();
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
