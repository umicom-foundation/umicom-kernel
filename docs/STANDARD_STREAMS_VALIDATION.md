# Umicom Kernel — Standard-stream validation evidence

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Baseline and source preservation

The baseline is `cfc903150062282b863403ce1ed6809d0140ab36` in
`umicom-foundation/umicom-kernel`, containing the interactive console update.
The reconstructed build-relevant arch, cmake, include, kernel, platform, programs
and tests directories match their complete Git tree identities at that commit.
The root build files also match. This statement concerns source identity, not
whether every hardware path has executed.

| Existing file | Added lines | Removed or rewritten lines |
|---|---:|---:|
| CMakeLists.txt | 4 | 0 |
| include/umicom/kernel/console_shell.h | 2 | 0 |
| include/umicom/kernel/user_scheduler.h | 2 | 0 |
| kernel/console_runtime.c | 20 | 0 |
| kernel/console_shell.c | 51 | 0 |
| kernel/main.c | 6 | 0 |
| kernel/user_monitor.c | 11 | 0 |
| kernel/user_scheduler.c | 62 | 0 |
| Total | 158 | 0 |

Each original line remains in order. The new hooks leave unbound schedulers and
unattached shells on their existing paths. In particular, the previous
foreground-input discard code remains as the unattached-shell fallback; it is
not replaced with an empty compatibility wrapper. No existing implementation
is superseded or disabled. No platform or existing Assembly file changes.

## Tools and important limitations

The local environment is Linux x86-64 with Clang/LLD 17.0.0, GCC 14.2.0,
CMake 3.31.6 and Ninja 1.12.1. The existing cross-toolchain selects
`riscv64-unknown-elf`, the integer lp64 ABI and its established ISA flags.
CMake chooses Clang 17's C23 draft spelling. Strict warnings remain errors.

**RISC-V QEMU is not installed here.** Native tests below model privileged entry
and machine observations. The QEMU acceptance sequence and physical serial
input/output were not executed locally. The owner's Windows Clang 22 toolchain
is newer; no Windows compiler pass is claimed.

The normal Kernel, independent nested-fault image and interactive console all
cross-compile in Debug and Release. Each includes the new services, while only
the interactive target enters the indefinitely waiting prompt. The separate
stream program is compiled and linked independently with entry `0x00400000`.
Its executable, read-only and writable/BSS segments have separate permissions;
its aligned private buffer crosses actual virtual-page boundaries in the guest.
The Kernel entry remains `0x80200000`.

A second completed build reports no work to do. The complete-file ZIP is applied
to a separate baseline copy with spaces in source and build paths and rebuilt.
Its native stream suite is also rebuilt from those packaged source files.

## Native standard-stream suite

The new suite has 67 named cases. It compiles the actual scheduler, slice
admission adapter, stream service, C trap dispatcher, checked-copy functions,
loader, allocator, VFS/RAMFS, file services, IPC and supervision. The terminal
cases compile the actual input editor, command engine and terminal adapter.

The architecture entry is an explicit model that supplies user ECALL and timer
observations. Immediate syscall replies are followed by a model timer; waiting
calls return their retained frame. These tests check values and lifetime policy,
not RISC-V register-save instructions. Terminal input is injected through the
real editor, not read from a physical UART. Its output goes to a bounded native
capture buffer, not a live terminal.

| Configuration | Result |
|---|---:|
| Clang Debug with AddressSanitizer and UndefinedBehaviorSanitizer | 67/67 |
| Clang Release with AddressSanitizer and UndefinedBehaviorSanitizer | 67/67 |
| GCC Debug with AddressSanitizer and UndefinedBehaviorSanitizer | 67/67 |

Coverage includes:

- Zero-filled owner admission, repeated/late attachment, copied owner and stale
  or late grants. Exact session identity is checked against a forged copy.
- Zero, excessive and overflowing requests, wrong directions and unknown flags.
- Full 256-byte cross-page reads and writes using the unchanged small-copy
  helper. Invalid second pages preserve input and distinctly seeded destination
  bytes; the tests do not use equal bytes that would conceal a partial write.
- Copied input lifetime, input-ring wrap, full-line admission refusal, ordered
  stdout/stderr writes and zeroed unused packet tails.
- Empty/full nonblocking calls, blocking input/output, source changes after a
  suspended write, EOF after buffered input, destination revalidation, repeated
  pumping and no user dispatch/call-budget charge while a request remains pending.
- Exit-output retention, explicit discard, input/output cancellation, stale slot
  reuse, final-slice waiting, cumulative call budgets and refused entry retry.
- Corrupt counts/selectors and unverified machine return. These deliberately
  unsafe cases retain their images and do not claim successful forced cleanup.
