/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/types.h
 *
 * PURPOSE:
 *   Define the small set of fixed-width integer/address types K1 needs without
 *   importing hosted libc headers or the full Umicom Framework.
 *
 * EDUCATIONAL NOTE:
 *   A freestanding kernel cannot assume that Windows/Linux system headers are
 *   available or appropriate.  These first definitions make the intended bit
 *   widths explicit and immediately verify them at compile time.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_TYPES_H
#define UMICOM_KERNEL_TYPES_H

/* Unsigned 8-bit integer used for raw bytes and byte-wide MMIO registers. */
typedef unsigned char UmiU8;

/* Unsigned 16-bit integer reserved for fixed-width data/protocol fields. */
typedef unsigned short UmiU16;

/* Unsigned 32-bit integer used by the QEMU test-finisher MMIO register. */
typedef unsigned int UmiU32;

/* Unsigned 64-bit integer used for RV64 counters, values and addresses. */
typedef unsigned long long UmiU64;

/* Signed 8-bit integer for future fixed-width signed values. */
typedef signed char UmiI8;

/* Signed 16-bit integer for future fixed-width signed values. */
typedef signed short UmiI16;

/* Signed 32-bit integer for future fixed-width signed values. */
typedef signed int UmiI32;

/* Signed 64-bit integer for future fixed-width signed values. */
typedef signed long long UmiI64;

/* K1's size type is 64-bit because the target ABI is LP64/RV64. */
typedef UmiU64 UmiUsize;

/* A physical/virtual address-sized integer for this first RV64 milestone. */
typedef UmiU64 UmiAddress;

/* Stop compilation if the compiler does not provide an 8-bit unsigned char. */
_Static_assert(sizeof(UmiU8) == 1U, "UmiU8 must occupy exactly 1 byte");

/* Stop compilation if unsigned short is not the required 16-bit width. */
_Static_assert(sizeof(UmiU16) == 2U, "UmiU16 must occupy exactly 2 bytes");

/* Stop compilation if unsigned int is not the required 32-bit width. */
_Static_assert(sizeof(UmiU32) == 4U, "UmiU32 must occupy exactly 4 bytes");

/* Stop compilation if unsigned long long is not the required 64-bit width. */
_Static_assert(sizeof(UmiU64) == 8U, "UmiU64 must occupy exactly 8 bytes");

#endif /* UMICOM_KERNEL_TYPES_H */
