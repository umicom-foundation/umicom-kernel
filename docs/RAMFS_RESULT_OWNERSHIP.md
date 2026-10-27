# RAM filesystem results and ownership

A RAM filesystem stores named files in working memory. Umicom's RAMFS owns
the records that describe each file and the pages containing its bytes. A
result returned to a caller must occupy separate storage. Otherwise writing
a count can damage the very filesystem being measured or cleaned up.

This guide covers trusted Kernel callers. It does not make arbitrary
addresses safe to dereference, provide user-pointer validation or introduce
concurrent filesystem access.

## Understand the two operations

`UmicomKernelRamfsSnapshot` reports nodes, data pages, metadata pages, pins,
unlinked nodes and the namespace epoch. A pin is an active reference that
keeps a node alive. The epoch identifies namespace changes. A snapshot is a
set of measurements, not a backup of file contents.

`UmicomKernelRamfsReap` frees only unlinked, unpinned nodes. Unlink removes
the directory name; an open file can remain alive until its last handle
closes. Reap reports the number of completely reclaimed nodes.

Both functions require a non-null, correctly aligned result with a complete
non-overflowing address range. It must not overlap:

- Any part of the filesystem owner, including the embedded root.
- Any owned object-cache page, including padding and object guards.
- Any owned file-data page, including bytes beyond the file's current length.

The caller must supply actually valid Kernel storage and serialize calls
under the existing object-cache execution rules.

## Read the implementation

1. Open `include/umicom/kernel/ramfs.h` for the public types and contracts.
2. Read `UmicomRamfsOutputAdmission` in `kernel/ramfs_snapshot.inc`. It checks
   address spans and owner overlap before accessing the owner, validates
   filesystem state, then checks every owned metadata and data page.
3. Continue to `UmicomKernelRamfsSnapshot`. It prepares a local result, checks
   pin-count addition for overflow and copies the result only on success.
4. Read `UmicomKernelRamfsReap` in `kernel/ramfs.c`. It uses the same admission
   boundary before clearing the counter or releasing any page.
5. Read `tests/ramfs/snapshot_tests.c` and `tests/ramfs/reap_tests.c`. The
   deliberate aliases there are fault injection, not supported application use.

Snapshot output stays unchanged on failure. Reap admission failures also
leave its counter unchanged and release no pages. Filesystem validation can
still mark a corrupt owner as poisoned under its established corruption
policy; an unchanged-output guarantee does not imply a corrupted owner is
usable.

After Reap admission succeeds, a later release failure can leave partial
progress. The counter reports complete nodes, not individual pages. Addresses
of unreleased pages remain owned so a subsequent call can retry safely.

## Follow a concrete example

A file containing 4,200 bytes needs two 4,096-byte pages. The first holds
4,096 bytes; the second holds 104 visible bytes. All of both pages belong to
the filesystem, including the unused 3,992 bytes in the second page.

An independent local `UmicomKernelRamfsInfo` is appropriate output storage.
A pointer into either page is not. Even a result in the unused tail could
damage future writes or ownership guards.

If the file is unlinked while open, Reap leaves it alone. After its final
handle closes, Reap may release the two pages and node. If release of the
second page fails, the node is not yet counted as reclaimed; its remaining
page is remembered for a retry.

## Run the focused host regressions on Windows

These tests compile only the Kernel filesystem and allocator components.
They do not build Umicom Framework or the application suite.

1. Install the UCRT64 C compiler, CMake and Ninja described in
   [Windows setup](WINDOWS_BEGINNER_SETUP.md).
2. Open PowerShell. Use your Kernel checkout and a separate build directory.
3. Run each stage separately. Stop if a command reports an error. A failed
   native command does not automatically stop a PowerShell paste.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
New-Item -ItemType Directory -Force "C:\umicom\_local\logs\kernel-ownership"
cmake -S ".\tests\ramfs" -B ".\build\ramfs-ownership" -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DUMICOM_VFS_SANITIZERS=OFF 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-ownership\configure.log"
cmake --build ".\build\ramfs-ownership" --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-ownership\build.log"
ctest --test-dir ".\build\ramfs-ownership" --output-on-failure --no-tests=error --output-log "C:\umicom\_local\logs\kernel-ownership\tests.log"
```

Expected result: all registered tests pass. Read any failure's exact test
name and diagnostic before changing code. The default host configuration
does not register the optional real-ELF loader bridge. This absence is
reported at configuration; it is not evidence that the bridge passed.

If you already built the RISC-V diagnostic program, add the following
argument to the configure command to include that integration test:

```text
-DUMICOM_VFS_TEST_EXECUTABLE=C:/umicom/umicom-kernel/build/riscv64-clang-debug/programs/umicom-diagnostic.elf
```

The file must exist. Do not point the test at an unrelated executable.
Sanitizers are disabled in the commands above; a successful run does not
provide sanitizer evidence. On a supported toolchain, enable
`UMICOM_VFS_SANITIZERS` in a separate build directory and retain those logs
separately.

## What these checks establish

The host suite uses the real RAMFS, VFS, object cache and physical allocator
in an isolated page-aligned memory arena. Machine-state admission and
selected allocation/release failures are controlled test inputs. Rejected
aliases are checked against saved owner and arena bytes, followed by a file
readback. Release tests verify ownership and retry, not just a return code.

Guest tests and actual hardware exercise additional boundaries. Continue
with the [console guide](CONSOLE_LEARNING_GUIDE.md) and
[build identity guide](BUILD_IDENTITY.md). Keep the tested source identity
with every result.

## Troubleshooting

- **Configuration cannot find the ELF:** finish the diagnostic-program build
  or omit the optional loader-bridge argument. Do not count omitted coverage
  as a pass.
- **A refusal test changes file contents:** inspect the shared output admission
  path before adding exceptions in the test.
- **Reap reports zero after a release failure:** a node is counted only after
  all of its data pages and metadata record are reclaimed. Inspect remaining
  ownership before retrying.
- **A result pointer is rejected:** provide an independent aligned variable;
  do not cast filesystem storage to the result type.
- **The owner is poisoned:** preserve the evidence and inspect the failing
  invariant. Resetting its fields would hide the failure and risk losing
  ownership of allocated memory.

For a connected lesson with a memory diagram and exercises, use
`content/courses/umicom-kernel-from-scratch/lessons/12-ownership-and-cleanup.html`
in Bits2Banking.
