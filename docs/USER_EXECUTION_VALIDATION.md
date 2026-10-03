# Umicom Kernel — User execution delivery evidence

## Source baseline

This update was prepared against the pushed commit:

```text
72d0ad439835a1aa7c2dfda954497a5026d255fe
feat(kernel): validate supervisor execution and protection
```

The owner's Windows transcript for that commit reports seven passing CTests and
all five supervisor execution cases completing successfully. That is baseline
evidence supplied by the owner; it is not a run of the new user-mode code.

The local build copy was checked against the Git blob identifiers for all 35
source, header, linker and configuration files needed by this cross-build.
It was not reconstructed from the text of the earlier conversation.

## Existing-source preservation

| Previously tracked file | Lines added | Lines removed or rewritten |
|---|---:|---:|
| CMakeLists.txt | 32 | 0 |
| arch/riscv64/linker.ld | 9 | 0 |
| kernel/main.c | 8 | 0 |
| include/umicom/kernel/platform.h | 5 | 0 |
| platform/qemu-riscv64/timer.c | 9 | 0 |

Every original line in those files remains in its original order, including
comments and blank lines. The comparison uses Git's committed file bytes with
LF line endings. No existing implementation is superseded or disabled here.
The old trap, MMU and supervisor execution sources are unchanged.

The platform addition is only a timer-compare query. It uses the existing
register-address helper rather than creating a second CLINT address definition.
The linker addition gives user executable pages a separate bounded range. The
startup addition runs the new validation after the existing supervisor checks.

New source uses the full Umicom prefix. The update does not introduce shortened
aliases, batch-number identifiers, or a new scripting dependency. Existing
historical source is left exactly as committed rather than cleaned up again.

## Checks performed before delivery

The validation host is Linux, using Clang/LLD 17.0.0, CMake 3.31.6 and Ninja
1.12.1. CMake's C23 setting selects Clang's `c2x` spelling on that compiler.
This is not a claim to have executed the Windows Clang toolchain locally.

| Check | Observed result |
|---|---|
| Complete Debug RISC-V cross-build | Pass |
| Complete Release RISC-V cross-build | Pass |
| Repeated incremental Debug build | Pass; Ninja reports no work to do |
| ELF architecture and entry | RV64, little-endian, entry 0x80200000 |
| User payload instruction range | Separate page-aligned range in Debug and Release |
| Shared C/Assembly offsets | Compile-time assertions pass for every saved register and CSR |
| Existing-line preservation | 63 additions; zero removals or rewrites |
| Native C user-boundary suite | 24 of 24 pass with AddressSanitizer and UndefinedBehaviorSanitizer |
| Main CTest registration | Eight entries, including the existing current-image fixture |
| Intentional build failure in an isolated copy | Build fixture fails; dependent user execution test is not run |
| New user-mode QEMU execution | Not run on the delivery host; QEMU is unavailable |

The registration/fixture checks used an explicitly failing executable as a
placeholder in an isolated validation copy, not an emulator. That executable
was never used to claim a guest pass. An intentional compile error made the
fixture fail before the runtime command could be started. The placeholder,
intentional error, generated build directories and internal preparation tools
are not in the delivery.

## What the native tests prove

The separate `tests/user_memory` project compiles the real address helpers,
physical allocator, software page walker, new user-memory layer and new C
monitor. It uses aligned host memory as the frame backing store. No RISC-V
Assembly or hardware privilege transition is executed by those tests.

The 24 cases cover cross-page reads and writes, read-only and supervisor-only
refusals, missing pages, wrapping/noncanonical spans, no partial destination
writes, excessive lengths, zero-length behaviour, invalid Kernel buffers,
wrong or unregistered backing frames, widened permissions, invalid requested
access, unknown calls, Kernel-assigned identities, successful/rejected copies,
exit, call budget, a synthetic deadline event, wrong-origin privilege and a
refused continuation PC. The tests also check teardown accounting.

A synthetic deadline event tests C policy only. It does not prove that the
hardware timer delivered a user-origin interrupt; that is one of the guest
acceptance cases below.

## Required runtime qualification on the development machine

Run the ordinary incremental configure, build and CTest commands described in
`USER_EXECUTION.md`. There should be eight main tests, including
`kernel.riscv64.user_execution`. No tool reinstall or build-directory deletion
is required.

The new guest validation must complete all twelve cases: both task identities,
address-space reentry, guard/read-only/supervisor-only/NX protection, illegal
supervisor CSR access, exit with an invalid stack, call-budget exhaustion, a
real machine-timer stop of an endless user loop, and normal execution after the
negative cases.

It must then prove that the original machine trap handler still works and that
all temporary page-table and data frames have returned to the original physical
accounting. Expected final output is:

```text
user.original-trap-handler=pass
user.frame-accounting=restored
user.completed-cases=12
user-execution-test=pass
UMICOM_KERNEL_USER_EXECUTION_READY
UMICOM_KERNEL_END
```

A compiler pass, an ELF inspection or a native test cannot replace this QEMU
qualification. Until that run succeeds, the new hardware execution path remains
unqualified. Even a QEMU pass would not establish physical-hardware support,
SMP safety, a complete scheduler, or a production process-isolation guarantee.
