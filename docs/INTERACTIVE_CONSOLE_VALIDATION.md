# Umicom Kernel — Interactive-console validation evidence

Author: Sammy Hegab, Umicom Foundation. MIT licence.

## Source identity and preservation

The baseline is `04316fe35638e826fe34cbfe0013cc93ac88a60d` of
`umicom-foundation/umicom-kernel`, containing process-owned file descriptors and
checked file services. The build-relevant `arch`, `cmake`, `include`, `kernel`,
`platform`, `programs` and `tests` trees were reconstructed and checked against
their exact Git tree identities. Root build files were checked as well. No claim
is made that a cumulative local copy of the older documentation is a separately
verified complete documentation tree.

| Existing file | Added lines | Removed or rewritten lines |
|---|---:|---:|
| `CMakeLists.txt` | 4 | 0 |
| `kernel/main.c` | 12 | 0 |
| `platform/qemu-riscv64/serial.c` | 27 | 0 |
| **Total** | **43** | **0** |

The comparison requires every original line, including every comment, to remain
unchanged and in order. There are thirteen new files. Nothing is superseded,
disabled, deleted or renamed. The implementation does not introduce an alias or
rename an established interface. The old VFS, RAMFS, process, file-service,
scheduler, allocation and Assembly implementations are unchanged.

The only platform change is a polling receive operation appended to `serial.c`.
It shares the established register-address helper. The public input declaration
lives in a new header rather than rewriting `platform.h`.

## Toolchain and limitations

Local tools: Linux x86-64, Clang/LLD 17, GCC 14.2.0, CMake 3.31.6 and Ninja 1.12.1.
The cross target remains `riscv64-unknown-elf`, RV64 integer lp64, through the
existing toolchain. Clang 17 uses CMake's C23 draft flag for this target.
Warnings remain errors and no hosted runtime is linked into the Kernel.

**RISC-V QEMU is not installed in this environment.** Neither the complete new
guest validation sequence nor an interactive UART session was executed locally.
The native process entry, CSR observations and timer are models. In particular,
there is no claimed Windows Clang 22 or Windows terminal-input pass.

Required local acceptance is therefore: build with the installed Windows tools,
run the existing CTests plus the new console test, then start the separate
console image and complete the manual file/program session from the guide.

## Cross-build evidence

Full Debug and Release builds compile and link the ordinary Kernel, separate
interactive console and existing nested-fault test image. All retain the
RISC-V ELF entry point `0x80200000`. The existing diagnostic and file-client
programs remain independently linked ELF inputs; the shell copies them into its
own RAMFS instead of replacing their source.

The first local Debug compilation revealed compiler-generated `memcpy`/`memset`
references from new aggregate initialisation/copy. Those were corrected before
delivery with bounded console-local byte operations. No warning suppression,
new libc or alteration of the old build flags was used. Debug and Release then
linked successfully. Repeating each completed build reports no work to do.

The finished source ZIP was applied to a separate baseline copy with spaces in
both source and build paths. That copy builds all three images and the native
console suite. Repeating the packaged build is also incremental with no work.
The interactive CMake target inherits dependencies before adding itself as a
normal-Kernel dependency, preventing a dependency cycle.

## Native console suite

**65/65 cases passed** in each of these configurations:

| Configuration | Instrumentation |
|---|---|
| Clang Debug | AddressSanitizer and UndefinedBehaviorSanitizer |
| Clang Release | AddressSanitizer and UndefinedBehaviorSanitizer |
| GCC Debug | AddressSanitizer and UndefinedBehaviorSanitizer |

There were no skipped cases in those runs. The same suite is rebuilt from the
finished overlay and rerun under Clang Debug with both sanitizers.

The first 59 tests link the actual editor, parser and shell command engine with
the actual process supervisor, scheduled-task owner, checked file services,
message service, VFS, RAMFS, object cache, physical allocator, mapper and loader.
The earlier scheduling test fixture supplies explicitly synthetic ELF bytes and
modelled privileged execution. The hardware model can issue a real FILE request
through the actual C trap dispatcher, then return so the existing deferred file
service completes it. The native allocation gate is unavailable during that
simulated trap; tests do not pass by permitting RAMFS allocation in the borrowed
user context.

Coverage includes:

* CR/LF framing, backspace, Delete, Ctrl-U/C, empty/maximum/overlong lines,
  unsupported controls, NUL, ESC, and length corruption.
* Complete-token quotes, empty quoted text, excess arguments, malformed grammar,
  no interpolation, unsigned parsing and numeric overflow.
* 100,000 deterministic input-byte operations and 10,000 bounded parser inputs.
  These are regression workloads, not exhaustive fuzzing or correctness proofs.
* Twenty-four available-allocation budgets during initialisation, cleanup of
  partial setup, rejection of reinitialised/copied owners and independent seeds.
* File round trips, quoted names, namespace refusals, destructive overwrite,
  safe binary display, typed removal, reclamation and shared file clients.
