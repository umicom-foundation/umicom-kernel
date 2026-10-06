# Umicom Kernel — Managed services, readiness and health

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## A running process is not automatically a ready service

A one-shot boot job is useful when some work must finish before startup proceeds.
A long-running service has a different lifetime: its consumer needs a *live*
prerequisite which has explicitly reported that it is ready. Successful loading,
printed text and even exit value zero do not establish that condition.

`UmicomKernelServiceManager` owns that live relationship above the existing
process supervisor, user scheduler, copied streams and executable loader. The
Master Controller manages native Slave Controller instances. It does not replace
the one-shot boot controller, introduce another trap entry, or change the
meaning of the existing `services` console report.

This is a bounded development service domain, not yet an indefinitely running
production daemon manager. Existing per-task syscall and slice limits remain
in force. Successful heartbeats do not reset those limits.

## What the controller owns

Keep a manager in stable, fully zero-filled Kernel storage. Do not copy, reset or
reinitialise it, including after Close. Its embedded process supervisor and
stream owner outlive every admitted instance. Access is serial on hart zero,
between user quanta, under the existing allocation/context admission gate.

A plan contains one through four named services, no more than the existing
scheduler's task capacity. Each specification has:

| Field | Contract |
|---|---|
| Name | Unique, 1–31 identifier characters: letters, digits, dot, dash, underscore |
| Executable | Immutable caller-owned ELF bytes which remain alive until Close |
| Launch | Arguments/environment copied with the existing launch packer |
| Dependencies | Plan-index bits naming live healthy prerequisites |
| Required | Exhaustion recommends recovery for this entire manager domain |
| Attempts | 1–4 total admissions, including dependency-driven replacements |
| Startup interval | 1–100,000,000 platform timer ticks |
| Health interval | 1–100,000,000 platform timer ticks |
| Report interval | Positive and strictly shorter than the health interval |
| Retry interval | 0–100,000,000 platform timer ticks |
| Slice limit | Existing allowed per-process limit, at most 4,096 |

The complete graph is checked before initialisation is published or frames are
allocated. Duplicate names, unknown or self dependencies, disconnected cycles,
bad launch data and invalid bounds are refused. Definition order need not be
execution order: the controller computes a prerequisite-first order.

Names and launch strings are copied. ELF bytes are borrowed immutable input,
not secretly copied into a second cache. Fresh instances are always created
through the established ELF loader. Services receive closed standard input,
separate copied stdout/stderr, and no VFS client grant.

## Explicit service reports

The native syscall contract in `service_report_abi.h` is:

| Register | Meaning |
|---|---|
| a7 | SERVICE_REPORT, number 80 |
| a0 | READY (1) or HEARTBEAT (2); replaced by returned status |
| a1 | Strictly increasing sequence, beginning at one |
| a2, a3 | Reserved, must be zero |

The existing monitor first validates privilege origin, ECALL instruction and
syscall budget. The report hook then requires the exact current task/report
pointer, matching generation and identity, and the dynamically bound manager.
Copying a session with the same numeric identity grants nothing. An ordinary
unmanaged process receives UNBOUND.

READY is allowed only for a starting instance. HEARTBEAT requires an already
healthy instance. Duplicated READY, replayed or skipped sequence values, unknown
operations and reserved arguments are refused without renewing the lease.
An arrival at or after the current deadline is late, not an extension of an
already-expired interval. Overflowing deadline arithmetic is also refused.

Reports carry no user pointers, arbitrary service identity or user-selected
health interval. The trusted plan, not the process, determines the lease.

## Readiness is published after safe return

A successful syscall captures a pending report and advances the validated ECALL
continuation exactly once. It then returns through the unchanged saved-frame
Assembly rather than publishing readiness while the borrowed user context is
still installed.

After the existing slice adapter verifies machine-state restoration, the new
scheduler hook checks that the retained frame and pending report agree. Only
then does it update sequence, health deadline and HEALTHY state.

The call parks the same user continuation until the plan's next report time.
This avoids a tight user heartbeat loop. The programme counter, local variables,
private memory and existing budgets remain with the task; it does not restart
at ELF entry. A report accepted on the final permitted slice still ends in
ordinary budget exhaustion, not a free budget renewal or a fabricated timer
interrupt.

A heartbeat is self-reported progress/liveness. It is not proof that a service
produced correct business results, persisted data, can answer real requests or
is free from defects. Application-specific health checks remain necessary.

## A health lease can be revoked

`UmicomKernelServiceManagerStep()` observes time and process state, revokes
invalid readiness, performs cleanup and admits at most one new instance. It
runs at most one user quantum per call and drains bounded output afterwards.
The dispatcher must call Step regularly. It must not bypass the manager and
run the embedded scheduler directly.

When a prerequisite exits, faults, exhausts its budget, misses its deadline or
receives a restart request, its consumers lose readiness before any new quantum
is chosen. Revocation propagates through the entire graph, not just the first
level. Consumers are cancelled and collected in reverse dependency order.

