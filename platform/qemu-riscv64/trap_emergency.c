/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: platform/qemu-riscv64/trap_emergency.c
 *
 * PURPOSE:
 *   Print a bounded last-resort trap report and stop the QEMU validation guest.
 *
 * EDUCATIONAL OVERVIEW:
 *   The ordinary console deliberately waits until UART space is available.
 *   Reusing that unbounded wait after a fault inside a trap handler could hide
 *   the original failure behind a hung diagnostic. This independent emergency
 *   path limits both bytes and polls and then attempts the test-finisher write.
 *   It uses no allocator, formatting library, lock or current thread stack.
 *
 *   Device addresses stay in the QEMU adapter. This is not a physical-machine
 *   crash-storage service, and a failed MMIO device cannot promise a transcript.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/trap_integrity.h"

#define UMICOM_TRAP_UART_BASE ((UmicomAddress)0x10000000U)
#define UMICOM_TRAP_FINISH_BASE ((UmicomAddress)0x00100000U)

static void UmicomTrapEmergencyByte(UmicomU8 byte)
{
    volatile UmicomU8 *const uart = (volatile UmicomU8 *)UMICOM_TRAP_UART_BASE;
    /* A failed transmitter may lose output, but cannot keep this loop alive
     * forever. Each MMIO read remains explicit because the pointer is volatile. */
    for (UmicomU32 attempt = 0U; attempt < 10000U; ++attempt) {
        if ((uart[5] & 0x20U) != 0U) {
            uart[0] = byte;
            return;
        }
    }
}

static void UmicomTrapEmergencyText(const char *text)
{
    /* All strings are local immutable literals, never a faulting user pointer. */
    for (UmicomSize index = 0U; index < 128U && text[index] != '\0'; ++index) {
        UmicomTrapEmergencyByte((UmicomU8)text[index]);
    }
}

static void UmicomTrapEmergencyHex(UmicomU64 value)
{
    const char *const digits = "0123456789abcdef";
    UmicomTrapEmergencyText("0x");
    for (UmicomU32 nibble = 16U; nibble != 0U; --nibble) {
        const UmicomU32 shift = (nibble - 1U) * 4U;
        UmicomTrapEmergencyByte((UmicomU8)digits[(value >> shift) & 15U]);
    }
    UmicomTrapEmergencyText("\r\n");
}

_Noreturn void UmicomPlatformTrapEmergencyFinish(UmicomU64 cause,
    UmicomU64 pc, UmicomU64 value, UmicomU64 status, UmicomBoolean expectedTest)
{
    UmicomTrapEmergencyText("trap-emergency.cause=");
    UmicomTrapEmergencyHex(cause);
    UmicomTrapEmergencyText("trap-emergency.pc=");
    UmicomTrapEmergencyHex(pc);
    UmicomTrapEmergencyText("trap-emergency.value=");
    UmicomTrapEmergencyHex(value);
    UmicomTrapEmergencyText("trap-emergency.status=");
    UmicomTrapEmergencyHex(status);

    /* A normal Kernel build never treats an emergency as successful work.
     * Only the separate fault-test image may recognise its precise injection. */
    UmicomU32 finish = 0x002a3333U;
#ifdef UMICOM_TRAP_NESTED_VALIDATION
    if (expectedTest != UMICOM_FALSE) {
        UmicomTrapEmergencyText("UMICOM_KERNEL_TRAP_NESTED_REJECTION_READY\r\n");
        finish = 0x00005555U;
    } else
#else
    (void)expectedTest;
#endif
    {
        UmicomTrapEmergencyText("UMICOM_KERNEL_FAIL\r\nreason=trap-integrity-stop\r\n");
    }
    *(volatile UmicomU32 *)UMICOM_TRAP_FINISH_BASE = finish;
    UmicomPlatformTrapEmergencyHalt();
}