- 1,000 successive task/stream lifetimes and 1,200 mixed variable-length
  input/read/write/drain iterations across another 100 task lifetimes.
- Canonical terminal lines, CR/LF, EOF, partial EOF, editing, overlong/damaged
  input, full input-ring refusal, escaped controls, cancellation, shutdown and
  starting a fresh foreground invocation without inherited input state.

The allocation-context gate is false during a simulated trap. This is important:
the stream path succeeds with preallocated queues and checked copies; it is not
made to pass by granting trap-time filesystem allocation or terminal callbacks.

During test development, two initial harness expectations were corrected: a
forged session must not increment the real session's call counter, and the
terminal's documented hexadecimal escaping is lowercase. Neither adjustment
changes a production refusal into a success. All final cases pass.

## Existing independent regression projects

The existing native projects retain their own compile-time feature selections.
Their passing counts do not imply that every project independently exercises the
new stream branch. The new suite above links the combined file/IPC/stream hooks.

| Existing suite | Passed tests |
|---|---:|
| Blocking IPC | 49 |
| Console shell and memory-model UART adapter | 65 |
| Events | 48 |
| Executable loader, including the actual new stream ELF as byte input | 68 |
| File services | 54 |
| Interrupt ownership | 68 |
| Mapped regions | 59 |
| Message channels | 72 |
| Object caches | 65 |
| Process registry, including real linked ELF loading | 49 |
| Process supervision | 54 |
| RAMFS/VFS, including diagnostic ELF byte loading | 66 |
| Cooperative threads | 55 |
| Trap integrity | 70 |
| User memory | 24 |
| User scheduling | 53 |

Address/undefined sanitizers are enabled where those existing projects support
them. Alternate-stack thread/event tests and the interrupt suite's alternate-
stack integration retain their existing UBSan-only configuration; ASan fibre
support is not claimed. These are host tests, not a guest boot transcript.

## Real-guest checks supplied with the update

`kernel.riscv64.standard_streams` runs the normal automatically terminating image.
Its acceptance function has six cases:

1. The separately linked input program blocks, spends no more user dispatches
   while input is absent, receives a page-crossing line, writes both stdout and
   stderr and returns with its original nested locals intact.
2. Twelve alternating output calls exceed the eight-record queue. The program
   suspends, resumes and finishes with the original accepted order intact.
3. EOF is distinct from empty-live input; wrong directions, bad buffers and
   nonblocking refusal are checked by the separately executed program.
4. Accepted output survives a real deliberate fault, timer budget exhaustion
   and ordinary exit. Reaping refuses until that output is drained.
5. A blocked input can be cancelled. Its replacement has no grant; the old
   generation cannot feed the new program.
6. The actual shell/editor/terminal route a line into a separately loaded
   foreground program, display its output and return to the prompt.

The final acceptance checks compare machine controls, the parked timer and
physical-frame accounting. These checks are compiled and supplied, **not reported
as locally executed**. Run the normal Windows build, CTest suite and interactive
console to qualify them.

Expected completion markers are:

```text
standard-streams.input-resume-without-spin=pass
standard-streams.stdout-stderr-order=preserved
standard-streams.terminal-output=retained-until-drained
standard-streams.machine-state=restored
standard-streams.frame-accounting=restored
standard-streams.completed-cases=6
standard-streams-test=pass
UMICOM_KERNEL_STANDARD_STREAMS_READY
UMICOM_KERNEL_END
```

The interactive target instead remains at the prompt after diagnostics and
prints its final completion marker only on orderly poweroff. Protected Kernel
size and reserved-frame counts grow with the additional static stream storage;
those absolute addresses/counts are not fixed acceptance values.

## Build-fixture qualification

The existing current-image fixture is retained. A separate validation copy is
configured with an explicit inert executable path solely to inspect generated
CTest registration when QEMU is absent. That path is not an emulator and is
never used as evidence of guest execution.

After successful linking, a deliberate C compilation failure is introduced in
that isolated copy. Running only the new dependent test selects the existing
build fixture. The fixture must fail, the standard-stream test must be Not Run,
and the previously linked Kernel/console/nested-fault ELF hashes must remain
unchanged. The injected defect is removed and the same directory rebuilds.

This validates fixture dependency and stale-image refusal, not emulator behaviour.
The regular configure path still reports that QEMU is absent and does not invent
passing runtime tests.

## Source and binary publication boundary

The delivery contains complete source files and guides only. No local build
output, compiler, executable ELF, Python/PowerShell/shell script or host-model
binary is delivered. The review records the actual package hash and source
comparison. The new implementation makes no claim of pipes, redirection, POSIX
stdio compatibility, structured argv, an authenticated terminal or persistence.