No replacement is admitted until the current cleanup pass has succeeded.
Waiting during retry delay does not hold the old executable frames. A new
instance gets a fresh process identity and report sequence. An unrelated service
can continue while another awaits its retry interval.

Dependencies are continuous relationships. A dependency which was ready once
but is no longer healthy does not permit its consumer to keep running. A
replacement prerequisite must report READY anew before its consumers restart.

## Result evidence and retryable cleanup

Stopping a service, draining its output and releasing its image are separate
steps. The first observed stop reason survives a cleanup retry. The last
successfully collected terminal result is copied into the manager's public
snapshot; it is not a pointer to freed task memory.

Accepted output is drained before collection. If the lower reaper retires stream
metadata and then encounters a frame-release failure, the retry does not try to
drain that retired stream token again. The process handle remains available for
retry. Unpublished loader rollback failures are also retained and retried before
new admission.

`Restart(index)` immediately revokes readiness and its dependent graph. Step
performs cancellation, collection and bounded delayed replacement. The restart
request cannot add to the plan's attempt ceiling. A successful call is a request,
not a promise that a replacement has already become healthy.

`Close()` stops admission, cancels the domain and collects its remaining tasks.
A failed close leaves the owner STOPPING with its resources and records available
for another Close. A completed close is idempotent. It never resets generations.
An unverified machine return or reversed clock leaves the domain UNSAFE and
refuses normal reclamation; do not force-reset the owner to hide that condition.

## Required failures and recovery policy

When a required service has no attempts left, the manager recommends RECOVERY,
revokes all remaining service readiness and admits no replacements. Cleanup can
still require subsequent Step/Close calls. RECOVERY does not mean every last
frame was already released at the instant that phase became visible.

An optional failure leaves the manager degraded while independent services can
continue. A dependent cannot use a permanently failed prerequisite. A required
dependent which cannot start also causes the domain to recommend recovery.

The optional interactive demonstration does **not** switch the entire operating
system to the recovery console. It shows the manager's recovery recommendation
and stops that domain. Wiring required persistent system services into boot and
whole-system recovery is a later policy integration.

## Interactive demonstration

The existing `umicom-system.elf` keeps its normal startup and one-shot jobs.
There is no new automatic background plan. At its prompt, type:

```text
daemons start
daemons status
daemons restart 0
daemons status
daemons stop
daemons status
```

The demonstration contains `clock-sample` and `consumer-sample`; the latter
requires the former. They both run the separately linked
`programs/umicom-health-service.elf`. These names describe diagnostic roles,
not a shipped clock, networking, storage or application service.

Allow a few polling passes before expecting HEALTHY. The values printed include
state, attempts, process identity, report sequence and a named reason. Following
a restart, allow the configured retry interval before expecting fresh identities.
Status is printed only when asked, so background reports cannot overwrite an
unfinished console command. Foreground execution and input still use their
existing paths; each poll returns from the service quantum before either runs.

The sample uses 20,000,000 startup ticks, 30,000,000 health ticks, 10,000,000
report ticks and 10,000,000 retry ticks. Every instance keeps the existing
32-call budget. It eventually exhausts that budget despite reporting correctly;
four admissions are the total ceiling. This is intentionally **not endless
budget renewal**. Configure and validate a distinct long-lived resource policy
before using this as a permanently running production service.

A domain can be started once per boot. Stop retains its records and does not
make a new `start` valid. Use a fresh boot to try another diagnostic plan:

```text
daemons start silent
```

The first service reports READY and then spins without another heartbeat. The
manager should revoke it on health expiry and revoke/restart the consumer with
it. The alternatives `fault` and `unready` respectively fault after readiness or
never report readiness. `daemons status` makes each outcome visible.

`poweroff` closes the optional domain before unmounting the console filesystem.
A partial cleanup remains retryable. None of these commands rewrites or removes
the original `services` report for completed boot jobs.

## What remains outside this implementation

This does not add writable service manifests, a user administration syscall,
restart-rate windows, credential or per-path authority, unlimited lifetimes,
SMP scheduling, Kernel pre-emption or interrupt-driven idle waiting. The sample
controller remains trusted machine-mode policy using the existing serial
owners. File grants, IPC service discovery and endpoint rebinding across
replacement generations require their own explicit designs.

The existing one-shot startup, foreground shell, user programmes, recovery
profile and earlier diagnostics remain independent and available.

## Normal developer commands

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new runtime test is `kernel.riscv64.managed_services`. It uses the normal
terminating image and the existing current-image fixture. The interactive
system is not started as an indefinitely waiting CTest.

The optional native policy suite runs on a Linux development host:

```text
cmake -S tests/managed_services -B build/native-managed-services -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_MANAGED_SANITIZERS=ON
cmake --build build/native-managed-services --parallel 2
ctest --test-dir build/native-managed-services --output-on-failure
```

That optional host suite models privileged entry. It is not an extra installation
step for the normal Windows-to-RISC-V build.
