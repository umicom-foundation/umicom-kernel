# Umicom Kernel — Events and wait-any validation

## Source and comparison baseline

The reviewed Kernel commit is
`299a273353cf545e3af70cb8fc4346033bcdffee` from
`umicom-foundation/umicom-kernel`.

This overlay changes only `CMakeLists.txt` and `kernel/main.c`.
The first receives four inserted lines; the second receives seven.
Every existing line remains identical and in the original order. There are no
removed or rewritten lines, no file renames and no disabled replacement of an
existing implementation. All event, test and planning files are new.

The exact Git blob IDs of the two integration inputs are:

| Input | Git blob |
|---|---|
| `CMakeLists.txt` | `6908a3c49737b017a80a6f619824c33569789427` |
| `kernel/main.c` | `11e2ca4b1cf3b3104941d1768d0584872b950194` |

All local build/source/test subtrees were independently checked against the
commit's Git tree hashes: `arch`, `cmake`, `include`, `kernel`, `platform`,
`programs` and `tests`. The CMake preset blob was checked separately. This
comparison is stronger than assuming a historical delivery ZIP still matches
the files the project owner actually committed.

The old `docs/ROADMAP.md` is not replaced. The new planning document is
`docs/KERNEL_AND_OS_RELEASE_ROADMAP.md`, with an HTML reading copy beside it.

## Tools and actual execution

The source-delivery environment is Linux x86-64, using Clang/LLD 17, GCC 14.2,
CMake 3.31.6 and Ninja. It is not the Windows UCRT64 installation used by the
project owner. Clang 17 accepts the requested C23 standard through its `c2x`
spelling; the repository retains `CMAKE_C_STANDARD 23`.

The compiler options remain strict. No warning was disabled to make this
delivery pass.

| Check | Actual result |
|---|---|
| Complete freestanding Debug build | Passed |
| Complete freestanding Release build | Passed |
| Repeated incremental build | Passed; Ninja reported no work |
| Final ZIP applied to a separate source copy with spaces in its path | Passed complete cross-build |
| ELF metadata | RISC-V 64-bit, entry `0x80200000`; no undefined symbols |
| Event suite, Clang Debug with UBSan | 48 of 48 passed |
| Event suite, Clang Release with UBSan | 48 of 48 passed |
| Event suite, GCC Debug with UBSan | 48 of 48 passed |
| Existing thread suite with UBSan | 55 of 55 passed |
| Existing message-channel suite with UBSan | 72 of 72 passed |
| Existing process-registry suite with UBSan | 49 of 49 passed |
| Existing executable suite with UBSan | 68 of 68 passed |
| Existing user-memory suite with UBSan | 24 of 24 passed |
| Isolated deliberately failed build-fixture test | Setup failed; dependent event test was not run |
| Real RISC-V QEMU execution | Not run: emulator unavailable locally |
| Windows UCRT64 / Clang 22 execution | Not run locally; project-owner qualification required |

The complete rebuild includes the separately linked diagnostic and message
programs and their embedded carriers. A successful cross-build does not prove
that a guest privilege transition or hardware timer executed.

## What the new native suite really runs

The suite compiles the actual `kernel/events.c` and `kernel/threads.c`. It uses
the existing test-only Linux x86-64 context adapter, so callbacks really suspend
and resume on separate stacks. This is not an RV64 boot or an x86 Kernel port.

There are 47 policy cases plus the guest C acceptance sequence run under an
explicit host hardware model. Those 48 tests cover:

- domain admission, stable address, reinitialisation and unsafe-machine refusal;
- invalid arguments, modes, booleans, tokens and capacity;
- remembered manual and auto-reset state, binary-credit coalescing and FIFO;
- wait-any input validation, selection ordering and completed-result retention;
- spurious wakeups, deadline boundaries, clock ordering and maximum time;
- close, token reuse, cancelled/reaped waiters and awarded-credit cancellation;
- registration-ticket exhaustion and deliberately corrupted private state;
- separate domains, all waiter slots and 2,000 successive event/thread lifetimes.

The acceptance model runs the same new C validation routine as the guest.
Its scheduler stacks, events and message queues are real implementations.
Its CSR, timer and physical-accounting observations are modelled and therefore
must not be cited as hardware evidence.

The acceptance model completed seven cases and 913 checks, including 32 copied
messages coordinated by remembered events.

## Sanitiser boundaries

UndefinedBehaviorSanitizer was enabled with non-recovering diagnostics for the
native results reported above.

AddressSanitizer is not claimed for the alternate-stack event or thread suites:
the existing test adapter does not supply ASan fibre-switch notifications.

An ASan run was also attempted for the non-stack-switching regression suites.
The runtime could not reserve its shadow memory under this environment's hard
virtual-address-space limit and aborted before test execution. That is an
environment limitation, not a passing source result. Those suites were rerun
successfully with UBSan only. This delivery makes no ASan-pass claim.

## The current-image fixture

A separate, disposable copy of the source received an intentional `#error`.
CTest then ran `kernel.build.current` and failed to compile that copy.
`kernel.riscv64.events` was reported **Not Run** because its setup fixture
failed.

For this registration-only experiment, `/usr/bin/false` stood in for emulator
discovery. It was not launched and did not emulate a machine. Neither the
intentional error nor the stand-in is part of the delivered files.

With a real QEMU available, the main configuration now registers thirteen tests:
the current-image setup and the twelve cumulative capability tests. The new
capability is `kernel.riscv64.events`. Earlier test definitions are unchanged.

## Required project-owner qualification

Use the ordinary incremental configure/build/test sequence. Do not delete the
build directory:

```powershell
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The manual QEMU command remains:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot
```

Successful guest validation must reach:

```text
events.messages-received=32
events.message-coordination=pass
events.original-trap-handler=pass
events.machine-state=unchanged
events.frame-accounting=unchanged
events.completed-cases=7
kernel-events-test=pass
UMICOM_KERNEL_EVENTS_READY
UMICOM_KERNEL_END
```

The exact check count is diagnostic rather than a public ABI. The static
scheduler stacks increase the protected Kernel image size; no particular
absolute Kernel-end address or reserved-frame count is promised.

Until that guest run succeeds, the event implementation is cross-built and
host-tested, not locally QEMU-qualified. The roadmap's OS release gates remain
future requirements rather than claims made by this validation report.
