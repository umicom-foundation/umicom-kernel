# Umicom Kernel wiki

This wiki is a route through the existing guides and real source. Umicom Kernel
is an experimental native kernel. You can study its code and run its named
emulator examples without installing it as your computer's operating system.

## Start with one complete journey

1. Read the [user guide](../USER_GUIDE.md). It explains source files, tools,
   build directories and the difference between configuration, compilation
   and testing.
2. Prepare the [Windows development environment](../WINDOWS_BEGINNER_SETUP.md)
   if you want to run the examples. Reading the wiki requires no installation.
3. Build the named RISC-V profile and keep each log. The
   [preset reference](../CMAKE_PRESET_REFERENCE.md) explains configuration names.
4. Follow [the console exercise](../CONSOLE_LEARNING_GUIDE.md): inspect the
   interface, create a RAM file, read it and run the supplied native program.
5. Finish with [build identity](../BUILD_IDENTITY.md). A passing log only
   describes the source, executable and environment that actually produced it.

A failed build is a reason to investigate that build log before testing. An old
executable can still exist after a failed compilation. Its presence alone does
not mean the new source is ready.

## Find a topic by the question you have

| Your question | Guide | What to look for |
| --- | --- | --- |
| What runs first? | [First boot](../FIRST_BOOT.md) and [normal startup](../NORMAL_STARTUP_AND_RECOVERY.md) | Entry, initialization and recovery choices |
| What is a processor allowed to do? | [User execution](../USER_EXECUTION.md) | Privilege boundaries and rejection |
| How does a program ask for a service? | [Native ABI discovery](../NATIVE_ABI_DISCOVERY.md) | Request numbers, registers and compatible replies |
| How does a program check its needs? | [Native program contracts](../NATIVE_PROGRAM_CONTRACTS.md) | Required features, copy limits and remaining calls |
| Who owns memory? | [Physical memory](../PHYSICAL_MEMORY.md) and [user-memory boundaries](../USER_MEMORY_BOUNDARIES.md) | Page ownership, complete spans and refusal |
| Why is an address not always a physical location? | [Virtual memory](../VIRTUAL_MEMORY.md) | Mappings, permissions and translation |
| How do programs communicate? | [Message channels](../MESSAGE_CHANNELS.md) and [blocking IPC](../BLOCKING_IPC.md) | Ownership, bounded queues and waiting |
| What happens when execution must stop? | [Process supervision](../PROCESS_SUPERVISION.md) | Return, fault and cleanup responsibility |
| How does a filename reach storage? | [VFS and RAMFS](../VFS_AND_RAMFS.md) | Names, handles and a memory-backed filesystem |
| Where should a filesystem result be stored? | [Result ownership](../RAMFS_RESULT_OWNERSHIP.md) | Independent storage, snapshots and cleanup retries |
| How much space is available? | [Allocation inspection](../FAT16_SPACE_GUIDE.md) | Clusters, bytes, read-only counts and their limits |
| What makes disk updates harder? | [Ordered commits](../FAT16_ORDERED_COMMITS.md) | Write ordering, refusal and recovery limits |
| How are devices identified? | [Hardware discovery](../HARDWARE_DISCOVERY.md) | Firmware observations and supported bindings |

VFS means virtual filesystem: the common interface for named files. RAMFS
stores files in memory; its contents disappear when that guest stops. IPC means
inter-process communication. ABI means application binary interface: a precise
agreement about requests, values and results.

## Read a real native program in order

Open these files in your local Kernel checkout:

1. `programs/diagnostic/entry.S` establishes the Assembly entry and call bridge.
2. `programs/diagnostic/main.c` checks initialized data, zero-filled memory,
   copy behavior and the selected diagnostic case.
3. `programs/support/native_contract.c` gathers and validates interface replies.
4. `include/umicom/user/native_contract.h` states caller ownership and local
   compatibility requirements.
5. `kernel/user_monitor.c` checks environment calls inside the privileged
   implementation.

Keep the distinction between the program and the Kernel. A callback in a host
test can return invented numbers; an actual environment call changes execution
privilege. Both tests are useful, but they establish different things.

## Choose a learning route

The Bits2Banking repository contains connected courses in
`content/courses/learning-c`, `learning-riscv`,
`learning-riscv-assembly` and `umicom-kernel-from-scratch`.
Begin with Introduction to Computers if files, memory or processors are new
terms. Each source-reading lesson points back to a real Umicom implementation.

You can draw a result, explain it aloud or write it down. Predict an outcome,
follow one function or instruction, then compare the result with the prediction.
Typing speed and prior specialist vocabulary are not prerequisites.

## Understand the current boundary

The console and named guest examples are development interfaces. They are not
a graphical desktop, a complete application runtime, a general-purpose
installer or a promise of physical-device support. A compiled feature bit
describes a route; it does not grant a program permission to use it.

The user-space Umicom Framework and applications are separate repositories.
Their future native adapter must honor the Kernel contract rather than link
the complete Framework into the Kernel.

## When something does not work

Keep the exact command, exit result, source revision and log. Identify whether
the problem happened during configuration, compilation, test launch or guest
execution. Follow the troubleshooting section in the user guide. Avoid
changing several unrelated inputs before recording what failed: that makes
the useful evidence harder to compare.
