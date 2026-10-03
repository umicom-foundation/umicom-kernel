/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/user_execution.h
 *
 * PURPOSE:
 *   Share the user-entry request and trap-frame offsets between C23 and RV64
 *   Assembly. The user monitor can resume environment calls, unlike the
 *   separate supervisor experiment which returns after its first trap.
 *
 * EDUCATIONAL OVERVIEW:
 *   All control structures in this header are Kernel-owned. User code sees
 *   only its operation argument and mapped application pages. A trap saves
 *   registers on the machine stack before C checks any user request.
 *   This is a single-hart monitor, not yet a scheduler or a process loader.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_RISCV64_USER_EXECUTION_H
#define UMICOM_KERNEL_RISCV64_USER_EXECUTION_H
#include "umicom/kernel/user_abi.h"

/* Plain offsets let Assembly consume the same checked layout as C. */
#define UMICOM_USER_REQUEST_ROOT 0
#define UMICOM_USER_REQUEST_ENTRY 8
#define UMICOM_USER_REQUEST_STACK 16
#define UMICOM_USER_REQUEST_ARGUMENT 24
#define UMICOM_USER_REQUEST_PMP 32
#define UMICOM_USER_REQUEST_BYTES 40

/* Reuse the existing complete integer trap-frame shape without modifying it. */
#define UMICOM_USER_FRAME_X1_RA 0
#define UMICOM_USER_FRAME_X2_SP 8
#define UMICOM_USER_FRAME_X3_GP 16
#define UMICOM_USER_FRAME_X4_TP 24
#define UMICOM_USER_FRAME_X5_T0 32
#define UMICOM_USER_FRAME_X6_T1 40
#define UMICOM_USER_FRAME_X7_T2 48
#define UMICOM_USER_FRAME_X8_S0 56
#define UMICOM_USER_FRAME_X9_S1 64
#define UMICOM_USER_FRAME_X10_A0 72
#define UMICOM_USER_FRAME_X11_A1 80
#define UMICOM_USER_FRAME_X12_A2 88
#define UMICOM_USER_FRAME_X13_A3 96
#define UMICOM_USER_FRAME_X14_A4 104
#define UMICOM_USER_FRAME_X15_A5 112
#define UMICOM_USER_FRAME_X16_A6 120
#define UMICOM_USER_FRAME_X17_A7 128
#define UMICOM_USER_FRAME_X18_S2 136
#define UMICOM_USER_FRAME_X19_S3 144
#define UMICOM_USER_FRAME_X20_S4 152
#define UMICOM_USER_FRAME_X21_S5 160
#define UMICOM_USER_FRAME_X22_S6 168
#define UMICOM_USER_FRAME_X23_S7 176
#define UMICOM_USER_FRAME_X24_S8 184
#define UMICOM_USER_FRAME_X25_S9 192
#define UMICOM_USER_FRAME_X26_S10 200
#define UMICOM_USER_FRAME_X27_S11 208
#define UMICOM_USER_FRAME_X28_T3 216
#define UMICOM_USER_FRAME_X29_T4 224
#define UMICOM_USER_FRAME_X30_T5 232
#define UMICOM_USER_FRAME_X31_T6 240
#define UMICOM_USER_FRAME_MEPC 248
#define UMICOM_USER_FRAME_MSTATUS 256
#define UMICOM_USER_FRAME_MCAUSE 264
#define UMICOM_USER_FRAME_MTVAL 272
#define UMICOM_USER_FRAME_RESERVED 280
#define UMICOM_USER_FRAME_BYTES 288

#ifndef __ASSEMBLER__
#include "umicom/kernel/riscv64/trap.h"
#include "umicom/kernel/user_memory.h"

typedef struct UmicomRiscvUserRequest {
    UmicomAddress rootTablePhysicalAddress; /* Validated, page-aligned Sv39 root. */
    UmicomAddress entryVirtualAddress; /* Executable user entry, not a C callback. */
    UmicomAddress stackTopVirtualAddress; /* Exclusive, sixteen-byte aligned top. */
    UmicomU64 argument; /* Small operation selector passed as a0, not authority. */
    UmicomU64 pmpNapotAddress; /* Temporary RAM-only PMP region, never locked. */
} UmicomRiscvUserRequest;

/* A terminal reason describes what really happened, including expected faults.
 * The validation harness decides whether that reason matches the test case. */
