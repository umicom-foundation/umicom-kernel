# Inspect disk allocation with Umicom Kernel

A file has a length, but its filesystem reserves storage in groups called
clusters. `fatspace` counts those groups in a read-only FAT16 image. It reports
free, allocated and bad clusters separately. It does not reserve space or
approve a repair.

## Understand the result

This profile reads 512-byte sectors. Each cluster contains one or more sectors.
The reported data-byte total excludes partition metadata, allocation tables,
the root directory and any incomplete trailing cluster. An allocated cluster
may contain file data, directory data or an orphan that no directory owns.

The query compares both complete FAT copies, including padding. It rejects
mismatches, invalid link values, dirty flags, unsuccessful reads and unsupported
geometry. It accepts at most 256 sectors per FAT. A bad-cluster marker is
counted separately and is not counted as free space.

A successful count does not check directory ownership, crosslinks, cycles,
file contents or physical device health. The mutation planner has stronger
ownership requirements. Never clear a dirty flag to make a query succeed.

## Prepare the environment

1. Follow [the Windows setup guide](WINDOWS_BEGINNER_SETUP.md) and
   [the Kernel user guide](USER_GUIDE.md). The host compiler builds tests that
   run on Windows; Clang also builds the separate RISC-V guest images.
2. Use the synthetic fixture already in
   `tests/disk_inspection/fixture.raw`. It contains no user's disk data.
3. Create the log directory once. Run each command separately and stop at an
   error. PowerShell does not automatically stop after a native command fails.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
New-Item -ItemType Directory -Force "C:\umicom\_local\logs\kernel-storage"
```

## Build and run the focused host tests

Configuration creates build rules. Compilation produces executables. CTest
then runs those executables. These tests use the real parser and block driver
with controlled, synthetic byte and register models.

```powershell
cmake -S ".\tests\fat16_space" -B ".\build\fat16-space" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-storage\space-configure.log"

cmake --build ".\build\fat16-space" --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-storage\space-build.log"

ctest --test-dir ".\build\fat16-space" --output-on-failure --no-tests=error --output-log "C:\umicom\_local\logs\kernel-storage\space-tests.log"
```

A successful run reports zero failed tests. The failure cases deliberately
reject bad input; a passing negative test means the rejection was correct.
These are host tests, not proof that a guest has booted.

## Build and test the guest

```powershell
cmake --preset riscv64-clang-debug -DBUILD_TESTING=ON 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-storage\guest-configure.log"

