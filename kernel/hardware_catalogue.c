/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/hardware_catalogue.c
 *
 * reg values belong to a parent bus, not automatically to physical memory.
 * Empty ranges means identity translation; absent ranges does not. This reader
 * supports one- and two-cell ordinary bus addresses and sizes. PCI child-space
 * flags, interrupt routing and DMA translations require their own later owners.
 * An unsupported description is reported, not interpreted using a fixed address.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/hardware_catalogue.h"

typedef struct UmicomHardwareNodeContext {
    UmicomU32 addressCells;
    UmicomU32 sizeCells;
    UmicomU32 phandle;
    UmicomU32 interruptParent;
    UmicomBoolean enabled;
} UmicomHardwareNodeContext;

static void UmicomHardwareClear(void *p, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)p;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static UmicomBoolean UmicomHardwareEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < UMICOM_TREE_PATH_BYTES; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomU64 UmicomHardwareCells(const UmicomU8 *p, UmicomU32 cells)
{
    /* Only called with one or two cells after the tuple span was checked. */
    UmicomU64 value = 0U;
    for (UmicomU32 i = 0U; i < cells * 4U; ++i) value = (value << 8U) | p[i];
    return value;
}
static UmicomKernelTreeStatus UmicomHardwareNumber(const UmicomKernelDeviceTreeReader *r,
    UmicomU32 node, const char *name, UmicomU32 *value)
{
    UmicomKernelTreeSpan span = {0};
    const UmicomKernelTreeStatus status = UmicomKernelDeviceTreeProperty(r, node, name, &span);
    if (status == UMICOM_TREE_NOT_FOUND) return UMICOM_TREE_OK; /* Keep explicit caller default. */
    return status == UMICOM_TREE_OK ? UmicomKernelTreeU32(span, value) : status;
}
static UmicomKernelTreeStatus UmicomHardwareText(const UmicomKernelDeviceTreeReader *r,
    UmicomU32 node, const char *name, char *text, UmicomSize bytes)
{
    UmicomKernelTreeSpan span = {0};
    const UmicomKernelTreeStatus status = UmicomKernelDeviceTreeProperty(r, node, name, &span);
    if (status == UMICOM_TREE_NOT_FOUND) return UMICOM_TREE_OK;
    return status == UMICOM_TREE_OK ? UmicomKernelTreeString(span, text, bytes) : status;
}
static UmicomKernelTreeStatus UmicomHardwareCompatible(const UmicomKernelDeviceTreeReader *r,
    UmicomU32 node, char *first, UmicomKernelHardwareKind *kind)
{
    UmicomKernelTreeSpan span = {0};
    const UmicomKernelTreeStatus status = UmicomKernelDeviceTreeProperty(r, node, "compatible", &span);
    if (status == UMICOM_TREE_NOT_FOUND) return UMICOM_TREE_OK;
    if (status != UMICOM_TREE_OK) return status;
    /* This list classifies advertised interfaces only. Nothing here probes,
     * initialises or asserts compatibility of a real peripheral. */
    static const struct { const char *name; UmicomKernelHardwareKind kind; } bindings[] = {
        {"ns16550a", UMICOM_HARDWARE_UART}, {"ns16550", UMICOM_HARDWARE_UART},
        {"riscv,clint0", UMICOM_HARDWARE_TIMER}, {"sifive,clint0", UMICOM_HARDWARE_TIMER},
        {"riscv,aclint-mtimer", UMICOM_HARDWARE_TIMER},
        {"riscv,plic0", UMICOM_HARDWARE_INTERRUPT_CONTROLLER},
        {"sifive,plic-1.0.0", UMICOM_HARDWARE_INTERRUPT_CONTROLLER},
        {"riscv,cpu-intc", UMICOM_HARDWARE_INTERRUPT_CONTROLLER},
        {"virtio,mmio", UMICOM_HARDWARE_VIRTIO_MMIO},
        {"pci-host-ecam-generic", UMICOM_HARDWARE_PCI_HOST}
    };
    for (UmicomSize i = 0U; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        UmicomBoolean match = UMICOM_FALSE;
        const UmicomKernelTreeStatus valid = UmicomKernelTreeStringListContains(span, bindings[i].name, &match);
        if (valid != UMICOM_TREE_OK) return valid;
        if (match) *kind = bindings[i].kind;
    }
    UmicomSize length = 0U;
    while (length < span.bytes && span.data[length]) ++length;
    if (length >= UMICOM_HARDWARE_COMPATIBLE_BYTES) return UMICOM_TREE_LIMIT;
    for (UmicomSize i = 0U; i <= length; ++i) first[i] = (char)span.data[i];
    return UMICOM_TREE_OK;
}
static UmicomKernelTreeStatus UmicomHardwareContexts(const UmicomKernelDeviceTreeReader *r,
    UmicomHardwareNodeContext *contexts)
{
    for (UmicomU32 i = 0U; i < r->nodeCount; ++i) {
        UmicomHardwareNodeContext *c = &contexts[i];
        const UmicomU32 parent = r->nodes[i].parent;
        /* Cell counts describe this node's children. They are not inherited:
         * the specified defaults apply when this node omits either property. */
        c->addressCells = 2U; c->sizeCells = 1U;
        c->enabled = !i || contexts[parent].enabled ? UMICOM_TRUE : UMICOM_FALSE;
        /* Preserve only an explicitly inherited interrupt-parent reference.
         * This is not the interrupt-domain parent algorithm: default nexus
         * relationships, interrupt-map and interrupts-extended are not decoded. */
        c->interruptParent = i ? contexts[parent].interruptParent : 0U;
        UmicomKernelTreeStatus status = UmicomHardwareNumber(r, i, "#address-cells", &c->addressCells);
        if (status != UMICOM_TREE_OK) return status;
        status = UmicomHardwareNumber(r, i, "#size-cells", &c->sizeCells);
        if (status != UMICOM_TREE_OK) return status;
        status = UmicomHardwareNumber(r, i, "interrupt-parent", &c->interruptParent);
        if (status != UMICOM_TREE_OK) return status;
        status = UmicomHardwareNumber(r, i, "phandle", &c->phandle);
        if (status != UMICOM_TREE_OK) return status;
        UmicomU32 legacy = 0U;
        status = UmicomHardwareNumber(r, i, "linux,phandle", &legacy);
        if (status != UMICOM_TREE_OK) return status;
        UmicomKernelTreeSpan declared = {0};
        if ((UmicomKernelDeviceTreeProperty(r, i, "phandle", &declared) == UMICOM_TREE_OK &&
                (!c->phandle || c->phandle == 0xffffffffU)) ||
            (UmicomKernelDeviceTreeProperty(r, i, "linux,phandle", &declared) == UMICOM_TREE_OK &&
                (!legacy || legacy == 0xffffffffU))) return UMICOM_TREE_BAD_VALUE;
        if (legacy && c->phandle && legacy != c->phandle) return UMICOM_TREE_BAD_VALUE;
        if (!c->phandle) c->phandle = legacy;
        UmicomKernelTreeSpan present = {0};
        const UmicomBoolean hasHandle = UmicomKernelDeviceTreeProperty(r, i, "phandle", &present) == UMICOM_TREE_OK ||
            UmicomKernelDeviceTreeProperty(r, i, "linux,phandle", &present) == UMICOM_TREE_OK ? UMICOM_TRUE : UMICOM_FALSE;
        if (hasHandle && (!c->phandle || c->phandle == 0xffffffffU)) return UMICOM_TREE_BAD_VALUE;
        if (c->phandle)
            for (UmicomU32 j = 0U; j < i; ++j)
                if (contexts[j].phandle == c->phandle) return UMICOM_TREE_DUPLICATE;
        char state[64];
        UmicomHardwareClear(state, sizeof(state));
        status = UmicomHardwareText(r, i, "status", state, sizeof(state));
        if (status != UMICOM_TREE_OK) return status;
        if (UmicomKernelDeviceTreeProperty(r, i, "status", &present) == UMICOM_TREE_OK &&
            !UmicomHardwareEqual(state, "ok") && !UmicomHardwareEqual(state, "okay")) c->enabled = UMICOM_FALSE;
        if (UmicomKernelDeviceTreeProperty(r, i, "interrupt-parent", &present) == UMICOM_TREE_OK && !c->interruptParent)
            return UMICOM_TREE_BAD_VALUE;
    }
    /* References may point forwards, so resolve their existence only after all
     * nodes have been indexed. This does not decode an interrupt specifier. */
    for (UmicomU32 i = 0U; i < r->nodeCount; ++i) {
        if (!contexts[i].interruptParent) continue;
        UmicomBoolean found = UMICOM_FALSE;
        for (UmicomU32 j = 0U; j < r->nodeCount; ++j)
            if (contexts[j].phandle == contexts[i].interruptParent) found = UMICOM_TRUE;
        if (!found) return UMICOM_TREE_BAD_VALUE;
    }
    return UMICOM_TREE_OK;
}
static UmicomKernelTreeStatus UmicomHardwareTranslate(const UmicomKernelDeviceTreeReader *r,
    const UmicomHardwareNodeContext *contexts, UmicomU32 bus, UmicomU64 address,
    UmicomU64 bytes, UmicomU64 *out)
{
    while (bus != 0U) {
        const UmicomU32 parent = r->nodes[bus].parent;
        const UmicomU32 ac = contexts[bus].addressCells, pc = contexts[parent].addressCells;
        const UmicomU32 sc = contexts[bus].sizeCells;
        if (!ac || ac > 2U || !pc || pc > 2U || !sc || sc > 2U) return UMICOM_TREE_UNSUPPORTED_CELLS;
        UmicomKernelTreeSpan ranges = {0};
        const UmicomKernelTreeStatus status = UmicomKernelDeviceTreeProperty(r, bus, "ranges", &ranges);
        if (status == UMICOM_TREE_NOT_FOUND) return UMICOM_TREE_UNTRANSLATED;
        if (status != UMICOM_TREE_OK) return status;
        if (ranges.bytes) {
            /* Each window joins a child address, a parent address and a size.
             * Their widths can differ, so there is no universal C tuple to cast
             * onto this byte sequence. Validate the stride before any decode. */
            const UmicomSize stride = ((UmicomSize)ac + pc + sc) * 4U;
            if (ranges.bytes % stride) return UMICOM_TREE_BAD_VALUE;
            if (ranges.bytes / stride > UMICOM_HARDWARE_RANGES_LIMIT) return UMICOM_TREE_LIMIT;
            UmicomBoolean matched = UMICOM_FALSE;
            UmicomU64 translated = 0U;
            for (UmicomSize offset = 0U; offset < ranges.bytes; offset += stride) {
                const UmicomU8 *tuple = ranges.data + offset;
                const UmicomU64 child = UmicomHardwareCells(tuple, ac);
                const UmicomU64 host = UmicomHardwareCells(tuple + ac * 4U, pc);
                const UmicomU64 length = UmicomHardwareCells(tuple + (ac + pc) * 4U, sc);
                if (!length || child > ~(UmicomU64)0U - length || host > ~(UmicomU64)0U - length)
                    return UMICOM_TREE_BAD_VALUE;
                if ((ac == 1U && length > 0x100000000ULL - child) ||
                    (pc == 1U && length > 0x100000000ULL - host)) return UMICOM_TREE_BAD_VALUE;
                /* Ambiguous windows are rejected even if this register lies in
                 * a different window. A later caller must see the same bus. */
                for (UmicomSize previous = 0U; previous < offset; previous += stride) {
                    const UmicomU64 old = UmicomHardwareCells(ranges.data + previous, ac);
                    const UmicomU64 oldBytes = UmicomHardwareCells(ranges.data + previous + (ac + pc) * 4U, sc);
                    if (child < old + oldBytes && old < child + length) return UMICOM_TREE_BAD_VALUE;
                }
                /* The complete register extent must fit one window. Matching
                 * its first byte is insufficient when the device extends into
                 * a different or unmapped portion of the parent bus. */
                if (address >= child && address - child < length && bytes <= length - (address - child)) {
                    translated = host + (address - child); matched = UMICOM_TRUE;
                }
            }
            if (!matched) return UMICOM_TREE_UNTRANSLATED;
            address = translated;
        }
        /* Empty ranges is the explicit identity case. Check narrowing before
         * walking to a parent with only one address cell. */
        if (pc == 1U && (address > 0xffffffffU || bytes > 0x100000000ULL - address))
            return UMICOM_TREE_BAD_VALUE;
        bus = parent;
    }
    *out = address;
    return UMICOM_TREE_OK;
}
static UmicomKernelTreeStatus UmicomHardwareRegisters(const UmicomKernelDeviceTreeReader *r,
    const UmicomHardwareNodeContext *contexts, UmicomU32 node, UmicomKernelHardwareDevice *device)
{
    UmicomKernelTreeSpan reg = {0};
    const UmicomKernelTreeStatus present = UmicomKernelDeviceTreeProperty(r, node, "reg", &reg);
    device->registerStatus = present;
    if (present == UMICOM_TREE_NOT_FOUND) {
        return device->kind == UMICOM_HARDWARE_MEMORY || device->kind == UMICOM_HARDWARE_CPU
            ? UMICOM_TREE_BAD_VALUE : UMICOM_TREE_OK;
    }
    if (present != UMICOM_TREE_OK) return present;
    const UmicomU32 parent = r->nodes[node].parent;
    const UmicomU32 ac = contexts[parent].addressCells, sc = contexts[parent].sizeCells;
    if (!ac || ac > 2U || sc > 2U) {
        device->registerStatus = UMICOM_TREE_UNSUPPORTED_CELLS; return UMICOM_TREE_OK;
    }
    const UmicomSize stride = ((UmicomSize)ac + sc) * 4U;
    if (!reg.bytes || reg.bytes % stride) return UMICOM_TREE_BAD_VALUE;
    if (reg.bytes / stride > UMICOM_HARDWARE_REG_LIMIT) return UMICOM_TREE_LIMIT;
    device->registerCount = (UmicomU32)(reg.bytes / stride);
    for (UmicomU32 i = 0U; i < device->registerCount; ++i) {
        UmicomKernelHardwareRegister *out = &device->registers[i];
        out->busAddress = UmicomHardwareCells(reg.data + i * stride, ac);
        out->bytes = sc ? UmicomHardwareCells(reg.data + i * stride + ac * 4U, sc) : 0U;
        if (out->busAddress > ~(UmicomU64)0U - out->bytes ||
            (ac == 1U && out->bytes > 0x100000000ULL - out->busAddress)) return UMICOM_TREE_BAD_VALUE;
        if (device->kind == UMICOM_HARDWARE_CPU) {
            if (sc != 0U || device->registerCount != 1U) return UMICOM_TREE_BAD_VALUE;
            out->translation = UMICOM_TREE_OK; /* busAddress is a hart ID, not RAM. */
            continue;
        }
        if (!sc || !out->bytes) {
            device->registerStatus = UMICOM_TREE_UNSUPPORTED_CELLS;
            out->translation = UMICOM_TREE_UNSUPPORTED_CELLS;
            continue;
        }
        out->translation = UmicomHardwareTranslate(r, contexts, parent, out->busAddress, out->bytes, &out->physicalAddress);
        if (out->translation != UMICOM_TREE_OK && out->translation != UMICOM_TREE_UNTRANSLATED &&
            out->translation != UMICOM_TREE_UNSUPPORTED_CELLS) return out->translation;
    }
    return UMICOM_TREE_OK;
}
static UmicomKernelTreeStatus UmicomHardwareBuild(const UmicomKernelDeviceTreeReader *r,
    UmicomKernelHardwareCatalogue *out)
{
    UmicomHardwareNodeContext contexts[UMICOM_TREE_NODE_LIMIT];
    UmicomHardwareClear(contexts, sizeof(contexts));
    UmicomKernelTreeStatus status = UmicomHardwareContexts(r, contexts);
    if (status != UMICOM_TREE_OK) return status;
    status = UmicomHardwareText(r, 0U, "model", out->model, sizeof(out->model));
    if (status != UMICOM_TREE_OK) return status;
    /* Validate root compatibility too, without presenting the root as a device. */
    char compatible[UMICOM_HARDWARE_COMPATIBLE_BYTES];
    UmicomHardwareClear(compatible, sizeof(compatible));
    UmicomKernelHardwareKind ignored = UMICOM_HARDWARE_OTHER;
    status = UmicomHardwareCompatible(r, 0U, compatible, &ignored);
    if (status != UMICOM_TREE_OK) return status;
    UmicomU32 cpus = 0U, chosen = 0U, reserved = UMICOM_TREE_NO_NODE;
    status = UmicomKernelDeviceTreeFind(r, "/cpus", &cpus);
    if (status == UMICOM_TREE_OK) {
        UmicomU32 frequency = 0U;
        status = UmicomHardwareNumber(r, cpus, "timebase-frequency", &frequency);
        if (status != UMICOM_TREE_OK) return status;
        out->timebaseFrequency = frequency;
    } else if (status != UMICOM_TREE_NOT_FOUND) return status;
    status = UmicomKernelDeviceTreeFind(r, "/chosen", &chosen);
    if (status == UMICOM_TREE_OK) {
        status = UmicomHardwareText(r, chosen, "stdout-path", out->stdoutPath, sizeof(out->stdoutPath));
        if (status != UMICOM_TREE_OK) return status;
    } else if (status != UMICOM_TREE_NOT_FOUND) return status;
    status = UmicomKernelDeviceTreeFind(r, "/reserved-memory", &reserved);
    if (status != UMICOM_TREE_OK && status != UMICOM_TREE_NOT_FOUND) return status;
    for (UmicomU32 i = 1U; i < r->nodeCount; ++i) {
        char type[64];
        UmicomHardwareClear(type, sizeof(type));
        UmicomKernelHardwareDevice device;
        UmicomHardwareClear(&device, sizeof(device));
        status = UmicomHardwareText(r, i, "device_type", type, sizeof(type));
        if (status != UMICOM_TREE_OK) return status;
        status = UmicomHardwareCompatible(r, i, device.compatible, &device.kind);
        if (status != UMICOM_TREE_OK) return status;
        if (UmicomHardwareEqual(type, "memory")) device.kind = UMICOM_HARDWARE_MEMORY;
        else if (UmicomHardwareEqual(type, "cpu")) device.kind = UMICOM_HARDWARE_CPU;
        else if (r->nodes[i].parent == reserved) device.kind = UMICOM_HARDWARE_RESERVED_MEMORY;
        UmicomKernelTreeSpan span = {0};
        const UmicomBoolean hasReg = UmicomKernelDeviceTreeProperty(r, i, "reg", &span) == UMICOM_TREE_OK ? UMICOM_TRUE : UMICOM_FALSE;
        /* Inventory addressable unrecognised devices too. Non-addressable
         * organisational nodes stay in the reader but need no device record. */
        if (device.kind == UMICOM_HARDWARE_OTHER && !hasReg) continue;
        if (out->devices == UMICOM_HARDWARE_DEVICE_LIMIT) return UMICOM_TREE_LIMIT;
        status = UmicomKernelDeviceTreePath(r, i, device.path, sizeof(device.path));
        if (status != UMICOM_TREE_OK) return status;
        device.enabled = contexts[i].enabled;
        device.phandle = contexts[i].phandle;
        device.interruptParent = contexts[i].interruptParent;
        device.hasInterruptSpecifiers = UmicomKernelDeviceTreeProperty(r, i, "interrupts", &span) == UMICOM_TREE_OK ||
            UmicomKernelDeviceTreeProperty(r, i, "interrupts-extended", &span) == UMICOM_TREE_OK ? UMICOM_TRUE : UMICOM_FALSE;
        status = UmicomHardwareRegisters(r, contexts, i, &device);
        if (status != UMICOM_TREE_OK) return status;
        /* Copy the record rather than retaining a view of the parser scratch.
         * The report can outlive that index and needs no future DTB dereference. */
        volatile UmicomU8 *target = (volatile UmicomU8 *)&out->entries[out->devices++];
        const UmicomU8 *source = (const UmicomU8 *)&device;
        for (UmicomSize byte = 0U; byte < sizeof(device); ++byte) target[byte] = source[byte];
    }
    /* A duplicate enabled hart or overlapping advertised RAM would make later
     * resource admission ambiguous. Device-register overlap can be legitimate
     * (for example a parent syscon and its functions), so it is not forbidden. */
    for (UmicomU32 i = 0U; i < out->devices; ++i) {
        const UmicomKernelHardwareDevice *a = &out->entries[i];
        if (!a->enabled) continue;
        for (UmicomU32 j = 0U; j <= i; ++j) {
            const UmicomKernelHardwareDevice *b = &out->entries[j];
            if (!b->enabled || a->kind != b->kind) continue;
            if (i != j && a->kind == UMICOM_HARDWARE_CPU && a->registerCount && b->registerCount &&
                a->registers[0].busAddress == b->registers[0].busAddress) return UMICOM_TREE_DUPLICATE;
            if (a->kind != UMICOM_HARDWARE_MEMORY) continue;
            for (UmicomU32 x = 0U; x < a->registerCount; ++x)
                for (UmicomU32 y = 0U; y < b->registerCount; ++y) {
                    if (i == j && y >= x) continue;
                    const UmicomKernelHardwareRegister *ar = &a->registers[x], *br = &b->registers[y];
                    if (ar->translation == UMICOM_TREE_OK && br->translation == UMICOM_TREE_OK &&
                        ar->physicalAddress < br->physicalAddress + br->bytes &&
                        br->physicalAddress < ar->physicalAddress + ar->bytes) return UMICOM_TREE_BAD_VALUE;
                }
        }
    }
    out->nodes = r->nodeCount; out->properties = r->propertyCount;
    out->firmwareReservations = r->reservationCount;
    for (UmicomU32 i = 0U; i < r->reservationCount; ++i) out->reservations[i] = r->reservations[i];
    out->ready = UMICOM_TRUE;
    return UMICOM_TREE_OK;
}
UmicomKernelTreeStatus UmicomKernelHardwareCatalogueBuild(const UmicomKernelDeviceTreeReader *r,
    UmicomKernelHardwareCatalogue *out)
{
    if (!r || !out || r->self != r || !r->opened || !r->blob || !r->nodeCount ||
        r->nodeCount > UMICOM_TREE_NODE_LIMIT || r->propertyCount > UMICOM_TREE_PROPERTY_LIMIT ||
        r->reservationCount > UMICOM_TREE_RESERVATION_LIMIT) return UMICOM_TREE_INVALID_ARGUMENT;
    const UmicomUIntPtr target = (UmicomUIntPtr)out, index = (UmicomUIntPtr)r, blob = (UmicomUIntPtr)r->blob;
    if (r->bytes > UMICOM_TREE_MAX_BYTES || sizeof(*out) > ~(UmicomUIntPtr)0U - target ||
        sizeof(*r) > ~(UmicomUIntPtr)0U - index || r->bytes > ~(UmicomUIntPtr)0U - blob ||
        (target < index + sizeof(*r) && index < target + sizeof(*out)) ||
        (target < blob + r->bytes && blob < target + sizeof(*out))) return UMICOM_TREE_INVALID_ARGUMENT;
    UmicomHardwareClear(out, sizeof(*out));
    const UmicomKernelTreeStatus status = UmicomHardwareBuild(r, out);
    if (status != UMICOM_TREE_OK) UmicomHardwareClear(out, sizeof(*out));
    return status;
}
const char *UmicomKernelHardwareKindName(UmicomKernelHardwareKind kind)
{
    switch (kind) {
    case UMICOM_HARDWARE_MEMORY: return "memory";
    case UMICOM_HARDWARE_CPU: return "cpu";
    case UMICOM_HARDWARE_UART: return "serial";
    case UMICOM_HARDWARE_TIMER: return "machine-timer";
    case UMICOM_HARDWARE_INTERRUPT_CONTROLLER: return "interrupt-controller";
    case UMICOM_HARDWARE_VIRTIO_MMIO: return "virtio-mmio-slot";
    case UMICOM_HARDWARE_PCI_HOST: return "pci-host";
    case UMICOM_HARDWARE_RESERVED_MEMORY: return "reserved-memory";
    default: return "unclassified";
    }
}