typedef enum UmicomKernelUserStopReason {
    UMICOM_USER_STOP_NONE,
    UMICOM_USER_STOP_EXIT,
    UMICOM_USER_STOP_FAULT,
    UMICOM_USER_STOP_CALL_BUDGET,
    UMICOM_USER_STOP_DEADLINE,
    UMICOM_USER_STOP_MONITOR_ERROR
} UmicomKernelUserStopReason;

typedef struct UmicomKernelUserSession {
    UmicomKernelUserMemory memory; /* Borrowed immutable page-table/owner records. */
    UmicomU64 identity; /* Supplied by Kernel setup, never by the user program. */
    UmicomU64 callCount; /* Includes the exit or budget-exhausting request. */
    UmicomU64 rejectedCalls; /* Refusals returned to the payload without a copy. */
    UmicomU64 copiedBytes; /* Bytes committed by successful COPY operations. */
    UmicomU64 exitValue; /* User exit value, meaningful only for STOP_EXIT. */
    UmicomKernelUserStopReason stopReason;
    UmicomU64 trapCause; /* Last raw mcause, including its interrupt bit. */
    UmicomU64 trapPc; /* Faulting instruction or environment-call instruction. */
    UmicomU64 trapValue; /* mtval, interpreted according to the cause. */
    UmicomU64 trapStatus; /* MPP must prove U-mode at each user-origin trap. */
    UmicomAddress trapStack; /* Captured value, never dereferenced by the monitor. */
} UmicomKernelUserSession;

/* Check every shared register slot, not just the total structure size. */
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x1_ra) == UMICOM_USER_FRAME_X1_RA, "User trap offset: x1_ra");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x2_sp) == UMICOM_USER_FRAME_X2_SP, "User trap offset: x2_sp");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x3_gp) == UMICOM_USER_FRAME_X3_GP, "User trap offset: x3_gp");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x4_tp) == UMICOM_USER_FRAME_X4_TP, "User trap offset: x4_tp");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x5_t0) == UMICOM_USER_FRAME_X5_T0, "User trap offset: x5_t0");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x6_t1) == UMICOM_USER_FRAME_X6_T1, "User trap offset: x6_t1");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x7_t2) == UMICOM_USER_FRAME_X7_T2, "User trap offset: x7_t2");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x8_s0) == UMICOM_USER_FRAME_X8_S0, "User trap offset: x8_s0");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x9_s1) == UMICOM_USER_FRAME_X9_S1, "User trap offset: x9_s1");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x10_a0) == UMICOM_USER_FRAME_X10_A0, "User trap offset: x10_a0");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x11_a1) == UMICOM_USER_FRAME_X11_A1, "User trap offset: x11_a1");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x12_a2) == UMICOM_USER_FRAME_X12_A2, "User trap offset: x12_a2");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x13_a3) == UMICOM_USER_FRAME_X13_A3, "User trap offset: x13_a3");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x14_a4) == UMICOM_USER_FRAME_X14_A4, "User trap offset: x14_a4");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x15_a5) == UMICOM_USER_FRAME_X15_A5, "User trap offset: x15_a5");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x16_a6) == UMICOM_USER_FRAME_X16_A6, "User trap offset: x16_a6");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x17_a7) == UMICOM_USER_FRAME_X17_A7, "User trap offset: x17_a7");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x18_s2) == UMICOM_USER_FRAME_X18_S2, "User trap offset: x18_s2");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x19_s3) == UMICOM_USER_FRAME_X19_S3, "User trap offset: x19_s3");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x20_s4) == UMICOM_USER_FRAME_X20_S4, "User trap offset: x20_s4");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x21_s5) == UMICOM_USER_FRAME_X21_S5, "User trap offset: x21_s5");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x22_s6) == UMICOM_USER_FRAME_X22_S6, "User trap offset: x22_s6");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x23_s7) == UMICOM_USER_FRAME_X23_S7, "User trap offset: x23_s7");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x24_s8) == UMICOM_USER_FRAME_X24_S8, "User trap offset: x24_s8");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x25_s9) == UMICOM_USER_FRAME_X25_S9, "User trap offset: x25_s9");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x26_s10) == UMICOM_USER_FRAME_X26_S10, "User trap offset: x26_s10");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x27_s11) == UMICOM_USER_FRAME_X27_S11, "User trap offset: x27_s11");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x28_t3) == UMICOM_USER_FRAME_X28_T3, "User trap offset: x28_t3");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x29_t4) == UMICOM_USER_FRAME_X29_T4, "User trap offset: x29_t4");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x30_t5) == UMICOM_USER_FRAME_X30_T5, "User trap offset: x30_t5");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, x31_t6) == UMICOM_USER_FRAME_X31_T6, "User trap offset: x31_t6");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mepc) == UMICOM_USER_FRAME_MEPC, "User trap offset: mepc");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mstatus) == UMICOM_USER_FRAME_MSTATUS, "User trap offset: mstatus");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mcause) == UMICOM_USER_FRAME_MCAUSE, "User trap offset: mcause");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, mtval) == UMICOM_USER_FRAME_MTVAL, "User trap offset: mtval");
_Static_assert(__builtin_offsetof(UmicomRiscvTrapFrame, reserved) == UMICOM_USER_FRAME_RESERVED, "User trap offset: reserved");
_Static_assert(sizeof(UmicomRiscvTrapFrame) == UMICOM_USER_FRAME_BYTES, "User trap frame size");
_Static_assert(sizeof(UmicomRiscvUserRequest) == UMICOM_USER_REQUEST_BYTES, "User request size");
_Static_assert(__builtin_offsetof(UmicomRiscvUserRequest, rootTablePhysicalAddress) == UMICOM_USER_REQUEST_ROOT, "User request: rootTablePhysicalAddress");
_Static_assert(__builtin_offsetof(UmicomRiscvUserRequest, entryVirtualAddress) == UMICOM_USER_REQUEST_ENTRY, "User request: entryVirtualAddress");
_Static_assert(__builtin_offsetof(UmicomRiscvUserRequest, stackTopVirtualAddress) == UMICOM_USER_REQUEST_STACK, "User request: stackTopVirtualAddress");
_Static_assert(__builtin_offsetof(UmicomRiscvUserRequest, argument) == UMICOM_USER_REQUEST_ARGUMENT, "User request: argument");
_Static_assert(__builtin_offsetof(UmicomRiscvUserRequest, pmpNapotAddress) == UMICOM_USER_REQUEST_PMP, "User request: pmpNapotAddress");

