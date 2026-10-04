/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/message_service.h
 *
 * PURPOSE:
 *   Bind a trusted execution identity to message services for one serial user
 *   invocation, without exposing a domain pointer in the native register ABI.
 *
 * EDUCATIONAL OVERVIEW:
 *   The process registry assigns identity before execution. Kernel admission
 *   binds that identity and its chosen message domain, then calls the existing
 *   run path. A trap uses session->identity, never an owner value supplied by
 *   the program. The binding must be removed after exit, fault or deadline.
 *
 *   This single-hart binding is temporary infrastructure, not a global service
 *   locator for a future concurrent scheduler. It refuses nested bindings.
 *   Scheduling will need a binding owned by each resumable execution context.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_MESSAGE_SERVICE_H
#define UMICOM_KERNEL_MESSAGE_SERVICE_H
#include "umicom/kernel/message_channel.h"
#include "umicom/kernel/riscv64/user_execution.h"

UmicomKernelMessageStatus UmicomKernelMessageServiceBind(
    UmicomKernelMessageDomain *domain, UmicomU64 processIdentity);
UmicomKernelMessageStatus UmicomKernelMessageServiceUnbind(
    UmicomKernelMessageDomain *domain, UmicomU64 processIdentity);
UmicomBoolean UmicomKernelMessageServiceRecognizes(UmicomU64 number);
/* Internal trap-dispatch entry. Both pointers are Kernel-owned. The caller has
 * already checked user origin, executable PC, next PC and the call budget. */
UmicomKernelMessageStatus UmicomKernelMessageServiceDispatch(
    UmicomKernelUserSession *session, const UmicomRiscvTrapFrame *frame);
#endif /* UMICOM_KERNEL_MESSAGE_SERVICE_H */
