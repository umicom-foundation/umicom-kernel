/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/main.c
 *
 * PURPOSE:
 *   Enter original portable C23 kernel code after the RISC-V bootstrap has
 *   established a stack and cleared BSS. K1 proves the architecture boundary,
 *   deterministic serial evidence and clean QEMU completion only.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/build.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/kernel.h"
#include "umicom/kernel/platform.h"

static void WriteKeyValue(const char *key, const char *value)
{
    UmiKernelConsoleWrite(key);
    UmiKernelConsoleWrite("=");
    UmiKernelConsoleWriteLine(value);
}

void UmiKernelMain(UmiU64 hartId, UmiAddress deviceTreeAddress)
{
    UmiKernelConsoleInitialize();

    UmiKernelConsoleWriteLine("UMICOM_KERNEL_BEGIN");
    WriteKeyValue("name", UMICOM_KERNEL_NAME);
    WriteKeyValue("version", UMICOM_KERNEL_VERSION);
    WriteKeyValue("milestone", UMICOM_KERNEL_MILESTONE);
    WriteKeyValue("arch", UMICOM_KERNEL_ARCHITECTURE);
    WriteKeyValue("machine", UMICOM_KERNEL_MACHINE);
    WriteKeyValue("build", UMICOM_KERNEL_BUILD_ID);

    UmiKernelConsoleWrite("hart=");
    UmiKernelConsoleWriteUnsigned(hartId);
    UmiKernelConsoleWriteLine("");

    UmiKernelConsoleWrite("dtb=");
    UmiKernelConsoleWriteHex64((UmiU64)deviceTreeAddress);
    UmiKernelConsoleWriteLine("");

    UmiKernelConsoleWriteLine("state=booted");
    UmiKernelConsoleWriteLine("UMICOM_KERNEL_END");

    /* QEMU's test-finisher gives K1 a deterministic success exit instead of
     * requiring a host-side script to kill a machine that already completed. */
    UmiPlatformFinishSuccess();

    /* The finisher must not return. Retain a safe halt path if a future machine
     * implementation cannot terminate the emulator directly. */
    UmiPlatformHalt();
}
