/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/cvm_validation.c
 *
 * PURPOSE:
 *   Check isolated CVM user execution and restoration of owned frames.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/cvm_validation.h"
#include "umicom/kernel/process.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"

extern const UmicomU8 UmicomEmbeddedCvmO0Start[], UmicomEmbeddedCvmO0End[];
extern const UmicomU8 UmicomEmbeddedCvmO2Start[], UmicomEmbeddedCvmO2End[];
static UmicomKernelProcess cvmProcess;

static void Require(UmicomBoolean condition, const char *reason)
{
    if (condition == UMICOM_FALSE) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomKernelConsoleWrite("cvm.reason=");
        UmicomKernelConsoleWriteLine(reason);
        UmicomPlatformFinishFailure(191U);
        UmicomPlatformHalt();
    }
}
static void CheckImage(const UmicomU8 *begin, const UmicomU8 *end, const char *mode)
{
    const UmicomSize bytes = (UmicomSize)((UmicomAddress)end - (UmicomAddress)begin);
    static const UmicomU64 expected[] = {42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U};
    UmicomKernelConsoleWrite("cvm.optimization=");
    UmicomKernelConsoleWriteLine(mode);
    for (UmicomSize selector = 0U; selector < 5U; ++selector) {
        UmicomKernelPhysicalMemorySnapshot before;
        UmicomKernelPhysicalMemorySnapshot after;
        UmicomKernelExecutableStatus inspected = UMICOM_EXECUTABLE_INVALID_ARGUMENT;
        Require(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
            "baseline");
        Require(UmicomKernelProcessCreate(&cvmProcess, begin, bytes, 700U + selector,
            &inspected) == UMICOM_PROCESS_OK && inspected == UMICOM_EXECUTABLE_OK,
            "load");
        /* This is the real U-mode monitor. The arithmetic bytecode itself has no
         * system calls; the native envelope issues EXIT only after checking it. */
        Require(UmicomKernelProcessRun(&cvmProcess, selector, 1000000U) == UMICOM_PROCESS_OK,
            "run");
        Require(cvmProcess.state == UMICOM_PROCESS_EXITED &&
            cvmProcess.report.exitValue == expected[selector] &&
            cvmProcess.report.trapCause == 8U &&
            ((cvmProcess.report.trapStatus >> 11U) & 3U) == 0U &&
            cvmProcess.report.callCount == 1U && cvmProcess.report.rejectedCalls == 0U,
            "user-result");
        Require(UmicomKernelProcessRun(&cvmProcess, selector, 1000000U) == UMICOM_PROCESS_BAD_STATE,
            "terminal-reentry");
        Require(UmicomKernelProcessDestroy(&cvmProcess) == UMICOM_PROCESS_OK, "destroy");
        Require(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
            before.allocatedFrames == after.allocatedFrames &&
            before.freeFrames == after.freeFrames && before.reservedFrames == after.reservedFrames,
            "frame-leak");
        UmicomKernelConsoleWrite("cvm.case=");
        UmicomKernelConsoleWriteUnsigned(selector);
        UmicomKernelConsoleWriteLine(" complete");
    }
}
void UmicomKernelCvmValidate(void)
{
    CheckImage(UmicomEmbeddedCvmO0Start, UmicomEmbeddedCvmO0End, "O0");
    CheckImage(UmicomEmbeddedCvmO2Start, UmicomEmbeddedCvmO2End, "O2");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_CVM_READY");
}
