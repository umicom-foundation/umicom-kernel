# Your first Umicom Kernel session

This guide takes you from source files to a small running system in QEMU. You
will inspect the system, create a note in memory and shut down the guest. No
previous operating-system development experience is required. Follow
[Windows beginner setup](WINDOWS_BEGINNER_SETUP.md) first if the tools are missing.

## 1. Understand the two computers

Your **host** is the computer running Windows. A **guest** is the simulated
computer running inside QEMU. QEMU translates the guest's RISC-V instructions
so you can study them on a different processor. Closing the guest does not
replace Windows. RISC-V is an instruction-set architecture: an agreement about
the instructions and registers a processor exposes.

```text
Windows host
  PowerShell -> CMake -> Ninja -> Clang and LLD -> umicom-system.elf
  QEMU virtual RISC-V computer
    Umicom Kernel -> serial console -> files held in guest memory
```

The **compiler** translates C source into processor instructions. Assembly
provides selected low-level instructions directly. The **linker** combines
compiled pieces into an executable. An ELF file is one executable format; it
is not a CD image, a Windows application or a USB installer.

Umicom Kernel is the native RISC-V research kernel. Umicom OS also has a separate
Linux-based recovery image. Building one does not build or qualify the other.

## 2. Prepare one terminal and a log directory

Use PowerShell. The example checkout is `C:\umicom\umicom-kernel`; replace that
prefix if you keep source elsewhere. Logs use a new timestamp for each session.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$LogRoot = "C:\umicom\_local\logs\kernel-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force $LogRoot
git rev-parse HEAD | Tee-Object "$LogRoot\source-revision.txt"
git status --short --branch | Tee-Object "$LogRoot\source-status.txt"
cmake --version
clang --version
ld.lld --version
ninja --version
qemu-system-riscv64 --version
```

Each tool should print a version. The revision identifies the committed source;
the status lists local differences. Together they help explain a later failure.
Keep the source and logs; do not delete a build directory to hide an error.

## 3. Configure, build and test

Run each command separately. Check `$LASTEXITCODE` immediately after it. A
nonzero value means stop and inspect that log. PowerShell can otherwise continue
to the next pasted command after a compiler failure.

```powershell
cmake --preset riscv64-clang-debug -DBUILD_TESTING=ON 2>&1 | Tee-Object "$LogRoot\configure.log"
$LASTEXITCODE
cmake --build --preset riscv64-clang-debug --parallel 2 2>&1 | Tee-Object "$LogRoot\build.log"
$LASTEXITCODE
ctest --preset riscv64-clang-debug --parallel 2 --output-on-failure --no-tests=error --output-log "$LogRoot\tests.log"
$LASTEXITCODE
```

Configuration writes build instructions. Building produces executable files.
Tests compare behaviour against expectations. A skipped test is not a pass.
The preset selects the RISC-V cross-compiler settings; it is different from
the Windows Applications preset. A successful build is still not physical
hardware qualification.

## 4. Launch the interactive system

Keep this command connected directly to the terminal so keyboard input can
reach the guest. It attaches no host disk. The `logfile` property records the
guest's serial output separately from interactive input.

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
  -machine "virt,aclint=off" `
  -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" `
  -display none -monitor none `
  -chardev "stdio,id=console,signal=off,logfile=$LogRoot\console.log" `
  -serial "chardev:console" -m 128M -smp 1 -no-reboot
```

`-m 128M` gives the guest memory. `-smp 1` gives it one virtual processor.
`-bios` loads this project's machine-mode executable. The supported board is
QEMU `virt` with the stated timer configuration. The diagnostic
`umicom-kernel.elf` runs checks and exits; use `umicom-system.elf` for this session.
If QEMU reports an unsupported character-device property, check its version and
installed help; do not replace the diskless command with a physical-drive command.

## 5. Explore without changing a disk

At the guest prompt, type one line and press Enter:

```text
help boot
about
abi
mem
status
```

`help boot` explains this console's boundary. `abi` describes the compiled
interface used by native programs. An **ABI**, or application binary interface,
specifies how already-compiled code requests services. Merely displaying it
does not grant a program permission to use every service.

Now create a disposable note:

```text
help files
ls /notes
create /notes/first.txt
write /notes/first.txt "First note"
cat /notes/first.txt
append /notes/first.txt " and more"
cat /notes/first.txt
stat /notes/first.txt
hex /notes/first.txt 0 10
```

The second `cat` should show `First note and more`. The leading `/` is the
root of the guest filesystem, not the Windows drive. Quotation marks keep
several words in one argument. `write` replaces a file's contents; `append`
adds at the end. `hex` displays byte values so invisible data can be inspected.
These commands use RAM: the note disappears when the guest stops.

Backspace edits an accepted line. Ctrl-U clears it and Ctrl-C cancels it. If
the console rejects a damaged or overlong line, retype the complete command;
the rejected prefix is deliberately never executed.

## 6. Discover programs and leave the guest

```text
help programs
ls /bin
status
poweroff
```

The image determines which programs appear in `/bin`. Read the program's guide
before running it. `run PATH` grants read-only shared-file access; `runrw PATH`
grants writes explicitly. An Umicom C `.umcb` bytecode file requires a compatible
interpreter and is not accepted as a native ELF simply by renaming it.

`poweroff` closes this guest. Expect control to return to PowerShell. Keep the
serial log alongside the build logs if the guest does not reach its prompt.

## 7. Accounts, installation, CDs and USB drives

This console has no user database, password login or installer. It is a trusted
development console. Creating `/notes/name` does not create a user account.
Do not type account passwords into it.

There is currently no qualified bootable CD ISO or USB installer for this native
Kernel. Copying an ELF onto a USB drive, renaming it `.iso`, or burning it as a
normal file does not supply the firmware bootloader, partitions and drivers a
physical computer needs. The current native target is RISC-V QEMU, not an
arbitrary x86 or ARM computer. Continue with QEMU while those ports are developed.

Before physical-media instructions can be supplied, a release must name the
supported machine/firmware, provide an actual boot image and digest, document
its partition layout, pass boot/recovery checks and state whether it writes
to disks. A later image-writing guide must identify the destination USB device
and its capacity before any destructive operation. None of the commands here
write a CD or USB drive.

## 8. Follow the implementation

| File | Responsibility |
| --- | --- |
| `kernel/console_line.c` | Collect and validate a complete line |
| `kernel/console_shell.c` | Interpret commands and present focused help |
| `kernel/console_file_range.inc` | Read bounded text and hexadecimal ranges |
| `kernel/ramfs.c` | Keep files in guest memory |
| `include/umicom/kernel/console_shell.h` | Public console types and ownership rules |
| `tests/console_shell/console_shell_tests.c` | Exercise real services with a host hardware model |

Start by finding `UmicomShellTopicHelp`, then follow the `cat` command to its
reader. A host model replaces processor/device behaviour for a focused test;
it does not prove real RISC-V boot. For the wider architecture, read
[the existing user guide](USER_GUIDE.md) and [boot targets](BOOT_TARGETS_GUIDE.md).

**Exercise:** after appending text, issue `write /notes/first.txt "Revised"`.
Predict the next `cat`. **Answer:** only `Revised`; the old contents were replaced.
The source branch for `write` and `append` explains why the results differ.
