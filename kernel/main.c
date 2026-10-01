/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/main.c
 *
 * PURPOSE:
 *   Enter original portable C23 kernel code after the RISC-V bootstrap has
 *   established a valid stack and cleared BSS.
 *
 * EDUCATIONAL NOTE:
 *   K1 is intentionally small.  The goal is to prove the boundary between
 *   architecture bootstrap, generic kernel C and a machine adapter.  It does
 *   not pretend to provide memory management, processes, files or a desktop.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import stable text describing exactly which milestone/image is running. */
#include "umicom/kernel/build.h"

/* Import architecture-neutral early console helpers. */
#include "umicom/kernel/console.h"

/* Import the C entry-point declaration shared with boot.S. */
#include "umicom/kernel/kernel.h"

/* Import the small machine boundary used for successful finish/halt. */
#include "umicom/kernel/platform.h"

/* Write one "key=value" line without requiring printf().
 *
 * static keeps this helper private to this source file because no other module
 * needs to call it in K1. */
static void WriteKeyValue(const char *key, const char *value)
{
    /* Print the field name, for example "name". */
    UmiKernelConsoleWrite(key);

    /* Separate the field name from its value using a machine-readable '='. */
    UmiKernelConsoleWrite("=");

    /* Print the value and terminate the record with a new line. */
    UmiKernelConsoleWriteLine(value);
}

void UmiKernelMain(UmiU64 hartId, UmiAddress deviceTreeAddress)
{
    /* Initialize the machine's earliest console before printing any evidence. */
    UmiKernelConsoleInitialize();

    /* Mark the start of the deterministic K1 evidence block. */
    UmiKernelConsoleWriteLine("UMICOM_KERNEL_BEGIN");

    /* Identify the product that produced this serial output. */
    WriteKeyValue("name", UMICOM_KERNEL_NAME);

    /* Identify the source-level kernel version. */
    WriteKeyValue("version", UMICOM_KERNEL_VERSION);

    /* Identify the roadmap milestone this image is expected to prove. */
    WriteKeyValue("milestone", UMICOM_KERNEL_MILESTONE);

    /* State the CPU architecture selected by this build. */
    WriteKeyValue("arch", UMICOM_KERNEL_ARCHITECTURE);

    /* State the machine adapter selected by this build. */
    WriteKeyValue("machine", UMICOM_KERNEL_MACHINE);

    /* State a simple build identity without depending on generated scripts. */
    WriteKeyValue("build", UMICOM_KERNEL_BUILD_ID);

    /* Begin the record containing the actual boot hart identifier. */
    UmiKernelConsoleWrite("hart=");

    /* Convert the numeric hart identifier to decimal text. */
    UmiKernelConsoleWriteUnsigned(hartId);

    /* End the hart record. */
    UmiKernelConsoleWriteLine("");

    /* Begin the record containing QEMU's device-tree address. */
    UmiKernelConsoleWrite("dtb=");

    /* Print the raw address as a fixed-width hexadecimal value.
     *
     * Parsing the device tree is deliberately deferred to a later milestone. */
    UmiKernelConsoleWriteHex64((UmiU64)deviceTreeAddress);

    /* End the device-tree-address record. */
    UmiKernelConsoleWriteLine("");

    /* State exactly what K1 has proven before attempting to finish QEMU. */
    UmiKernelConsoleWriteLine("state=booted");

    /* Mark the end of the evidence block.
     *
     * CTest searches for this exact marker when a real emulator is available. */
    UmiKernelConsoleWriteLine("UMICOM_KERNEL_END");

    /* Ask the QEMU-specific adapter to terminate the virtual machine as a
     * successful smoke test.
     *
     * Generic kernel code does not know the MMIO address or finisher encoding. */
    UmiPlatformFinishSuccess();

    /* A correct QEMU finisher never returns.
     *
     * Retain an explicit safe halt so an unsupported/future machine cannot let
     * control fall into whatever bytes happen to follow this function. */
    UmiPlatformHalt();
}
