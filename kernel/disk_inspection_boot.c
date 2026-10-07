/* Umicom Kernel isolated partition-fixture startup.
 * The previous raw-sector test expects its original 128-sector pattern. This
 * separate image validates the FAT fixture without changing that older test or
 * making its success marker accept unrelated bytes. Essential boot setup is
 * reused; no hosted filesystem or interactive input is required.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/disk_console.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

_Noreturn void UmicomKernelDiskInspectionBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
    UmicomKernelConsoleWriteLine("boot.mode=disk-inspection-test");
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x86U); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
    UmicomKernelDiskInspectionValidate();
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Existing platform stop declarations do not imply noreturn. */
}
