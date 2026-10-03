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
 *   machine correctly.  The types below make the intended widths explicit and
 *   allow the compiler to reject a target whose fundamental integer widths do
 *   not match the assumptions made by the low-level RISC-V implementation.
 *
 *   The active Kernel vocabulary uses the full "Umicom..." project name.
 *   Short compatibility aliases are deliberately not part of the interface.
 *   Git history already preserves earlier naming experiments without making
 *   those experiments part of today's compiled API.
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
 * The Kernel uses this for raw bytes and byte-wide device/bitmap fields. */
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
 * The current memory manager starts with physical addresses, but using one explicit address type keeps
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

/*-----------------------------------------------------------------------------
 * HISTORICAL SHORT-NAME DEFINITIONS — RETAINED FOR REVIEW, NOT COMPILED
 *
 * Earlier experimental source used the shorter `Umi...` type names shown
 * below.  The active Kernel now uses the full `Umicom...` names declared
 * above.  The historical definitions are deliberately kept in this file so a
 * new developer can see how the naming evolved and so Beyond Compare/Git
 * history remains easy to follow.
 *
 * They are disabled instead of being deleted.  Re-enabling them would require
 * an explicit compatibility decision; new Kernel code must use the full
 * `Umicom...` names.
 *---------------------------------------------------------------------------*/
#if 0
/*-------------------------------------------------------------------------
 * LEGACY SOURCE-COMPATIBILITY ALIASES
 *
 * Do not remove these aliases merely to make the source look newer.  They
 * preserve the already committed educational milestones while new code adopts
 * the full Umicom names.
 *-------------------------------------------------------------------------*/

/* Legacy unsigned byte alias. */
typedef UmicomU8 UmiU8;

/* Legacy unsigned 16-bit alias. */
typedef UmicomU16 UmiU16;

/* Legacy unsigned 32-bit alias. */
typedef UmicomU32 UmiU32;

/* Legacy unsigned 64-bit alias. */
typedef UmicomU64 UmiU64;

/* Legacy signed 8-bit alias. */
typedef UmicomI8 UmiI8;

/* Legacy signed 16-bit alias. */
typedef UmicomI16 UmiI16;

/* Legacy signed 32-bit alias. */
typedef UmicomI32 UmiI32;

/* Legacy signed 64-bit alias. */
typedef UmicomI64 UmiI64;

/* Legacy size alias. */
typedef UmicomSize UmiUsize;

/* Legacy address alias. */
typedef UmicomAddress UmiAddress;

#endif

#endif /* UMICOM_KERNEL_TYPES_H */
