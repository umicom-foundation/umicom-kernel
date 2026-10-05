/*-----------------------------------------------------------------------------
 * Umicom Kernel protected mapped-stack probes
 * File: arch/riscv64/mapped_region_probe.S
 *
 * These instructions run in S-mode on a separately allocated, translated stack.
 * A fault returns through the existing supervisor monitor; no new trap handler
 * is introduced. The guarded-stack loop really changes sp and uses it for each
 * store, rather than passing a guard address to an unrelated memory operation.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/riscv64/mapped_region_probe.h"
    .section .text.umicom_region_payload, "ax", @progbits
    .balign 4
    .global UmicomRiscvRegionPayloadEntry
    .type UmicomRiscvRegionPayloadEntry, @function
UmicomRiscvRegionPayloadEntry:
    /* a0 is the mapped observation address supplied by the trusted caller. */
    call UmicomKernelRegionPayload
    li a7, UMICOM_REGION_PROBE_COOKIE /* Identify the deliberate completion. */
    .global UmicomRiscvRegionCompletion
UmicomRiscvRegionCompletion:
    ecall /* S-mode ECALL is cause 9. The machine monitor captures a0 and a7. */
    unimp /* A terminal diagnostic invocation must never resume after ECALL. */
    .size UmicomRiscvRegionPayloadEntry, . - UmicomRiscvRegionPayloadEntry

    .global UmicomRiscvRegionLowerGuard
    .type UmicomRiscvRegionLowerGuard, @function
UmicomRiscvRegionLowerGuard:
    .global UmicomRiscvRegionLowerInstruction
UmicomRiscvRegionLowerInstruction:
    ld a0, 0(a0) /* The unmapped lower margin must cause a load page fault. */
    ret
    .size UmicomRiscvRegionLowerGuard, . - UmicomRiscvRegionLowerGuard

    .global UmicomRiscvRegionUpperGuard
    .type UmicomRiscvRegionUpperGuard, @function
UmicomRiscvRegionUpperGuard:
    .global UmicomRiscvRegionUpperInstruction
UmicomRiscvRegionUpperInstruction:
    sd zero, 0(a0) /* The exclusive stack top belongs to the unmapped upper guard. */
    ret
    .size UmicomRiscvRegionUpperGuard, . - UmicomRiscvRegionUpperGuard

    .global UmicomRiscvRegionExhaustStack
    .type UmicomRiscvRegionExhaustStack, @function
UmicomRiscvRegionExhaustStack:
    mv t0, sp /* Retain a continuation only to diagnose an unexpectedly mapped guard. */
.Lumicom_region_push:
    addi sp, sp, -16 /* Keep ABI alignment while stepping through each stack page. */
    .global UmicomRiscvRegionStackInstruction
UmicomRiscvRegionStackInstruction:
    sd zero, 0(sp) /* The first address below the data region must fault here. */
    addi a0, a0, -1
    bnez a0, .Lumicom_region_push
    mv sp, t0 /* If every forbidden store worked, return so the harness fails. */
    ret
    .size UmicomRiscvRegionExhaustStack, . - UmicomRiscvRegionExhaustStack

    .global UmicomRiscvRegionExecuteStack
    .type UmicomRiscvRegionExecuteStack, @function
UmicomRiscvRegionExecuteStack:
    jr a0 /* An RW stack is not executable: instruction fetch must fault. */
    .size UmicomRiscvRegionExecuteStack, . - UmicomRiscvRegionExecuteStack
