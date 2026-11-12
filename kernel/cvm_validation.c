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
    /* Additional user selectors qualify the same traced runtime at O0 and O2.
     * Retain the former table for comparison; original result codes are stable. */
#if 0
    static const UmicomU64 expected[] = {42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U};
#endif
    /* Preserve the previous trace table; appended comparison selectors receive
     * the same user-mode, terminal-reentry and frame-restoration checks. */
#if 0
    static const UmicomU64 expected[] = {
        42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U,
        0xc005U, 0xc006U, 0xc007U, 0xc008U
    };
#endif
    /* Retain comparison-only expectations; profile discovery appends one
     * selector and inherits the same exit, reentry and frame-accounting checks. */
#if 0
    static const UmicomU64 expected[] = {
        42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U,
        0xc005U, 0xc006U, 0xc007U, 0xc008U,
        0xc009U, 0xc00aU, 0xc00bU, 0xc00cU
    };
#endif
    /* Keep earlier selector results; locals extend the same frame ownership checks. */
#if 0
    static const UmicomU64 expected[] = {
        42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U,
        0xc005U, 0xc006U, 0xc007U, 0xc008U,
        0xc009U, 0xc00aU, 0xc00bU, 0xc00cU, 0xc00dU
    };
#endif
    /* Branch selectors inherit real exit, reentry and frame accounting.
     * Keep the local-only table as historical contract evidence. */
#if 0
    static const UmicomU64 expected[] = {
        42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U,
        0xc005U, 0xc006U, 0xc007U, 0xc008U,
        0xc009U, 0xc00aU, 0xc00bU, 0xc00cU, 0xc00dU,
        0xc00eU, 0xc00fU, 0xc010U, 0xc011U, 0xc012U
    };
#endif
    static const UmicomU64 expected[] = {
        42U, 0xc001U, 0xc002U, 0xc003U, 0xc004U,
        0xc005U, 0xc006U, 0xc007U, 0xc008U,
        0xc009U, 0xc00aU, 0xc00bU, 0xc00cU, 0xc00dU,
        0xc00eU, 0xc00fU, 0xc010U, 0xc011U, 0xc012U,
        0xc013U, 0xc014U, 0xc015U, 0xc016U, 0xc017U, 0xc018U, 0xc019U
    };
    UmicomKernelConsoleWrite("cvm.optimization=");
    UmicomKernelConsoleWriteLine(mode);
    /* Derive the bound from the result contract so new selectors cannot be
     * omitted from frame-restoration and terminal-reentry qualification. */
#if 0
    for (UmicomSize selector = 0U; selector < 5U; ++selector) {
#endif
    for (UmicomSize selector = 0U; selector < sizeof expected / sizeof expected[0]; ++selector) {
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
