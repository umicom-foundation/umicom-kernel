# Umicom Kernel — Checked device-tree discovery and hardware inventory

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Why discovery comes before a storage driver

The working platform adapter knows where the selected QEMU machine places RAM,
its serial port and its machine timer. Those facts are appropriate for a fixed
qualification profile. A new driver must also establish what hardware has been
advertised, which bus owns its register addresses, and which parts of that
information the Kernel actually understands.

This implementation reads the firmware-supplied flattened device tree and
publishes a copied inventory. It does not read a peripheral register, initialise
a driver, configure interrupts, enable DMA, or issue a disk command. A later
VirtIO transport owner will use discovery as input, then validate an attached
device and negotiate its features independently.

The existing bounded managed-service demonstration and its lifetime budgets are
unchanged. This work progresses the hardware/storage part of the release roadmap;
it does not silently claim that the service demonstration has become an
indefinitely running production daemon manager.

## Three ownership boundaries

```text
Existing platform RAM bounds and minimum DTB extent inspector
                              |
                              v
Checked DTB reader: immutable borrowed bytes, bounded node/property index
                              |
                              v
Hardware catalogue: copied names, values and translated address observations
                              |
                              v
Read-only hardware command; later driver admission remains a separate decision
```

`UmicomKernelDeviceTreeInspect()` remains unchanged. It establishes that the
minimum header and then the declared complete blob fit within the selected RAM
profile. It does not establish that an arbitrary caller-supplied pointer is safe
on an unrelated machine.

`UmicomKernelDeviceTreeOpen()` indexes that bounded byte buffer. The caller must
keep both its buffer and reader at stable, non-overlapping addresses and keep
the bytes immutable while the index is used. It is a borrowed view, not a
relocated tree. Invalid pointer or overlapping-owner arguments are refused
before writing the owner. Once valid separate storage is supplied, a parsing
failure clears the index instead of leaving a partially usable tree.

`UmicomKernelHardwareCatalogueBuild()` copies supported observations into a
separate owner. A semantic failure or capacity overflow clears that catalogue.
A completed catalogue no longer borrows strings, properties or pointers from the
DTB. Input/output overlap is rejected before either source owner is changed.

The boot adapter captures once and clears its temporary reader afterwards. The
`hardware` command reads the copied catalogue, not a newly supplied physical
address. It neither reparses the source nor replaces the original boot evidence.
These are trusted, single-hart Kernel interfaces; arbitrary mutation of the
validated reader or output metadata violates their contracts.

## The byte reader

The reader decodes big-endian values byte by byte. It does not cast untrusted
bytes to a native structure and does not dereference addresses encoded inside
a reservation entry or property. An unaligned host buffer can therefore be
used in native parser tests, even though firmware should provide properly
aligned DTB storage.

It accepts the complete header encoding compatible with DTB binary format 17,
including a later format declaring compatibility with 17. It does not support
the shorter format-16 header. Those are external protocol formats, not Umicom
release names.

Checks cover the total accessible extent; header, reservation, structure and
string-block separation; token alignment and zero padding; reservation-table
termination and non-overlap; bounded property values and names; one closed root;
properties before child nodes; duplicate sibling/property names; and an exact
final END token. A missing END is not inferred from reaching a size limit.

The reader has explicit storage and work limits:

| Resource | Limit |
|---|---:|
| Complete blob | 128 KiB |
| Indexed nodes | 128 |
| Indexed properties | 512 |
| Nested nodes, including root | 32 |
| Firmware reservation entries | 32 |
| Stored unit-name length | 95 bytes |
| Property-name length | 63 bytes |
| Generated absolute path, including terminator | 256 bytes |

The name bounds and lexical checks are implementation limits, not a complete
Devicetree binding-schema validator. Device-specific naming, unit-address
consistency and every unknown property's semantics are not validated here.
The caller receives a refusal rather than a truncated tree when a limit is hit.

Queries match complete names. Path lookup requires the exact absolute name,
including a unit address when present; it does not perform alias lookup or
omit an unambiguous unit-address suffix. Property string helpers validate the
complete supplied value. A valid first compatible string cannot conceal an
unterminated later entry. String-table suffix references remain valid: an offset
may point at a terminated suffix rather than a separately stored full name.

## Turning bus addresses into physical observations

A `reg` value describes an address in its immediate parent's bus space. Its
address and size widths come from that parent. Each node's own `#address-cells`
and `#size-cells` describe its children; those values are not inherited from an
ancestor. The reader applies the specified defaults of two address cells and
one size cell where the parent omits those properties.

The ordinary translation walker supports one- and two-cell addresses and sizes:

```text
Child register span
       |
       v
Current bus ranges: child address -> parent address
       |
       v
Parent bus ranges, repeated until the root physical address space
```

An empty `ranges` property explicitly permits identity translation. An absent
property does not. In that case the result remains `untranslated` and no physical
address is published. A whole register span must fit one translation window;
matching only its first byte is not enough. Overlapping child windows, malformed
tuples, arithmetic overflow and narrowing outside a one-cell address space are
refused.

Unsupported encodings, including three-cell PCI child-address flags, remain
visibly `unsupported-cells`. The generic PCI host's own parent-encoded register
span can still be recorded; this is not PCI device enumeration. Zero-size
addressable device resources are not invented into usable MMIO windows.

CPU `reg` values are hart identifiers, not physical RAM addresses. The catalogue
handles that binding separately and prints `hart-id`, never a fabricated CPU
register address. Duplicate enabled hart identities and overlapping translated
memory declarations are refused as ambiguous input. Device register spans can
legitimately overlap, so a broad ban on all device overlap is not imposed.

## What the catalogue knows