* A file snapshot passed through the actual loader, then staging-buffer scrubbing;
  malformed ELF rejection and allocation failure during loading.
* Foreground faults, budgets, Ctrl-C cancellation, type-ahead rejection and a
  subsequent successful run without restarting the console owner.
* Deferred file-client lifetime on exit, fault and cancellation; read-only versus
  explicit writable grants; grant refusal without accidentally running a task.
* Unsafe machine admission/return, reentrant command refusal, retained cleanup
  after an injected frame-release failure, orderly shutdown and retry.
* 1,000 successive file command lifetimes and 200 successive process lifetimes,
  checking physical-frame accounting rather than only terminal output.

The remaining six tests compile **the actual `serial.c`** against anonymous
host RAM mapped at the QEMU UART address. They verify idle/output-preservation,
all byte values, receive errors with and without data, invalid output arguments
and absence of register writes. They do not model destructive FIFO reads,
interrupt timing, serial transmission, terminal behavior or hardware error
acknowledgement. No host UART or other hardware device is opened.

A host that refuses the safe, non-replacing fixed mapping returns skip code 77
for these optional adapter tests. Such a skip must not be reported as a UART
pass. The local runs permitted the mapping and all six tests executed.

## Existing native regression projects

All fifteen projects passed with Clang Debug. Address/undefined sanitizers were
enabled where supported. Real alternate-stack thread/event cases use UBSan only,
as their existing host adapter does not implement ASan fibre notifications.
The interrupt suite similarly uses ASan/UBSan for 64 policy tests and UBSan for
its four alternate-stack scheduler tests.

| Existing project | Passed |
|---|---:|
| User memory and syscall boundary | 24/24 |
| Executable loading | 68/68 |
| Process registry | 49/49 |
| Message channels | 72/72 |
| Cooperative threads | 55/55 |
| Events and wait-any | 48/48 |
| Trap integrity | 70/70 |
| Interrupt ownership | 68/68 |
| User scheduling | 53/53 |
| Blocking IPC | 49/49 |
| Process supervision | 54/54 |
| Object caches | 65/65 |
| Mapped regions | 59/59 |
| VFS/RAMFS | 66/66 |
| Process file services | 54/54 |

The executable, registry and VFS projects were given the actual newly cross-built
`umicom-diagnostic.elf` where their optional byte-level loader check is available.
Those checks inspect/load real executable bytes; they do not execute RISC-V
instructions on the host.

## Current-image fixture refusal check

In an isolated source/build copy, QEMU discovery was explicitly set to
`/bin/false` to register tests without pretending an emulator exists. The complete
source built first. CTest metadata listed 24 tests and confirmed the new
`kernel.riscv64.console_shell` requires `kernel_current_image`. No registered test
executes `umicom-console.elf` as an indefinitely waiting process.

An intentional compile error was then appended only to that isolated copy of
`console_line.c`. Running the selected console CTest caused `kernel.build.current`
to fail and `kernel.riscv64.console_shell` to be **Not Run**. SHA-256 checks
confirmed that the previously linked ordinary, interactive and nested-fault ELFs
were unchanged. No `/bin/false` runtime result is counted as a guest pass.

The deliberate error and discovery override are absent from the delivered files.
The fixture mechanism's behavior is documented by
[CMake](https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html).

## New guest acceptance sequence — supplied, not executed locally

The normal image's new test drives real console calls without host stdin:

| Case | Required guest evidence |
|---|---|
| Editing and file commands | Backspace/CRLF editing, actual create/write/append/read contents and enumeration. |
| Refused input | An escape-contaminated line and malformed quotes cannot create a file; malformed executable admission fails. |
| File execution and fault recovery | A separately loaded diagnostic exits with identity + 40, then a deliberate read-only store faults with cause 15 and returns control. |
| Explicit file authority | The real file-client writer uses checked calls and terminal descriptor cleanup; the reader runs with read-only authority. |
| Foreground cancellation | A real non-yielding program is timer-interrupted and cancelled through Ctrl-C handling. |
| Orderly shutdown | File clients close before unmount; all temporary frames and borrowed machine controls return to baseline. |

Successful completion emits `UMICOM_KERNEL_CONSOLE_SHELL_READY`. The interactive
image then initialises a **different owner** and emits `UMICOM_KERNEL_CONSOLE_READY`
before waiting for input. Passing the scripted guest test does not substitute
for checking the actual terminal receive path.

## Release interpretation

This provides an interactive development front end, not a complete console-alpha
or general-purpose OS. The shell runs with trusted Kernel authority; `runrw`
grants broad rights in its private shared RAMFS. There is no login, persistent
storage, secure remote administration, user-process standard-stream terminal,
job control, working-directory implementation or terminal escape decoder.

Input polling intentionally consumes host CPU while idle. No UART interrupt is
enabled, so replacing the poll with WFI would not establish a usable wakeup path.
A later terminal/interrupt and normal-boot service design must address that
responsibility explicitly rather than declaring this loop production-ready.
