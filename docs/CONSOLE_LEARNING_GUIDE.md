# Use the Umicom Kernel development console

Author: Sammy Hegab, Umicom Foundation. MIT licence.

This exercise uses files held in the emulated machine's RAM. It does not attach
a host disk or make a public network service. Complete the setup and build in
[the user guide](USER_GUIDE.md) first.

## 1. Choose the correct image

An image is a file containing the program that QEMU loads into its emulated
machine. `umicom-kernel.elf` runs automatic diagnostics and exits.
`umicom-console.elf` runs the diagnostics and then waits for commands.
The normal system image has a different startup purpose; do not interchange
these files without reading their guides.

## 2. Start a terminal session and retain serial output

Run these commands in PowerShell. The log folder must exist before QEMU starts.

```powershell
Set-Location "C:\umicom\umicom-kernel"
New-Item -ItemType Directory -Force "C:\umicom\_local\logs\kernel-guide"
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" -machine "virt,aclint=off" -bios ".\build\riscv64-clang-debug\bin\umicom-console.elf" -display none -monitor none -chardev "stdio,id=console,signal=off,logfile=C:/umicom/_local/logs/kernel-guide/console-run.log,logappend=off" -serial "chardev:console" -m 128M -smp 1 -no-reboot
```

QEMU's serial logger records guest output while the terminal remains the input
device. Do not pipe a sequence of commands into this Windows stdio backend:
automated pipe delivery is not equivalent to terminal input. The native
socket-based check below exercises complete command delivery independently.
The interactive terminal itself still needs checking on your terminal/QEMU
combination.

After the diagnostics, expect `UMICOM_KERNEL_CONSOLE_READY` and `umicom>`.
Type commands one at a time. An arrow key sends a control sequence that the
current bounded editor deliberately rejects; it does not implement history
or cursor movement. Press Ctrl-U to discard a line and type it again.

## Inspect the native program interface

At the prompt, type `abi`. This read-only command uses the same metadata reader
as the user-program environment-call route. It prints the major/minor interface
version, machine word width, byte-order code, page size, copy limit, invocation
limit and compiled-service bit mask.

For the current RV64 contract, word width is 64, byte-order code 1 means
little-endian, pages contain 4096 bytes, a copy request is bounded to 64 bytes,
and the ordinary invocation budget is 32 calls. These are interface facts, not
measurements of free RAM or a promise that a particular program has permission.

Feature mask bits describe compiled message, blocking IPC, file, stream and
service-report routes. A route can be present while a particular caller has no
bound service or suitable handle. The command grants nothing. `abi extra` is
refused because this command takes no arguments.

## 3. Inspect and create one RAM file

At the guest's prompt, enter:

```text
pwd
create /notes/lesson.txt
write /notes/lesson.txt "Kernel services use explicit requests"
cat /notes/lesson.txt
stat /notes/lesson.txt
append /notes/lesson.txt "."
cat /notes/lesson.txt
```

These are Kernel console commands, not PowerShell commands.

1. `pwd` prints `/`, the root. The console uses absolute paths and has no
   mutable current directory.
2. `create` makes an empty file. An existing name is refused rather than replaced.
3. `write` truncates that existing file and writes the quoted text. This is
   deliberately not an atomic replacement. A later write failure can leave a prefix.
4. `cat` displays the content. Control bytes are escaped rather than executed.
5. `stat` reports the file's identity and length. The text above is 37 ASCII bytes.
6. `append` adds one byte at the end without erasing the earlier content.
7. The final display includes the period.

A byte is a unit of stored data. In this ASCII example each character occupies
one byte. That is not a general rule for every text encoding or character.
File identifiers are assigned at runtime; compare meaning and byte counts,
not an unrelated run's identifier.

## Read part of a file without flooding the terminal

After appending the period above, the file contains 38 bytes. Type:

```text
read /notes/lesson.txt 7 8
read /notes/lesson.txt 34 10
read /notes/lesson.txt 38 1
```

An **offset** is the number of bytes to skip before reading. The first byte has
offset zero. The first command displays `services`, then reports:

```text
file.read offset=7 requested=8 read=8 next=15 eof=no
```

The second command displays `sts.` and reports four bytes read, next offset 38
and `eof=yes`. EOF means end of file. Requesting ten bytes does not invent six
extra bytes. The last command reads zero bytes because its starting position
is already at EOF.

Each command accepts 0..4096 source bytes and an offset from zero through the
RAM file limit, currently 131072. Numbers are decimal, without signs or suffixes.
A zero-byte request performs no byte transfer. It can report `eof=no`: asking
for nothing before EOF is different from reaching the end.

