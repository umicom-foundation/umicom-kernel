# Message-channel delivery validation

## Source baseline and preservation

The source baseline is commit
`f3a0792e33aeb455a9ae0f569d693cae91328ed8` of
`umicom-foundation/umicom-kernel`.

The three replaced files were compared against their exact Git blob identities:

| File | Baseline blob | Added lines | Removed/rewritten lines |
|---|---|---:|---:|
| `CMakeLists.txt` | `13a4c93fa5ccf8e7c257d88e26d771616d30c224` | 4 | 0 |
| `kernel/main.c` | `b4801eef79a99271b5d4e7765d0176d4e8a0ebb0` | 7 | 0 |
| `kernel/user_monitor.c` | `d4ced85d56410104126f6f397f792f41466712d5` | 15 | 0 |

Every original line remains unchanged and in its original order. The source
and its existing educational commentary are not reformatted. There is no old
implementation to disable: the change adds new operations and keeps the
established operations active.

The fifteen new files contain the queue/service implementation, independent
program, native tests and two guides. No new abbreviated project identifier,
compatibility alias, Python script, PowerShell script, shell script, deleted
file or renamed file is part of the overlay. Existing historical material in
other repository files is untouched.

## Toolchain used here

Validation was performed on a Linux host with Clang/LLD 17.0.0, GCC 14,
CMake 3.31.6 and Ninja 1.12.1. CMake selects `-std=c2x` for this Clang release
when the project requests C23; the native GCC configuration uses its C23 mode.
No claim is made that a Windows compiler or Windows QEMU executed here.

The full cross-build retained the repository's existing strict warning flags,
including `-Werror`, and its RV64 freestanding configuration. Debug and Release
both linked. The Kernel entry remained `0x80200000`. The new separate program
has entry `0x00400000` and distinct RX, R and RW PT_LOAD segments. Its two-page
BSS is absent from the file bytes and was also accepted by the actual native
ELF loader test.

## Tests executed

| Check | Result |
|---|---|
| Complete Debug and Release RISC-V cross-builds | Passed |
| Repeated incremental build | Passed; Ninja reported no work |
| Source and build paths containing spaces | Passed |
| Native message suite with Clang ASan/UBSan | 72/72 passed |
| Native message suite with GCC ASan/UBSan | 72/72 passed |
| Existing executable-loader suite with sanitizers, including the new actual ELF | 68/68 passed |
| Existing process-registry suite with sanitizers and actual ELF loading | 49/49 passed |
| Existing user-boundary suite with sanitizers | 24/24 passed |
| CTest registration and dependency relationship | Eleven main tests registered; new test requires current-image fixture |
| Deliberately failed build in an isolated copy | Build fixture failed; dependent message test was not run |
| RISC-V guest message exchange | Not run locally: QEMU unavailable |

The new native suite compiles the actual message core, service adapter, C trap
dispatcher, physical allocator, Sv39 walker and checked-copy functions. It does
not replace queues or memory checks with mocks. Native tests call the C trap
dispatcher directly; they do not emulate ECALL, MRET, privilege checks in CPU
hardware or execution of the separate program.

The registry regression suite retains its existing native substitute for the
architecture run boundary. The ELF loader test loads and inspects actual bytes
without executing RISC-V instructions. Those limitations are important when
interpreting the passing native counts.

The 2,000-lifetime sweep preserves generations throughout the loop, using public
close operations before the next pair is created. Exhaustion and corruption
cases deliberately edit private fields to reach otherwise impractically large
counters; they are explicitly labelled fault injection. Ordinary cleanup does
not reset the domain or allocator to hide leaked ownership.

## Build-fixture failure check

A separate disposable source copy was built successfully. An intentional
compiler error was then inserted into that copy's message implementation.
CTest was asked to run only `kernel.riscv64.message_channels`.

It automatically scheduled `kernel.build.current`, which failed compilation.
The dependent message test was reported **Not Run**. This proves the dependency
behaviour, not guest execution. The configured emulator placeholder was never
executed. No fault injection appears in the delivered source.

## Required Windows runtime qualification

Run the normal configure/build/CTest sequence in `MESSAGE_CHANNELS.md`, then the
manual QEMU command. Eleven main tests should pass, and the final guest section
must contain:

```text
message.user-exchange=pass
message.original-trap-handler=pass
message.frame-accounting=restored
message.completed-cases=6
message-channels-test=pass
UMICOM_KERNEL_MESSAGE_CHANNELS_READY
UMICOM_KERNEL_END
```

The manual QEMU exit code must be zero. A successful compile or native suite is
not a substitute for that result. An unexpected diagnostic, hang or nonzero exit
must be resolved before this execution capability is considered qualified.
