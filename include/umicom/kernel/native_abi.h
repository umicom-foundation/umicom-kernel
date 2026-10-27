/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/native_abi.h
 *
 * PURPOSE:
 *   Expose one read-only source of native ABI metadata to the monitor and console.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_NATIVE_ABI_H
#define UMICOM_KERNEL_NATIVE_ABI_H
#include "umicom/kernel/types.h"
#include "umicom/kernel/user_abi.h"

/* Trusted Kernel callers may read compiled metadata without simulating ECALL.
 * User programs must use operation ABI_QUERY: its trap entry retains origin,
 * instruction and invocation-budget checks. This reader accepts no pointer,
 * creates no handle and grants no rights. Unknown selectors or nonzero
 * reserved values return UMICOM_USER_ABI_UNAVAILABLE. */
UmicomU64 UmicomKernelUserAbiQuery(UmicomU64 selector,
    UmicomU64 reservedFirst, UmicomU64 reservedSecond);
#endif
