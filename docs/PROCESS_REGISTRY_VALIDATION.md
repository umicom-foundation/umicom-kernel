# Umicom Kernel — Process registry validation record

## Source baseline and preservation

The delivery starts from repository commit:

```text
44dcb0cbe59fe69b364ccef73c9b12b8f6a87b5b
```

That commit contains executable loading and process lifetimes. This metadata
records the source input; it is not a runtime version or an API identifier.

The two modified source inputs were checked against their Git blob identities:

| Path | Baseline blob |
|---|---|
| CMakeLists.txt | eae2ce678f9713b9c1e716fe161e7facc3fd1130 |
| kernel/main.c | ea0c7be6d5a8c4fcd851530700f6ad92bdfb6506 |

Existing-file changes are four added CMake lines and seven added main.c lines.
Every original line remains in its original order. No comment, implementation,
function, file or source name is removed or rewritten. No prior logic is
superseded, so there is no new disabled implementation block.

All registry implementation, integration, tests and teaching material are new
files. The existing platform, trap, MMU, supervisor, user monitor, executable
parser, process-image owner, and physical/virtual-memory implementations are
unchanged by this delivery.

## Build environment used here

The delivery environment is Linux x86-64 with Clang/LLD 17.0.0 and CMake 3.31.6.
The project still requests C23. CMake selects Clang 17's `-std=c2x` spelling;
it selects the compiler-appropriate spelling on the user's newer Clang toolchain.
No warning suppression, source-standard downgrade or replacement toolchain file
was added to the delivery.

Debug and Release complete freestanding RISC-V cross-builds passed with the
existing strict warning policy, including `-Werror`. The Kernel ELF remains
64-bit little-endian RISC-V with entry `0x80200000`. The separately linked
`umicom-diagnostic.elf` remains part of the normal dependency graph.

A repeated incremental build reported `ninja: no work to do.` A separate fresh
validation copy was reconstructed from the finished delivery and the baseline,
then configured and built independently. That was a delivery qualification
check, not an instruction to delete the user's incremental build directory.

## Native evidence

The following suites passed with AddressSanitizer and UndefinedBehaviorSanitizer:

| Suite | Passing tests | Scope |
|---|---:|---|
| New process registry | 49 | 48 named cases plus the real linked diagnostic ELF as a loading input. |
| Existing executable loader | 68 | Original parser, copy/zero-fill, lifetime and malformed-input regressions. |
| Existing user boundary | 24 | Original checked memory-copy and dispatcher regressions. |

The registry suite includes:

- null/invalid admission, copied-owner and reinitialisation refusal;
- owner binding, stale handles and slot reuse;
- query-only, duplicate, grant and irreversible restriction rules;
- separate checks for missing DUPLICATE, missing TRANSFER and rights amplification;
- final-reference teardown, page scrubbing and shared owner cleanup;
- object, per-owner handle and global handle capacity;
- generation retirement and process-identity exhaustion through test-only counter injection;
- execution argument routing, refused entry, unsafe return and recursive access refusal;
- actual destructor refusal after test-only page-table count corruption, followed by repair/retry;
- partial owner cleanup and an injected unpublished-cleanup record;
- 21 available-frame budgets to exercise ordinary load rollback;
- 2,000 successive real load/close lifetimes with stale-token checks.

Native tests run the production registry, parser, loader, allocator, page walker
and user-memory checks. Only `UmicomKernelProcessRun` is substituted because an
x86-64 host cannot execute the RISC-V privilege transition. The substitution is
explicit in the test file and validates call routing and reentry policy only.

The real diagnostic-file case validates loading and ownership, not execution of
its RISC-V instructions. The unpublished-cleanup test injects the retained record
shape; it does not claim to reproduce every possible failure during load rollback.
The tests are bounded regression evidence, not exhaustive security verification.

## Build fixture evidence

A separate throwaway source/build pair first produced a valid ELF. A deliberate
compile error was then added only to that throwaway copy. Running the selected
registry test caused `kernel.build.current` to fail and the dependent test to be
reported **Not Run**. The old ELF remained present, but was not accepted as an
execution result for the changed source.

Because QEMU is unavailable here, an inert failing executable path was used only
to allow CTest registration in this fixture test. It was never executed as a
successful emulator and provided no guest evidence. Neither the placeholder nor
the deliberate compile error is included in the source delivery.

## Guest execution not performed here

`qemu-system-riscv64` is not installed in this delivery environment. Consequently
no claim is made that the six new guest cases have already passed on QEMU or
physical hardware. The user must run the ten-test main CTest suite and the normal
manual QEMU command before committing this update as runtime-qualified.

The expected guest marker is:

```text
UMICOM_KERNEL_PROCESS_REGISTRY_READY
```

It is printed only after all six cases, restoration of the original trap path,
and final physical-frame accounting succeed. No static host fixture prints it.
