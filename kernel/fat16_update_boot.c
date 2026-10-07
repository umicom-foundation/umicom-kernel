/* Umicom Kernel dedicated existing-file update and fresh-readback startup.
 * Only these explicit images select the disposable FAT16 qualification disk.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_update.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

#if defined(UMICOM_KERNEL_FAT16_UPDATE_TEST) == defined(UMICOM_KERNEL_FAT16_UPDATE_READBACK_TEST)
#error "Select exactly one dedicated FAT16 update qualification image"
#endif

_Noreturn void UmicomKernelFat16UpdateBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
#ifdef UMICOM_KERNEL_FAT16_UPDATE_TEST
    UmicomKernelConsoleWriteLine("boot.mode=fat16-update-test");
#else
    UmicomKernelConsoleWriteLine("boot.mode=fat16-update-readback-test");
#endif
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x89U); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
#ifdef UMICOM_KERNEL_FAT16_UPDATE_TEST
    UmicomKernelFat16UpdateValidate();
#else
    UmicomKernelFat16UpdateReadbackValidate();
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Existing platform stop declarations do not imply noreturn. */
}