cmake --build --preset riscv64-clang-debug --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-storage\guest-build.log"

ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error --output-log "C:\umicom\_local\logs\kernel-storage\guest-tests.log"
```

The disk-inspection guest checks the same allocation query through a real
emulated device. The normal system console can also inspect the fixture.
Use `umicom-system.elf` here: `umicom-console.elf` first runs diagnostics whose
raw-block fixture is different from this partitioned image.

<!-- Earlier draft selected umicom-console.elf for this partitioned image.
The normal system image replaces that invocation; the diagnostic image and
its raw-block acceptance checks remain unchanged. -->

```powershell
& "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" -machine "virt,aclint=off" -bios ".\build\riscv64-clang-debug\bin\umicom-system.elf" -display none -monitor none -chardev "stdio,id=console,signal=off" -serial "chardev:console" -global virtio-mmio.force-legacy=false -drive "file=./tests/disk_inspection/fixture.raw,if=none,format=raw,id=lesson,readonly=on" -device "virtio-blk-device,drive=lesson,bus=virtio-mmio-bus.0" -m 128M -smp 1 -no-reboot
```

At the guest prompt, type one command at a time:

```text
disks
fatspace 0 0
fatstat 0 0 /FRAG.BIN
fatcat 0 0 /README.TXT
diskclose
poweroff
```

The device argument explicitly binds the fixture to `virtio-mmio-bus.0`.
The two zeros select device slot zero and primary partition zero. They do not
refer to Windows drive letters. Omitting the bus permits QEMU to choose a
different slot; an unpopulated slot reports `block=no-device`.

<!-- Implicit device placement formerly used virtio-blk-device,drive=lesson.
Explicit bus placement replaces that assumption while keeping the same disk
and read-only policy. -->

`disks` lists transport identities without mounting a filesystem. With this
command, slot zero should show `base=0x10001000`, `probe=ok` and `device-id=2`.
Other empty slots are not failed disks. If the chosen slot reports `no-device`,
check the QEMU bus argument before trying filesystem commands.

The fixture's capacity output includes:

```text
disk.space.total-clusters=12159 free-clusters=12153 allocated-clusters=6 bad-clusters=0
disk.space.cluster-bytes=512 data-bytes=6225408 free-bytes=6222336 allocated-bytes=3072 bad-bytes=0
```

The command emits capacity lines only after successful reads and device
cleanup. An error produces diagnostic status rather than an incomplete count.
The inspection command does not mount this image into its RAM filesystem.
To compare the separate read-only mount interface in the same guest, use:

```text
mountdisk 0 0
diskcat /README.TXT
unmountdisk
```

Run these before `poweroff`. Expect the synthetic README text and
`disk.read=ok`, then `disk.unmount=ok`. The RAM namespace remains separate.
An invalid coordinate or missing argument is refused and points you to
`help`; it must not consume the mount's admission. This mount supports one
lifetime per boot, so restart the guest to mount again after closing it.

## Capture a repeatable console session

The Windows serial fixture uses a loopback socket and drives one owned QEMU
process. It keeps the original RAM-only journey and adds a separate read-only
disk journey. This avoids depending on piped console-input timing. It does
not connect the guest to a network or expose a public listener.

```powershell
cmake -S ".\tests\console_session" -B ".\build\console-session" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DUMICOM_CONSOLE_IMAGE=C:/umicom/umicom-kernel/build/riscv64-clang-debug/bin/umicom-console.elf -DUMICOM_CONSOLE_SYSTEM_IMAGE=C:/umicom/umicom-kernel/build/riscv64-clang-debug/bin/umicom-system.elf 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-storage\serial-configure.log"

cmake --build ".\build\console-session" --parallel 2 2>&1 | Tee-Object "C:\umicom\_local\logs\kernel-storage\serial-build.log"

ctest --test-dir ".\build\console-session" --verbose --no-tests=error --output-log "C:\umicom\_local\logs\kernel-storage\serial-tests.log"
```

The read-only test checks command rejection, exact counts, subsequent metadata
and file reads, cleanup and orderly shutdown. A successful serial log contains
`UMICOM_KERNEL_SERIAL_SESSION_PASS`. Use a current console image; the host
fixture does not compile or establish the identity of an arbitrary supplied ELF.

## Read the implementation

- [Result contract](../include/umicom/kernel/fat16_space.h): caller storage,
  capacity categories and limits.
- [Allocation scan](../kernel/fat16_space.inc): bounded mirrored reads and
  complete-result publication.
- [Console adapter](../kernel/disk_console.c): the same guarded ownership
  lifecycle serves file metadata and allocation snapshots.
- [Parser tests](../tests/fat16_space/space_tests.c): every read failure,
  boundary entries, busy ownership and unchanged outputs.
- [Transport tests](../tests/fat16_space/transport_tests.c): read-only requests,
  clock failures, retained cleanup and recursive callbacks.

## Investigate a refusal

| Observation | Next step |
| --- | --- |
| Unknown command | Check that the current console image was rebuilt and selected |
| Invalid arguments | Use exactly `fatspace SLOT PART`; this command has no path argument |
| I/O error | Keep the block and close status; verify the synthetic image was attached |
| FAT mismatch or dirty volume | Preserve the image and inspect it separately; do not repair it during this lesson |
| Inspection limit | Check the documented geometry bound; do not raise it without qualification |
| Close failure | Preserve the log; the command retains ownership for cleanup rather than silently discarding it |

The connected Bits2Banking course explains these concepts through
`10-understanding-disk-space.html` and `11-testing-storage-boundaries.html`
inside `content/courses/umicom-kernel-from-scratch/lessons`.
