/*-----------------------------------------------------------------------------
 * Umicom Kernel normal-startup contracts
 * File: include/umicom/kernel/startup.h
 *
 * Normal boot reserves memory without exercising destructive diagnostic cases.
 * Recovery accepts only fixed-storage inspection and poweroff; it never needs
 * an allocator, filesystem, Framework service or a runnable user process.
 *
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_STARTUP_H
#define UMICOM_KERNEL_STARTUP_H
#include "umicom/kernel/types.h"

typedef enum UmicomKernelBootMemoryStatus {
    UMICOM_BOOT_MEMORY_OK,
    UMICOM_BOOT_MEMORY_BAD_INPUT,
    UMICOM_BOOT_MEMORY_ALREADY_INITIALISED,
    UMICOM_BOOT_MEMORY_ALLOCATION_ERROR
} UmicomKernelBootMemoryStatus;
/* Called once in physical-addressed machine startup before any frame consumer.
 * Kernel bounds and the platform RAM descriptor are trusted platform data.
 * The DTB range is checked before dereferencing its header. Output-free errors
 * never authorise resetting an already initialised physical allocator. */
UmicomKernelBootMemoryStatus UmicomKernelBootMemoryInitialize(UmicomU64 hart,
    UmicomAddress deviceTree, UmicomAddress kernelStart, UmicomAddress kernelEnd);

typedef void (*UmicomKernelStartupOutput)(void *context, const char *text, UmicomSize bytes);
typedef void (*UmicomKernelRecoveryReport)(void *context);
typedef enum UmicomKernelRecoveryAction {
    UMICOM_RECOVERY_CONTINUE,
    UMICOM_RECOVERY_POWEROFF
} UmicomKernelRecoveryAction;
/* Interpret a complete line without touching hardware or allocating memory.
 * Input, reason and callbacks are trusted Kernel storage; callbacks do not
 * reenter or retain pointers. Only an exact one-token poweroff is accepted. */
UmicomKernelRecoveryAction UmicomKernelRecoveryCommand(const char *line, UmicomSize bytes,
    const char *reason, UmicomKernelStartupOutput output, UmicomKernelRecoveryReport report, void *context);

_Noreturn void UmicomKernelNormalBoot(UmicomU64 hart, UmicomAddress deviceTree);
void UmicomKernelNormalBootRecover(const char *reason);
void UmicomKernelNormalBootReport(void *context, UmicomKernelStartupOutput output);
#endif /* UMICOM_KERNEL_STARTUP_H */
