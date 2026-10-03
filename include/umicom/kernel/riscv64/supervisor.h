/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/supervisor.h
 *
 * PURPOSE:
 *   Describe a bounded supervisor-mode execution experiment and the evidence
 *   returned to its machine-mode caller.
 *
 * EDUCATIONAL OVERVIEW:
 *   Building a page table and translating one data access are not the same as
 *   running C code behind that page table. This interface takes the next small
 *   step: enter supervisor mode with a separate stack, run a known payload,
 *   then regain machine-mode control through a trap.
 *
 *   This is a validation interface, not a scheduler or a general process ABI.
 *   The payload is trusted Kernel source. It cannot choose the machine return
 *   address; Assembly saves that address before lowering privilege. Expected
 *   faults return a report rather than jumping back into a broken payload.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_RISCV64_SUPERVISOR_H
#define UMICOM_KERNEL_RISCV64_SUPERVISOR_H

/* These offsets are plain integers so preprocessed Assembly can share the
 * exact same contract as C. The assertions below check every shared field. */
#define UMICOM_SUPERVISOR_REQUEST_ROOT       0
#define UMICOM_SUPERVISOR_REQUEST_ENTRY      8
#define UMICOM_SUPERVISOR_REQUEST_STACK     16
#define UMICOM_SUPERVISOR_REQUEST_OBSERVATION 24
#define UMICOM_SUPERVISOR_REQUEST_PMP       32
#define UMICOM_SUPERVISOR_REQUEST_BYTES     40

/* The report captures the event before machine CSRs are restored. A report
 * says why execution stopped; it does not by itself mean the test passed. */
#define UMICOM_SUPERVISOR_REPORT_CAUSE       0
#define UMICOM_SUPERVISOR_REPORT_PC          8
#define UMICOM_SUPERVISOR_REPORT_VALUE      16
#define UMICOM_SUPERVISOR_REPORT_STATUS     24
#define UMICOM_SUPERVISOR_REPORT_RETURN     32
#define UMICOM_SUPERVISOR_REPORT_COOKIE     40
#define UMICOM_SUPERVISOR_REPORT_STACK      48
#define UMICOM_SUPERVISOR_REPORT_BYTES      56

/* These modes select deliberately small, inspectable payload paths. They are
 * operations, not build or release identifiers. */
#define UMICOM_SUPERVISOR_OPERATION_RETURN       0
#define UMICOM_SUPERVISOR_OPERATION_GUARD_LOAD   1
#define UMICOM_SUPERVISOR_OPERATION_READONLY_STORE 2
#define UMICOM_SUPERVISOR_OPERATION_MACHINE_CSR  3

/* A normal return carries this explicit cookie in a7. The machine harness also
 * checks the exact ECALL instruction address and the previous privilege. */
#define UMICOM_SUPERVISOR_COMPLETION_COOKIE 0x554d49434f4d

#ifndef __ASSEMBLER__

/* All public types keep the full Umicom spelling used by the Kernel. */
#include "umicom/kernel/types.h"

/* Machine code consumes this request before entering the supervisor payload.
 * The request itself need not be mapped in the supervisor address space. */
typedef struct UmicomRiscvSupervisorRequest {
    /* Physical, page-aligned root of an already validated Sv39 hierarchy. */
    UmicomAddress rootTablePhysicalAddress;
    /* Identity-mapped address of the small Assembly supervisor entry. */
    UmicomAddress entryVirtualAddress;
    /* Exclusive top of the separately allocated writable, non-executable stack. */
    UmicomAddress stackTopVirtualAddress;
    /* Writable virtual address of the shared observation page. */
    UmicomAddress observationVirtualAddress;
    /* NAPOT encoding of the RAM-only, unlocked PMP range used during the test. */
    UmicomU64 pmpNapotAddress;
} UmicomRiscvSupervisorRequest;

/* This stays on the machine stack. Supervisor page tables never expose it. */
typedef struct UmicomRiscvSupervisorReport {
    /* Raw mcause, including the interrupt bit if an unexpected interrupt arrives. */
    UmicomU64 cause;
    /* Exact instruction at which the payload exited or faulted. */
    UmicomU64 programCounter;
    /* Fault address or architecture-supplied instruction detail. */
    UmicomU64 trapValue;
    /* Saved trap-entry mstatus; MPP proves the payload's previous privilege. */
    UmicomU64 machineStatus;
    /* Payload return register, recorded before using argument registers as scratch. */
    UmicomU64 returnValue;
    /* Payload a7, used to distinguish the normal completion ECALL. */
    UmicomU64 completionCookie;
    /* Supervisor sp captured by the first trap-entry instruction. */
    UmicomAddress interruptedStack;
} UmicomRiscvSupervisorReport;

/* Volatile access is appropriate for this single-hart shared observation: the
 * payload writes it while the ordinary machine call is suspended. It is not a
 * substitute for the synchronisation a future multi-hart design will need. */
typedef struct UmicomRiscvSupervisorObservation {
    /* Select a normal return or one specifically expected protection fault. */
    UmicomU64 operation;
    /* Unmapped address used by the guard-page load. */
    UmicomAddress guardVirtualAddress;
    /* Read-only alias whose backing word must survive an attempted write. */
    UmicomAddress readOnlyVirtualAddress;
    /* Set by the C payload before it attempts any deliberately failing operation. */
    UmicomU64 entered;
    /* satp as read while actually executing in supervisor mode. */
    UmicomU64 observedSatp;
    /* A real supervisor stack pointer observed while the C frame is live. */
    UmicomAddress observedStack;
    /* Result computed through volatile stack locals; not a preprinted success. */
    UmicomU64 stackResult;
    /* Set only when the C payload reaches its normal return. */
    UmicomU64 completed;
} UmicomRiscvSupervisorObservation;

