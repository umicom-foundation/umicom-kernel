# Umicom Kernel — Structured program arguments and launch context

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

## Why a launch needs more than one number

The existing `run` command passes one unsigned value to a program. That remains
useful for the diagnostic programs and is not changed. A real command also needs
separate arguments: an operation, an account name, a quantity, or a file path.
Converting everything into one command-line string would force every program to
invent its own quoting rules and could lose an intentionally empty argument.

This implementation copies a bounded argument vector and an explicitly supplied
environment into the new program's existing private stack pages. It prepares the
saved initial registers before the first user instruction. The existing loader,
page ownership, scheduler, Assembly entry, standard streams and file authority
remain the execution path. There is no alternative ELF loader or user syscall.

## New console commands, unchanged older commands

```text
exec /bin/umicom-launch-client.elf transfer "Account one" "" 250 approved
execrw /bin/umicom-launch-client.elf replacement
run /bin/umicom-diagnostic.elf 7
```

`exec` and `execrw` create a new foreground child. Despite the command name, this
is **not POSIX exec replacement** of the shell or the calling process. The shell
remains the trusted machine-mode controller and gets its prompt back after
collecting the child.

The executable path becomes `argv[0]`. Quotes surround a complete token. An empty
quoted token becomes a real zero-length argument, not a missing entry. Spaces
inside a quoted token remain part of it. Single and double quotes use the same
literal rules; there is no backslash escape, expansion, substitution, wildcard,
redirection, pipeline or variable interpolation.

The existing line editor still accepts at most 511 characters. The structured
commands permit sixteen arguments including the executable path: at most fifteen
after it. Ordinary commands still use their original four-token interface.

`exec` grants the same read/query/enumerate file ceiling as `run`. `execrw` grants
the same full shared-RAM-filesystem ceiling as `runrw`. Neither environment
strings nor argument names grant file, process, terminal or memory authority.

The shell supplies exactly these environment entries:

```text
LANG=C
UMICOM_CONSOLE=serial
```

They are explicit program data. They are not inherited from Windows, the build
host, another process or a previous launch. No current-directory or PATH-search
service is implied. The Kernel-side API can supply a different explicit list;
there is no shell environment-editing command in this implementation.

## One tokenizer, not two competing grammars

The earlier `UmicomKernelShellParse` body had storage for four tokens. Its full
33-line implementation and every comment remain verbatim in an explained
`#if 0` block. The public wrapper now delegates to `UmicomKernelShellTokenize`
with capacity four. The structured command path invokes the same tokenizer
with capacity seventeen: one command token plus the sixteen program arguments.

This is the only superseded existing logic. Retaining one active tokenizer
prevents future quoting fixes from changing `run` but not `exec`, or vice versa.
The tokenizer stages all output locally. A bad final quote or an excess token
therefore cannot publish a partially accepted argument vector.

Existing command admission checks run before structured command handling. An
invalid command cannot bypass an active foreground task or a closing shell.

## Kernel-side preparation and parent authority

The trusted caller describes each string with a pointer and a byte length:

```c
UmicomKernelLaunchString arguments[] = {
    { "/bin/umicom-launch-client.elf", 29U },
    { "transfer", 8U },
    { "Account one", 11U },
    { "", 0U }
};
```

Lengths exclude the NUL which the packer appends. These are readable, stable,
non-overlapping Kernel inputs, not unchecked pointers supplied by a user syscall.
A nonempty span needs a readable pointer. Empty ordinary arguments are permitted;
`argv[0]` must not be empty. Embedded NUL bytes are refused so the declared length
and the program's terminated string cannot disagree.

The layers are:

```text
trusted parent / guardian
       |
       v
UmicomKernelProcessSupervisorSetLaunch
       | existing parent authority and lifecycle checks
       v
UmicomKernelUserTaskSetLaunch
       | current task token, READY, no previous quantum, not already prepared
       v
UmicomKernelProgramLaunchPrepare
       | pack and preflight the owned user destination
       v
existing saved frame -> existing Assembly entry -> independent user program
```

The supervisor uses its established authority lookup. A sibling cannot prepare
another parent's child merely by possessing its token. The lower scheduler API
remains a trusted Kernel mechanism, not a new user-facing authority domain.

Preparation is allowed once and before any quantum. After committing structured
entry, the older numeric setter refuses to replace `argc`. Repeating preparation,
using a stale token, or preparing a paused/running task is refused. Old numeric
launches continue to receive their original `a0` value.

The shell temporarily borrows its local specification only during the existing
load-and-grant function. That pointer is cleared before the shell returns or the
child is dispatched. The task keeps copies of bytes, not a pointer into the
parser's stack. A refused preparation follows the existing cancellation and
collection path; it is not allowed to run with half-prepared arguments.

## Native entry contract

| Register | Value on entry |
|---|---|
| `a0` | Argument count, including `argv[0]` |
| `a1` | User virtual address of the NUL-terminated pointer array `argv` |
| `a2` | User virtual address of the NUL-terminated pointer array `envp` |
| `a3` | User virtual address of `UmicomProgramLaunchInfo` |
| `sp` | Sixteen-byte-aligned lower edge of the reserved launch block |

