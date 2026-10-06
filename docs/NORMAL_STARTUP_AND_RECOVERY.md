# Umicom Kernel — Normal startup, native boot services and recovery

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Four images with different jobs

The normal system no longer needs to execute the cumulative diagnostic sequence
before accepting commands. The original images remain available; selecting a
normal image does not change what the older images mean.

| Image | Purpose |
|---|---|
| `bin/umicom-kernel.elf` | Cumulative automated diagnostics, then poweroff |
| `bin/umicom-console.elf` | The same diagnostics, then the existing interactive shell |
| `bin/umicom-system.elf` | Essential boot setup, supervised startup jobs, then that same shell |
| `bin/umicom-recovery.elf` | Essential console/trap setup and independent recovery; no allocator or job admission |

The normal and recovery images still contain the shared source, including code
which their entry does not execute. This is behavioural separation, not a claim
that the binary has been reduced to a minimal production footprint.

All profiles are still for the declared single-hart QEMU RISC-V machine with
128 MiB of RAM. There is no disk installer, persistent filesystem, authenticated
login, host-directory mount, networking stack or complete supported OS here.
The recovery interface is a trusted development console, not authenticated
administration. Whoever controls the serial endpoint controls the session.

## Normal startup is not a shortened test transcript

The normal entry is selected before the original diagnostic main begins:

```text
boot.S supplies hart and DTB
             |
             v
existing console and owned machine trap vector
             |
             v
park the timer; validate RAM, Kernel bounds and DTB extent
             |
             v
initialise the existing frame owner once; reserve Kernel and DTB pages
             |
             v
validate and run the native startup plan
             |
       +-----+----------------------+
       |                            |
required jobs succeeded      required failure or unsafe setup
and images collected                 |
       |                            v
       v                    independent recovery
existing console shell
       |
       v
existing file, program, stream and shutdown services
```

`boot_memory.c` calls the established DTB inspector and physical allocator. It
does not add a second bitmap or infer a hardware memory map from incomplete
information. The fixed platform profile remains authoritative. All accessible
ranges, arithmetic, page boundaries and DTB overlap are checked before the
allocator is initialised. A second initialisation attempt is refused, not used
as a recovery shortcut.

Normal startup does not run deliberate ECALL, timer, page-fault, allocator or
process failure tests. Continue running the diagnostic image through CTest for
that evidence. A normal boot marker must not be reported as proof that the
omitted self-tests passed.

## What the startup controller manages

`UmicomKernelBootServices` runs **one-shot native startup jobs**. A job succeeds
only when its process has exited with the configured value, the machine return
has been verified by the existing execution path, output has been drained and
the image has been collected successfully.

A printed word such as "ready" is not interpreted as authority. Loading an ELF
is not enough to satisfy a dependency either.

This is the boot portion of a future System Manager. Its Master Controller
orders native startup workers above the existing process supervisor. It is not
yet a continuously running daemon manager. Long-running services need an
explicit readiness protocol, health observation, dependency-loss handling and
appropriate long-lived execution budgets. Those are not silently inferred from
a successful startup command.

### Plan and storage ownership

The current plan is compiled Kernel data. No service-manifest parser, shell
expansion, network lookup or writable service configuration is introduced.

A plan contains up to eight uniquely named jobs. Names contain letters, digits,
periods, underscores or hyphens and are at most 31 bytes. Dependency bits refer
to indices in that complete plan. Cycles, including disconnected cycles, missing
indices, self-dependencies and duplicate names are refused before admitting any
process. Input order need not be topological.

The manager copies every name and the complete structured launch data. The
existing launch packer owns argument and environment validation. A caller can
release or overwrite its argument arrays after initialisation; later attempts
use the manager's snapshot.

The executable byte buffers are different: they are borrowed immutable source
and must remain readable for the manager's lifetime. The compiled boot plan uses
existing embedded ELF bytes. This is not a filesystem path search or permission
to follow arbitrary user pointers.

Keep the owner at a stable, initially zero-filled Kernel address. Do not copy or
reset it. Public records support static allocation and explicit tests, not
consumer mutation. All calls are serial on hart zero between user quanta under
the existing allocation-context gate. Callbacks must not reenter the manager.

### Execution and dependencies

Only one startup job is active at a time. This intentionally small scheduling
policy reuses the process supervisor rather than introducing a competing
scheduler. Up to eight definitions can be processed despite the underlying
scheduler's smaller simultaneous-task limit.

A job begins when all dependencies have succeeded. A permanently unsuccessful
dependency skips its dependents; a backoff does not. An independent job may run
while another job waits for its retry time.

Each attempt has independent bounds:

| Bound | Contract |
|---|---|
| Attempts | One through four, including the first attempt |
| Per-attempt wall interval | Nonzero, at most 100,000,000 platform timer ticks |
| Retry delay | Zero through 100,000,000 timer ticks |
| Instruction execution | Existing scheduler slice limit, not a reset syscall budget |
| One step | At most one existing user quantum |

Deadlines are checked between user quanta and after return. They are not a
hard real-time bound on Kernel bookkeeping or a blocking output callback. The
caller must continue stepping during a backoff; no low-power idle mechanism is
added. Timer wrap or reversed observations are not accepted as valid time.

An architecture admission refusal retains the ready image and does not start
another attempt. The caller can correct the external ownership condition and
retry the step. Normal system startup treats an unexpected controller refusal
as a recovery condition rather than spinning indefinitely.

