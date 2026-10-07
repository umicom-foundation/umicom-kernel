# Umicom Kernel — Read-only VirtIO block transport and ownership

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## What storage capability is being added

The hardware catalogue describes possible devices. A block driver goes further:
it identifies an actual device, negotiates an implemented protocol, lends it
explicitly owned memory and checks the completion of real sector reads.

This implementation supports only modern VirtIO MMIO block devices configured
read-only. The public interface has no disk write, flush or discard operation.
It refuses a backend which does not advertise the read-only feature, even though
its own request builder can only generate reads. The supplied QEMU commands also
open the synthetic host file read-only. These are separate safeguards.

Read-only storage still requires writes to *device control registers* and shared
queue memory. Selecting features, announcing a queue and resetting a device do
not mean writing a disk sector. The only block request type constructed here is
READ. RAMFS is unchanged and is not mounted on this block device.

## Responsibilities

```text
checked firmware catalogue
            |
            v
fixed QEMU profile and MMIO-window qualification
            |
            v
one platform-owned transport domain
            |
            v
modern feature negotiation and generation-tagged lease
            |
            v
owned descriptor/queue frame + owned bounce frame
            |
            v
one synchronous read, with checked completion
            |
            v
explicit reset acknowledgement, scrub and release
```

`block_platform_policy.c` checks copied catalogue values without probing MMIO.
`platform/qemu-riscv64/block_device.c` supplies aligned MMIO access, ordering and
the existing clock. `virtio_block.c` owns the protocol and frame lifetime.
`block_console.c` provides inspection commands without becoming a filesystem.

No existing allocator, mapper, interrupt handler or execution path is replaced.
The platform adapter has the only domain for its physical windows; clients
borrow it instead of constructing independent owners of the same hardware.

## The supported machine is deliberately narrow

The policy accepts the established QEMU RISC-V `virt,aclint=off` configuration:

| Property | Required observation |
|---|---|
| CPU | One enabled CPU, hart zero |
| RAM | One translated span: 128 MiB at `0x80000000` |
| Clock | Device-tree timebase of 10,000,000 ticks per second |
| VirtIO | Enabled, ordinary translated 4 KiB MMIO windows |
| Window addresses | `0x10001000` through `0x10008000`, page aligned |
| DMA | Coherent, little-endian, guest physical addressing |
| Execution | Trusted serial Kernel calls, hart zero, Bare addressing, interrupts disabled |

The adapter also checks the actual allocator geometry. It reuses the existing
object-cache access gate, including refusal while managed critical-section or
timer ownership is active. It reads time; it never reprograms the timer.

The current allocator does not enforce arbitrary firmware reservations. Rather
than quietly lend possibly reserved pages to DMA, this adapter refuses a
catalogue with firmware reservation entries or enabled reserved-memory nodes.
Expanding boot reservation policy remains a prerequisite to wider board support.

A catalogue is evidence used for qualification, not an authority to touch any
address named by firmware. PCI BARs and user-provided MMIO addresses do not enter
this adapter. Duplicate or unqualified windows are rejected before any probe.

Slots are sorted by physical address. Their indices are stable for that captured
catalogue but are not permanent disk identities. Inspect `disks` rather than
assuming an attached drive occupies slot zero.

## Distinguish discovery from opening

`UmicomKernelBlockDomainInitialize()` acquires no page and performs no MMIO.
Its owner must begin fully zero-filled, remain at a stable address, and never be
copied or reinitialised.

`UmicomKernelBlockProbe()` reads identity words. It performs no register writes,
no feature negotiation and no reset. An empty device ID ends the probe before
any later identity register is read. Unsupported legacy transport is reported,
not activated through an unreviewed fallback.

`UmicomKernelBlockOpen()` is the first mutating device operation. A nonzero
existing status is refused without resetting another active driver. A claimed
slot is refused independently of that hardware observation. Successful opening
requires the modern transport and read-only features, accepted feature status,
a usable queue and a stable nonzero sector capacity.

Only the features implemented here are accepted. Packed queues, indirect
descriptors, event-index negotiation, multi-queue operation and IOMMU semantics
are not enabled simply because a device offers them.

