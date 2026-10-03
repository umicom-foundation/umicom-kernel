# Umicom Kernel — normal recurring build, test and Git workflow

This is the short workflow used after a developer has completed the one-time
Windows setup in `WINDOWS_BEGINNER_SETUP.md`.

Do not reinstall the toolchain for each update.

Do not delete the build directory for ordinary development.

CMake and Ninja are intentionally used incrementally.

## Configure after receiving an update

From the repository root:

```powershell
Set-Location "C:\umicom\umicom-kernel"
```

When an update changes `CMakeLists.txt`, adds source files, changes a preset or you
are unsure whether CMake's generated graph is current, run:

```powershell
cmake --preset riscv64-clang-debug
```

This reconfigures the existing build tree; it does not erase previously built
objects unnecessarily.

## Build incrementally

```powershell
cmake --build `
    --preset riscv64-clang-debug `
    --parallel 2
```

Ninja rebuilds only sources affected by the new source and then relinks when
needed.

## Run all registered acceptance tests

```powershell
ctest `
    --preset riscv64-clang-debug `
    --output-on-failure
```

Later capabilities keep earlier regression tests so regressions remain visible.

## Run the current QEMU image manually

Use the QEMU command below for the current RISC-V `virt` configuration:

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

Using the full QEMU path is valid even if the MSYS2 UCRT64 directory has not
been added permanently to the Windows user `PATH`.

## Review before committing

```powershell
git status
```

```powershell
git diff --stat
```

## Stage, commit and push to main

```powershell
git add -A
```

Use the descriptive commit message supplied with each delivery, then:

```powershell
git push
```

Finally:

```powershell
git status
```

The normal expected result is a clean `main` branch synchronized with
`origin/main`.

## When a clean build is actually justified

Deleting the build directory is exceptional, not routine.  It is appropriate
when, for example:

- the target compiler/toolchain changes materially;
- the CMake generator changes;
- the target architecture changes;
- the cache is proven stale/corrupt;
- a formal clean-room/release qualification explicitly requires it.

When a future update genuinely needs a clean build, its instructions will state
that reason explicitly.
