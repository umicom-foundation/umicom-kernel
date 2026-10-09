# Umicom Kernel and Umicom OS — Release Roadmap

**Planning baseline:** 4 October 2026  
**Status:** proposed implementation sequence, with evidence-gated releases  
**Project lead:** Sammy Hegab, Umicom Foundation

## The destination

There are two related products, and they must not be confused.

**Umicom Kernel** is the original native kernel: boot, memory, execution,
scheduling, protection, IPC and device mechanisms. **Umicom OS** is the system
people install and use: the kernel choice, runtime, filesystems, accounts,
services, applications, desktop, installer, updates and recovery.

A kernel that completes its tests and exits QEMU is valuable progress, but is
not yet an interactive operating system. Conversely, an installable Umicom OS
using Linux-libre would not prove that the native Umicom Kernel is ready to
replace it. We should publish these achievements separately.

The near-term native objective is a usable console system on a precisely named
QEMU machine. The next objectives are a persistent developer/server system and
then a desktop/workstation. Broader hardware, ARM64, mobile and small embedded
profiles remain in the programme; they are not implied by a RISC-V VM boot.

This is a sequence of deliverables, not a calendar promise. A named work package
can require more than one source delivery. Fixes, review and qualification belong
to its completion cost. We do not have measurements that justify a release date
or a percentage-complete claim yet.

## What was checked

