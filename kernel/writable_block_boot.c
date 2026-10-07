/* Umicom Kernel dedicated writable-transport and fresh readback startup.
 * Only the two explicit test images compile this entry. CTest attaches its
 * disposable raw copy; the source fixture is never the writable backend.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/writable_block_validation.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

#if defined(UMICOM_KERNEL_WRITABLE_BLOCK_TEST) == defined(UMICOM_KERNEL_BLOCK_WRITE_READBACK_TEST)
#error "Select exactly one dedicated writable-block qualification image"
#endif

_Noreturn void UmicomKernelWritableBlockBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
#ifdef UMICOM_KERNEL_WRITABLE_BLOCK_TEST
    UmicomKernelConsoleWriteLine("boot.mode=writable-block-test");
#else
    UmicomKernelConsoleWriteLine("boot.mode=block-write-readback-test");
#endif
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x88U); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
    UmicomKernelWritableBlockValidateExecution();
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* Existing platform stop declarations do not imply noreturn. */
}
