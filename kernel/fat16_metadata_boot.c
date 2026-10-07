/* Umicom Kernel dedicated persisted FAT16 metadata startup.
 * All three roles attach media read-only. The committed and dirty roles run
 * in fresh processes after the existing file-commit producer has exited.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_metadata.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

#if (defined(UMICOM_KERNEL_FAT16_METADATA_TEST) + \
     defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST) + \
     defined(UMICOM_KERNEL_FAT16_METADATA_DIRTY_TEST)) != 1
#error "Select exactly one dedicated FAT16 metadata qualification image"
#endif

_Noreturn void UmicomKernelFat16MetadataBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
#if defined(UMICOM_KERNEL_FAT16_METADATA_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-metadata-test");
#elif defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=fat16-metadata-committed-test");
#else
    UmicomKernelConsoleWriteLine("boot.mode=fat16-metadata-dirty-test");
#endif
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x8bU); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
#if defined(UMICOM_KERNEL_FAT16_METADATA_TEST)
    UmicomKernelFat16MetadataValidate();
#elif defined(UMICOM_KERNEL_FAT16_METADATA_COMMITTED_TEST)
    UmicomKernelFat16MetadataCommittedValidate();
#else
    UmicomKernelFat16MetadataDirtyValidate();
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Existing platform stop declarations do not imply noreturn. */
}
