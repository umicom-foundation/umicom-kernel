# Umicom Kernel — Console input and the interactive development shell

Author: Sammy Hegab, Umicom Foundation. MIT licence.

## What is now interactive

The separate `umicom-console.elf` image stays at an `umicom>` prompt. You can
create RAM-backed files, inspect their contents and run an executable stored
under `/bin` without rebuilding the Kernel between commands. When a foreground
program exits, faults or is cancelled, its terminal report is collected and the
prompt returns.

The ordinary `umicom-kernel.elf` remains the deterministic diagnostic image. It
runs the new scripted console checks alongside the existing checks, prints its
final marker and exits QEMU. CTest never waits for somebody to type into the
interactive image.

This is a **trusted machine-mode development console**, not a user-mode shell,
login service, complete System Manager or supported OS release. Whoever controls
the serial input controls this development session. There is no password,
credential service, persistent disk, host directory mount or network listener.
All console files disappear at poweroff.

## Two images, one implementation

| Image | Purpose | How it ends |
|---|---|---|
| `bin/umicom-kernel.elf` | Deterministic regression evidence, including scripted shell commands | Existing final marker and QEMU finisher |
| `bin/umicom-console.elf` | The same boot checks, then an interactive prompt | Successful `poweroff`, or a fatal Kernel/lifecycle error |

The interactive target is constructed after all build fragments have contributed
their sources. It inherits the same source-local feature definitions, include
paths and target flags. Only it receives `UMICOM_KERNEL_INTERACTIVE_CONSOLE`.
The existing main function gains an additional call under that definition; its
original final-marker, finisher and safety halt remain untouched.

The default `UMICOM_BUILD_INTERACTIVE_CONSOLE=ON` builds both images. A developer
who deliberately needs only the automatic diagnostic image can configure the
option off. No alternative preset, compiler or clean build directory is needed.

The normal current-image fixture also builds the interactive target as a
dependency. Thus an old console ELF is not silently considered current after a
failed normal incremental build. The fixture does not execute the interactive
image as an automated test.

## Responsibility boundaries

```text
serial receiver -> bounded line editor -> complete-token command parser
                                                |
                                                v
                                   trusted console command engine
                                      |                  |
                                      v                  v
                              existing VFS/RAMFS   existing process supervisor
                                      |                  |
                                      v                  v
                              existing allocators existing scheduler/user entry
                                                         |
                                                         v
                                               existing checked file services
```

`console_line.c` knows about bytes and tokens, not UART registers or page tables.
`console_shell.c` calls existing file and process APIs. It does not inspect RAMFS
page lists, parse ELF headers itself, or install another trap handler.
`console_runtime.c` supplies real serial input/output and bounds how much input
is drained before giving a foreground program another quantum.

The only addition to the platform implementation is
`UmicomPlatformConsoleTryReadByte()`. It reuses the established UART register
helper and leaves the original transmit and initialisation code unchanged.

## Receiving input without guessing at damaged commands

A receive poll returns one of four results: no byte available, a received byte,
a line error, or an invalid output argument. An idle or failed read leaves the
caller's output byte unchanged.

The UART line-status register distinguishes receive-ready from transmit-ready.
Overrun, parity, framing or break errors invalidate input. When an errored byte
is available, the adapter consumes it but does not publish it as a good character.
No UART register is written by the receive operation, and it does not change the
divisor or interrupt-enable policy.

The editor accepts at most **511 printable ASCII characters** before the
terminator. Backspace and Delete remove the previous character, Tab becomes one
space, Ctrl-U clears the current command and Ctrl-C cancels it. CR and LF submit
a line; a CR/LF pair submits only once.

Unsupported control bytes, NUL, ESC, high-bit bytes, overflow and reported input
loss cause the entire line to be discarded through the next Enter. Backspacing
cannot reconstruct missing bytes, so it cannot turn a damaged prefix into an
accepted command. Ctrl-U or Ctrl-C explicitly discards that input and starts a
fresh line.

There is no cursor-motion or history implementation. An arrow key normally
sends an escape sequence; this editor rejects that line rather than accidentally
interpreting its trailing bytes as a command. Type commands one at a time and
wait for the next prompt. Terminal bracketed-paste control sequences, if added
by a terminal, are rejected rather than executed.

