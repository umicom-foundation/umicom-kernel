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
 *   Status 0x5555 means PASS.
 *   Status 0x3333 means FAIL.
 *
 *   This mechanism is only a QEMU test convenience.  A physical Umicom machine
 *   will eventually use proper power/reboot/platform mechanisms instead.
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
#define UMI_QEMU_TEST_BASE ((UmiAddress)0x00100000ULL)

/* Low 16-bit status accepted by QEMU as a successful guest test completion. */
#define UMI_QEMU_FINISHER_PASS ((UmiU32)0x00005555U)

/* Low 16-bit status accepted by QEMU as a failed guest test completion. */
#define UMI_QEMU_FINISHER_FAIL ((UmiU32)0x00003333U)

/* Only 16 high bits are available for the test exit code. */
#define UMI_QEMU_FINISHER_CODE_MASK ((UmiU32)0x0000ffffU)

/* Return a volatile pointer to the test-finisher register. */
static volatile UmiU32 *TestFinisherRegister(void)
{
    /* Convert the machine-specific integer MMIO address to a device pointer.
     *
     * volatile prevents the compiler from removing/merging a store whose real
     * purpose is to cause an external device action. */
    return (volatile UmiU32 *)UMI_QEMU_TEST_BASE;
}

void UmiPlatformFinishSuccess(void)
{
    /* Obtain the machine test register. */
    volatile UmiU32 *const finisher = TestFinisherRegister();

    /* A PASS status with zero high bits asks QEMU to shut down successfully. */
    *finisher = UMI_QEMU_FINISHER_PASS;

    /* QEMU should stop before execution continues.
     *
     * Preserve an explicit halt for a future machine/emulator that ignores the
     * finisher store rather than returning into unrelated kernel bytes. */
    UmiPlatformHalt();
}

void UmiPlatformFinishFailure(UmiU32 code)
{
    /* Keep only the 16 bits that physically fit QEMU's high exit-code field. */
    const UmiU32 boundedCode = code & UMI_QEMU_FINISHER_CODE_MASK;

    /* Move that bounded code into bits 31:16. */
    const UmiU32 encodedCode = boundedCode << 16U;

    /* Combine the high exit code with QEMU's low FAIL status. */
    const UmiU32 finisherValue = encodedCode | UMI_QEMU_FINISHER_FAIL;

    /* Obtain the same machine test register used by the success path. */
    volatile UmiU32 *const finisher = TestFinisherRegister();

    /* Ask QEMU to terminate with a failed guest result. */
    *finisher = finisherValue;

    /* Never continue execution if the device did not terminate the emulator. */
    UmiPlatformHalt();
}

void UmiPlatformHalt(void)
{
    /* There is no caller to return to after a fatal kernel stop. */
    for (;;) {

        /* Wait For Interrupt reduces pointless execution while parked.
         *
         * A halt is not a scheduler sleep; no ordinary work is expected to
         * resume from this loop in K2. */
        __asm__ volatile("wfi");
    }
}
