![Umicom](../assets/branding/umicom-logo.svg)

# Follow the normal-boot coordinator

A kernel has several kinds of startup checks. The normal system image prepares a usable session. A diagnostic image deliberately exercises subsystems. The automatic startup image checks the normal path and powers off before opening a shell. Choosing one image does not exercise the other two.

The coordinator in [normal_boot.c](../kernel/normal_boot.c) connects existing owners: boot memory, the hardware catalogue, one-shot startup jobs, the console shell and the platform shutdown operation. Its compiled jobs check launch arguments and environment handling. They are not continuously running storage or network services.

## Read the ownership sequence

1. Install the trap vector and park the timer before admitting work.
2. For the dedicated recovery image, check that the physical-memory owner is still uninitialised. Enter recovery without creating services.
3. For normal startup, validate and reserve boot memory. Save its allocation counters before starting jobs.
4. Interpret the embedded executable's linker symbols as target addresses. Require the end to follow the start before computing the byte count.
5. Admit the complete two-job plan. The session-environment job depends on the runtime-contract job. A retry is a new attempt, not permission to skip the dependency.
6. Step the existing controller until it leaves the starting phase. Close its owners and compare allocation counters with the saved snapshot.
7. Finish any partial job-output line. Print the retained job report and publish normal readiness only after the preceding checks succeed.
8. An automatic startup image powers off here. The interactive system image enters the existing shell and powers off after the shell returns.

A linker symbol is an address assigned by the linker. Two separately declared boundary symbols are not two elements of one ordinary C array. The Kernel uses its existing target-address conversion before subtracting the ordered values. An empty or reversed range selects recovery before service admission.

## Keep output ownership visible

Startup output arrives in bounded packets. A packet boundary need not be a line boundary: a program might write a word and then its newline in separate calls. The coordinator therefore keeps a line open across packets from the same job and stream.

A change from stdout to stderr, even within one job, ends the old partial line and introduces the new stream's prefix. Changing jobs does the same. Empty packets do not alter the current line. Non-printable bytes are escaped, so a job cannot issue a terminal-control command through this output path.

For example, three writes containing stdout `out`, stderr `err` and stdout `next\n` are presented as:

~~~text
[first] out
[first] stderr: err
[first] next
~~~

Before the Kernel prints recovery reasons or the final startup report, it finishes an open job line. Otherwise a partial payload could appear to own text such as `boot memory: reserved`. Finishing the line preserves its bytes and adds only the required console newline.

## Run the hosted coordinator checks

The [hosted fixture](../tests/boot_services/normal_boot_tests.c) compiles the actual coordinator. It substitutes explicit observations for machine operations, linked addresses, physical-memory snapshots and controller outcomes. The existing controller and memory suites still exercise their real implementations separately.

Use a native host C23 compiler for this project. The following PowerShell commands create a separate hosted test directory; stop if a command fails.

~~~powershell
Set-Location C:\umicom\umicom-kernel
cmake -S tests/boot_services -B build/host-boot-services -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-boot-services
ctest --test-dir build/host-boot-services -R "^kernel\.normal_boot\." --output-on-failure
~~~

There are 32 registered coordinator cases across four compiled profiles. They check normal readiness, admission failures, release-accounting failures, exact expected recovery selection, output boundaries and whether a successful interactive profile reaches the shell call. The shell itself is an observation in this fixture; no keyboard session is simulated as a passing guest session.

The checks are authored coverage. A reader must run them with the matching source and retain the actual result before reporting a pass. A successful hosted result does not establish Assembly startup, privileged instructions, real timer behaviour, user execution, UART I/O or a bootable physical medium.

## Qualify the actual images separately

Follow [Normal startup and recovery](NORMAL_STARTUP_AND_RECOVERY.md) for the RISC-V build and emulator commands. The normal startup, expected required-job failure and forced-recovery images each need their own result. Then use [Your first system session](FIRST_SYSTEM_SESSION.md) for actual input, file commands and shutdown.

Record the source revision and local changes, toolchain, image hash, emulator version, machine arguments, exit status and complete serial output. A dedicated negative test passes only when its exact expected cause is observed. An unrelated recovery entry must remain a failure.

If a hosted case fails, inspect its assertion and profile first. If the guest fails before the coordinator's begin marker, inspect firmware entry, linker placement and Assembly startup. If it reaches recovery, use the printed reason to identify the failed boundary. Preserve that evidence before changing the source or replacing an image.
