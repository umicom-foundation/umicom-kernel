# Umicom Kernel — File-service validation evidence

Author: Sammy Hegab, Umicom Foundation. MIT licence.

## Baseline and scope of the comparison

The source baseline is commit
`1c3c4aa547592378bc88065827b73e39634054c2` of
`umicom-foundation/umicom-kernel`, containing the typed VFS and RAM-backed files.

The build-relevant `arch`, `cmake`, `include`, `kernel`, `platform`, `programs`
and `tests` directory trees used for validation match their GitHub Git tree
identities at that commit. The root build files also match. This is a source
identity statement, not evidence that every program has executed in a guest.

| Existing file | Inserted lines | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| include/umicom/kernel/user_scheduler.h | 3 | 0 |
| kernel/main.c | 7 | 0 |
| kernel/user_monitor.c | 12 | 0 |
| kernel/user_scheduler.c | 62 | 0 |
| Total | 88 | 0 |

Every original line remains in its original order. The scheduler declaration
adds an optional owner pointer; the monitor recognises FILE after its established
checks; the scheduler binds, completes and closes client lifetimes. No existing
implementation is superseded, so no historical code block is added. In particular,
there is no change to VFS, RAMFS, either allocator, the user-copy functions, the
supervisor or any Assembly file.

## Toolchain and execution boundary

Validation uses Linux x86-64, Clang/LLD 17, GCC 14.2, CMake 3.31.6 and Ninja 1.12.1.
The RISC-V target is the existing `riscv64-unknown-elf` toolchain and lp64 integer
ABI. Clang 17 uses its C23 draft-standard flag selected by CMake. Warnings remain
errors. No new warning is suppressed and no hosted C runtime is linked into the
Kernel or its independent program.

RISC-V QEMU is not installed in this validation environment. The actual
machine-mode/user-mode transitions and the new guest acceptance sequence were
**not executed here**. The Windows compiler previously reported by the owner is
newer than this local cross-compiler. No Windows Clang pass is claimed.

## Cross-build checks

The full Debug and Release source builds produce the Kernel, separate nested-fault
image and independent file-client ELF. The Kernel entry remains `0x80200000`.
The file client entry is `0x00400000`, with separate RX text, read-only data and
non-executable writable/BSS pages. It contains an 8 KiB aligned user buffer for
boundary-crossing I/O.

Repeating each completed build reports no work to do. A separate baseline copy,
with spaces in its source and build paths, is used to check the packaged overlay.
The generated carrier depends on the program's actual ELF file; the nested-fault
target also depends on the new executable producer. These are build dependency
checks, not a proof of a running guest.

The normal Windows incremental build directory need not be deleted.

## New native suite

The new suite registers **54 cases**. It passes under:

| Configuration | Result | Instrumentation |
|---|---:|---|
| Clang Debug | 54/54 | AddressSanitizer and UndefinedBehaviorSanitizer |
| Clang Release | 54/54 | AddressSanitizer and UndefinedBehaviorSanitizer |
| GCC Debug | 54/54 | AddressSanitizer and UndefinedBehaviorSanitizer |

It links the actual file lifecycle owner, copied-request service, scheduler,
architecture admission/restoration C adapter, syscall dispatcher, IPC, VFS, RAMFS,
object cache, ELF loader, page walker, physical allocator and checked-copy helpers.

Only privileged entry, machine observations and deliberately injected failures
are models. The allocation gate returns false while the host model is inside the
trap dispatcher. An accepted file operation must therefore be deferred before
it can allocate or call the filesystem. This test would fail if it legitimised
filesystem allocation simply by pretending the trap had a normal machine context.

The suite includes:

* Exact session-pointer binding, no-grant refusal, rights ceilings and client-local
  descriptor domains. A copied session with the same numeric identity is refused.
* Copied request/path/WRITE data, including changing the original bytes after
  trap capture but before completion. Inputs and outputs cross real modelled
  page-table boundaries.
* Invalid request/result sizes and addresses; rechecked result mappings; missing
  second source/destination pages; oversized requests; overlapping data/result
  spans; embedded path terminators and reserved-field refusal.
