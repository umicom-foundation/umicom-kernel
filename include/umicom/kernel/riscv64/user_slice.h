/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/riscv64/user_slice.h
 *
 * PURPOSE:
 *   Connect a stable user continuation to the existing machine entry and C
 *   syscall dispatcher. None of these pointers comes from a user register.
 *
 * EDUCATIONAL NOTE:
 *   The ordinary run-once path is still available. Only a session explicitly
 *   bound for one slice treats a due timer as a pause rather than a terminal
 *   deadline. The binding ends before another address space can be selected.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_RISCV64_USER_SLICE_H
#define UMICOM_KERNEL_RISCV64_USER_SLICE_H
#include "umicom/kernel/user_scheduler.h"

/* The Assembly shares its save/restore body with UmicomRiscvUserExecute.
 * frame is Kernel-owned and remains alive until return. It supplies the user
 * registers on entry and receives the complete last trap on a captured return.
 * Refused hardware admission leaves it unchanged. */
UmicomU64 UmicomRiscvUserExecuteFrame(const UmicomRiscvUserRequest *request,
    UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame);
void UmicomRiscvUserCounterStateRead(UmicomU64 *machine, UmicomU64 *supervisor);
void UmicomRiscvExecutableSynchronize(void);

/* Internal slice protocol. Serialisation belongs to the Kernel caller; the
 * boolean guard prevents accidental recursive binding, not parallel harts. */
UmicomBoolean UmicomKernelUserSliceBegin(UmicomKernelUserSession *session, UmicomU64 deadline);
UmicomBoolean UmicomKernelUserSliceEnd(UmicomKernelUserSession *session, UmicomBoolean *outExpired);
/* 0: keep the original dispatcher; 1: return to Kernel; 2: resume this user.
 * Only a matching user-origin machine-timer trap is intercepted. */
UmicomU64 UmicomKernelUserSliceOnTrap(UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame);
UmicomBoolean UmicomKernelUserFrameValid(const UmicomKernelUserMemory *memory,
    const UmicomRiscvTrapFrame *frame);
/* A successful result has either a captured terminal event or a due quantum.
 * expired distinguishes the latter without extending the established syscall
 * ABI or assigning a fake terminal reason to a live program. */
UmicomKernelUserScheduleStatus UmicomKernelUserSliceRun(UmicomKernelUserTask *task,
    UmicomU64 quantumTicks, UmicomBoolean *outExpired);
#endif /* UMICOM_KERNEL_RISCV64_USER_SLICE_H */
