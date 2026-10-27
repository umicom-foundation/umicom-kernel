# Keep Kernel build and test evidence together

A passing test describes the program that actually ran. If a source file or an ELF image changes afterwards, an old log cannot qualify the new bytes. An ELF file is the linked program image that QEMU loads; a SHA-256 digest is a compact identifier calculated from its bytes.

The `umicom-kernel-evidence` target builds the existing Kernel dependencies and writes `qualification/identity.json` inside the selected build directory. The manifest contains source and image digests, compiler identity, configured machine and ABI, and available Git/QEMU context. It includes unpublished files in the known source directories. Git HEAD alone does not identify a modified working tree.

## Configure and capture

From a prepared Windows checkout:

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
New-Item -ItemType Directory -Force "C:\umicom\_local\logs\kernel"
cmake --preset riscv64-clang-debug -DBUILD_TESTING=ON 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel\configure.log"
cmake --build --preset riscv64-clang-debug --target umicom-kernel-evidence umicom-boot-memory-contracts --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel\build.log"
ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error --parallel 2 --output-log "C:\umicom\_local\logs\kernel\tests.log"
```

Run one command at a time and stop on errors. Keep the manifest with those logs. The normal build also includes the evidence target. The current-image CTest fixture builds that target before dependent QEMU tests, so a failed build cannot silently qualify an older image.

The fixture is intended for an already built checkout. Its deadline catches an unexpectedly long incremental build; perform the full build command first on a fresh checkout.

## Compare evidence without replacing it

```powershell
cmake -DSOURCE=C:/umicom/umicom-kernel -DBINARY=C:/umicom/umicom-kernel/build/riscv64-clang-debug -DMODE=verify -P .\cmake\QualificationEvidence.cmake
```

Verification compares current source and ELF files with the captured manifest. It fails if a file changed, was added or disappeared, and does not rewrite evidence. Capture intentionally replaces the current manifest after a successful build; save a dated copy with historical logs before starting another development cycle.

## What the record proves

The record identifies the observed source tree and ELF set. It does not sign a release, certify a compiler, prove that sources stayed unchanged during compilation or establish that runtime tests passed. Those limits are explicit in `sourceStableDuringBuildVerified=false` and `runtimePassed=false`. Keep a stable source checkout during build/testing and interpret runtime logs separately.

The source inventory includes CMakeLists.txt, CMakePresets.json when present, and files below arch, cmake, include, kernel, platform, programs, scripts, tests and tools. Nested build directories are excluded. Documentation and private notes are not part of this code identity. The image inventory covers ELF files below bin, tests and contracts. A disk fixture or host tool outside those sets needs its own evidence.

The manifest is build output, not source for publication. It can contain local compiler and emulator paths, so review it before sharing.

## Explore the tests

Host evidence contracts use inert text as synthetic image bytes. They never execute that text. Cases change source, add/remove files, change an image, corrupt the record and attempt a failed capture. They check that stale input is rejected and prior evidence remains intact.

Real guest execution stays in the separate RISC-V QEMU tests. Start with [Windows setup](WINDOWS_BEGINNER_SETUP.md). Study [the boot-memory link contracts](../cmake/BootMemoryContracts.cmake) to understand why a successful hosted C test alone cannot prove that early-boot code links without a C runtime.

For a focused native boundary check, follow the [user-memory guide](USER_MEMORY_BOUNDARIES.md). It separates host copy contracts from actual guest privilege transitions.
