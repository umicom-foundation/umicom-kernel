/* Umicom Kernel dedicated final-cluster append and interruption startup.
 * Every role receives an explicitly prepared disposable FAT16 image. Reader
 * roles start in separate QEMU processes with read-only device admission.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_file_append.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

#if (defined(UMICOM_KERNEL_FAT16_APPEND_TEST) + \
     defined(UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_APPEND_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 append qualification image"
#endif

_Noreturn void UmicomKernelFat16AppendBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
#if defined(UMICOM_KERNEL_FAT16_APPEND_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-append-test");
#elif defined(UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-append-readback-test");
#elif defined(UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-append-interrupted-test");
#else
    UmicomKernelConsoleWriteLine("boot.mode=fat16-append-rejected-test");
#endif
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
#if defined(UMICOM_KERNEL_FAT16_APPEND_TEST)
    UmicomKernelFat16AppendValidate();
#elif defined(UMICOM_KERNEL_FAT16_APPEND_READBACK_TEST)
    UmicomKernelFat16AppendReadbackValidate();
#elif defined(UMICOM_KERNEL_FAT16_APPEND_INTERRUPTED_TEST)
    UmicomKernelFat16AppendInterruptedValidate();
#else
    UmicomKernelFat16AppendRejectedValidate();
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Existing platform stop declarations do not imply noreturn. */
}
