/* Umicom Kernel isolated read-only filesystem-provider qualification entry.
 * Reuse normal boot memory and hardware capture, with no change to the older
 * raw-sector or partition-inspection image's fixture and acceptance markers.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/disk_filesystem.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

_Noreturn void UmicomKernelDiskFilesystemBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
    UmicomKernelConsoleWriteLine("boot.mode=disk-filesystem-test");
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x87U); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
    UmicomKernelDiskFilesystemValidate();
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Platform halt declarations are not universally noreturn. */
}
