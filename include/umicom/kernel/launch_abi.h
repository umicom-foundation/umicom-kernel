/*-----------------------------------------------------------------------------
 * Umicom native program launch contract
 * File: include/umicom/kernel/launch_abi.h
 *
 * The first instruction receives argc in a0, argv in a1, envp in a2 and the
 * address of this information record in a3. The entry shim may call ordinary
 * C with those four arguments. This is an Umicom entry contract, not Linux's
 * process stack layout or a claim of POSIX executable compatibility.
 *
 * All addresses below point into the receiving program's own writable stack
 * allocation. No Kernel pointer or ambient authority is carried by a string.
 * Both pointer arrays end in a zero address. Strings end in NUL; empty argv
 * elements are allowed. The application may change its copies after entry.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_LAUNCH_ABI_H
#define UMICOM_KERNEL_LAUNCH_ABI_H
#include "umicom/kernel/types.h"

#define UMICOM_LAUNCH_ARGUMENT_LIMIT 16U
#define UMICOM_LAUNCH_ENVIRONMENT_LIMIT 8U
#define UMICOM_LAUNCH_STRING_LIMIT 255U
#define UMICOM_LAUNCH_TEXT_BYTES 1536U
#define UMICOM_LAUNCH_BLOCK_BYTES 2048U
/* A recognisable type tag distinguishes this record from arbitrary stack data. */
#define UMICOM_LAUNCH_COOKIE ((UmicomU64)0x554d49434f4d4152ULL)

typedef struct UmicomProgramLaunchInfo {
    UmicomU64 cookie;       /* Recognise the native structured-entry contract. */
    UmicomU64 bytes;        /* Full reserved block extent, including zero padding. */
    UmicomU64 argumentCount;
    UmicomU64 environmentCount;
    UmicomU64 arguments;    /* User virtual address of the terminated argv array. */
    UmicomU64 environment;  /* User virtual address of the terminated envp array. */
    UmicomU64 text;         /* First string byte in the same owned user block. */
    UmicomU64 textBytes;    /* Includes every string's terminating NUL. */
} UmicomProgramLaunchInfo;
_Static_assert(sizeof(UmicomProgramLaunchInfo) == 64U,
    "Launch metadata must have eight explicit RV64 words");
#endif /* UMICOM_KERNEL_LAUNCH_ABI_H */
