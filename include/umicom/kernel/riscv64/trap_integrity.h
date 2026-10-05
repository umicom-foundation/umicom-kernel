/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/trap_integrity.h
 *
 * PURPOSE:
 *   Describe the machine trap landing area and the checks performed before a
 *   saved frame is allowed to become architectural state again.
 *
 * EDUCATIONAL OVERVIEW:
 *   A trap can arrive with an unusable interrupted stack. The machine vector
 *   therefore borrows a dedicated stack through mscratch. A second fault must
 *   not reuse that frame: it goes to a separate emergency vector and stops.
 *   This is a single-hart, non-nesting contract, not an interrupt scheduler.
 *
 *   The existing frame layout and C cause dispatcher remain the source of
 *   truth. These checks surround that dispatcher rather than reproducing it.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_RISCV64_TRAP_INTEGRITY_H
#define UMICOM_KERNEL_RISCV64_TRAP_INTEGRITY_H

/* Plain constants are shared with preprocessed Assembly. These are byte
 * offsets in the established frame, not a second choice of register layout. */
#define UMICOM_TRAP_INTEGRITY_FRAME_BYTES 288
#define UMICOM_TRAP_INTEGRITY_PC 248
#define UMICOM_TRAP_INTEGRITY_STATUS 256
#define UMICOM_TRAP_INTEGRITY_CAUSE 264
#define UMICOM_TRAP_INTEGRITY_VALUE 272
#define UMICOM_TRAP_INTEGRITY_RESERVED 280
#define UMICOM_TRAP_INTEGRITY_STACK_BYTES 16384
#define UMICOM_TRAP_INTEGRITY_CANARY 0x537461636B536166

#ifndef __ASSEMBLER__
#include "umicom/kernel/riscv64/trap.h"

/* A route names the only policy adjustments permitted for a returning frame.
 * The two probe routes are selected only by a Kernel-armed, exact-PC test.
 * They are not a general facility for skipping a faulting instruction. */
typedef enum UmicomKernelTrapRoute {
    UMICOM_TRAP_ROUTE_MACHINE_ECALL,
    UMICOM_TRAP_ROUTE_MACHINE_TIMER,
    UMICOM_TRAP_ROUTE_PROBE_LOAD,
    UMICOM_TRAP_ROUTE_PROBE_STORE
} UmicomKernelTrapRoute;

typedef enum UmicomKernelTrapIntegrityStatus {
    UMICOM_TRAP_INTEGRITY_OK,
    UMICOM_TRAP_INTEGRITY_INVALID_ARGUMENT,
    UMICOM_TRAP_INTEGRITY_WRONG_PRIVILEGE,
    UMICOM_TRAP_INTEGRITY_UNSAFE_STATUS,
    UMICOM_TRAP_INTEGRITY_INVALID_PC,
    UMICOM_TRAP_INTEGRITY_WRONG_CAUSE,
    UMICOM_TRAP_INTEGRITY_CHANGED_REGISTERS,
    UMICOM_TRAP_INTEGRITY_CHANGED_DIAGNOSTICS,
    UMICOM_TRAP_INTEGRITY_BAD_RETURN_STATUS,
    UMICOM_TRAP_INTEGRITY_BAD_RETURN_PC
} UmicomKernelTrapIntegrityStatus;

/* These helpers read Kernel-owned records only. textEnd is exclusive. They
 * neither read instruction memory nor touch CSRs, so hostile frame candidates
 * can also be tested on a native host without executing a privilege transition. */
UmicomKernelTrapIntegrityStatus UmicomKernelTrapFrameCheck(
    const UmicomRiscvTrapFrame *frame, UmicomKernelTrapRoute route,
    UmicomAddress textBegin, UmicomAddress textEnd);
UmicomKernelTrapIntegrityStatus UmicomKernelTrapReturnCheck(
    const UmicomRiscvTrapFrame *before, const UmicomRiscvTrapFrame *after,
    UmicomKernelTrapRoute route, UmicomAddress textBegin, UmicomAddress textEnd);
const char *UmicomKernelTrapIntegrityStatusName(UmicomKernelTrapIntegrityStatus status);

/* Only the machine entry calls this wrapper. It checks the actual landing
 * address and canaries, calls the established policy, then validates its result. */
void UmicomRiscvMachineTrapDispatchGuarded(UmicomRiscvTrapFrame *frame);
void UmicomRiscvMachineTrapPrepare(void);
void UmicomRiscvMachineTrapEnter(void);
void UmicomRiscvMachineTrapEmergency(void);
_Noreturn void UmicomKernelTrapEmergencyStop(UmicomU64 cause, UmicomU64 pc,
    UmicomU64 value, UmicomU64 status);

/* Bounded QEMU-only diagnostics live in the platform adapter. A nonzero
 * expectedTest flag exists solely for the separately compiled fault-test ELF. */
_Noreturn void UmicomPlatformTrapEmergencyFinish(UmicomU64 cause,
    UmicomU64 pc, UmicomU64 value, UmicomU64 status, UmicomBoolean expectedTest);
_Noreturn void UmicomPlatformTrapEmergencyHalt(void);

/* The assembler owns the stack storage. Consumers can inspect its boundaries,
 * but cannot choose another address to be used as the machine landing area. */