/* Caller supplies trusted request/session pointers, a validated user mapping,
 * disabled interrupts and a configured machine timer deadline. Returns zero
 * after a captured terminal event, one when entry policy refuses execution. */
UmicomU64 UmicomRiscvUserExecute(
    const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session
);
/* Called only on the safe machine stack. One resumes U-mode; zero ends this
 * invocation. The dispatcher never uses a user register as a Kernel pointer. */
UmicomU64 UmicomKernelUserTrapDispatch(
    UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame
);
void UmicomKernelUserExecutionValidate(void);

/* Payload helpers live in their own executable pages. No machine monitor code
 * is mapped into that range. These are not privileged Kernel entry points. */
void UmicomRiscvUserEntry(void);
UmicomU64 UmicomRiscvUserPayload(UmicomU64 operation);
UmicomU64 UmicomRiscvUserSystemCall(UmicomU64 number, UmicomU64 first, UmicomU64 second, UmicomU64 third);
UmicomU64 UmicomRiscvUserRegisterProbe(void);
UmicomU64 UmicomRiscvUserLoad(UmicomAddress address);
void UmicomRiscvUserStore(UmicomAddress address, UmicomU64 value);
UmicomU64 UmicomRiscvUserSupervisorCsrRead(void);
void UmicomRiscvUserJump(UmicomAddress address);
void UmicomRiscvUserExitWithInvalidStack(UmicomAddress stack);
void UmicomRiscvUserBusyLoop(void);
extern char UmicomRiscvUserExitInstruction[];
extern char UmicomRiscvUserLoadInstruction[];
extern char UmicomRiscvUserStoreInstruction[];
extern char UmicomRiscvUserSupervisorCsrInstruction[];
extern char UmicomRiscvUserInvalidStackExitInstruction[];
extern char UmicomRiscvUserBusyInstruction[];
extern char __umicom_user_text_start[];
extern char __umicom_user_text_end[];
#endif /* C declarations are not assembler input. */
#endif /* UMICOM_KERNEL_RISCV64_USER_EXECUTION_H */
