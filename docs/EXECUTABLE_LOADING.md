# Umicom Kernel — executable loading and process-image lifetime

## What changes for an application developer

Until now, the user execution tests called a payload linked into the Kernel's
own executable. That proved privilege transitions and the system-call boundary,
but it did not prove that the Kernel could take a separate program file and
turn it into an owned process image.

The diagnostic program in `programs/diagnostic` now has its own entry point,
linker layout and ELF file:

```text
programs/diagnostic/main.c + entry.S
                    |
             separate RV64 link
                    |
           umicom-diagnostic.elf
                    |
        checked executable-file parser
                    |
      freshly allocated physical frames
                    |
      private Sv39 root and user stack
                    |
       existing user execution monitor
```

The file is carried inside the boot image as read-only bytes using Assembly
`.incbin`. This is a delivery mechanism until a boot archive or filesystem can
supply files; it is not a call into the Kernel-linked diagnostic function. The
loader receives a pointer and byte count and has no dependency on that carrier.
The diagnostic's virtual entry is `0x00400000`, whereas the Kernel still starts
at `0x80200000`.

No existing allocator, mapper, trap handler, supervisor experiment, user monitor
or system-call implementation is replaced. The only existing-source changes are
an additional CMake include and a documented validation call from `main.c`.

## Three responsibilities, kept separate

`kernel/executable.c` inspects a byte span without allocating frames. It decodes
little-endian fields explicitly rather than casting an unaligned file buffer to
packed C structures. Every field read follows a range check. A rejected input
does not publish a partial plan through the caller's output object.

`kernel/process_image.c` owns the resources needed by a successfully inspected
program. It allocates page tables and backing frames, copies file contents,
creates the stack and rolls back an incomplete load. It contains no privileged
instructions.

`arch/riscv64/process_execution.c` connects the ready image to the existing
RISC-V user monitor. It checks entry conditions, synchronises new instructions,
sets a bounded deadline, and checks the machine state after the monitor returns.
An exit or user fault leaves the image allocated for inspection. Destruction is
an explicit, separate operation.

## The supported ELF profile

This is a deliberately bounded native executable profile, not a general-purpose
Linux or dynamic ELF loader. Accepted files are ELF64, little endian, RISC-V,
fixed-address `ET_EXEC`, with System V identification and the integer calling
convention. The RVC flag is permitted; floating-register, RVE, TSO and unknown
ABI flag combinations are refused. ELF's standard format fields are file-format
requirements, not Umicom development-version labels.

The parser bounds the input to one MiB, at most sixteen program headers, eight
loadable segments and fourteen image pages. Two further pages belong to the
user stack, fitting the existing sixteen-entry backing-page catalogue. Page-table
frames are separately owned by the established address-space object;
they do not consume entries in the user data-page catalogue.

The following restrictions are intentional:

- Load segments must be ordered, readable, non-empty and disjoint at page
  granularity. Even two byte-disjoint segments sharing one physical page are
  refused rather than silently widening that page's rights.
- No mapping may be writable and executable. Code uses RXU, read-only data RU,
  and mutable data/stack RWU. The initial letter U here means the architectural
  user-access permission, not an abbreviated project identifier.
- File ranges, virtual ranges and alignment must fit without arithmetic wrap.
  Only the lower canonical Sv39 region is admitted, with the first page absent.
- The entry must be correctly aligned and lie in file-backed executable bytes,
  not in a zero-filled tail. The existing monitor also checks a four-byte
  executable span at entry before admitting an invocation.
- Segment pages must not overlap the reserved stack or either guard page.
- Interpreter, dynamic, TLS and unknown program-header requirements are
  rejected. The non-executable GNU stack declaration is accepted without
  granting it authority over stack size. Bounded notes and RISC-V attributes
  remain inert metadata, not an instruction-set validator.

A section-header table is not required or used for loading. `p_paddr` is ignored:
the file never chooses a physical frame. No relocation, dynamic symbol binding,
argument-vector convention, auxiliary vector, libc startup or POSIX process ABI
is implemented here. Other RV64 binaries must use the supported profile and the
existing Umicom native system-call convention; being an ELF file is not enough.

## File bytes, memory bytes and zero-filled storage