The display escapes control bytes: a stored zero appears as `\x00`, taking four
display characters but counting as one file byte. Do not treat terminal output
as a byte-for-byte file export. The source file is unchanged; each command opens
and closes its own read-only descriptor. Another open descriptor keeps its
position. On a read error, any displayed prefix is reported before the error;
the command does not retry silently.

Use `read /notes/lesson.txt 0 4097` to check the budget refusal. The file remains
unchanged. `cat` still displays the supported whole RAM file; range reading is
an additional choice. These commands read the RAM namespace. Persistent disk
inspection uses its separately documented commands.

## 4. Run a separately loaded native program

```text
run /bin/umicom-diagnostic.elf 4
```

The console reads this ELF through its virtual filesystem and loads a separate
user program. The argument `4` selects native contract discovery. Expect an
exited process report with `calls=10`: identity, copy, seven contract queries
and exit. The diagnostic's exit value is its assigned identity plus 37.
It is a deliberately computed test result, not a conventional host process
success code.

The console grants read-only file rights for `run`. The separate `runrw`
command grants broader rights and is not needed for this exercise. Knowing a
service exists does not grant permission to use it.

## 5. End the session

```text
poweroff
```

Expect `RAM filesystem closed. Powering off.` followed by
`UMICOM_KERNEL_END`. QEMU then returns to PowerShell. The RAM file disappears;
restart and check that it is absent if you want to verify volatility.

Do not infer disk durability from this exercise. Disk writes and fault recovery
have separate fixtures and guides.

## 6. Repeat the serial journey automatically on Windows

The native C23 fixture launches only its own QEMU process, uses an ephemeral
loopback socket as the serial cable, verifies each response, and checks QEMU's
final exit. It attaches no guest network device or disk.

Build the main Kernel first so the named console image is current, then:

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S ".\tests\console_session" -B ".\build\console-session-host" -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DUMICOM_CONSOLE_IMAGE=C:/umicom/umicom-kernel/build/riscv64-clang-debug/bin/umicom-console.elf -DUMICOM_CONSOLE_QEMU=C:/msys64/ucrt64/bin/qemu-system-riscv64.exe 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-guide\console-configure.log"
$LASTEXITCODE
cmake --build ".\build\console-session-host" --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-guide\console-build.log"
$LASTEXITCODE
ctest --test-dir ".\build\console-session-host" --verbose --no-tests=error --output-log "C:\umicom\_local\logs\kernel-guide\console-tests.log"
$LASTEXITCODE
```

Run each command separately; continue only after a zero exit code.
The fixture currently uses Windows host APIs and accepts ASCII local paths,
including spaces. Other host platforms need a native transport adapter; a
Windows pass does not qualify those hosts.

## 7. Follow the implementation

- `platform/qemu-riscv64/serial.c` receives actual UART bytes.
- `kernel/console_line.c` accepts complete bounded lines and rejects damaged input.
- `kernel/console_shell.c` calls the existing filesystem and process services.
- `programs/diagnostic/main.c` runs inside a separately loaded ELF.
- `programs/support/native_contract.c` validates the monitor's replies.
- `tests/console_session/session_tests.c` drives the real serial boundary.

If a test stops, preserve its complete output and source identity. A timeout
before the first prompt, a malformed command, a refused file operation and an
unexpected process result are different failures. Do not replace the test with
a direct function call merely to bypass a broken transport.

## 8. Inspect a file as bytes

After creating the file in section 3, use `hex /notes/lesson.txt 7 8`.
The row starts at hexadecimal offset `00000007`, shows the byte pairs
`73 65 72 76 69 63 65 73`, and ends with `|services|`.
The summary uses decimal numbers:

```text
file.hex offset=7 requested=8 read=8 next=15 eof=no
```

Each row has at most sixteen bytes. Only printable ASCII appears in the
right-hand column; other byte values appear as dots. The pairs always preserve
the exact byte values. No file byte is emitted as a terminal control sequence.

Both `read` and `hex` accept decimal offsets and at most 4096 source bytes per
request. They open independent read descriptors, preserve file contents and
report any real prefix before a backend error. A successful end-of-file read
may return zero bytes. A failed read remains a failure even if a prefix was
displayed. Inspect the summary and the final error message together.

The renderer and shared read owner are in `kernel/console_file_range.inc`.
Host tests check all 256 byte values and split reads; the serial-session
fixture checks a live guest command. Neither constitutes physical-device
qualification.
