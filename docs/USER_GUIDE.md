# Umicom Kernel user guide

Umicom Kernel is an experimental native RISC-V kernel. It is different from
Umicom OS's desktop applications. This guide starts with a simulated machine:
QEMU runs the RISC-V instructions while Windows remains your normal operating
system. You are not replacing Windows or installing the Kernel on your computer.

## 1. Know which files you are using

A **repository** is a folder whose source history is recorded by Git. A **source
file** is text written by a developer. An **executable** contains instructions
prepared for a processor. Our RISC-V executable ends in `.elf`; it is not a
Windows `.exe` and cannot be launched by double-clicking it.

Keep the Kernel at `C:\umicom\umicom-kernel` for the commands below. If you use
another location, replace that prefix consistently. The build folder contains
generated files. Keep personal logs outside the product repository.

## 2. Prepare the tools

Follow [Windows beginner setup](WINDOWS_BEGINNER_SETUP.md) if Git, MSYS2 UCRT64,
Clang, LLD, CMake, Ninja or QEMU is missing. The compiler translates C and Assembly;
the linker combines compiled pieces; CMake prepares build rules; Ninja executes
them; CTest runs registered checks. QEMU supplies the simulated processor and
devices. These tools run on Windows; the Kernel does not depend on their host
runtime libraries.

Open PowerShell. Run:

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake --version
clang --version
ld.lld --version
ninja --version
qemu-system-riscv64 --version
```

Each command should print a version. If one is not recognized, fix the tools or
PATH before continuing. PATH is the list of folders searched for a command.
This change applies to the current terminal session.

## 3. Create a place for evidence

```powershell
New-Item -ItemType Directory -Force "C:\umicom\_local\logs\kernel-guide"
```

The directory is outside the product checkout. Keep logs with the source
revision and tool versions that produced them. An old successful log is not
evidence for new source. Use another log directory for a later run if you want
to retain this run unchanged.

## 4. Configure

```powershell
cmake --preset riscv64-clang-debug -DBUILD_TESTING=ON 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-guide\configure.log"
$LASTEXITCODE
```

Configure checks tools and writes build rules; it does not prove the Kernel
works. A zero exit code means the command succeeded. Stop after a nonzero exit
code and read the first diagnostic. PowerShell does not automatically stop a
pasted list of native commands after a failure.

Do not add `UMICOM_BUILD_NATIVE_TOOL`: this is a Framework/Applications option,
not a required Kernel option. Do not use Applications' preset name here.

## 5. Build

```powershell
cmake --build --preset riscv64-clang-debug --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-guide\build.log"
$LASTEXITCODE
```

Two parallel jobs limit how many compiler tasks run together. Ninja normally
rebuilds changed inputs and their dependants; a successful build need not
recompile every file. Stop on an error rather than running tests against an
unknown mixture of old and new executables.

## 6. Start with a focused check

```powershell
ctest --preset riscv64-clang-debug -R "^kernel\.riscv64\.(boot|native_contract_client|executable_loading)$" --output-on-failure --no-tests=error --output-log "C:\umicom\_local\logs\kernel-guide\focused-tests.log"
$LASTEXITCODE
```

CTest may also run required preparation fixtures. A fixture is a prerequisite
such as checking that the current image is built. Read the entire summary.
A skipped test is missing coverage, not a passing test. The native contract test
checks a separately loaded program using real RISC-V environment calls.

## 7. Run all registered Kernel checks

```powershell
ctest --preset riscv64-clang-debug --parallel 2 --output-on-failure --no-tests=error --output-log "C:\umicom\_local\logs\kernel-guide\tests.log"
$LASTEXITCODE
```

Some tests create synthetic disk files under the build directory. These are
test fixtures, not a request to attach your real disk. Do not replace their
paths with physical disks. A complete pass establishes this source/tool/QEMU
combination, not production readiness on all hardware.

## 8. See the diagnostic guest directly

After a successful build, run the same basic machine used by the boot tests:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" -machine "virt,aclint=off" -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" -display none -monitor none -serial stdio -m 128M -smp 1 -no-reboot 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-guide\diagnostic-run.log"
$LASTEXITCODE
```

This diagnostic image prints its checks and finishes. It is not an interactive
desktop. `-bios` supplies our machine-mode image, `-serial stdio` connects its
text console to the terminal, and `-m 128M` gives the guest 128 MiB of memory.
The explicit machine options match the supported test configuration.

For the separate interactive system image and storage commands, follow
[build and test](BUILD_AND_TEST.md) and the relevant storage guides. Do not
substitute an arbitrary disk into a write test.

## 9. Understand a failure before changing code

- **Command not found:** the host tool is missing or PATH does not name it.
- **Unknown preset:** check that the terminal is in the Kernel repository.
- **Compiler/linker error:** read the first source diagnostic in the build log.
  Testing cannot repair a build failure.
- **No tests found:** check configuration, BUILD_TESTING and QEMU discovery.
- **Timeout:** distinguish a deliberate negative guest case from CTest exceeding
  its deadline. Keep the entire log and the exact source identity.
- **UMICOM_KERNEL_FAIL:** retain the following reason and preceding case name.
  Do not remove the assertion just to obtain a green summary.
- **Antivirus removed an executable:** preserve its detection record and compare
  the expected build output. Do not disable protection or declare a false
  positive based only on a filename.

## 10. Learn from a small part first

[Native ABI discovery](NATIVE_ABI_DISCOVERY.md) describes the service vocabulary.
[Native program contracts](NATIVE_PROGRAM_CONTRACTS.md) explains the client.
[User memory boundaries](USER_MEMORY_BOUNDARIES.md) follows checked pointers.
[Build identity](BUILD_IDENTITY.md) ties evidence to source and executable
digests. A digest identifies bytes; it is not a signature or a trust decision.

In Bits2Banking, use Introduction to RISC-V, Learning RISC-V Assembly, Learning C
and Umicom Kernel from Scratch together. First predict one request on paper,
then compare that prediction with a focused test log.

For a complete RAM-file and native-program exercise, continue with
[Use the development console](CONSOLE_LEARNING_GUIDE.md). It includes serial
logging and a separate Windows host check for the real input path.