The parser copies complete tokens into bounded Kernel storage before performing
an operation. Single or double quotes can surround an entire token to preserve
spaces. Quotes within an unquoted token and unmatched quotes are errors.
There are no escapes, variables, expansions, pipelines or multi-command syntax.
Characters such as `$` or `;` are not evaluated as shell operators.

## Commands

All filesystem paths are canonical absolute paths under the existing VFS rules.
There is no mutable current directory; `pwd` reports `/`.

| Command | Meaning |
|---|---|
| `help` | Show the supported grammar and important limitations. |
| `about` | Identify the trusted, RAM-only development console. |
| `status` | Read actual filesystem counts and the latest collected process result. |
| `mem` | Read actual physical-frame accounting. |
| `pwd` | Print `/`. |
| `ls [PATH]` | Enumerate the named directory; default is `/`. |
| `stat PATH` | Read node kind, identity and length. |
| `cat PATH` | Read and display file bytes safely. |
| `mkdir PATH` | Create a directory exclusively. |
| `create PATH` | Create an empty file exclusively. |
| `write PATH "TEXT"` | Truncate an existing file and write literal text. |
| `append PATH "TEXT"` | Append literal text to an existing file. |
| `rm PATH` | Remove a file's name, then reclaim eligible unlinked storage. |
| `rmdir PATH` | Remove an eligible empty directory. |
| `reap` | Retry eligible RAMFS reclamation. |
| `run PATH [UNSIGNED]` | Load a native file and grant read-only file operations. |
| `runrw PATH [UNSIGNED]` | Load it with explicit shared-filesystem write/namespace authority. |
| `poweroff` | Close owned resources in dependency order, then end the guest. |

`write` is not an atomic replacement operation. The old file is truncated first;
a later allocation failure may leave only an accepted prefix. The output reports
how many bytes were written. Text commands do not append an implicit newline.

`cat` escapes binary/control bytes as `\xHH`, except a file newline is rendered
as CR/LF. An ESC stored in a file therefore does not become a terminal command.
Directory names already obey the VFS's printable-ASCII restrictions.

The filesystem retains its existing limits: 64 nodes, 128 KiB per file, 128
file-data frames and separately accounted metadata-cache pages. The console
adds no persistence or different file-size policy.

## A foreground program is loaded from the file you name

Boot seeds a fresh private RAMFS with these directories and files:

```text
/bin/umicom-diagnostic.elf
/bin/umicom-file-client.elf
/notes/
/records/
/shared/report
/README
```

The two executable byte spans are the existing separately linked programs. They
are copied into RAMFS during console initialisation. The shell subsequently
reads the selected file through VFS, rather than invoking the embedded function
or skipping the file layer.

`run` opens a source descriptor, verifies a bounded file length, reads at most
128 KiB into its owned staging buffer, and closes that descriptor before spawning
the process. The original loader makes the process's private segment copies.
The staging bytes are then scrubbed; the running process borrows neither that
buffer nor the source file's physical pages.

The one optional argument is an unsigned decimal machine value. It defaults to
zero. It is **not** an `argc`/`argv` array or environment block. Signs, numeric
suffixes and integer overflow are refused before process creation.

The shell has one foreground process at a time. It runs through the existing
process-supervisor wrapper, with a 50,000-tick quantum and a 256-slice total
budget. User instructions can be timer-pre-empted; ordinary Kernel work remains
serial. The budget is not a wall-clock promise for all boot or filesystem work.

Ctrl-C requests cancellation through the existing supervisor, then collects the
stopped program before returning a prompt. Ordinary input while a foreground
program runs is discarded, not queued for a later command. A partial line typed
during execution is discarded through a subsequent Enter so its suffix cannot
become a different command after the program stops.

The console displays the collected identity, state, exit value or fault, slices
and call count. Programs do **not yet have console stdin/stdout/stderr endpoints**.
Printing a process's terminal result is not the same as providing a complete
terminal device or POSIX standard streams.

## File authority is an explicit choice

`run` grants only READ, QUERY and ENUMERATE on the shared console filesystem.
`runrw` grants the existing complete VFS rights ceiling. That permits namespace
and write operations throughout this RAMFS, including `/bin`; it is deliberately
not represented as a path-restricted sandbox.

