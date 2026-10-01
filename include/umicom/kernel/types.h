/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/types.h
 *
 * PURPOSE:
 *   Publish the first freestanding fixed-width types used by the native kernel
 *   without depending on a hosted C library or the full Umicom Framework.
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

typedef unsigned char UmiU8;
typedef unsigned short UmiU16;
typedef unsigned int UmiU32;
typedef unsigned long long UmiU64;
typedef signed char UmiI8;
typedef signed short UmiI16;
typedef signed int UmiI32;
typedef signed long long UmiI64;
typedef UmiU64 UmiUsize;
typedef UmiU64 UmiAddress;

_Static_assert(sizeof(UmiU8) == 1U, "UmiU8 must be 8 bits");
_Static_assert(sizeof(UmiU16) == 2U, "UmiU16 must be 16 bits");
_Static_assert(sizeof(UmiU32) == 4U, "UmiU32 must be 32 bits");
_Static_assert(sizeof(UmiU64) == 8U, "UmiU64 must be 64 bits");

#endif