For a load segment, `p_filesz` describes bytes present in the file. `p_memsz`
describes the requested memory extent. A larger memory extent requires zeroes in
the part not backed by file bytes. This is how an executable describes BSS.

The loader goes further: it clears every byte of each newly allocated frame
before copying the segment's initialised portion. That includes leading
alignment padding and the final partial page. A new process cannot inherit
bytes left by a previous owner merely because those bytes sit outside a named
variable.

The sample has RX instructions, a read-only `Umicom Kernel` label, an initialised
writable counter and a 4,097-byte zero-initialised array that crosses a page
boundary. Its entry code does not clear BSS. A successful run therefore requires
the loader itself to have copied and cleared the correct bytes.

## Ownership and rollback

Start with a zero-initialised `UmicomKernelProcess` at a stable, Kernel-owned
address. Do not copy it after creation: its checked-user-memory view points into
its own page catalogue. Keep the input span readable and immutable throughout
creation, and do not overlap that span, the owner or its output parameters.
These are trusted Kernel caller contracts, not operations on unchecked user
pointers. After successful creation, the caller may release the original file
buffer: the process owns independent copies of the required bytes.

The order for each page is:

```text
allocate physical frame
        |
record ownership immediately
        |
clear the complete frame
        |
copy the intersection with file-backed bytes
        |
map with final user permissions
```

Recording ownership before mapping matters. A failed map can then return the
frame instead of losing the only record that it was allocated.

Ordinary load failure tears down the partial image and returns the owner to
EMPTY. A cleanup failure leaves CLEANUP_REQUIRED and retains the remaining
records. It does not report success while abandoning resources.

Destruction first removes the page-table hierarchy, then scrubs and releases
backing pages. It pops a page record only after a successful free. The operation
refuses a RUNNING owner or one whose `quiesced` flag is false. That flag is set
only when execution has not begun, or after the architecture adapter has proved
that the machine context was restored. A failed restoration must not allow page
reclamation while the hardware might still refer to the image.

The current allocator/mapper assume trustworthy Kernel-owned metadata. This
loader does not make their structures safe against arbitrary Kernel corruption.
A failed low-level teardown remains a recovery problem to diagnose, not a case
where the loader can safely guess which frames may be freed.

## The lifecycle

```text
EMPTY -> LOADING -> READY -> RUNNING -> EXITED
                              |        |----> FAULTED
                              |        |----> TIMED_OUT
                              |        |----> CALL_LIMIT
                              |        |----> MONITOR_ERROR
                              |
                 explicit destruction -> EMPTY
```

Only READY may run. A terminal image cannot be started again by calling Run a
second time. Destroy it and create a fresh image instead. READY may be destroyed
without ever entering user mode. Double creation and double destruction are
refused.

Identity is supplied by the trusted caller and is not taken from the executable
file. There is not yet a central identity allocator or process table. The owner
is not a copyable handle, a scheduler task or a concurrent object.

## Execution, protection and restoration

The run adapter reuses the existing `UmicomRiscvUserExecute` monitor and the
existing IDENTITY, COPY and EXIT services. It does not add another trap or syscall
implementation. The monitor remains in machine mode while the application runs
in user mode under Sv39.

Machine-mode stores have just populated executable frames. `FENCE.I` establishes
instruction visibility for the same hart before those bytes are executed. A
page-table fence alone does not serve this purpose. The new Assembly helper is
built with an explicit Zifencei requirement; no existing compile flags change.
This single-hart protocol is not remote instruction-cache synchronisation.

The adapter borrows the timer deadline and the monitor borrows the established
machine control registers. On return, the adapter checks the saved CSR fields,
counter-enable registers and timer compare value. It does not release the image
if restoration cannot be proved. Deadlines are bounded in timer ticks and
terminate an invocation; they do not time-slice between runnable processes.

A code fault or read-only violation is a process result. A restoration failure
is a Kernel monitor error. Keeping those outcomes distinct prevents a damaged
machine context from being mistaken for an ordinary application crash.

## Runtime validation cases

The new validation is cumulative: all prior Kernel checks run first.

Two owners load the same embedded ELF with the same virtual entry, but with
different roots and entirely separate backing frames. The diagnostic calls
IDENTITY and COPY, confirms its data/BSS state, and exits with a value computed
from its assigned identity, argument and mutable counter.