These grants use the existing scheduled-task file service. A task that pauses
retains its descriptors. Exit, fault, budget exhaustion or cancellation closes
its client before its process memory is collected. The shell then asks RAMFS to
reclaim any unlinked nodes whose last descriptor has gone away.

The file-client demonstration expects `/records` and `/shared/report`, which the
console creates. Its writer mode uses argument zero and needs `runrw`; reader
mode uses argument one and succeeds with `run`. The report initially contains
`Umicom report` followed by a newline.

## Failure and shutdown ownership

A temporary descriptor is stored in the shell owner. A failed close does not
leave its only token on an abandoned C stack. A later command retries that close
before opening another temporary descriptor.

A foreground process is also retained until collection succeeds. If restoration
of the machine context cannot be verified, the interactive runtime stops with a
Kernel failure report instead of printing a prompt that implies it is safe to
perform more filesystem operations. It does not force-free the affected image.

On orderly poweroff, the owner requests process shutdown and collection, closes
the process-file service, closes its administrator client, unmounts VFS, and
closes RAMFS. Only after those steps succeed does the interactive function return
to the unchanged final-marker and finisher code.

A recoverable partial storage shutdown remains in `CLOSING`. The prompt becomes
`cleanup>` and accepts only the exact `poweroff` retry. New file operations are
not admitted while dependencies are being torn down. No live owner is reset to
make an error disappear.

## Build and use on the existing Windows environment

Normal incremental commands remain:

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

The new normal test is `kernel.riscv64.console_shell`. Its scripted input runs
inside the automatic image, not through host terminal stdin.

Start the **interactive** image using this separate command:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-console.elf" `
    -display none `
    -monitor none `
    -chardev "stdio,id=console,signal=off" `
    -serial "chardev:console" `
    -m 128M `
    -smp 1 `
    -no-reboot
```

QEMU documents that its stdio backend normally handles terminal signals, which
includes Control-C terminating QEMU. `signal=off` disables that host behavior so
Control-C can reach the guest. See the [official character-device options](https://www.qemu.org/docs/master/system/qemu-manpage.html#character-device-options).
This command does not start a monitor multiplexer or network console. Actual
Windows terminal behavior still requires the local acceptance run.

After the existing checks, expect:

```text
UMICOM_KERNEL_CONSOLE_READY
Umicom Kernel development console. Type help.
RAM-only data; no login, disk persistence or host filesystem access.
umicom>
```

The waiting prompt is intentional, not an emulator hang. Type one command at a
time; these commands are entered **inside Umicom Kernel**, not PowerShell:

```text
help
ls /
cat /README
create /notes/plan
write /notes/plan "Transfer reviewed"
append /notes/plan " and approved"
cat /notes/plan
run /bin/umicom-diagnostic.elf 7
run /bin/umicom-diagnostic.elf 2
runrw /bin/umicom-file-client.elf 0
run /bin/umicom-file-client.elf 1
status
mem
poweroff
```

The ordinary diagnostic with argument 7 returns its identity plus 40. Argument 2
is a deliberate read-only-store fault; its collection should return the prompt.
The absolute identity is not a fixed value after earlier runs.

After `poweroff`, back in PowerShell:

```powershell
$LASTEXITCODE
```

A successful shutdown is zero. Do not interpret a successful cross-build or
native host test as evidence that this interactive guest session has already run.

## Optional native developer tests

These Linux x86-64 tests are separate from the normal Windows cross-build:

```text
cmake -S tests/console_shell -B build/native-console -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_CONSOLE_SANITIZERS=ON
cmake --build build/native-console --parallel 2
ctest --test-dir build/native-console --output-on-failure --no-tests=error
```

They exercise actual C parser, shell, filesystem and process-lifetime policy.
Privileged execution and timer/CSR state are explicit models. The UART adapter
check maps ordinary anonymous memory, not a host device. The accompanying
validation document distinguishes those checks from required QEMU execution.

## What remains after this console

This front end creates the first practical interactive workflow, but the full
console-alpha gate also requires broader boot/service composition. Follow-on
work includes standard input/output/error services for programs, structured
arguments and environment, shell operation in a restricted user context, and a
normal System Manager path independent of always running every diagnostic.
Filesystem persistence, authenticated administration, installation and updates
remain later release gates. Existing recovery and OS/Framework ownership
decisions are not changed by this development console.
