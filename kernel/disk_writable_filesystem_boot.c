/* Umicom Kernel dedicated writable-VFS and process-file-services startup.
 * The writer and the fresh read-only reader execute in separate QEMU processes.
 * No normal boot automatically mounts or mutates a discovered disk.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/disk_writable_filesystem_validation.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/riscv64/trap.h"

#if (defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_TEST) + \
     defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_TEST) + \
     defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READ_ONLY_TEST)) != 1
#error "Select exactly one dedicated writable-filesystem qualification role"
#endif

_Noreturn void UmicomKernelDiskWritableFilesystemBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    UmicomKernelConsoleInitialize();
    UmicomRiscvTrapInstall();
    /* A user slice borrows and restores a parked machine timer. Dedicated
     * storage startup must establish that same entry contract as normal boot. */
    UmicomPlatformTimerDisable(0U);
#if defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=disk-writable-test");
#elif defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_TEST)
    UmicomKernelConsoleWriteLine("boot.mode=disk-writable-readback-test");
#else
    UmicomKernelConsoleWriteLine("boot.mode=disk-writable-read-only-test");
#endif
    const UmicomKernelBootMemoryStatus status = UmicomKernelBootMemoryInitialize(hart, deviceTree,
        UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end));
    if (status != UMICOM_BOOT_MEMORY_OK) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x96U); UmicomPlatformHalt();
    }
    UmicomKernelHardwareCapture(deviceTree);
#if defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_TEST)
    UmicomKernelDiskWritableFilesystemValidate();
#elif defined(UMICOM_KERNEL_DISK_WRITABLE_FILESYSTEM_READBACK_TEST)
    UmicomKernelDiskWritableFilesystemReadbackValidate();
#else
    UmicomKernelDiskWritableFilesystemReadOnlyValidate();
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {}
}