* Shared duplicate positions, independent opens, reduced-rights duplicates,
  fixed-width metadata, directory CHANGED/rewind behaviour, append mode and
  shrink/grow zero filling.
* A partial write with one allocatable page: six accepted bytes are reported
  together with NO_MEMORY, and the file length reflects that prefix.
* Exit, fault, cancellation, timer pause, slice exhaustion and a last-slice OPEN.
  Descriptor ownership survives a pause but ends before safe image collection.
* Cancellation of an IPC-blocked file owner and an IPC wait on the final slice.
  Neither branch may leave descriptor cleanup dependent on another user call.
* Backend unpin failure, retained cleanup state and successful retry. Reaping
  cannot bypass a failing descriptor close.
* Client-capacity and allocation refusal during Grant, followed by a successful
  retry without losing the original task.
* 1,000 successive task/client lifetimes. A replacement without a grant cannot
  inherit the previous descriptor namespace.
* Refused machine entry without spending a slice, and unverified machine return
  which retains both the process and file ownership rather than claiming safe
  reclamation.

The last containment test deliberately does not report successful cleanup. It
uses a static host arena; leaving poisoned Kernel ownership in that arena is
not evidence of a production recovery mechanism.

## Existing native regressions

All fourteen existing test projects pass against the updated source:

| Project | Result |
|---|---:|
| User memory and original monitor | 24/24 |
| User scheduling | 53/53 |
| Blocking IPC | 49/49 |
| Message channels | 72/72 |
| Process registry | 49/49 |
| Process supervision | 54/54 |
| Executable loading | 68/68 |
| VFS/RAMFS | 66/66 |
| Object cache | 65/65 |
| Mapped regions | 59/59 |
| Trap integrity | 70/70 |
| Interrupt ownership | 68/68 |
| Cooperative threads | 55/55 |
| Events | 48/48 |

AddressSanitizer and UndefinedBehaviorSanitizer are enabled where supported by
the existing project. Alternate-stack thread/event tests use UndefinedBehaviorSanitizer
only. The interrupt suite retains its existing split between policy and
alternate-stack instrumentation; no ASan fibre instrumentation is claimed.

The optional loader, registry and VFS bridge cases receive the actual separately
cross-built file-client ELF. They inspect/copy its bytes and reclaim its memory;
that is not execution of the RISC-V instructions in a native process.

## Runtime test registration and stale-image protection

The new main test is `kernel.riscv64.file_services`. It requires
`kernel_current_image`, supplied by `kernel.build.current`. The main suite now
contains 23 tests on a machine where QEMU is found.

A deliberately failing source is introduced only into an isolated validation
copy after its old ELF has been built. Running the file-service CTest selection
must fail the build fixture and mark the dependent runtime test Not Run. The
old ELF hash must remain unchanged. No failing source or fake emulator is part
of the delivery.

The unavailable-emulator path used to inspect this test graph is not run as a
substitute for QEMU, and no output marker is fabricated.

## Required guest acceptance on the development machine

Run the normal cross-build, all CTests and the existing manual QEMU command.
After the preceding VFS results, the new guest must print:

```text
file-services-test=begin
file-services.case=independent-writer-and-reader
file-services.user-file-operations=pass
file-services.exit-closes-descriptors=pass
file-services.case=fault-closes-descriptors
file-services.case=budget-closes-descriptors
file-services.case=cancel-and-reuse
file-services.case=ungranted-replacement
file-services.machine-state=restored
file-services.frame-accounting=restored
file-services.completed-cases=5
file-services.completed-checks=<observed count>
file-services-test=pass
UMICOM_KERNEL_FILE_SERVICES_READY
UMICOM_KERNEL_END
```

QEMU must exit with zero. The supervisor-driven writer/reader prove actual
ECALL continuation and access to separately loaded user buffers. The fault and
CPU-bound cases must produce the correct real terminal classifications. Before
the cancellation test proceeds, it must observe a completed successful OPEN.

A source commit, native test count or successful ELF link cannot replace this
execution evidence. Absolute image addresses and reserved-frame counts may grow
because the additional static owner metadata and validation buffers are part of
the protected Kernel image.
