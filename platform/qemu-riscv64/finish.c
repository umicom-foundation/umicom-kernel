/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/finish.c
 *
 * PURPOSE:
 *   Terminate QEMU teaching runs through the `virt` machine's SiFive
 *   test-finisher MMIO device while keeping that machine-specific protocol out
 *   of generic kernel code.
 *
 * EDUCATIONAL OVERVIEW:
 *   QEMU maps a small test device at physical address 0x00100000.
 *
 *   A 32-bit write is interpreted as:
 *
 *     low 16 bits   = status
 *     high 16 bits  = process/test exit code
 *
 *   Status 0x5555 means PASS and status 0x3333 means FAIL.
 *
 *   This is intentionally a validation convenience rather than the future
 *   physical-machine shutdown interface.  Generic Kernel code asks the
 *   platform to finish; only this QEMU adapter knows the special MMIO protocol.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

/* Import platform declarations and fixed-width MMIO types. */
#include "umicom/kernel/platform.h"

/* QEMU `virt` maps the SiFive test-finisher at this physical address. */
#define UMICOM_QEMU_TEST_BASE ((UmicomAddress)0x00100000ULL)

/* Low 16-bit status accepted by QEMU as successful guest completion. */
#define UMICOM_QEMU_FINISHER_PASS ((UmicomU32)0x00005555U)

/* Low 16-bit status accepted by QEMU as failed guest completion. */
#define UMICOM_QEMU_FINISHER_FAIL ((UmicomU32)0x00003333U)

/* Only the high 16 bits are available for the bounded guest exit code. */
#define UMICOM_QEMU_FINISHER_CODE_MASK ((UmicomU32)0x0000ffffU)

/* Return the test-finisher MMIO register as a volatile pointer. */
static volatile UmicomU32 *TestFinisherRegister(void)
{
    /* Convert the machine-specific integer MMIO address to a device pointer.
     *
     * volatile prevents the compiler from removing/merging a store whose real
     * purpose is to cause an external device action. */
    return (volatile UmicomU32 *)UMICOM_QEMU_TEST_BASE;
}

void UmicomPlatformFinishSuccess(void)
{
    /* Obtain the machine test register. */
    volatile UmicomU32 *const finisher = TestFinisherRegister();

    /* A PASS status with zero high bits asks QEMU to shut down successfully. */
    *finisher = UMICOM_QEMU_FINISHER_PASS;

    /* QEMU should stop before execution continues.
     *
     * Preserve an explicit halt for a future machine/emulator that ignores the
     * finisher store rather than returning into unrelated kernel bytes. */
    UmicomPlatformHalt();
}

void UmicomPlatformFinishFailure(UmicomU32 code)
{
    /* Keep only the 16 bits that physically fit QEMU's high exit-code field. */
    const UmicomU32 boundedCode = code & UMICOM_QEMU_FINISHER_CODE_MASK;

    /* Move that bounded code into bits 31:16. */
    const UmicomU32 encodedCode = boundedCode << 16U;

    /* Combine the high exit code with QEMU's low FAIL status. */
    const UmicomU32 finisherValue = encodedCode | UMICOM_QEMU_FINISHER_FAIL;

    /* Obtain the same machine test register used by the success path. */
    volatile UmicomU32 *const finisher = TestFinisherRegister();

    /* Ask QEMU to terminate with a failed guest result. */
    *finisher = finisherValue;

    /* Never continue execution if the device did not terminate the emulator. */
    UmicomPlatformHalt();
}

void UmicomPlatformHalt(void)
{
    /* There is no caller to return to after a fatal kernel stop. */
    for (;;) {

        /* Wait For Interrupt reduces pointless execution while parked.
         *
         * A halt is not a scheduler sleep; no ordinary work is expected to
         * resume from this loop in the trap/timer foundation. */
        __asm__ volatile("wfi");
    }
}
