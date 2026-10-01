# Windows beginner setup for Umicom Kernel K1

This guide starts from a Windows 11 computer with no assumption that the kernel
development tools are installed.

K1 needs:

1. Git — source-control commands.
2. GitHub CLI (`gh`) — repository authentication/management.
3. MSYS2 UCRT64 — a maintained Windows package environment.
4. Clang — C23 and Assembly compiler front end.
5. LLD — RISC-V ELF linker.
6. CMake — build configuration.
7. Ninja — build executor.
8. LLVM tools — ELF/symbol inspection.
9. QEMU — RISC-V virtual machine.

The kernel itself does **not** link to MSYS2 or any of those host libraries.
They are developer tools running on Windows.

## PATH versus Java CLASSPATH

Umicom Kernel K1 does not use Java.

`CLASSPATH` is therefore irrelevant.

Windows `PATH` is the list of directories PowerShell searches when you type an
executable name such as `cmake`, `clang` or `qemu-system-riscv64`.

The recommended tool directory is:

```text
C:\msys64\ucrt64\bin
```

For one PowerShell window you may place it first with:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

This does not permanently modify Windows. Closing that PowerShell window removes
the temporary change.

## Install WinGet prerequisites

On current Windows 11, WinGet is normally supplied by App Installer.

Check:

```powershell
winget --version
```

If PowerShell says `winget` is not recognized, install/update **App Installer**
from Microsoft Store before continuing.

## Install Git if missing

Check:

```powershell
git --version
```

If it is missing:

```powershell
winget install --id Git.Git --source winget
```

Close the complete terminal window and open a new PowerShell window after the
installer updates PATH.

Verify again:

```powershell
git --version
```

## Install GitHub CLI if missing

Check:

```powershell
gh --version
```

If it is missing:

```powershell
winget install --id GitHub.cli --source winget
```

Close the complete terminal window and open a new PowerShell window.

Verify:

```powershell
gh --version
```

Authenticate if required:

```powershell
gh auth login
```

Then verify:

```powershell
gh auth status
```

## Install MSYS2 if missing

Check:

```powershell
Test-Path "C:\msys64\ucrt64.exe"
```

If the result is `False`:

```powershell
winget install --id MSYS2.MSYS2 --source winget
```

Use the normal default installation directory:

```text
C:\msys64
```

When installation completes, open **MSYS2 UCRT64** from the Windows Start menu.

## Update MSYS2 before installing packages

These commands are entered inside the **MSYS2 UCRT64** terminal, not PowerShell.

First:

```text
pacman -Suy
```

Accept the normal update prompts.

If MSYS2 tells you that all MSYS2 terminals must close, allow it to close,
re-open **MSYS2 UCRT64**, and run the same command again:

```text
pacman -Suy
```

Repeat until it reports that there is nothing left to update.

## Install all K1 compiler/build/emulator packages

Still inside **MSYS2 UCRT64**, enter:

```text
pacman -S --needed mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-llvm mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qemu
```

Press Enter when pacman asks you to confirm the package transaction.

The QEMU package is large because it includes many machine emulators and runtime
dependencies. K1 specifically uses `qemu-system-riscv64.exe`.

Close the MSYS2 terminal after installation.

## Return to PowerShell and expose UCRT64 tools

Open a fresh PowerShell window.

Add the UCRT64 binary directory to the current PowerShell session:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

Verify which directory PowerShell will use:

```powershell
Get-Command clang.exe
Get-Command ld.lld.exe
Get-Command llvm-readobj.exe
Get-Command cmake.exe
Get-Command ninja.exe
Get-Command qemu-system-riscv64.exe
```

For the MSYS2 installation these should normally point under:

```text
C:\msys64\ucrt64\bin
```

## Verify versions

Run each command separately:

```powershell
clang --version
```

```powershell
ld.lld --version
```

```powershell
llvm-readobj --version
```

```powershell
cmake --version
```

```powershell
ninja --version
```

```powershell
qemu-system-riscv64 --version
```

A version number from every command means the basic host tools are visible.

## Verify the exact QEMU file

Run:

```powershell
Test-Path "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe"
```

Expected:

```text
True
```

Then:

```powershell
Get-Item "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe"
```

## Important: reconfigure after installing QEMU

CMake searches for QEMU while configuring the build.

If you configured K1 before QEMU existed, installing QEMU alone does not
retroactively add the CTest to the old build tree.

After installation either remove the generated K1 build directory or re-run
configuration. For the clearest beginner workflow:

```powershell
Set-Location "C:\umicom\umicom-kernel"
```

If this generated directory exists:

```powershell
Test-Path ".\build\riscv64-clang-debug"
```

and the result is `True`, remove only that generated build directory:

```powershell
Remove-Item -Recurse -Force ".\build\riscv64-clang-debug"
```

Then configure again:

```powershell
cmake --preset riscv64-clang-debug
```

Look for a successful compiler configuration and **do not** accept the message
that QEMU is missing.

## Why no additional target libraries are installed

K1 is a freestanding kernel.

It deliberately uses:

```text
-nostdlib
-ffreestanding
```

Therefore we do not install or link a RISC-V libc, Windows SDK runtime, GTK,
SQLite or Umicom Framework for the K1 image.

The host tools build the ELF; they do not become part of the kernel.


# Troubleshooting: QEMU opens but prints nothing

If the build succeeds, `llvm-readobj` reports a RISC-V ELF, `_start` is at
`0x80200000`, but QEMU prints absolutely nothing and never exits, first stop the
emulator with:

```text
Ctrl+C
```

A previous K1 command used:

```text
-bios none
-kernel .\build\riscv64-clang-debug\bin\umicom-kernel.elf
```

That command is wrong for this machine-mode K1 design.

Use this corrected command:

```powershell
qemu-system-riscv64.exe `
    -machine virt `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none `
    -monitor none `
    -serial stdio `
    -m 128M `
    -smp 1 `
    -no-reboot
```

K1 is loaded as firmware because its earliest Assembly runs in RISC-V machine
mode.  No OpenSBI or Linux kernel is required for this milestone.

After updating the K1 files, delete the generated build directory and configure
again so CTest records the corrected QEMU command:

```powershell
Remove-Item -Recurse -Force ".\build\riscv64-clang-debug"
```

```powershell
cmake --preset riscv64-clang-debug
```

```powershell
cmake --build --preset riscv64-clang-debug --parallel 2
```

Then inspect the registered test:

```powershell
ctest --preset riscv64-clang-debug -N -V
```

The printed QEMU command should contain:

```text
-bios <path-to-umicom-kernel.elf>
```

and must not contain the old combination:

```text
-bios none -kernel
```
