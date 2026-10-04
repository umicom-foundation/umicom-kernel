/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/thread_context.h
 *
 * PURPOSE:
 *   Define the small call-boundary context used by cooperative Kernel threads.
 *
 * EDUCATIONAL OVERVIEW:
 *   A cooperative switch is an ordinary function call that returns much later.
 *   The compiler already treats argument and temporary registers as clobbered
 *   by that call. The context therefore saves the return address, stack pointer
 *   and callee-saved integer registers, not a complete asynchronous trap frame.
 *   Do not use this layout to pre-empt an arbitrary instruction: that requires
 *   the full interrupted state and a separate interrupt-entry contract.
 *
 *   The production implementation is RV64 with the integer-only lp64 ABI.
 *   A test-only host adapter exercises the same C scheduler on another machine;
 *   it does not make that host architecture an Umicom Kernel boot target.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_THREAD_CONTEXT_H
#define UMICOM_KERNEL_THREAD_CONTEXT_H

/* Numeric offsets are also consumed by preprocessed Assembly. */
#define UMICOM_THREAD_CONTEXT_RETURN 0
#define UMICOM_THREAD_CONTEXT_STACK 8
#define UMICOM_THREAD_CONTEXT_SAVED 16
#define UMICOM_THREAD_CONTEXT_GP 112
#define UMICOM_THREAD_CONTEXT_TP 120
#define UMICOM_THREAD_CONTEXT_BYTES 128

#ifndef __ASSEMBLER__
#include "umicom/kernel/types.h"

typedef struct UmicomKernelThreadContext {
    UmicomU64 returnAddress; /* Continuation after ContextSwitch, or bootstrap. */
    UmicomU64 stackPointer; /* Suspended stack, never an unrelated task's stack. */
    UmicomU64 saved[12]; /* RV64 s0..s11, preserved across an ordinary C call. */
    UmicomU64 globalPointer; /* Preserve gp even though small-data is disabled. */
    UmicomU64 threadPointer; /* Preserve tp without claiming a TLS service. */
} UmicomKernelThreadContext;

_Static_assert(sizeof(UmicomKernelThreadContext) == UMICOM_THREAD_CONTEXT_BYTES,
    "Thread context and Assembly must agree on size");
_Static_assert(__builtin_offsetof(UmicomKernelThreadContext, returnAddress) == UMICOM_THREAD_CONTEXT_RETURN,
    "Thread return-address offset must match Assembly");
_Static_assert(__builtin_offsetof(UmicomKernelThreadContext, stackPointer) == UMICOM_THREAD_CONTEXT_STACK,
    "Thread stack offset must match Assembly");
_Static_assert(__builtin_offsetof(UmicomKernelThreadContext, saved) == UMICOM_THREAD_CONTEXT_SAVED,
    "Thread saved-register offsets must match Assembly");
_Static_assert(__builtin_offsetof(UmicomKernelThreadContext, globalPointer) == UMICOM_THREAD_CONTEXT_GP,
    "Thread gp offset must match Assembly");
_Static_assert(__builtin_offsetof(UmicomKernelThreadContext, threadPointer) == UMICOM_THREAD_CONTEXT_TP,
    "Thread tp offset must match Assembly");

/* These primitives accept only Kernel-owned storage validated by the scheduler.
 * Prepare makes a fresh context; Switch saves the caller before entering next.
 * Both sides of Switch must retain their context and stack until resumption. */
void UmicomKernelThreadContextPrepare(UmicomKernelThreadContext *context,
    UmicomAddress stackTop, UmicomAddress scheduler, UmicomSize slot);
void UmicomKernelThreadContextSwitch(UmicomKernelThreadContext *previous,
    const UmicomKernelThreadContext *next);

/* Admission is deliberately narrow: hart zero, machine mode, Bare satp,
 * interrupts/MPRV off, no floating-point or vector context to preserve.
 * Reading machine CSRs itself assumes the trusted caller is in machine mode. */
UmicomBoolean UmicomKernelThreadMachineReady(void);
#endif
#endif /* UMICOM_KERNEL_THREAD_CONTEXT_H */
