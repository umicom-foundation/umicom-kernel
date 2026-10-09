# Testing the writable disk console

Author: Sammy Hegab, Umicom Foundation. Licence: MIT.

The writable mount can be exercised interactively or by the host-side
PowerShell runner. The runner drives the normal firmware through its real
serial console and verifies a file after a second boot. It needs no Python
installation and does not depend on clipboard support inside QEMU.

## Understanding the two namespaces

The ordinary console filesystem and the writable disk have separate owners:

| Command | Namespace |
| --- | --- |
| ls, cat, mkdir, create, write, append | The existing RAM filesystem. |
| diskrwls, diskrwcat, diskmkdir, diskcreate, diskwrite, diskappend | The explicitly mounted writable FAT16 filesystem. |
| diskls, diskcat | The separate read-only FAT16 mount. |

Creating /WORK on the writable disk does not add it to the output of ls. Use
diskrwls / or diskrwls /WORK. There is no cd command; disk operations use an
absolute path such as /WORK/NOTE.TXT.

The disks command probes device identities without opening a mount. Its
raw-read guidance describes readsector and blockclose, rather than restricting
the separate filesystem commands.

## Mount arguments and failed commands

The writable mount takes three arguments:

    mountdiskrw SLOT PART YYYY-MM-DDTHH:MM:SS

SLOT is the device slot reported by disks. PART is the decimal primary
partition index, from 0 to 3. The supplied synthetic fixture uses partition 0.
The sample calendar is explicit test data; it is not obtained from a wall clock.

For the single-disk QEMU example:

    mountdiskrw 7 0 2044-02-29T23:58:56

The character @ is not a partition number. Invalid command arguments now
produce a diagnostic and corresponding usage before any mount is admitted.
They leave the mount available for a corrected command in the same boot.
After a mount attempt has actually been admitted, the existing single-use and
retained-cleanup rules still apply.

diskrw.state=unused means there is no admitted writable mount. A following
diskmkdir or diskcreate consequently returns bad-state. A not-found result
from diskrwcat after a successful mount means that path is absent; it is
expected before the file has been created.

The mounted provider's failure and acceptance contract is described in
[Writable FAT16 through native file services](WRITABLE_FAT16_VFS.md).
An invalid command is distinct from an accepted operation or a failed media
mutation; this console change does not change disk persistence semantics.

## Run the normal-console test from PowerShell

First configure and build the ordinary debug preset in the repository:

    cmake --preset riscv64-clang-debug
    cmake --build --preset riscv64-clang-debug --parallel 2

Then run:

    & ".\scripts\Test-UmicomKernelWritableConsole.ps1"

Default QEMU discovery includes the customary MSYS2 UCRT64 location. An
explicit executable and build directory can be selected:

    & ".\scripts\Test-UmicomKernelWritableConsole.ps1" -QemuPath "C:\msys64\ucrt64\bin\qemu-system-riscv64.exe" -BuildDirectory ".\build\riscv64-clang-debug"

The runner:

1. Checks the known source fixture and creates a fresh, unique disposable copy.
2. Boots the normal bin/umicom-system.elf with a writable modern VirtIO device.
3. Reads disks and selects the sole reported block-device slot.
4. Mounts partition 0, creates WORK and NOTE.TXT, writes hello, appends a space
   and world, and checks the file, directory and accepted-operation count.
5. Unmounts, powers off and waits for a successful QEMU exit.
6. Boots the same copy again, mounts and reads the 11-byte hello world file.
7. Checks that readback did not change the image and the source is unchanged.

It waits for each serial character's echo and each command's next prompt.
It does not paste a whole batch into a small UART buffer. Time and output
limits prevent a missing prompt or noisy guest from waiting indefinitely.
The listener is owned by the runner and bound to loopback on a system-selected
port. The runner stops only its own QEMU process on a failed run.

The disposable image and evidence are retained in a unique directory under
the build output. The runner prints their location. Repeating the test starts
with a new copy and does not erase an earlier run or the user's manual disk.
There is no option to supply an arbitrary existing disk as the writable medium.

The optional CheckSyntaxDiagnostics switch also checks the rejected @ argument,
the still-unused mount and successful corrected admission. Use it after building
the console diagnostics update:

    & ".\scripts\Test-UmicomKernelWritableConsole.ps1" -CheckSyntaxDiagnostics

Default testing works with earlier writable-VFS firmware too. Actual
qualification environments and executed cases are in the accompanying review;
compatibility intent alone is not Windows runtime evidence.

## Existing automatic process-file tests

The existing CTest group is independent of clipboard and PowerShell automation.
It runs native U-mode file-service operations and checks the complete disposable
image in a fresh read-only guest:

    ctest --preset riscv64-clang-debug --output-on-failure --no-tests=error -R "kernel.riscv64.disk_writable_filesystem"

This group includes its build and disposable-image fixtures. It already
participates in the full CTest preset. The new runner adds normal console
coverage; it does not replace those process tests.

## Interactive paste and reboot

Clipboard input comes from the host terminal. In Windows Terminal, the
documented default Paste bindings include Ctrl+Shift+V and Shift+Insert.
Try one complete command at a time and wait for the next umicom prompt.
Ctrl+C inside this console cancels the current input line; it is not Paste.

See [Microsoft's Windows Terminal action documentation](https://learn.microsoft.com/en-us/windows/terminal/customize-settings/actions#paste)
for host bindings. The runner uses QEMU's documented
[socket character backend](https://www.qemu.org/docs/master/system/qemu-manpage.html#character-device-options).

After an interactive create/write test, unmount and power off. Re-run the same
QEMU launch command with the same disk, mount it, and use diskrwcat.
Do not copy fixture.raw over that disk between boots: that restores the
original fixture and removes the evidence you are trying to read back.
