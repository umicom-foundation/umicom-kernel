/* Umicom Kernel object-cache guest-sequence host model.
 * The exact guest C validation and real physical/object allocators are linked.
 * Serial output, control-register observations, ECALL and section ownership are
 * explicitly modelled. A passing host sequence does not boot a RISC-V machine.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/object_cache.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/interrupts.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"

alignas(4096) static UmicomU8 umicomGuestArena[96U * 4096U];
static UmicomU64 umicomGuestSectionOwner;
static UmicomU64 umicomGuestExceptionCount;
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void)
{
    return umicomGuestSectionOwner == 0U ? UMICOM_TRUE : UMICOM_FALSE;
}
UmicomKernelInterruptStatus UmicomKernelCriticalSectionEnter(UmicomU64 owner,
    UmicomKernelCriticalSection *outSection)
{
    /* The guest sequence expects a single balanced refusal probe. Refuse a
     * wrong model sequence instead of silently manufacturing its success. */
    if (owner == 0U || outSection == NULL || umicomGuestSectionOwner != 0U) return UMICOM_INTERRUPT_UNSAFE_CONTEXT;
    umicomGuestSectionOwner = owner;
    *outSection = 1U;
    return UMICOM_INTERRUPT_OK;
}
UmicomKernelInterruptStatus UmicomKernelCriticalSectionLeave(UmicomU64 owner,
    UmicomKernelCriticalSection section)
{
    if (owner == 0U || owner != umicomGuestSectionOwner || section != 1U) return UMICOM_INTERRUPT_WRONG_OWNER;
    umicomGuestSectionOwner = 0U;
    return UMICOM_INTERRUPT_OK;
}
void UmicomRiscvSupervisorMachineStateRead(UmicomRiscvSupervisorMachineState *outState)
{
    memset(outState, 0, sizeof(*outState)); /* Model values, not CSR observations. */
}
void UmicomRiscvTrapSnapshotRead(UmicomRiscvTrapSnapshot *outSnapshot)
{
    memset(outSnapshot, 0, sizeof(*outSnapshot));
    outSnapshot->exceptionCount = umicomGuestExceptionCount;
}
void UmicomRiscvTriggerMachineEcall(void) { ++umicomGuestExceptionCount; }
void UmicomKernelConsoleWrite(const char *text) { fputs(text, stdout); }
void UmicomKernelConsoleWriteLine(const char *text) { puts(text); }
void UmicomKernelConsoleWriteUnsigned(UmicomU64 value) { printf("%llu", value); }
void UmicomPlatformFinishFailure(UmicomU32 code) { (void)code; exit(1); }
void UmicomPlatformHalt(void) { abort(); }
int main(void)
{
    /* Match the guest invariant that the lowest bootstrap page is reserved. */
    if (UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomGuestArena, sizeof(umicomGuestArena)) != UMICOM_KERNEL_MEMORY_OK ||
        UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)umicomGuestArena, 4096U) != UMICOM_KERNEL_MEMORY_OK) return 1;
    UmicomKernelObjectCachesValidateExecution();
    return umicomGuestSectionOwner == 0U && umicomGuestExceptionCount == 1U ? 0 : 1;
}
