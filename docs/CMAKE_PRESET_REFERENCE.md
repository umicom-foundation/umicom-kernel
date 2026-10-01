# K1 CMake preset — field-by-field explanation

`CMakePresets.json` must remain valid JSON. JSON does not support ordinary
comments, so this document explains every field instead of inserting invalid
comment syntax into the preset file.

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

Records the oldest CMake version this preset file is intended to require.

## Configure preset

```json
"name": "riscv64-clang-debug"
```

This is the machine-readable preset name typed after `cmake --preset`.

```json
"displayName": "Umicom Kernel K1 - RISC-V 64 Clang Debug"
```

This is the human-readable label shown by tools that list presets.

```json
"generator": "Ninja"
```

CMake generates Ninja build rules instead of Visual Studio/MSBuild project
files. Ninja is small and works well for cross-compilation.

```json
"binaryDir": "${sourceDir}/build/riscv64-clang-debug"
```

All generated build files stay outside the source directories in a predictable
build tree.

```json
"toolchainFile": "${sourceDir}/cmake/toolchains/riscv64-clang.cmake"
```

Loads the file that tells CMake the output is freestanding RISC-V code produced
by Clang, not a normal Windows host application.

```json
"CMAKE_BUILD_TYPE": "Debug"
```

Build the teaching/development image with debug information rather than
aggressive release optimisation.

```json
"BUILD_TESTING": "ON"
```

Enable CTest registration.  If QEMU is installed, the real K1 boot test is
registered during configuration.

## Build preset

The build preset reuses the configure preset so this command is sufficient:

```powershell
cmake --build --preset riscv64-clang-debug --parallel 2
```

## Test preset

The test preset points at the same configured build tree and enables
`outputOnFailure`, so QEMU serial output is printed when a boot test fails.