This is an Umicom process-entry contract. It is not Linux's initial process-stack
layout, an auxiliary vector, a hosted C runtime or a declaration of POSIX binary
compatibility. The small program entry shim calls a four-argument C function
using the ordinary RISC-V integer calling convention.

The [RISC-V ABI specification](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)
describes integer argument registers and sixteen-byte stack alignment. It does
not prescribe this Umicom-specific metadata record or command grammar.

## Stack layout and limits

| Limit | Value |
|---|---:|
| Arguments, including program name | 16 |
| Explicit environment entries | 8 |
| Bytes in one string, excluding its terminator | 255 |
| Combined text, including every terminator | 1,536 bytes |
| Reserved launch block | 2,048 bytes |

The currently loaded task stack has two 4 KiB pages. The upper 2 KiB contain the
launch block, leaving 6 KiB below the initial stack pointer for ordinary C stack
use. No additional physical page or page table is allocated by launch setup.

```text
existing exclusive stack top
         |
         | explicit zero padding
         | terminated copied text
         | envp pointers and zero sentinel
         | argv pointers and zero sentinel
         | 64-byte information record
         |
initial sp (2 KiB below the original top)
         |
         | C activation records grow down into the remaining stack
         v
existing stack base
```

Every published address refers to the receiving program's own user virtual
block. Fixed-width words avoid exporting compiler-dependent structure padding;
unused pointers, unused text and explicit padding are zeroed. The recognisable
cookie is a record type tag, not an authentication secret.

This block is writable application data. A program may modify its copies after
entry; the Kernel does not later trust those copies as rights, credentials or
permission checks. Stack exhaustion and deliberate pointer misuse remain faults
handled by the established execution boundary, not new launch-recovery policies.

## Validation before any user write

`UmicomKernelProgramLaunchPack` builds a detached value image. It checks counts,
span sizes, terminators, combined capacity and address arithmetic before copying
anything to the caller's output image. Rejected plans leave that output unchanged.

Environment names follow `[A-Za-z_][A-Za-z0-9_]*=VALUE`. An empty value is valid;
an empty name, missing equals sign or duplicate name is refused. Names are
case-sensitive. Values are literal bytes, not executable shell expressions.

`UmicomKernelProgramLaunchPrepare` preflights the complete destination against
the existing user-memory view and write permissions. It then uses the existing
64-byte checked-copy limit without enlarging that API. Only after copying the
whole block does it publish `sp` and the four argument registers. The entry PC,
privilege state and underlying image ownership remain with the scheduler.

The preflight argument depends on stable mappings and serial Kernel execution.
It is not an atomic rollback guarantee against hardware failure or arbitrary
machine-mode corruption occurring during the copies. No concurrent mapper,
other hart or asynchronous launch mutation is admitted by this contract.

## Lifetime, streams and cleanup

The bytes live in the task's existing stack allocation. Timer pauses, blocked
stream writes and file calls therefore retain the same argument vector. There
is no separate launch heap allocation to leak or free prematurely.

The existing process destructor scrubs the stack pages when the task is reaped.
A later task starts from its own cleared image. Launch preparation does not reset
syscall budgets, slice limits, identities or stream grants.

The demonstration buffers its output in existing stream-sized chunks. It prints
each argument and environment entry, validates both array sentinels, and checks
a start counter and a volatile local after the writes. That distinguishes a
continuation from restarting the program after output backpressure.

## Guest acceptance and normal workflow

The new main CTest is `kernel.riscv64.program_launch`. The normal diagnostic image
runs it before its final marker; the interactive image runs the same validation
before entering the prompt. The existing current-image fixture remains required.

```powershell
Set-Location "C:\umicom\umicom-kernel"
cmake --preset riscv64-clang-debug
cmake --build --preset riscv64-clang-debug --parallel 2
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

In the interactive image:

```text
exec /bin/umicom-launch-client.elf transfer "Account one" "" 250 approved
run /bin/umicom-diagnostic.elf 7
poweroff
```

The structured example reports `argc=6`, `argv[2]=<Account one>`, `argv[3]=<>`,
the two explicit environment entries and `launch-context=valid`, then exits zero.
The numeric diagnostic still returns its assigned identity plus forty.

## Optional native tests

On a Linux development host, build the separate native project without changing
the Windows-to-RISC-V workflow:

```text
cmake -S tests/program_launch -B build/native-program-launch -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DUMICOM_LAUNCH_SANITIZERS=ON
cmake --build build/native-program-launch --parallel 2
ctest --test-dir build/native-program-launch --output-on-failure --no-tests=error
```

This suite models privileged entry. A passed host test is not a guest execution
pass; see PROGRAM_LAUNCH_VALIDATION.md for the actual evidence and limitations.

## Deliberately not included

No in-place process replacement, hosted `main(argc, argv)` runtime, environment
inheritance, current-directory service, PATH search, shell expansion, credential
service or normal-boot System Manager is added. The next integration remains a
normal boot/service lifecycle distinct from cumulative diagnostics. The existing
recovery and diagnostic paths must remain available as that work proceeds.
