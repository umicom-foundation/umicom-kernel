# Umicom Kernel — Interrupt ownership validation record

## Source basis and preservation

Prepared against `umicom-foundation/umicom-kernel` commit
`09d555f3b99fb615b66458cfe1332ec32a0b0925` (machine trap hardening).
The four replacement files were checked against these Git blob identities:

| File | Baseline Git blob | Added lines | Removed/rewritten lines |
|---|---|---:|---:|
| `CMakeLists.txt` | `082bafd1c17f2a0972b69be326982d382775688f` | 4 | 0 |
| `kernel/main.c` | `9917a02f321bcc26c7ee7e1d4b1470ce61fd79b7` | 7 | 0 |
| `arch/riscv64/process_execution.c` | `6ca483c0b1823c00b9834fb91248789a35064222` | 11 | 0 |
| `arch/riscv64/thread_context.S` | `98c1c511239827da607c46abba81c68c4c47811b` | 29 | 0 |

Every baseline line remains unchanged, in order. Nothing is superseded, deleted,
renamed or moved into a disabled block. The fourteen other files are new.
No existing platform, trap, allocator, scheduler C implementation, event, channel,
loader or registry source is included as an unnecessary replacement.

## Tools actually used

- Linux x86-64 build host.
- Clang 17.0.0 and LLD 17.0.0 for RV64 cross-compilation and Clang native tests.
- GCC 14.2.0 for the independent native compiler check.
- CMake 3.31.6 and Ninja.
- Freestanding C23 selection, RV64 `lp64` ABI and the existing strict warnings,
  including `-Werror`. No warning was disabled for the new implementation.

The available compiler is **not** the Windows Clang 22.1.8 previously reported
by the project owner. No claim is made that this environment reproduces every
newer compiler diagnostic. Outputs are passed through writable parameters, and
local status/token variables have defined starting values where needed.

## Builds

The complete Kernel, separately linked programs and nested-fault image were
cross-compiled in Debug and Release. Both images retain entry `0x80200000`.
A repeated build reports `ninja: no work to do`.

The finished delivery is also applied over a separate baseline source copy in
paths containing spaces and rebuilt. This checks the delivered full files, not
only the editing workspace. The target commands contain the source-local
`UMICOM_KERNEL_INTERRUPT_OWNERSHIP` definition for both relevant RV64 adapters
in both Kernel images.

No native-test control symbols are present in the production Kernel ELF. The
token-limit and internal-depth observation seams are compiled only into the
isolated host tests.

## New native checks: 68 tests in each configuration

| Configuration | Policy cases | Alternate-stack boundary cases | Outcome |
|---|---:|---:|---|
| Clang Debug | 64, ASan and UBSan | 4, UBSan only | 68 passed |
| Clang Release | 64, ASan and UBSan | 4, UBSan only | 68 passed |
| GCC Debug | 64, ASan and UBSan | 4, UBSan only | 68 passed |

The policy executable links the actual `kernel/interrupts.c` and
`platform/qemu-riscv64/interrupt_ownership.c`. Only register state and the timer
compare read are modelled. Tests cover:

- Initialisation, foreign vector/hart/translation and invalid inputs.
- Initially enabled/disabled delivery, nested restoration, owner and LIFO checks,
  depth limits, unchanged failed outputs and stale tokens.
- Restoring MIE without restoring a changed unrelated status field.
- Detected raw MIE/source/delegation/vector/scratch/translation changes while a
  section is open; these retain ownership records and leave delivery masked.
- Exclusive timer leases, source validation, enabled/pending/future-deadline
  refusal, source-specific updates and hardware readback refusal.
- Explicit global delivery permissions and refusal inside a live scope.
- State publication before modelled immediate delivery on the outer unlock.
- Ten thousand load/release lifetimes and ten thousand deterministic nesting
  decisions checked against an independent expected-depth model.
- No wrap at token exhaustion.

The four boundary tests execute the real cooperative C scheduler and existing
host alternate-stack adapter. Linker wrappers supply the ownership checks added
to the RV64 adapter; these wrappers do **not** validate those Assembly branches.
Yield/Wait/Sleep and dispatcher entry are refused while appropriate, resumption
works after release, and abandoned completion is checked in a child process for
one exact stop code rather than accepting an arbitrary crash.

## Existing native regressions

| Suite | Passed |
|---|---:|
| Trap integrity | 70/70 |
| Cooperative threads | 55/55 |
| Events | 48/48 |
| Message channels | 72/72 |
| Process registry, including actual linked diagnostic ELF loading | 49/49 |
| Executable loader, including actual linked diagnostic ELF loading | 68/68 |
| User memory | 24/24 |

Clang Debug runs use each suite's sanitizer option. The thread/event alternate
stack suites use UBSan only. Other listed suites use ASan and UBSan. The native
ELF checks inspect/load bytes; they do not run RISC-V instructions. Existing
native adapters keep their documented substitution boundaries.

## Build-fixture negative check

An isolated verification source copy is first built successfully so it contains
a real, previously linked ELF. A deliberate compile error is then inserted only
into that disposable copy. Selecting the new runtime test must add the existing
build fixture, which fails, and report the dependent runtime test as Not Run.

A known failing host executable is used only as an emulator-path sentinel for
CTest registration in that isolated check. It is not an emulator and produces
no passing guest evidence. It is never configured or included in the delivery.

## Runtime evidence still required

RISC-V QEMU is not installed in this environment; obtaining the package was also
unavailable. Therefore none of the new pending-timer, RV64 context-admission or
loaded-program retry sequences is claimed as locally executed.

The owner must still run the Windows build and normal QEMU/CTest sequence. The
new test requires a real pending machine timer inside nested sections, exactly
one original timer-handler invocation after the outer leave, refused thread and
process entry while ownership is live, a real loaded program after release,
and restored machine controls/frame accounting.

Passing native counts cannot replace these observations. Physical hardware,
NMI behaviour, SMP, general ISR routing, pre-emptive scheduling and timing at
platform counter wrap are outside this delivery's qualification.
