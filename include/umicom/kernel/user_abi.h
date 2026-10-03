/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/user_abi.h
 *
 * PURPOSE:
 *   Give the small user payload and its Kernel monitor one explicit vocabulary
 *   for environment calls. This is an experimental native contract, not POSIX.
 *
 * EDUCATIONAL OVERVIEW:
 *   A user program cannot call a privileged C function by jumping to it. It
 *   puts a request number in a7, arguments in a0-a2, and executes ECALL. The
 *   processor enters our machine-mode monitor, which checks the request before
 *   returning a result in a0. No Kernel pointer crosses this boundary.
 *
 *   Numbers below identify operations and results, not development releases.
 *   Successful calls preserve integer registers other than a0. EXIT does not
 *   return. An unknown request returns an error rather than stopping the Kernel.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_ABI_H
#define UMICOM_KERNEL_USER_ABI_H

/* Stop this invocation with the application's exit value in a0. */
#define UMICOM_USER_CALL_EXIT 0
/* Obtain the Kernel-assigned identity; a user argument cannot choose it. */
#define UMICOM_USER_CALL_IDENTITY 1
/* Copy a2 bytes from user source a1 to user destination a0 through checked
 * Kernel-owned scratch storage. This teaching service has no device access. */
#define UMICOM_USER_CALL_COPY 2

/* COPY returns zero on success; IDENTITY returns the assigned identity. */
#define UMICOM_USER_RESULT_OK 0
#define UMICOM_USER_RESULT_UNKNOWN_CALL 1
#define UMICOM_USER_RESULT_BAD_ADDRESS 2
#define UMICOM_USER_RESULT_DENIED 3
#define UMICOM_USER_RESULT_TOO_LARGE 4

/* Limit the Kernel's stack buffer and the work one request may demand. */
#define UMICOM_USER_COPY_LIMIT 64
/* A second bound stops a payload that continually enters otherwise valid calls. */
#define UMICOM_USER_CALL_LIMIT 32

/* These modes select deliberate acceptance cases, not application privileges. */
#define UMICOM_USER_OPERATION_NORMAL 0
#define UMICOM_USER_OPERATION_GUARD_LOAD 1
#define UMICOM_USER_OPERATION_READONLY_STORE 2
#define UMICOM_USER_OPERATION_SUPERVISOR_LOAD 3
#define UMICOM_USER_OPERATION_SUPERVISOR_CSR 4
#define UMICOM_USER_OPERATION_NONEXECUTABLE 5
#define UMICOM_USER_OPERATION_INVALID_STACK_EXIT 6
#define UMICOM_USER_OPERATION_CALL_BUDGET 7
#define UMICOM_USER_OPERATION_BUSY_LOOP 8

/* The executable is position independent within its dedicated text pages;
 * these data addresses are the deliberately shared virtual layout of each
 * isolated address space, not physical addresses or Kernel pointers. */
#define UMICOM_USER_STACK_BASE 0x0000001800010000
#define UMICOM_USER_DATA_BASE 0x0000001800020000
#define UMICOM_USER_READONLY_BASE 0x0000001800040000
#define UMICOM_USER_SUPERVISOR_BASE 0x0000001800050000
#define UMICOM_USER_PAGE_BYTES 4096

#endif /* UMICOM_KERNEL_USER_ABI_H */
