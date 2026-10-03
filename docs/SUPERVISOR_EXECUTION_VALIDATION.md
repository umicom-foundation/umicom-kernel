# Umicom Kernel — supervisor execution delivery and validation

## Source baseline

This overlay was prepared against the pushed commit:

```text
e99fe13934c08ca86fc28c53ffa8ea105256186f
fix(kernel): correct platform header and protect runtime test integrity
```

The supplied Windows transcript establishes that the six existing tests passed
and that the hardware-translation check returned its expected load/store values.
It is evidence for that baseline, not an emulator result for this new code.

The source used for the local build was checked against GitHub blob identities
for all 31 required existing source/header/build inputs. This avoids rebuilding
from an older overlay that happens to have a similar filename.

## Existing-line preservation audit

Every original line in each of the following files remains unchanged and in its
original order. A line-by-line comparison found insertions only: no replacement
or deletion operation. Existing headers, licences, comments, functions and
implementation statements remain intact.

| Existing file | Added lines | Removed or rewritten lines |
|---|---:|---:|
| `CMakeLists.txt` | 32 | 0 |
| `arch/riscv64/linker.ld` | 9 | 0 |
| `kernel/main.c` | 9 | 0 |

The integration adds source files and one CTest, isolates the new payload text
on complete executable pages, and adds a call after the existing hardware
translation check. No existing test is renamed or removed.

The other 28 existing build inputs are byte-identical to the baseline. In
particular, these files are not replaced by the ZIP:

```text
include/umicom/kernel/platform.h
include/umicom/kernel/riscv64/trap.h
arch/riscv64/trap.S
arch/riscv64/trap.c
arch/riscv64/mmu.S
kernel/physical_memory.c
kernel/virtual_memory.c
platform/qemu-riscv64/serial.c
platform/qemu-riscv64/timer.c
platform/qemu-riscv64/memory.c
platform/qemu-riscv64/finish.c
```

Nothing needs to be commented out: no existing implementation is superseded by
this experiment. The source adds a private transition rather than replacing the
normal machine trap handler. There are no new abbreviated project identifiers,
compatibility aliases, scripts, renamed files or deleted files.

## New files

```text
arch/riscv64/supervisor.S
include/umicom/kernel/riscv64/supervisor.h
kernel/supervisor_payload.c
kernel/supervisor_validation.c
docs/SUPERVISOR_EXECUTION.md
docs/SUPERVISOR_EXECUTION_VALIDATION.md
```

The archive contains these six new files plus the three complete modified files.
It is an overlay, not a separate repository checkout. No generated build files,
compiler binaries, emulator binaries or development scripts are included.

## Validation actually performed before delivery

| Check | Result and scope |
|---|---|
| Existing-line preservation | PASS: 50 added lines across three existing files; zero removed/rewritten lines |
| Full freestanding Debug cross-build | PASS: complete C and Assembly compilation and ELF link |
| Full freestanding Release cross-build | PASS: complete optimised C and Assembly compilation and ELF link |
| Incremental rebuild after no changes | PASS: Ninja reports no work to do |
| ELF entry | PASS: `_start` remains at `0x80200000` |
| Supervisor code isolation | PASS: entry, C payload, helpers and exact fault/completion labels are inside the isolated page range in both builds |
| Payload relocations | PASS: payload calls resolve to its mapped helper code; no ordinary Kernel rodata or hosted runtime call is needed |
| Undefined final ELF symbols | PASS: none in either build |
| C/Assembly layout | PASS: compiled offset and size assertions cover the request, report and machine-state snapshot |
| CTest registration | PASS: seven tests enumerated; the supervisor test requires the existing current-image fixture |
| Failed-build fixture experiment | PASS: deliberate compile error in a separate disposable copy fails the build fixture and leaves the supervisor test NOT RUN |
| New QEMU supervisor execution | **NOT RUN in the delivery environment: no RISC-V QEMU executable was available** |
| Windows/MSYS2 execution of this overlay | **NOT RUN before delivery; use the commands below on the established development machine** |

Local compilation used Clang/LLD 17.0.0, CMake 3.31.6 and Ninja 1.12.1 on Linux.
The project still requests C23; this Clang uses the `-std=c2x` spelling for that
language mode. This is not a claim that the local environment used the Windows
Clang version shown in the supplied transcript.

For the CTest graph/failure experiment, a never-run `/usr/bin/false` placeholder
occupied the emulator path only in a separate disposable configuration. It was
not an emulator and was never counted as a boot pass. That path is not present
in the delivered source or presets. The experiment proves fixture ordering and
failure propagation, not supervisor execution.

The existing CTests use readiness/failure expressions and timeouts. CMake's
`PASS_REGULAR_EXPRESSION` does not independently require a zero process exit
code. No existing property is changed here. Also check `$LASTEXITCODE` after the
manual QEMU run; a marker alone should not substitute for that check.

## Expected runtime result on the development machine

Successful validation should report seven passing CTests:

```text
kernel.build.current
kernel.riscv64.boot
kernel.riscv64.trap_timer
kernel.riscv64.physical_memory
kernel.riscv64.virtual_memory
kernel.riscv64.hardware_translation
kernel.riscv64.supervisor_execution
```

The new QEMU test runs the same cumulative Kernel image as the earlier tests.
It requires `UMICOM_KERNEL_SUPERVISOR_EXECUTION_READY`, which is printed only
when all five supervisor cases, machine-state restoration, the original trap
handler check and frame-accounting checks have succeeded.

This is an expected result for the user to verify, not a locally observed QEMU
pass. See `SUPERVISOR_EXECUTION.md` for the case-by-case transcript and limits.

## Merge and run

Use the same already installed toolchain and incremental build directory. No
reset, restore, clean, repository renaming or package installation is required.
Start from a clean working tree at the recorded baseline. Extract the overlay
or merge its complete files with Beyond Compare. A different baseline or local
source edits need a normal comparison before overwriting anything.

```powershell
Set-Location "C:\umicom\umicom-kernel"
git status
```

After merging, review the three existing files. Each should show additions only:

```powershell
git diff --numstat -- CMakeLists.txt arch/riscv64/linker.ld kernel/main.c
```

Configure, build and test as separate commands. Stop when any step fails.

```powershell
cmake --preset riscv64-clang-debug
```

```powershell
cmake --build --preset riscv64-clang-debug --parallel 2
```

```powershell
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error
```

Run the newly built image with the established QEMU path:

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" `
    -machine "virt,aclint=off" `
    -bios ".\build\riscv64-clang-debug\bin\umicom-kernel.elf" `
    -display none `
    -monitor none `
    -serial stdio `
    -m 128M `
    -smp 1 `
    -no-reboot
```

```powershell
$LASTEXITCODE
```

Expected exit: `0`. If a test times out or a failure marker is printed, keep the
complete output and do not treat the new capability as qualified.

Once the tests and manual run pass:

```powershell
git status
git add -A
git diff --cached --stat
git diff --cached --numstat
git commit -m "feat(kernel): validate supervisor execution and protection"
git push
git status
```

Git's LF/CRLF warnings describe text conversion under the existing Git settings.
This update does not change `.gitattributes`, Git configuration or the line-ending
policy. Do not renormalise the repository as part of this merge.

## References

- CTest fixture dependency behaviour:
  https://cmake.org/cmake/help/latest/prop_test/FIXTURES_REQUIRED.html
- CTest pass-expression and process-exit behaviour:
  https://cmake.org/cmake/help/latest/prop_test/PASS_REGULAR_EXPRESSION.html
- Git text and end-of-line attributes:
  https://git-scm.com/docs/gitattributes