On failure, Open normally resets and releases the new owner. If cleanup cannot
complete, the returned handle remains nonzero and MUST be retained for Close.
Initialise the caller's handle to zero before Open. Never overwrite a retained
handle merely because the operation that created it reported an error.

## Queue and DMA ownership

An open device owns two independently allocated 4 KiB frames:

| Frame | Contents |
|---|---|
| Queue frame | Eight descriptors, available ring, used ring, request header and status byte |
| Bounce frame | Up to eight complete 512-byte sectors of device-written data |

One request uses a fixed three-descriptor chain: request header, writable data,
then writable status. Neither an application address nor an arbitrary caller
buffer is lent to the device. This also separates response validation from
publishing bytes to the caller.

The frames need not be contiguous. Their addresses come from the existing
physical allocator. The read-only frame query checks that each is still
classified ALLOCATED before the owner dereferences it. This is not a global
allocation-generation proof; exclusive ownership of those frames remains a
Kernel contract.

The queue and payload are explicitly cleared before publication. RISC-V
`fence iorw, iorw` surrounds the real adapter's control accesses and the driver's
DMA publication/completion boundaries. Volatile accesses alone would not express
all the required ordering.

The available ring's interrupt-suppression flag is a hint, not IRQ ownership.
The implementation polls completion, acknowledges an observed queue interrupt,
and refuses configuration changes. It does not configure or claim a PLIC source.

## A read either publishes a complete request or leaves output unchanged

`UmicomKernelBlockRead()` accepts one through eight sectors, beginning at a
64-bit sector number. It validates the full request against the device capacity
using subtraction, avoiding overflow in an unchecked end-sector addition.

The output must have enough capacity and must not overlap the domain metadata
or any queue/bounce frame owned by that domain. The caller supplies trusted,
valid Kernel storage for the duration of this synchronous call.

The used-index transition must account for exactly one completion, including
16-bit wrap. The completion must refer to the submitted chain, carry a bounded
used length and contain a defined status. A successful result must account for
the complete payload and its status byte. Truncated success is refused rather
than presented as a complete read.

The driver rechecks status and configuration before exposing bytes. Only then
does it copy the bounce data into the caller's buffer. Every non-OK return leaves
that caller buffer unchanged. A valid device IO_ERROR or UNSUPPORTED completion
consumes the chain but exposes no data, allowing an explicit subsequent retry.

Malformed completion, unexpected queue progress, changed configuration or a
failed/timed-out device faults the lease and attempts reset. It is not silently
returned to READY. Explicit Close remains responsible for releasing memory.

## A timeout is not a DMA cancellation acknowledgement

Consider a device which accepts a buffer, fails to answer in time, and writes
that buffer later. Freeing the page at timeout could corrupt a different owner
which has since reused it.

The driver therefore distinguishes:

```text
caller stopped waiting
        !=
device confirmed that it stopped touching memory
```

The transport is reset and status zero must be observed before exposed pages
are scrubbed or freed. A reset timeout retains the handle, queue and bounce
frame. Even clearing a pending bounce buffer would be inappropriate while the
device could still access it.

The controller uses both a requested elapsed-time bound and a finite poll cap.
The cap prevents a stalled clock from causing an infinite loop; it can expire
before the nominal time interval. A backward time observation is also refused.
Neither elapsed time nor a poll count proves DMA has stopped.

After acknowledged reset, Close scrubs and releases the bounce frame and then
the queue frame. Each pointer is cleared only after its own release succeeds.
A later release failure leaves a smaller owner for retry. Successfully closed
handles cannot select a new generation of the same slot; generation exhaustion
retires the slot rather than wrapping.

This reasoning assumes a compliant trusted device. There is no IOMMU here to
stop a malicious bus master from writing outside its descriptor buffers or
ignoring reset. It is not a hostile-device sandbox.

## Console inspection

The old `hardware` command remains a copied inventory; it still performs no
MMIO probes. The new commands have a different, explicit purpose:

