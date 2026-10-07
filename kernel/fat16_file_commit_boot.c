/* Umicom Kernel dedicated timestamped file-commit and interruption startup.
 * Every role receives an explicitly prepared disposable FAT16 image. Reader
 * roles start in separate QEMU processes with read-only device admission.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_file_commit.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

#if (defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST) + \
     defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_TEST) + \
     defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_REJECTED_TEST)) != 1
#error "Select exactly one dedicated FAT16 file-commit qualification image"
#endif

_Noreturn void UmicomKernelFat16FileCommitBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
#if defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-file-commit-test");
#elif defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-file-commit-readback-test");
#elif defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-file-commit-interrupted-test");
#else
    UmicomKernelConsoleWriteLine("boot.mode=fat16-file-commit-rejected-test");
#endif
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x8aU); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
#if defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_TEST)
    UmicomKernelFat16FileCommitValidate();
#elif defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_READBACK_TEST)
    UmicomKernelFat16FileCommitReadbackValidate();
#elif defined(UMICOM_KERNEL_FAT16_FILE_COMMIT_INTERRUPTED_TEST)
    UmicomKernelFat16FileCommitInterruptedValidate();
#else
    UmicomKernelFat16FileCommitRejectedValidate();
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Existing platform stop declarations do not imply noreturn. */
}