extern UmicomU8 UmicomRiscvMachineTrapStackLow[];
extern UmicomU8 UmicomRiscvMachineTrapStackTop[];
extern UmicomU8 __umicom_trap_text_begin[];
extern UmicomU8 __umicom_trap_text_end[];

typedef struct UmicomKernelTrapIntegritySnapshot {
    UmicomU64 returned;
    UmicomU64 recoveredProbes;
    UmicomAddress interruptedStack;
    UmicomAddress frameAddress;
    UmicomU64 cause;
} UmicomKernelTrapIntegritySnapshot;
/* Call only from the interrupts-disabled dispatcher after a trap has returned. */
void UmicomKernelTrapIntegritySnapshotRead(UmicomKernelTrapIntegritySnapshot *out);
UmicomBoolean UmicomKernelTrapProbeArm(UmicomKernelTrapRoute route);
UmicomBoolean UmicomKernelTrapProbeFinished(void);
void UmicomKernelTrapHardeningValidate(void);

/* Test instructions have fixed addresses and explicit return labels. The
 * monitor never accepts a resume PC from untrusted input. */
UmicomU64 UmicomRiscvTrapRegistersProbe(void);
UmicomU64 UmicomRiscvTrapTimerRegistersProbe(void);
void UmicomRiscvTrapLoadProbe(UmicomAddress address);
void UmicomRiscvTrapStoreProbe(UmicomAddress address, UmicomU64 value);
void UmicomRiscvTrapNestedProbe(void);
void UmicomRiscvTrapNestedFault(void);
extern UmicomU8 UmicomRiscvTrapLoadInstruction[];
extern UmicomU8 UmicomRiscvTrapLoadResume[];
extern UmicomU8 UmicomRiscvTrapStoreInstruction[];
extern UmicomU8 UmicomRiscvTrapStoreResume[];
extern UmicomU8 UmicomRiscvTrapNestedEcall[];
extern UmicomU8 UmicomRiscvTrapNestedInstruction[];

/* Each integer slot below corresponds to one explicit store/load in the new
 * entry. A future frame edit must fail compilation rather than silently move
 * a value to another register on return. */
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x1_ra) == 0U,
    "Machine landing register offset: x1_ra");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x2_sp) == 8U,
    "Machine landing register offset: x2_sp");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x3_gp) == 16U,
    "Machine landing register offset: x3_gp");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x4_tp) == 24U,
    "Machine landing register offset: x4_tp");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x5_t0) == 32U,
    "Machine landing register offset: x5_t0");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x6_t1) == 40U,
    "Machine landing register offset: x6_t1");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x7_t2) == 48U,
    "Machine landing register offset: x7_t2");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x8_s0) == 56U,
    "Machine landing register offset: x8_s0");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x9_s1) == 64U,
    "Machine landing register offset: x9_s1");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x10_a0) == 72U,
    "Machine landing register offset: x10_a0");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x11_a1) == 80U,
    "Machine landing register offset: x11_a1");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x12_a2) == 88U,
    "Machine landing register offset: x12_a2");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x13_a3) == 96U,
    "Machine landing register offset: x13_a3");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x14_a4) == 104U,
    "Machine landing register offset: x14_a4");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x15_a5) == 112U,
    "Machine landing register offset: x15_a5");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x16_a6) == 120U,
    "Machine landing register offset: x16_a6");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x17_a7) == 128U,
    "Machine landing register offset: x17_a7");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x18_s2) == 136U,
    "Machine landing register offset: x18_s2");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x19_s3) == 144U,
    "Machine landing register offset: x19_s3");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x20_s4) == 152U,
    "Machine landing register offset: x20_s4");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x21_s5) == 160U,
    "Machine landing register offset: x21_s5");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x22_s6) == 168U,
    "Machine landing register offset: x22_s6");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x23_s7) == 176U,
    "Machine landing register offset: x23_s7");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x24_s8) == 184U,
    "Machine landing register offset: x24_s8");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x25_s9) == 192U,
    "Machine landing register offset: x25_s9");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x26_s10) == 200U,
    "Machine landing register offset: x26_s10");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x27_s11) == 208U,
    "Machine landing register offset: x27_s11");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x28_t3) == 216U,
    "Machine landing register offset: x28_t3");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x29_t4) == 224U,
    "Machine landing register offset: x29_t4");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x30_t5) == 232U,
    "Machine landing register offset: x30_t5");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x31_t6) == 240U,
    "Machine landing register offset: x31_t6");

/* Fail compilation if C and Assembly stop agreeing about any shared boundary. */
_Static_assert(sizeof(UmicomRiscvTrapFrame) == UMICOM_TRAP_INTEGRITY_FRAME_BYTES,
    "Machine landing frame must match the established trap frame");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mepc) == UMICOM_TRAP_INTEGRITY_PC,
    "Machine landing PC offset must match");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mstatus) == UMICOM_TRAP_INTEGRITY_STATUS,
    "Machine landing status offset must match");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mcause) == UMICOM_TRAP_INTEGRITY_CAUSE,
    "Machine landing cause offset must match");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mtval) == UMICOM_TRAP_INTEGRITY_VALUE,
    "Machine landing trap-value offset must match");
#endif /* C declarations */
#endif /* UMICOM_KERNEL_RISCV64_TRAP_INTEGRITY_H */
