# the physical-memory foundation CMake preset — field-by-field explanation

`CMakePresets.json` must remain valid JSON. JSON does not support ordinary
comments, so this document explains every field rather than placing invalid
comment syntax in the JSON file.

## Top-level `version`

```json
"version": 6
```

Selects version 6 of the CMake Presets file format.

## `cmakeMinimumRequired`

```json
"cmakeMinimumRequired": {
  "major": 3,
  "minor": 24,
  "patch": 0
}
```

Records the oldest CMake release this preset file deliberately requires.

## Configure preset name

```json
"name": "riscv64-clang-debug"
```

This is the machine-readable name typed after:

```powershell
cmake --preset riscv64-clang-debug
```

the physical-memory foundation deliberately preserves the the first-boot and trap/timer preset name so existing developer muscle
memory and the incremental build directory do not change merely because the
Kernel milestone grows.

## Display name

```json
"displayName": "Umicom Kernel the physical-memory foundation - RISC-V 64 Clang Debug"
```

This is the human-readable description shown by tools that list presets.

## Generator

```json
"generator": "Ninja"
```

CMake generates Ninja build rules. Ninja is a native host build executor and
does not become part of the Umicom Kernel image.

## Binary directory

```json
"binaryDir": "${sourceDir}/build/riscv64-clang-debug"
```

Generated object files, CMake state, the ELF image and the linker map stay
under `build/` rather than mixing with source files.

The same build directory is intentionally reused between normal milestones.
You do not delete it unless there is a specific toolchain/cache/clean-room
qualification reason.

## Toolchain file

```json
"toolchainFile": "${sourceDir}/cmake/toolchains/riscv64-clang.cmake"
```

Loads the cross-compilation description that tells CMake:

- the target is not hosted Windows/Linux;
- the CPU is RISC-V 64;
- Clang emits `riscv64-unknown-elf`;
- LLD performs the final bare-metal ELF link.

## Build type

```json
"CMAKE_BUILD_TYPE": "Debug"
```

The Debug configuration favours teachability and debugger/symbol visibility over release
optimisation.

## Testing

```json
"BUILD_TESTING": "ON"
```

Allows CMake to register real QEMU acceptance tests when
`qemu-system-riscv64` is present.

the physical-memory foundation registers:

```text
kernel.riscv64.boot
kernel.riscv64.trap_timer
kernel.riscv64.physical_memory
```

The first protects the original the first-boot foundation boot path.

The second protects the the trap/timer foundation exception/timer path.

The third requires the physical-memory foundation RAM/Kernel/DTB reservation, allocation/release refusal
cases, accounting and invariant checks to reach `the physical-memory foundation_PHYSICAL_MEMORY_PASS`.

## Build preset

The build preset points back to the same configure preset, allowing incremental
Ninja builds:

```powershell
cmake --build --preset riscv64-clang-debug --parallel 2
```

## Test preset

The test preset uses the same generated tree and requests failure output so a
failed QEMU serial transcript is visible immediately:

```powershell
ctest --preset riscv64-clang-debug --output-on-failure
```
