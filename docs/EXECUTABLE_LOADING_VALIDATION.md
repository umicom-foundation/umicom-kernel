# Umicom Kernel — executable-loading validation record

## Source baseline

Repository: `umicom-foundation/umicom-kernel`.

Baseline commit:

```text
3b8ae5041dfeab02c675bd84c19d40afc76b301b
feat(kernel): add isolated user execution and checked system calls
```

The two existing files changed by this delivery were checked against these Git
blob identities before editing:

```text
CMakeLists.txt  7aa5a84cb0d85c9496659048b7b71a6e7f3c7b0c
kernel/main.c  bc626c0ecdf23451945d202bdfd1f82db8430265
```

No original line in either file is deleted, rewritten or reordered. CMake gets
four added lines and `main.c` gets seven. The full-file delivery contains those
two files and sixteen new files. Existing platform, trap, MMU, supervisor, user
monitor, user-memory and allocator sources remain unchanged.

No source file is deleted or renamed. No existing implementation is superseded,
so there is no new disabled historical block. No new abbreviated project API or
compatibility alias is introduced. No Python, PowerShell or shell script is
part of this delivery. The `.S.in` file is an Assembly input template and the
`.cmake` files are native build descriptions.

## Actual validation environment

The validation host is Linux, using Clang 17.0.0, LLD 17.0.0, CMake 3.31.6 and
Ninja 1.12.1. The freestanding target is `riscv64-unknown-elf`, integer LP64.
CMake requests C23 and selects Clang 17's `-std=c2x` spelling. The existing
strict warning options, including `-Werror`, remain enabled.

This is not a claim that Windows Clang 22.1.8 was run locally. The user's existing
Windows configuration is retained without changing its compiler selection or
preset.

## Completed checks

| Check | Observed result |
|---|---|
| Complete Debug cross-build, including the independent diagnostic ELF | Passed |
| Complete Release cross-build | Passed |
| Repeated incremental Debug build | Passed; Ninja reported no work to do |
| Separate source/build paths containing spaces | Passed |
| Program-to-carrier-to-Kernel dependency rebuild | Passed |
| Main test registration with all original test names retained | Nine tests |
| Deliberately broken current source in an isolated fixture test | Build failed; dependent executable-loading test was not run |
| Native loader suite under AddressSanitizer and UndefinedBehaviorSanitizer | 68 of 68 passed |
| Existing native user-boundary suite under the same sanitizers | 24 of 24 passed |
| Inspect/load the actual separately linked Release ELF in the native harness | Passed |
| Existing-line preservation comparison | Eleven insertions, zero removals or rewrites |
| Kernel ELF entry | `0x80200000` |
| Independent diagnostic ELF entry | `0x00400000` |
| RISC-V QEMU execution of the new loader path | Not run on the validation host |

The source-plus-ZIP reconstruction is also built separately before the delivery
is finalised. It checks that the packaged files, rather than only a development
working directory, reproduce the complete cross-build.

## Native test detail

The native suite compiles the actual parser, process-image owner, address
helpers, physical allocator, Sv39 mapper and checked-user-memory code. A
page-aligned host array supplies RAM. It is not a mock result generator and
never executes bytes from an executable input.

The 67 standard test names cover format/header/range/alignment/permission/entry
refusals, correct copy and zero fill, independent image ownership, source-buffer
independence, page rights, guard pages, owner-state checks and frame scrubbing.
Supplying the actual diagnostic ELF adds the sixty-eighth test. The real-file
case compares loaded bytes and zero-filled padding against its program headers.

The allocation-failure sweep runs 21 memory budgets, leaving zero through twenty
frames genuinely available in the existing allocator. Both successful loads and
out-of-memory refusals occur. Every attempt is checked against the appropriate
allocator snapshot; failed loads do not leave a partial owner.

The malformed-input sweep performs 4,000 deterministic header mutations. Inputs
which the parser accepts are materialised and destroyed, but are never executed.
Every iteration checks final allocator accounting. This finite mutation sweep is
useful regression evidence, not exhaustive proof of parser correctness.

The owner-state test manually marks an owner RUNNING, and separately marks it
non-quiescent, to check reclamation refusal. That is an API policy test, not
proof of hardware execution or concurrency handling.

## Build dependency check

In a separate working copy, a diagnostic-program initialiser was changed.
Building `umicom-kernel` then performed exactly the expected chain:

```text
compile diagnostic source
link programs/umicom-diagnostic.elf
reassemble generated/embedded_executable.S
link bin/umicom-kernel.elf
```

This catches the common mistake of adding only a target-order dependency and
then accidentally retaining stale embedded program bytes after an edit.

## Failed-build fixture check

A deliberate `#error` was inserted in a separate source copy. CTest was invoked
for only `kernel.riscv64.executable_loading`; it automatically included the
current-image fixture. Compilation failed and the dependent runtime test was
reported Not Run.

For registration and this negative orchestration check only, the isolated
configuration used a non-emulator command in the QEMU cache variable. It was
never executed because the fixture failed. No guest pass was inferred from that
configuration. Normal configurations still discover the user's real QEMU.

## What still needs the user's QEMU run

No `qemu-system-riscv64` executable is available on the validation host. Therefore
there is no locally observed user-mode execution result for the new loaded ELF.

The normal acceptance run must prove all five loaded-program cases, including
actual system calls, the read-only store fault, timer termination, restoration
of machine state, return to the original trap handler and final frame accounting.
All earlier Kernel tests must continue to pass as well.

Expected final guest markers are:

```text
executable.original-trap-handler=pass
executable.frame-accounting=restored
executable.completed-cases=5
executable-loading-test=pass
UMICOM_KERNEL_EXECUTABLE_LOADING_READY
UMICOM_KERNEL_END
```

A compile, ELF inspection or native host test is not substituted for that
runtime evidence. This delivery also makes no claim of physical hardware
qualification, arbitrary ELF compatibility, a filesystem, a scheduler or a
production-grade process sandbox.
