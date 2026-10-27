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

/* Discover the running native contract using only registers. a0 selects one
 * value; a1 and a2 must be zero. No user pointer is read or written. Existing
 * programs keep their operation numbers and do not need to make this call. */
#define UMICOM_USER_CALL_ABI_QUERY 3
#define UMICOM_USER_ABI_QUERY_VERSION 0
#define UMICOM_USER_ABI_QUERY_FEATURES 1
#define UMICOM_USER_ABI_QUERY_WORD_BITS 2
#define UMICOM_USER_ABI_QUERY_BYTE_ORDER 3
#define UMICOM_USER_ABI_QUERY_PAGE_BYTES 4
#define UMICOM_USER_ABI_QUERY_COPY_LIMIT 5
#define UMICOM_USER_ABI_QUERY_CALL_LIMIT 6

/* VERSION packs a 16-bit major and minor value. This is an interface contract,
 * not a product release. BYTE_ORDER uses 1 for little-endian. All-ones is the
 * refusal sentinel for unknown selectors or nonzero reserved arguments. An
 * older monitor returns UNKNOWN_CALL for operation 3, so query VERSION first. */
#define UMICOM_USER_ABI_VERSION 0x00010000
#define UMICOM_USER_ABI_LITTLE_ENDIAN 1
#define UMICOM_USER_ABI_UNAVAILABLE 0xffffffffffffffff

/* These bits describe compiled service routes, never a caller's authority.
 * A present service can still be unbound, deny a handle or refuse a request.
 * Unrecognised future bits must not be interpreted as permission. */
#define UMICOM_USER_ABI_FEATURE_MESSAGES 1
#define UMICOM_USER_ABI_FEATURE_BLOCKING_IPC 2
#define UMICOM_USER_ABI_FEATURE_FILES 4
#define UMICOM_USER_ABI_FEATURE_STREAMS 8
#define UMICOM_USER_ABI_FEATURE_SERVICE_REPORTS 16

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

#define UMICOM_USER_OPERATION_ABI_DISCOVERY 9

/* The executable is position independent within its dedicated text pages;
 * these data addresses are the deliberately shared virtual layout of each
 * isolated address space, not physical addresses or Kernel pointers. */
#define UMICOM_USER_STACK_BASE 0x0000001800010000
#define UMICOM_USER_DATA_BASE 0x0000001800020000
#define UMICOM_USER_READONLY_BASE 0x0000001800040000
#define UMICOM_USER_SUPERVISOR_BASE 0x0000001800050000
#define UMICOM_USER_PAGE_BYTES 4096

#endif /* UMICOM_KERNEL_USER_ABI_H */