| Case | Expected result |
|---|---|
| loaded-program-a | identity 501, argument 7, exit 541 |
| loaded-program-b | identity 502, argument 19, exit 554 |
| loaded-read-only-refusal | a store to the executable's R-only segment traps |
| loaded-program-deadline | a loop without calls is stopped by the machine timer |
| reload-after-fault-and-deadline | fresh identity 505, argument 7, exit 545 |

The failure cases still execute IDENTITY and COPY before attempting the fault
or endless loop. The validation checks their copied-byte evidence as well as
terminal privilege and cause. It also refuses overwriting a live owner, refuses
rerunning a terminal owner, checks the original machine trap handler afterwards,
and compares final physical-memory accounting with the starting snapshot.

Success ends with:

```text
executable.original-trap-handler=pass
executable.frame-accounting=restored
executable.completed-cases=5
executable-loading-test=pass
UMICOM_KERNEL_EXECUTABLE_LOADING_READY
```

These expected guest results require a real QEMU run. The separate native tests
inspect and load the program bytes but never execute RISC-V instructions.

## Build and recurring workflow

Use the existing configured Windows environment and persistent build directory:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Do not continue to runtime qualification after a failed build. The new test
`kernel.riscv64.executable_loading` also requires the established current-image
fixture. There are now nine main CTests including that build fixture.

The separately linked program is visible here:

```text
build/riscv64-clang-debug/programs/umicom-diagnostic.elf
```

CMake gives the generated `.incbin` object a file dependency on that ELF as well
as a target-order dependency. Editing the application therefore rebuilds its
ELF, reassembles the byte carrier and relinks the Kernel. No binary conversion
script is required. This integration targets the existing single-configuration
Ninja workflow; no multi-configuration-generator qualification is claimed.

Run the complete image with the existing machine settings:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none `
    -monitor none `
    -serial stdio `
    -m 128M `
    -smp 1 `
    -no-reboot
```

## Optional native loader tests

This suite builds the real parser, process-image owner, physical allocator,
page walker and checked user-memory code for the host. A page-aligned array
stands in for managed RAM. It does not replace these implementations with mock
allocators, and it does not claim to test privilege transitions.

After the cross-build has produced the sample ELF, use a separate native build
folder, not the RISC-V build folder:

```powershell
cmake -S tests/executable -B build/native-executable -G Ninja `
    -DCMAKE_C_COMPILER=clang `
    "-DUMICOM_EXECUTABLE_SAMPLE=C:/umicom/umicom-kernel/build/riscv64-clang-debug/programs/umicom-diagnostic.elf"
cmake --build build/native-executable --parallel 2
ctest --test-dir build/native-executable --output-on-failure --no-tests=error
```

There are 67 native tests without a sample file, or 68 with it. Address and
undefined-behaviour sanitizers can be enabled using
`-DUMICOM_EXECUTABLE_SANITIZERS=ON` on a host toolchain providing those runtimes.
They were exercised on the Linux validation host; that is not a claim that every
Windows Clang installation already has the corresponding runtimes.

## Boundaries to keep visible

This adds a reusable bounded loader and an explicit process-image owner. It
does not yet add a filesystem, independent on-disk application installation,
package signatures, general ELF compatibility, dynamic linking, POSIX, a
scheduler, IPC, SMP or a persistent supervisor-mode Kernel. The separate
executable bytes are still supplied by the boot image. Those limits are deliberate
and leave the existing working privilege-transition implementation intact.

## Primary references

- ELF generic ABI, program headers and loading:
  https://gabi.xinuos.com/elf/07-pheader.html
- ELF generic ABI, executable header:
  https://gabi.xinuos.com/elf/02-eheader.html
- RISC-V ELF processor ABI:
  https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-elf.adoc
- RISC-V instruction-fetch synchronisation:
  https://docs.riscv.org/reference/isa/v20260120/unpriv/zifencei.html
- CMake generated-object file dependencies:
  https://cmake.org/cmake/help/latest/prop_sf/OBJECT_DEPENDS.html
- CTest fixture dependencies:
  https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html

The limits and refusal policies above are Umicom design choices. The references
explain the underlying formats and hardware rules; they do not imply that the
whole ELF or operating-system interface has been implemented.