The Kernel baseline is
[`299a273353cf545e3af70cb8fc4346033bcdffee`](https://github.com/umicom-foundation/umicom-kernel/tree/299a273353cf545e3af70cb8fc4346033bcdffee).
It includes the cooperative-thread implementation, following executable loading,
process handles and copied message channels. Its
[thread contract](https://github.com/umicom-foundation/umicom-kernel/blob/299a273353cf545e3af70cb8fc4346033bcdffee/include/umicom/kernel/threads.h)
explicitly describes cooperative Kernel callbacks, not pre-emptive user scheduling.
The previous run-once process API and user monitor are still separate components.

The OS baseline checked is
[`21f72f4d7e1901c36fb6d0bc61b35fa4a34e3b8c`](https://github.com/umicom-foundation/umicom-os/tree/21f72f4d7e1901c36fb6d0bc61b35fa4a34e3b8c).
Its [README](https://github.com/umicom-foundation/umicom-os/blob/21f72f4d7e1901c36fb6d0bc61b35fa4a34e3b8c/README.md)
and [foundation architecture](https://github.com/umicom-foundation/umicom-os/blob/21f72f4d7e1901c36fb6d0bc61b35fa4a34e3b8c/docs/foundation/architecture.md)
describe an independent C23 init/recovery controller, a RAM-only root filesystem,
one-shot service checks, a Framework Data Server probe and a native image tool.
Those documents explicitly exclude a persistent installer, authenticated login,
network service and desktop from that foundation profile. Native component
tests and archive checks are not its real-image boot qualification.

This review did not rebuild the entire Umicom Framework/application catalogue or
the Linux-based OS images. Their release readiness is therefore not credited as
complete here.

### Evidence ledger

| Area | What the reviewed source contains | What that does not establish |
|---|---|---|
| Native boot and machine traps | RV64 bootstrap, serial output, ECALL and timer paths | Physical board support, nested-trap robustness or all device interrupts |
| Memory | Physical frame ownership, Sv39 mapping and controlled hardware checks | A hardened general VM subsystem, demand paging, swap or multi-hart TLB handling |
| Privilege boundary | Controlled S-mode and U-mode execution, checked copies and bounded user runs | A permanent supervisor-resident Kernel or a full adversarial process sandbox |
| Executables and processes | Separate static ELF loading, private frames, explicit destruction, owner-scoped handles | Dynamic linking, file-backed exec, fork or runnable process scheduling |
| Messages | Copied bounded queues and nonblocking user services | Blocking user IPC, shared memory or concurrent/SMP queues |
| Kernel threads | Separate resumable stacks, cooperative round-robin, sleep/wake and reaping | Pre-emption, user-process suspension or a guarantee against a non-yielding callback |
| This delivery | Remembered events, wait-any, deadlines and cancellation-aware registrations | User event syscalls, IRQ-safe waits or SMP synchronisation |
| OS composition | Source recipes and independent recovery foundation | A qualified complete free distribution or a shipping native-Kernel OS |

Existing repository documents remain historical project assets. Some older
roadmap headings still describe past work as current. This new roadmap is a
proposed current planning view; it does not rewrite those earlier documents.

## Architecture that stays fixed

The [committed decision register](https://github.com/umicom-foundation/umicom-kernel/blob/299a273353cf545e3af70cb8fc4346033bcdffee/docs/DECISION_REGISTER.md)
records a kernel-independent product, Linux-libre as the production-track
direction, FreeBSD as the principal non-Linux portability proof and an independent
original hybrid-microkernel project.

That does **not** mean the Linux-libre conversion is already demonstrated in
every OS recipe. The OS foundation document still names a pinned upstream Linux
recipe. Reconciling that implementation with the freedom policy is an explicit
release task, not an assumption hidden by the name of the project.

The dependency direction stays:

```text
Umicom applications and Desk
              |
              v
Umicom Framework user-space contracts
              |
              v
Portable system-service providers
              |
       +------+---------+
       |                |
Linux-libre/FreeBSD   Umicom native ABI and services
                        |
                        v
                  Umicom Kernel
```

Kernel and recovery must work without full Framework, GTK or the Data Server.
Framework owns reusable normal-user-space behaviour; product modules remain
thin. The System Manager's Master Controller / Slave Controllers organise
bounded services above the kernel mechanism layer.

Do not add the native Kernel as a Framework or Applications submodule. The OS
integration repository may eventually pin a qualified native commit for an
explicit experimental profile. That is composition, not ownership inversion.

C23 remains the default for original implementation, with Assembly where exact
machine operations require it. Mature free components can be integrated when
they avoid unnecessary risk. Cryptography and protocol interoperability are not
places to invent incompatible replacements simply to maximise original code.

## Release gates

| Gate | Deliverable a person can use | Required evidence before naming the release |
|---|---|---|
| Kernel developer preview | Reproducible native source build and bounded VM diagnostics | Clean and incremental builds, current-image tests, fault paths, limitations and exact machine configuration |
| Native console alpha | Boot remains running; a user can open a console session, navigate RAM-backed files and launch native programs | Interactive console, real scheduled process contexts, file-backed loading, IPC, service lifecycle and fault recovery |
| Native persistent-system alpha | Install on a disposable virtual disk, save data and reboot into the installation | Safe disk selection, filesystem integrity, persistent accounts/configuration, networking and recoverable boot |
| Native developer/server beta | Useful headless environment and a documented development workflow | Runtime/compatibility subset, stable service boundary, remote administration, tested updates and supported machine matrix |
| Native desktop beta | Usable graphical session on a declared supported target | Display/input, compositor/windowing, Desk, accessibility baseline, application workflows and session recovery |
| Native OS release candidate | A candidate system suitable for independent installation and testing | Upgrade/rollback, power-cut tests, security review, long-running stress, complete source/licence inventory and no unresolved release blockers |
| Supported native OS release | A maintained OS for the explicitly supported profiles and hardware | Candidate criteria remain satisfied; update, vulnerability response, recovery and support ownership are operational |

A preview can ship before self-hosting as an honestly labelled developer
experiment. It must not be represented as a complete FSDG-endorsed distribution.
A fully functioning native desktop release requires both the native Kernel gates
and the OS integration gates below.

## Native work sequence

The following are future source-delivery scopes, not names to embed into
functions, constants, comments or binary identity. Each row has an observable
exit criterion. Dependencies can be refined after review, but a missing gate
cannot be replaced by a passing compile.

### Execution and coordination

| Planned work package | Deliverable | Exit evidence |
|---|---|---|
| Remembered events and wait-any — this delivery | Manual/auto reset, durable signal state, bounded waits, deadlines and cancellation pruning above cooperative threads | Signal-before-wait, broadcast, FIFO auto-reset, spurious wakes, close/reuse, timeout ordering and message coordination |
| Trap and privilege hardening | Audited entry/return state, machine-stack safety, exception classification and protected control transitions | Inject faults at boundary points; no recursive failure or corrupted return state; old paths still pass |
| Interrupt ownership and critical sections | Explicit masking, nesting and shared-state ownership rules; no ad-hoc volatile synchronisation | Ordered acquisition/release tests and interrupt injection; unsafe contexts rejected |
| Timer-driven runnable contexts | Save complete interrupted user context, maintain runnable queues and resume at the interrupted PC | Two CPU-bound programs both progress; register and address-space isolation survive repeated switches |
| Blocking IPC and scheduler waits | Connect user message waiting to resumable processes, with deadlines and cancellation | Receiver blocks without spinning, sender wakes it, peer exit and timeout finish once, no lost wakeups |
| Process supervision and cleanup | Parent/service relationships, terminal collection, cancellation and handle/IPC cleanup coordinated with execution | Crash or kill one process; siblings remain live; no stale references, runnable freed contexts or leaked frames |

The existing cooperative scheduler remains useful for its documented Kernel
callbacks. It is not silently converted into an interrupt scheduler. The current
run-once process API remains an explicit path until the replacement is reviewed;
superseded code is retained with an explanation if it is ever disabled.

### Interactive console system

| Planned work package | Deliverable | Exit evidence |
|---|---|---|
| Kernel allocation and stronger VM ownership | Checked dynamic allocations, page-table frame ownership validation and protected stack mappings | Exhaustion, corruption, stale ownership, rollback and unmapped guard tests; memory accounting returns to baseline |
| VFS and RAM-backed files | Typed file/directory objects, per-process descriptors, bounded path resolution and RAMFS | Create, enumerate, read, write and remove files; rights and partial I/O results are explicit |
| Console input and shell | UART input, bounded editing, shell commands, process output/error channels | A user can inspect files, launch a program and regain the prompt after failure |
| Programs loaded from files | Loader accepts a VFS source; defined argument/environment layout; process start and exit reporting | Replace a program in RAMFS and execute its new bytes; invalid images cannot alter another process |
| Normal boot and System Manager | A persistent boot path distinct from self-test mode; service startup, dependencies, health and controlled restart | Boot stays alive; one failed service enters recovery or restarts according to policy; the diagnostic test path remains available |

**Console-alpha gate:** the result is interactive, rather than a transcript
which automatically powers off. RAM-only file state is acceptable at this gate
only because that limitation is explicit. This is a development console, not
yet authenticated administration or a persistent OS release.

### Storage, hardware and networking

| Planned work package | Deliverable | Exit evidence |
|---|---|---|
| Device discovery and driver contracts | Validated firmware/device-tree data, IRQ routing, MMIO/DMA ownership and device lifetime | Malformed descriptions are refused; attach/failure/remove paths release resources |
| VirtIO block transport | Bounded descriptor/queue handling and actual virtual-disk reads/writes | Read known blocks, write scratch blocks, flush, handle I/O failure and reject invalid ranges |
| Persistent filesystem | Selected free filesystem implementation/provider, cache/flush policy and recovery rules | Save/reboot/read verification; truncated writes, injected I/O errors and simulated power cuts |
| x86-64 architecture port | Second architecture using shared Kernel mechanisms, explicit boot/firmware boundary and device support | Independent x86-64 boot, traps, memory, user execution, storage and recovery tests; not inferred from RV64 |
| Network foundation | VirtIO network, packet ownership, supported IP/transport subset and socket/service interface | Packet exchange, disconnect/reconnect, malformed packet handling and controlled service communication |
| Network services and secure transport | DNS/DHCP where needed, remote administration, TLS through reviewed free components, time and trust policy | Real client/server workflows; rejected bad peers/keys, explicit timeout and resource limits |
| Accounts and local security | Principal database, local registration/login, password/key handling, permissions and audit boundaries | Unprivileged accounts cannot read/modify protected data; failed authentication and recovery are tested |

A RAM disk is not persistence. A packet echo is not a complete network stack.
A compiled x86 target is not a supported architecture. Those distinctions should
appear in the public feature matrix rather than being hidden in release notes.

### Runtime, portability and usable applications

| Planned work package | Deliverable | Exit evidence |
|---|---|---|
| Native runtime and C library | Defined startup, allocation, file/process/time/thread interfaces and a documented compatibility subset | Port ordinary free command-line programs without Kernel-private shortcuts |
| POSIX compatibility environment | Deliberately scoped semantics above the native ABI; process, file, socket and threading support expanded as required | Conformance/behaviour tests and successful supported tool/library ports |
| Self-hosted developer toolchain | Native editor/build tools, compiler, linker and source-package workflows | Rebuild selected system components inside the system, then perform the documented complete-system build |
| Framework native system provider | Implement the actual portable Framework contracts over native services | Real headless Framework examples, Data Server operations, faults, cancellation and restart; no Framework dependency inside Kernel |
| Headless application qualification | Required console/server Umicom application profiles, data migration and operational tooling | End-to-end workflows using production libraries, not test stubs; restart and backup/restore checks |
| Installer and recoverable updates | Image manifest, authenticated package/update chain, generation selection and rollback | Clean installation, failed update, interrupted write and rollback on disposable disks, then declared hardware |

The native ABI does not automatically make existing Linux binaries runnable.
We should port/rebuild supported software first, then make any binary-compatibility
work a separate, justified project. A POSIX subset is not a claim of complete
POSIX conformance.

### Desktop, release and expansion

| Planned work package | Deliverable | Exit evidence |
|---|---|---|
| Display and input services | Framebuffer/display modes, keyboard/pointer, event routing and device access policy | Real rendered frames and input inside the guest; denied cross-session access and device-loss handling |
| Graphical runtime and compositor | Window/surface lifecycle and the dependencies needed by the selected GUI backend | Multiple application windows, focus, resizing, clipboard boundaries and recovery after a client crash |
| Umicom Desk and Control Centre | Familiar desktop/workspace, launcher, settings, local account/session workflows and system information | Normal daily workflows from graphical login to shutdown, without a terminal prerequisite |
| Application catalogue and profiles | Studio, Desk, Trader, Bank and other qualifying Umicom applications packaged from Framework-based sources | Each included product completes its own acceptance journeys; missing ports are labelled unavailable, not claimed compatible |
| Hardware usability and accessibility | Required USB/audio/storage/input, power behaviour, keyboard accessibility, text/input localisation and assistive interfaces | Per-device/per-profile results; suspend/resume and long-running use where promised |
| Release hardening and maintenance | Fuzzing, performance/soak tests, threat review, source provenance, vulnerability response and support docs | Independent clean builds/installations, no open release blockers and an operational update/recovery process |
| ARM64 and broader profiles | Additional boards, small embedded systems and later mobile adaptation | Separate boot, power, input, networking and freedom-policy qualification for each named device |

A graphical window is not Desk completion. A Desk session is not evidence that
Studio, Trader or Bank is production-ready. The distribution's “install all”
selection must enumerate the actual qualified catalogue and its dependencies.

Mobile remains a long-term objective. Boot-chain access, power management,
modem isolation, input and free driver/firmware availability need device-specific
engineering. “All major devices” is the direction of travel, not an initial
support claim.

## Parallel Umicom OS distribution track

The native-kernel programme should not discard the independent OS image and
recovery work. The Linux-libre route can reach users earlier while the native
kernel matures, provided it passes its own qualification.

| OS work package | Owner and deliverable | Release evidence |
|---|---|---|
| Reconcile freedom policy with image recipes | OS: pinned allowed sources, licence/source inventory, Linux-libre selection and reproducible input capture | Every distributed component has an approved source/rights record; no assumption based solely on the kernel name |
| Qualify foundation and recovery images | OS: real RISC-V/x86-64 image builds and boot/recovery transcripts | Successful normal and forced recovery boots from the actual produced artefacts |
| Persistent installation and identities | OS: safe virtual-disk installer, accounts, mounts, configuration and bounded administration | Fresh install, reboot, login and persistent-data tests |
| Service/platform integration | Framework and OS: providers, system manager, Data Server, networking and package staging | Real service lifecycle and fault recovery with Framework absent or broken |
| Desktop and product profiles | Desk/application modules plus OS packaging | Minimal, Desktop, Developer/Workstation and Server profiles each have explicit application/driver requirements |
| Authenticated update and rollback | OS release engineering: package sources, signing/key policy and recovery | Rejected tampering, expired/untrusted metadata, interrupted upgrade and successful rollback |
| Supported release | OS and application maintainers: published support matrix, source availability and maintenance | Freedom audit, security/reliability acceptance, installation documentation and operating support channels |

The proposed first broad distribution target remains **x86-64**, with Minimal,
Desktop, Developer/Workstation, Server and Recovery qualification. RISC-V is the
native-kernel development machine and can have a separately labelled developer
preview. ARM64 and mobile do not block that narrowly declared first release.

These release profiles are a planning recommendation carried forward from the
project discussion, not a claim that the current OS repository already builds
all of them.

## Free-system policy as a release gate

The [GNU FSDG](https://www.gnu.org/distros/free-system-distribution-guidelines.en.html)
covers source and free licences for practical components, including documentation
and fonts, and forbids steering users to nonfree repositories/software. Its
complete-distribution criteria include self-hosting; small-device distributions
have a specified exception. Nonfree firmware cannot be included. Maintenance
and prompt correction of reported freedom problems are part of the commitment.

Our project gate should therefore require an auditable package/source inventory,
asset review, a free build route, documented supported hardware and a clear
reporting/remediation owner. Existing Windows-hosted development does not
establish the complete-system build requirement.

[FSF listing](https://www.gnu.org/distros/free-distros.html.en) is a separate
external decision, not an automatic outcome of a successful audit or choosing a
free kernel. Do not describe Umicom OS as endorsed before that actually occurs.

Freedom may limit initial hardware support. We should choose and publish a
tested compatible-device matrix rather than add an undocumented proprietary
driver/firmware path to meet a blanket hardware claim.

## Release evidence and responsibilities

For each work package, record its owner, reviewed commit, dependencies, supported
configuration, acceptance commands, actual results and remaining limitations.
The owner columns describe repository responsibility, not staffing commitments.

**Kernel:** mechanisms, native ABI, driver interfaces and architecture tests.  
**Framework:** reusable user-space contracts, portable providers and application infrastructure.  
**OS:** image composition, boot profiles, packages, installation, updates and recovery.  
**Applications:** product behaviour and acceptance journeys using Framework.  
**Release engineering:** integrated source/licence records, artefact publication and maintenance.

A normal delivery retains existing comments and implementation. Only required
integration points change. If an old implementation is truly superseded, retain
it with an explanation in an explicit disabled section unless removal is
approved. Do not manufacture compatibility code merely to record an old name.

The following are different pieces of evidence:

1. A source review checks the design.
2. A native test checks the code it actually executes.
3. A cross-build checks target compilation/linking.
4. A VM test checks the specific emulated machine.
5. Hardware qualification checks the named physical machine.
6. Installation/update/recovery tests check a composed OS product.

A build fixture can prevent a stale executable from being tested. It cannot
make an untested new capability complete. A readiness marker without its
supporting checks is not sufficient for a release gate.

## The next practical checkpoint

Merge and qualify the event/wait-any delivery first. The immediate follow-on
should address **trap/interrupt state and safe pre-emption boundaries**, then
**resumable user-process scheduling and blocking IPC**. The interactive
VFS/console lane follows rather than indefinitely adding isolated self-tests.

At the console-alpha checkpoint we should have a machine that stays running,
accepts commands and executes separately loaded programs. That is the point at
which development starts to feel like using an operating system rather than
only validating kernel mechanisms.

Do not announce a supported release date before the scheduling, persistent
storage and first application-provider integration risks have been measured.
Publish named previews and their exact limitations in the meantime. The goal is
a functioning, maintainable free system—not a version label ahead of the code.

## Qualified checkpoint: writable FAT16 native file services

**Engineering checkpoint:** 9 October 2026, developed from remote `main` at
`063a758095b48bb818d41aaa23191f3b055c44a3`. The earlier next-checkpoint section
above is retained as the original planning record.

The storage lane now includes an explicitly owned writable FAT16 VFS mount,
bounded positional writes and zero-extension, append, truncation, file and
directory creation, and removal of closed objects. The native process
file-service ABI can use this mounted domain under granted descriptor and
namespace rights. The normal console provides a separate `mountdiskrw` workflow.
Each successful mutation completes the existing Stage and accepted Finish.

The delivered qualification includes 661 lifecycle and 54 writable-provider
native tests under Clang with address/undefined-behaviour sanitizers, the 144
new native cases under GCC, all 194 RV64 preset tests, and two normal console
boots that write and then read the same persisted file. The RV64 process journey
also verifies the complete 8 MiB image against an independent expected result.
These are separate native, cross-build and virtual-machine results; they do not
qualify physical hardware or a composed OS installation. See
[the provider contract](WRITABLE_FAT16_VFS.md),
[the engineering review](WRITABLE_FAT16_VFS_REVIEW.html) and
[the qualification record](WRITABLE_FAT16_VFS_QUALIFICATION.json).

The persistent-system alpha gate remains open. A useful next substantial
integration is bounded executable/configuration loading from an explicitly
selected persistent mount, with normal process ownership and rights, followed
by fresh-boot validation. Ordinary console `run`/`runrw` currently retain their
RAMFS binding. Persistent boot policy, safe installation, authenticated identity,
networking, recovery and update/rollback still require their own work and gates.
The new provider does not add journalling, automatic repair or power-loss
atomicity, and introduces no dependency on Umicom Framework or an OS Data Server.