### Streams and authority

Every fresh job receives the existing private standard streams. Stdin is ended
explicitly: a startup job should not wait for a person before the prompt exists.
Stdout and stderr are copied by the existing service, drained between quanta and
labelled safely. Nonprinting bytes cannot issue terminal escape commands.

The packaged jobs receive no VFS client and no file grants. Arguments and
`UMICOM_BOOT=normal` are data, not credentials. The manager does not unify its
handles with the older run-once process registry.

### Retry and cleanup are ordered

An unsuccessful attempt is completely collected before a replacement is loaded.
Retries get fresh task identities and do not overlap the previous image.

Stopping execution, draining output and releasing memory are distinct steps.
A failure during collection retains the actual handle and retry position. The
existing lower reaper may already have retired the stream record before failing
to release an image page. The controller therefore does not attempt to drain
that retired stream again.

The terminal outcome is recorded when it is first observed. Delayed cleanup
cannot retroactively turn an on-time successful exit into a timeout. A completed
service is not published until collection finishes, however: a dependent must
never start while its prerequisite still has incomplete teardown.

Unpublished loader rollback failures are also retained. New admission is blocked
until that cleanup finishes. `Close` can retry the remaining release, but cannot
reset generations or erase the only record of outstanding memory.

### Required and optional jobs

A required job exhausting its attempts selects recovery. Unstarted jobs are
cancelled or skipped according to their dependencies; they are not launched
merely to make the plan look complete.

An optional unsuccessful job remains visible in the report. Independent required
jobs can still complete. An optional prerequisite does not make its required
dependent magically optional: that dependent's inability to start still prevents
normal readiness.

The controller retains value-only outcomes after closing. `services` reads those
records; it does not restart jobs, execute a callback or traverse process pages.

## Packaged startup jobs

The first plan uses the existing independently linked launch client for two
ordered jobs:

1. `runtime-contract` checks structured native entry and produces user stdout.
2. `session-environment` depends on it and checks another independently owned
   launch carrying the explicit boot environment.

Both expect exit zero. These are real user-mode bootstrap contract checks, not
pretend storage, networking or database services. They reuse the existing ELF,
loader, launch packer, streams, timer runner and supervisor. Their completed
images are gone before the shell creates its own RAM-backed session.

## Independent recovery

The explicit recovery image enters before physical-memory initialisation and
before admitting any job. Required-service exhaustion and certain normal-console
initialisation failures can also select this interface.

```text
recovery> help
recovery> status
recovery> poweroff
```

The interpreter uses fixed storage, the existing bounded line editor/tokenizer
and the polling UART. `exec`, `run`, file operations, memory reset, repair and
retry verbs are deliberately absent. `poweroff` must be exactly one complete
token. Unsupported or damaged input cannot turn into a truncated command.

Recovery's `status` prints its reason and any already-owned boot-result values.
It does not require a filesystem, user process or Framework service. A failed
owner may remain retained for diagnosis until explicit poweroff; recovery never
pretends that force-freeing it was safe.

An arbitrary unverified machine-context return is not recoverable in general.
The established fatal trap/foreground handling remains in place. A working UART
is not proof that an unsafe Kernel should execute more normal commands.

## Build, tests and images

The usual configure/build sequence now includes the normal and recovery images.
The first build compiles additional image targets; later builds remain
incremental. `UMICOM_BUILD_SYSTEM_IMAGES=OFF` deliberately omits these new image
profiles but does not delete the startup controller or diagnostic tests.

With `BUILD_TESTING=ON`, three more images exercise startup without waiting for
human input:

- `tests/umicom-startup-check.elf` follows normal memory and native-job startup,
  verifies cleanup accounting, emits normal readiness and powers off.
- `tests/umicom-recovery-check.elf` supplies malformed ELF to a required startup
  job. Only its actual two-attempt failure and blocked dependent select the
  accepted recovery result.
- `tests/umicom-forced-recovery-check.elf` selects recovery before the allocator
  has been initialised and verifies that observation before accepting the path.

The normal diagnostic image additionally exercises the controller with actual
native jobs, including wall deadline, execution budget and active cancellation.
All four new CTests require the existing current-image build fixture.

The automated startup image stops before the interactive shell. It is not an
interactive-terminal test; perform the manual system/recovery sessions below.

## Recurring manual workflow

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Start the normal image:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
    -display none -monitor none `
    -chardev "stdio,id=console,signal=off" -serial "chardev:console" `
    -m 128M -smp 1 -no-reboot
```

Expect `boot.mode=normal`, the two startup jobs' output and outcome records,
`UMICOM_KERNEL_NORMAL_STARTUP_READY`, then the existing `umicom>` prompt. There
should be no cumulative `exception-test=begin` or later self-test sequence.

```text
services
create /notes/startup
write /notes/startup "Normal startup reached the existing RAM filesystem"
cat /notes/startup
exec /bin/umicom-launch-client.elf startup "Account one" ""
poweroff
```

Start `umicom-recovery.elf` using the same QEMU command with that filename.
`status` should report `forced-recovery`, unavailable boot-memory allocation and
no admitted startup jobs. A command such as `run /bin/umicom-diagnostic.elf` must
be refused. End with `poweroff`, then check `$LASTEXITCODE` in PowerShell.

All session file data remains RAM-only and disappears at poweroff.