/* Save and compare the control state that this bounded experiment borrows.
 * Pending interrupt bits are intentionally excluded: time advances independently. */
typedef struct UmicomRiscvSupervisorMachineState {
    UmicomU64 mstatus;   /* Privilege and interrupt-enable state. */
    UmicomU64 mie;       /* Per-source interrupt enables. */
    UmicomU64 mtvec;     /* Existing machine trap-vector configuration. */
    UmicomU64 mscratch;  /* Existing machine scratch owner. */
    UmicomU64 medeleg;   /* Exception delegation policy. */
    UmicomU64 mideleg;   /* Interrupt delegation policy. */
    UmicomU64 satp;      /* Previously selected translation root and mode. */
    UmicomU64 pmpcfg0;   /* Permission/address-mode bytes for the first PMP bank. */
    UmicomU64 pmpaddr0;  /* Previously configured first PMP address. */
    UmicomU64 mepc;      /* Earlier machine trap return PC. */
    UmicomU64 mcause;    /* Earlier machine trap cause. */
    UmicomU64 mtval;     /* Earlier machine trap value. */
} UmicomRiscvSupervisorMachineState;

/* A shared field mismatch must be a compile error, never a trap-time surprise. */
#define UMICOM_SUPERVISOR_ASSERT_OFFSET(typeName, fieldName, byteOffset) \
    _Static_assert(__builtin_offsetof(typeName, fieldName) == (byteOffset), \
        "Supervisor C/Assembly field offset mismatch: " #fieldName)

UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorRequest, rootTablePhysicalAddress, UMICOM_SUPERVISOR_REQUEST_ROOT);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorRequest, entryVirtualAddress, UMICOM_SUPERVISOR_REQUEST_ENTRY);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorRequest, stackTopVirtualAddress, UMICOM_SUPERVISOR_REQUEST_STACK);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorRequest, observationVirtualAddress, UMICOM_SUPERVISOR_REQUEST_OBSERVATION);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorRequest, pmpNapotAddress, UMICOM_SUPERVISOR_REQUEST_PMP);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, cause, UMICOM_SUPERVISOR_REPORT_CAUSE);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, programCounter, UMICOM_SUPERVISOR_REPORT_PC);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, trapValue, UMICOM_SUPERVISOR_REPORT_VALUE);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, machineStatus, UMICOM_SUPERVISOR_REPORT_STATUS);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, returnValue, UMICOM_SUPERVISOR_REPORT_RETURN);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, completionCookie, UMICOM_SUPERVISOR_REPORT_COOKIE);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorReport, interruptedStack, UMICOM_SUPERVISOR_REPORT_STACK);
_Static_assert(sizeof(UmicomRiscvSupervisorRequest) == UMICOM_SUPERVISOR_REQUEST_BYTES, "Supervisor request size mismatch");
_Static_assert(sizeof(UmicomRiscvSupervisorReport) == UMICOM_SUPERVISOR_REPORT_BYTES, "Supervisor report size mismatch");
_Static_assert(sizeof(UmicomRiscvSupervisorMachineState) == 96U, "Supervisor machine snapshot size mismatch");

/* Match the CSR reader's fixed store offsets, not only its total size. */
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mstatus, 0);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mie, 8);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mtvec, 16);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mscratch, 24);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, medeleg, 32);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mideleg, 40);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, satp, 48);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, pmpcfg0, 56);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, pmpaddr0, 64);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mepc, 72);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mcause, 80);
UMICOM_SUPERVISOR_ASSERT_OFFSET(UmicomRiscvSupervisorMachineState, mtval, 88);

/* Run in machine mode. Returns zero after a captured payload trap, or one when
 * the pre-existing MPRV/PMP policy makes this early validation unsafe to start.
 * Callers must supply valid Kernel-owned pointers and validated mappings. */
UmicomU64 UmicomRiscvSupervisorExecute(
    const UmicomRiscvSupervisorRequest *request,
    UmicomRiscvSupervisorReport *report
);

/* Read the borrowed CSRs without changing them, for before/after comparison. */
void UmicomRiscvSupervisorMachineStateRead(UmicomRiscvSupervisorMachineState *state);

/* Execute the complete normal/fault/recovery validation from machine mode. */
void UmicomKernelSupervisorExecutionValidate(void);

/* These helpers execute only from the dedicated supervisor text pages. */
void UmicomRiscvSupervisorEntry(void);
UmicomU64 UmicomRiscvSupervisorPayload(volatile UmicomRiscvSupervisorObservation *observation);
UmicomU64 UmicomRiscvSupervisorSatpRead(void);
UmicomAddress UmicomRiscvSupervisorStackRead(void);
UmicomU64 UmicomRiscvSupervisorGuardLoad(UmicomAddress address);
void UmicomRiscvSupervisorReadOnlyStore(UmicomAddress address, UmicomU64 value);
UmicomU64 UmicomRiscvSupervisorMachineStatusRead(void);

/* Assembly labels identify the exact expected faulting/completion instruction. */
extern char UmicomRiscvSupervisorCompletionInstruction[];
extern char UmicomRiscvSupervisorGuardLoadInstruction[];
extern char UmicomRiscvSupervisorReadOnlyStoreInstruction[];
extern char UmicomRiscvSupervisorMachineStatusInstruction[];

/* The linker isolates these executable pages from other machine-only code. */
extern char __umicom_supervisor_text_start[];
extern char __umicom_supervisor_text_end[];

#endif /* C declarations are not fed to the assembler. */
#endif /* UMICOM_KERNEL_RISCV64_SUPERVISOR_H */
