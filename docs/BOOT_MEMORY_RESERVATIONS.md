# Firmware reservations during normal boot

The normal native Kernel bootstrap protects RAM declared as reserved by the
firmware device tree before it makes free frames available. This is separate
from the Linux-based Umicom OS image and does not qualify another hardware port.

## Follow the ownership boundary

1. Read kernel/boot_memory.c. The platform's declared RAM bounds are checked
   before the existing minimum DTB inspector reads a physical pointer.
2. Follow UmicomKernelDeviceTreeOpenFirmwareCopy. It uses the existing bounded
   compatibility adapter and strict DeviceTreeOpen grammar. A private static
   buffer avoids placing a 128 KiB copy on the 64 KiB bootstrap stack.
3. Read UmicomKernelDeviceTreeLayoutFromReader. It copies layout and reservation
   observations into a small value. No firmware-provided address is dereferenced.
4. Follow UmicomKernelBootReservationsPlan. It includes low RAM through the
   Kernel image, the device-tree blob and firmware reservation entries. It clips
   entries to declared RAM, rounds outward to pages, sorts them and merges
   overlapping or adjacent page ranges.
5. Only after that preflight succeeds does the bootstrap initialize the existing
   physical-frame owner and apply the merged reservations.

Two byte-disjoint firmware entries can touch the same page. Merging after page
rounding prevents a false overlap failure in the allocator. Reservations wholly
outside the declared RAM do not own frames in that allocator. Arithmetic
overflow, invalid layout and unsupported capacity are rejected before it changes.

## Run the focused host checks

Use a 64-bit host and a C23 compiler. From the Kernel repository:

~~~powershell
cmake -S ".\tests\device_tree" -B ".\build\device-tree-host" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build ".\build\device-tree-host" --parallel 2
ctest --test-dir ".\build\device-tree-host" --output-on-failure --no-tests=error --output-log ".\build\device-tree-host\tests.log"
~~~

Stop on a failed command. The existing tests/boot_services suite also has
startup cases that use the real physical allocator with an aligned host buffer.
Its memory-firmware-reserved case allocates across the reserved location and
checks that the firmware page is never returned.

These are expected checks, not a claim that this source has already passed
compilation, emulation or hardware execution. Keep normal-system and recovery
QEMU qualification separate from host checks.

## Supported limits and remaining work

The layout projection follows the existing strict reader's 128 KiB blob and
32-entry firmware reservation limits. It does not create a second larger DTB
profile. UmicomKernelDeviceTreeInspect remains the range-only compatibility API.
UmicomKernelDeviceTreeLayoutRead uses the strict reader for caller-owned bytes;
normal boot uses the explicit firmware-copy adapter first.

This change applies the firmware reservation table in the normal bootstrap.
It does not apply /reserved-memory node bindings, reconfigure the fixed RAM
geometry, authenticate firmware, create a general hardware allocator or change
the separate cumulative diagnostic image's reservation exercise. Those are
separate integration and qualification tasks.

The original bootstrap implementation remains disabled beside its replacement
for source comparison. The original strict reader and hardware catalogue remain
the grammar and discovery owners.

Format reference: [Devicetree Specification, flattened format](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html).
