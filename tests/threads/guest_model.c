/* Host model for the SAME C acceptance orchestration used inside QEMU.
 * Time, machine CSRs, physical accounting and ECALL are synthetic here. Only
 * scheduler execution, alternate stacks and message queues are real host code.
 * This supplements native policy tests; it must not be reported as a guest boot.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/threads.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "umicom/kernel/riscv64/trap.h"
static UmicomU64 now;
static UmicomU64 exceptions;
void UmicomKernelConsoleWrite(const char *text) { fputs(text, stdout); }
void UmicomKernelConsoleWriteLine(const char *text) { puts(text); }
void UmicomKernelConsoleWriteUnsigned(UmicomU64 value) { printf("%llu",value); }
void UmicomPlatformFinishFailure(UmicomU32 code) { fprintf(stderr,"host-model-failure=%u\n",code); exit(1); }
void UmicomPlatformHalt(void) { abort(); }
UmicomU64 UmicomPlatformTimerRead(void) { now += 10000U; return now; }
UmicomU64 UmicomPlatformTimerCompareRead(UmicomU64 hartId) { (void)hartId; return ~(UmicomU64)0U; }
void UmicomRiscvSupervisorMachineStateRead(UmicomRiscvSupervisorMachineState *state) { memset(state,0,sizeof(*state)); }
void UmicomRiscvTriggerMachineEcall(void) { ++exceptions; }
void UmicomRiscvTrapSnapshotRead(UmicomRiscvTrapSnapshot *outSnapshot)
{
    memset(outSnapshot,0,sizeof(*outSnapshot));
    outSnapshot->exceptionCount=exceptions; outSnapshot->lastCauseCode=11U;
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemorySnapshotRead(UmicomKernelPhysicalMemorySnapshot *outSnapshot)
{
    memset(outSnapshot,0,sizeof(*outSnapshot));
    outSnapshot->totalFrames=32768U; outSnapshot->reservedFrames=1024U; outSnapshot->freeFrames=31744U;
    return UMICOM_KERNEL_MEMORY_OK;
}
int main(void) { UmicomKernelThreadsValidateExecution(); return 0; }
