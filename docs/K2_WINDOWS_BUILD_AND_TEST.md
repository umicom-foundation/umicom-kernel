# K2 Windows Build and Test — Complete Beginner Steps

This guide assumes a Windows 11 computer and does not assume the required
developer tools are already installed or visible through `PATH`.

The kernel itself remains freestanding C23/RISC-V Assembly.  The Windows tools
below are host-side development programs only.

## 1. Required tools

K2 needs:

1. Git — source control.
2. GitHub CLI (`gh`) — GitHub authentication/repository operations.
3. MSYS2 UCRT64 — maintained Windows package environment.
4. Clang — C23 and RISC-V Assembly front end.
5. LLD — RISC-V ELF linker.
6. LLVM tools — ELF/symbol inspection.
7. CMake — build configuration.
8. Ninja — native build executor.
9. QEMU — RISC-V virtual machine.

No Java `CLASSPATH` is required.

No Python runtime is required by this kernel build.

No PowerShell script is required.

## 2. Open PowerShell

Use an ordinary Windows PowerShell/PowerShell terminal.

Move to the repository only after the tools below have been checked.

## 3. Check WinGet

Run:

```powershell
winget --version
```

If it is not recognised, install/update **App Installer** from Microsoft Store,
then open a fresh PowerShell window.

## 4. Check/install Git

Check:

```powershell
git --version
```

If missing:

```powershell
winget install --id Git.Git --source winget
```

Close and reopen PowerShell, then verify:

```powershell
git --version
```

## 5. Check/install GitHub CLI

Check:

```powershell
gh --version
```

If missing:

```powershell
winget install --id GitHub.cli --source winget
```

Close/reopen PowerShell, then:

```powershell
gh --version
```

Check authentication:

```powershell
gh auth status
```

If not authenticated:

```powershell
gh auth login
```

Follow the interactive GitHub instructions, then repeat:

```powershell
gh auth status
```

## 6. Check/install MSYS2

Check:

```powershell
Test-Path "C:\msys64\ucrt64.exe"
```

Expected on an already prepared machine:

```text
True
```

If `False`:

```powershell
winget install --id MSYS2.MSYS2 --source winget
```

Use the normal default installation directory:

```text
C:\msys64
```

## 7. Update MSYS2

Open **MSYS2 UCRT64** from the Windows Start menu.

Inside that terminal, run:

```text
pacman -Suy
```

Accept the upgrade.

If MSYS2 asks to close the terminal, let it close, reopen **MSYS2 UCRT64** and
run:

```text
pacman -Suy
```

again until there are no pending upgrades.

## 8. Install the complete K2 host toolset

Still inside **MSYS2 UCRT64**, run:

```text
pacman -S --needed mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-llvm mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qemu
```

Confirm the transaction.

Close the MSYS2 terminal afterwards.

## 9. Open a fresh PowerShell and set this session PATH

Run:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

This modifies only the current PowerShell process.  It does not permanently
change Windows.

## 10. Verify every required executable

Run each command separately:

```powershell
Get-Command clang.exe
```

```powershell
Get-Command ld.lld.exe
```

```powershell
Get-Command llvm-readobj.exe
```

```powershell
Get-Command llvm-nm.exe
```

```powershell
Get-Command cmake.exe
```

```powershell
Get-Command ninja.exe
```

```powershell
Get-Command qemu-system-riscv64.exe
```

Then check versions:

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

Do not continue if any command is not recognised.

## 11. Enter the kernel repository

```powershell
Set-Location "C:\umicom\umicom-kernel"
```

Verify:

```powershell
Get-Location
```

and:

```powershell
git status
```

Before applying K2, the working tree should normally be clean.

## 12. Verify the K1 base commit

Run:

```powershell
git log --oneline --decorate -5
```

The K2 source delivery was prepared against:

```text
eb4e2db feat(kernel): boot first educational freestanding RISC-V kernel
```

If your repository contains additional commits that you made yourself after
that point, inspect them before overwriting any file.

## 13. After extracting the K2 ZIP, remove the generated old build tree

K2 changes the source list and CTest graph.  Start from a clean generated build
directory:

```powershell
Test-Path ".\build\riscv64-clang-debug"
```

If `True`:

```powershell
Remove-Item -Recurse -Force ".\build\riscv64-clang-debug"
```

This deletes generated build output only.

## 14. Configure

```powershell
cmake --preset riscv64-clang-debug
```

Expected important lines include Clang for C/ASM and a build directory under:

```text
C:\umicom\umicom-kernel\build\riscv64-clang-debug
```

If QEMU cannot be found, stop and repair PATH/install before continuing.

## 15. Build

```powershell
cmake --build `
    --preset riscv64-clang-debug `
    --parallel 2
```

Expected final stage:

```text
Linking C executable bin\umicom-kernel.elf
```

## 16. Inspect the ELF

```powershell
llvm-readobj `
    --file-headers `
    ".\build\riscv64-clang-debug\bin\umicom-kernel.elf"
```

Expected essentials:

```text
Format: elf64-littleriscv
Arch: riscv64
Entry: 0x80200000
```

Inspect symbols:

```powershell
llvm-nm `
    -n `
    ".\build\riscv64-clang-debug\bin\umicom-kernel.elf"
```

Look for at least:

```text
_start
UmiRiscvTrapInstall
UmiRiscvTrapEntry
UmiRiscvTrapDispatch
UmiKernelMain
UmiPlatformTimerRead
UmiPlatformTimerSetCompare
```

## 17. Inspect registered tests before executing them

```powershell
ctest `
    --preset riscv64-clang-debug `
    -N `
    -V
```

You should see both:

```text
kernel.k1.riscv64.qemu_boot
kernel.k2.riscv64.trap_timer
```

The QEMU machine argument should include:

```text
virt,aclint=off
```

and the ELF should be supplied through:

```text
-bios <path-to-umicom-kernel.elf>
```

## 18. Run K2 manually

```powershell
qemu-system-riscv64.exe `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none `
    -monitor none `
    -serial stdio `
    -m 128M `
    -smp 1 `
    -no-reboot
```

The machine should print the K1 boot evidence, then exception and timer evidence,
then terminate itself.

Immediately check:

```powershell
$LASTEXITCODE
```

Expected:

```text
0
```

## 19. Run all CTest acceptance tests

```powershell
ctest `
    --preset riscv64-clang-debug `
    --output-on-failure
```

Expected:

```text
kernel.k1.riscv64.qemu_boot
kernel.k2.riscv64.trap_timer
```

and:

```text
100% tests passed
```

Do not commit if either test fails.

## 20. Review source changes

```powershell
git status
```

```powershell
git diff --stat
```

```powershell
git diff
```

Pay particular attention to:

```text
arch/riscv64/trap.S
arch/riscv64/trap.c
include/umicom/kernel/riscv64/trap.h
kernel/main.c
platform/qemu-riscv64/timer.c
CMakeLists.txt
```

## 21. Stage

```powershell
git add -A
```

Inspect:

```powershell
git status
```

```powershell
git diff --cached --stat
```

```powershell
git diff --cached
```

## 22. Commit to main

```powershell
git commit -m "feat(kernel): add RISC-V trap and timer foundation"
```

## 23. Push

```powershell
git push
```

## 24. Final verification

```powershell
git status
```

Expected:

```text
nothing to commit, working tree clean
```

Then:

```powershell
git log --oneline --decorate -5
```