It classifies memory and CPU nodes, the advertised 16550-compatible serial
interface, supported CLINT/ACLINT timer names, recognised interrupt-controller
names, VirtIO MMIO transport slots, a generic PCI host and reserved-memory
children. Unknown addressable nodes remain `unclassified` instead of being
silently omitted. Organisational nodes remain in the reader but need not occupy
a device record.

The catalogue holds up to 64 records and four register spans per record. An
ordinary bus can describe up to 32 translation windows. Printable model and
first-compatible strings use 96-byte storage including the terminator. The first
compatible string is copied for display, while classification checks the entire
validated list. A recognised fallback string can therefore classify the node
even when the first string is more specific.

The root model, literal `/chosen/stdout-path`, `/cpus/timebase-frequency`, firmware
reservation entries and each device's enabled observation are also copied. An
absent frequency is represented by zero, not a guessed clock rate. A chosen
value such as `serial0:115200n8` is reported literally; it does not retarget the
existing console. A disabled ancestor suppresses a descendant in this inventory's
enabled classification.

Declared `phandle` values must be nonzero, not all-ones and unique. A declared
legacy `linux,phandle` must agree with its canonical counterpart. Explicitly
inherited `interrupt-parent` references are checked for a matching declared
handle, including forward references.

**This is not interrupt-domain resolution.** Implicit interrupt parents,
interrupt nexuses, `interrupt-map`, `interrupts-extended` and controller-specific
interrupt cells are not decoded. The report prints `irq-route=not-decoded` when
interrupt specifiers are present. A raw reference is not an IRQ registration or
a claim that a device can safely interrupt the Kernel. DMA translation and
IOMMU configuration are likewise not implemented.

## Reservations are observations, not allocator changes

The existing fixed profile still selects 128 MiB of RAM starting at `0x80000000`
and reserves the regions already owned by its startup code. The new catalogue
**does not add firmware reservation entries or `/reserved-memory` nodes to the
allocator, alter its RAM bounds, or migrate the existing drivers to DTB-derived
addresses**.

That distinction is a release blocker for generic hardware support. Before
supporting arbitrary firmware or using its advertised additional RAM, startup
must validate and apply all required reservations before any allocation. Printing
a reservation is not evidence that it has been protected. This delivery stays
within the existing QEMU qualification profile; do not change `-m`, `-smp`, the
machine type, or provide another DTB and assume the rest of the Kernel adapts.

A `virtio,mmio` entry means an advertised transport slot. It does not prove that
a disk, network device, filesystem or any other peripheral is attached there.
Only a subsequent device driver can read a negotiated, checked transport and
establish its actual device identity. No such register access happens here.

## Boot and recovery

Diagnostic startup captures after installing its machine trap vector and before
the allocation tests. Its later hardware acceptance check compares the copied
inventory with the exact existing QEMU test profile. That check requires a
complete catalogue, matching RAM, hart zero, the expected serial/timer spans and
at least one advertised VirtIO transport slot.

Normal startup captures after the established boot-memory reservation step and
before its native boot jobs. Inventory failure remains visible as an error from
`hardware`; it does not silently rewrite device policy or newly prevent the
working fixed-profile shell from starting. Independent recovery takes its
existing earlier branch and does not execute the parser or gain the hardware
command. Its allocator-independent recovery contract remains unchanged.

The `hardware` command emits only copied printable strings and integer values.
It consumes no allocator frames and performs no peripheral I/O beyond writing
text through the shell's existing output callback. The parser/catalogue workspaces
are static Kernel storage and therefore do increase the reserved image size.

## Build, test and try it

Use the existing PowerShell workflow:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The additional main test is `kernel.riscv64.hardware_discovery`. It requires the
existing current-image fixture and runs the automatically terminating diagnostic
image. It does not launch an indefinitely waiting shell as a test.

Run `bin\umicom-system.elf` with the existing QEMU command and unchanged options.
At `umicom>` enter:

```text
hardware
services
poweroff
```

A successful inventory begins with `hardware.catalogue=ok`. Paths, node counts,
reservation counts and optional devices can vary with the QEMU version. Expected
profile observations include RAM at `0x80000000`, hart zero, the UART at
`0x10000000` and CLINT timer block at `0x02000000`. An untranslated or unsupported
entry must not be interpreted as a zero-address peripheral.

The actual decoder can also be tested natively without QEMU. On a host with a
working native Clang sanitizer runtime:

```powershell
cmake -S .\tests\hardware_catalogue -B .\build\native-hardware -G Ninja -DCMAKE_C_COMPILER=clang -DUMICOM_HARDWARE_SANITIZERS=ON
cmake --build .\build\native-hardware --parallel 2
ctest --test-dir .\build\native-hardware --output-on-failure --no-tests=error
```

These optional host tests do not replace RISC-V guest execution. They use a
constructed DTB fixture, not a captured emulator boot or a physical-board dump.

## Primary references and implementation boundary

The format and property semantics were checked against these primary sources:

- Devicetree Specification, flattened binary format:
  https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html
- Devicetree Specification, standard properties and bus addressing:
  https://devicetree-specification.readthedocs.io/en/stable/devicetree-basics.html
- QEMU RISC-V virt machine documentation:
  https://www.qemu.org/docs/master/system/riscv/virt.html

The code is an original bounded C implementation. It does not import libfdt or
copy another distribution's driver. The references describe the external format;
the admission limits, immutable index, copied catalogue and reporting policy are
Umicom design choices. They are not claims of full DTSpec conformance or universal
hardware support.

The next storage work is transport/device ownership and a narrowly qualified
read-only VirtIO block path, before writes or filesystem persistence are enabled.