| Command | Effect |
|---|---|
| `disks` | Probe qualified identity registers; no reset, negotiation or allocation |
| `readsector SLOT LBA` | Open read-only, read one sector, print hex/safe ASCII, close |
| `blockclose` | Retry a previously retained close |

Both numeric arguments are unsigned decimal values. `probe=ok device-id=2`
identifies a modern block slot; read-only feature enforcement occurs at Open.
A writable backend returns `writable-device-refused` when opening is attempted.

The sector dump escapes nonprintable data through its fixed hex/ASCII format.
Disk bytes cannot directly issue terminal controls. A failed close is stored
in static console ownership, and a subsequent read retries it before attempting
another open. Poweroff also checks this retained ownership before dismantling
the console. If reset is still pending, cleanup remains incomplete instead of
pretending the DMA pages are safe to free.

No `mount`, partition parser, disk filesystem, writable block operation or user
block syscall is provided. The RAM-backed shell files still disappear at reset.

## Synthetic disk fixture and safe manual use

Use only the supplied synthetic test file for this qualification. Do not attach
a physical disk or a file containing valuable data.

`tests/virtio_block/fixture.raw` contains 128 sectors (65,536 bytes), with a
readable first-sector label and a deterministic byte formula documented in
`fixture_format.h`. It has no filesystem and contains no copied host data.
Its SHA-256 is:

```text
E5BEA1290B3BE59BDAF9F3A99D0BE7527BAEFB3822B7D4B4E1C6C9736A4EFA46
```

CMake verifies that hash and copies the fixture to:

```text
build/riscv64-clang-debug/fixtures/umicom-read-only.raw
```

No disk-generation script or additional compiler is needed. Spaces in source
and build paths are supported. A comma in the build path is explicitly refused
because it conflicts with QEMU's drive-option separator.

Normal incremental commands remain:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

To inspect the fixture from the normal system:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
    -display none `
    -monitor none `
    -chardev "stdio,id=console,signal=off" `
    -serial "chardev:console" `
    -global virtio-mmio.force-legacy=false `
    -drive "file=./build/riscv64-clang-debug/fixtures/umicom-read-only.raw,if=none,format=raw,id=umicom_read_test,readonly=on" `
    -device "virtio-blk-device,drive=umicom_read_test" `
    -m 128M `
    -smp 1 `
    -no-reboot
```

Select the slot reported by `disks`, not a guessed identity. For example, when
the report identifies slot 7 as the block device:

```text
disks
readsector 7 0
readsector 7 127
readsector 7 128
mem
poweroff
```

The first two reads must succeed. Sector 128 is outside this 128-sector fixture
and must return `range` without issuing an out-of-bounds read. The first sector's
ASCII column begins with the synthetic Umicom Kernel label. Successful reads
print `block.read=ok close=ok`; the range refusal prints
`block.read=range close=ok`.

The explicit modern-transport selection is required. Removing it may produce
`unsupported-transport`; removing `readonly=on` makes Open refuse the backend.
Independent recovery still skips the allocator and this driver.

## Protocol references and implementation limits

The external protocol consulted is the OASIS VirtIO specification: device reset
and feature negotiation, modern MMIO registers, split queues, and block-device
sector/request semantics. In particular, acknowledged reset is the protocol
boundary after which the device must stop queue access. QEMU's command-line
reference documents raw drive format and read-only attachment; its MMIO source
provides the explicit modern/legacy selection property.

```text
https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html
https://www.qemu.org/docs/master/system/qemu-manpage.html
https://www.qemu.org/docs/master/system/riscv/virt.html
https://gitlab.com/qemu-project/qemu/-/blob/master/hw/virtio/virtio-mmio.c
```

The linked VirtIO 1.3 document identifies itself as Committee Specification
Draft 01. Only the reviewed modern split-ring/block subset is implemented;
this is not a claim to implement every feature in that document.

The bounds and refusal policies above are Umicom implementation choices.
No PCI transport, legacy fallback, async I/O, multi-queue, interrupts-driven
completion, hotplug, live resize, SMP locking, noncoherent cache maintenance or
IOMMU mapping is included. Partition and filesystem readers are subsequent work;
persistent writing requires separate safety and recovery qualification.
