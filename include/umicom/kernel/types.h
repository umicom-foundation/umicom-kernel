/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/types.h
 *
 * PURPOSE:
 *   Define the small fixed-width and address-sized types used by the
 *   freestanding Umicom Kernel without importing a hosted operating-system
 *   runtime or the full Umicom Framework.
 *
 * EDUCATIONAL OVERVIEW:
 *   A kernel cannot assume that Windows or Linux headers describe the target
 *   machine correctly.  The types below make the intended widths explicit.
 *
 *   K1 and K2 originally used short "Umi..." type names.  K3 establishes the
 *   full "Umicom..." naming convention requested for new Kernel development.
 *   The original aliases remain at the bottom of this file so already
 *   committed K1/K2 source and any early experiments continue to compile.
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

/* Unsigned 8-bit integer.
 *
 * K3 uses this for raw bytes and byte-wide device/bitmap fields. */
typedef unsigned char UmicomU8;

/* Unsigned 16-bit integer for fixed-width protocol or hardware values. */
typedef unsigned short UmicomU16;

/* Unsigned 32-bit integer for fixed-width counters and MMIO fields. */
typedef unsigned int UmicomU32;

/* Unsigned 64-bit integer for RV64 counters, physical addresses and sizes. */
typedef unsigned long long UmicomU64;

/* Signed 8-bit integer retained for future low-level signed values. */
typedef signed char UmicomI8;

/* Signed 16-bit integer retained for future low-level signed values. */
typedef signed short UmicomI16;

/* Signed 32-bit integer retained for future low-level signed values. */
typedef signed int UmicomI32;

/* Signed 64-bit integer retained for future low-level signed values. */
typedef signed long long UmicomI64;

/* Use the compiler's pointer-sized unsigned integer spelling.
 *
 * __UINTPTR_TYPE__ is a Clang/GCC compiler-provided freestanding type and does
 * not require <stdint.h>.  On our RV64 target it is 64 bits wide. */
typedef __UINTPTR_TYPE__ UmicomUIntPtr;

/* A byte count or collection size for this RV64 milestone. */
typedef UmicomU64 UmicomSize;

/* Integer representation of a physical or virtual address.
 *
 * K3 manages only physical addresses, but using one explicit address type keeps
 * later API reviews clear about when virtual-address support is introduced. */
typedef UmicomUIntPtr UmicomAddress;

/* Tiny boolean type used in freestanding helpers.
 *
 * We deliberately avoid importing hosted boolean headers just to express two
 * states in these first kernel milestones. */
typedef enum UmicomBoolean {
    /* Logical false. */
    UMICOM_FALSE = 0,

    /* Logical true. */
    UMICOM_TRUE = 1
} UmicomBoolean;

/* Stop compilation if the target/compiler does not provide an 8-bit byte. */
_Static_assert(
    sizeof(UmicomU8) == 1U,
    "UmicomU8 must occupy exactly 1 byte"
);

/* Stop compilation if unsigned short is not the expected 16-bit width. */
_Static_assert(
    sizeof(UmicomU16) == 2U,
    "UmicomU16 must occupy exactly 2 bytes"
);

/* Stop compilation if unsigned int is not the expected 32-bit width. */
_Static_assert(
    sizeof(UmicomU32) == 4U,
    "UmicomU32 must occupy exactly 4 bytes"
);

/* Stop compilation if unsigned long long is not the expected 64-bit width. */
_Static_assert(
    sizeof(UmicomU64) == 8U,
    "UmicomU64 must occupy exactly 8 bytes"
);

/* RV64 requires the compiler's pointer-sized integer to be 64 bits. */
_Static_assert(
    sizeof(UmicomUIntPtr) == 8U,
    "UmicomUIntPtr must occupy exactly 8 bytes on RV64"
);

/*-------------------------------------------------------------------------
 * K1/K2 SOURCE-COMPATIBILITY ALIASES
 *
 * Do not remove these aliases merely to make the source look newer.  They
 * preserve the already committed educational milestones while new code adopts
 * the full Umicom names.
 *-------------------------------------------------------------------------*/

/* Historical K1/K2 unsigned byte alias. */
typedef UmicomU8 UmiU8;

/* Historical K1/K2 unsigned 16-bit alias. */
typedef UmicomU16 UmiU16;

/* Historical K1/K2 unsigned 32-bit alias. */
typedef UmicomU32 UmiU32;

/* Historical K1/K2 unsigned 64-bit alias. */
typedef UmicomU64 UmiU64;

/* Historical K1/K2 signed 8-bit alias. */
typedef UmicomI8 UmiI8;

/* Historical K1/K2 signed 16-bit alias. */
typedef UmicomI16 UmiI16;

/* Historical K1/K2 signed 32-bit alias. */
typedef UmicomI32 UmiI32;

/* Historical K1/K2 signed 64-bit alias. */
typedef UmicomI64 UmiI64;

/* Historical K1/K2 size alias. */
typedef UmicomSize UmiUsize;

/* Historical K1/K2 address alias. */
typedef UmicomAddress UmiAddress;

#endif /* UMICOM_KERNEL_TYPES_H */
