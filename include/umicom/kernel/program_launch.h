/*-----------------------------------------------------------------------------
 * Umicom Kernel structured launch preparation
 * File: include/umicom/kernel/program_launch.h
 *
 * A supervisor supplies bounded Kernel-owned spans, not unchecked user C
 * strings. Preparation snapshots them before touching the new program's stack.
 * The caller keeps inputs stable for the call; no input is borrowed afterwards.
 * This service neither loads an image nor changes permissions or credentials.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_PROGRAM_LAUNCH_H
#define UMICOM_KERNEL_PROGRAM_LAUNCH_H
#include "umicom/kernel/launch_abi.h"
#include "umicom/kernel/user_memory.h"
#include "umicom/kernel/riscv64/trap.h"

typedef struct UmicomKernelLaunchString {
    const char *data; /* Trusted readable Kernel storage for exactly bytes bytes. */
    UmicomSize bytes; /* Excludes the NUL appended by the packer. */
} UmicomKernelLaunchString;
typedef struct UmicomKernelProgramLaunchSpec {
    const UmicomKernelLaunchString *arguments;
    UmicomSize argumentCount; /* Includes argv[0], normally the executable path. */
    const UmicomKernelLaunchString *environment;
    UmicomSize environmentCount; /* Explicit allowlist; nothing is inherited. */
} UmicomKernelProgramLaunchSpec;

typedef enum UmicomKernelLaunchStatus {
    UMICOM_LAUNCH_OK,
    UMICOM_LAUNCH_INVALID_ARGUMENT,
    UMICOM_LAUNCH_TOO_LARGE,
    UMICOM_LAUNCH_INVALID_ENVIRONMENT,
    UMICOM_LAUNCH_DUPLICATE_ENVIRONMENT,
    UMICOM_LAUNCH_BAD_MEMORY,
    UMICOM_LAUNCH_UNSAFE
} UmicomKernelLaunchStatus;

/* The layout uses only fixed-width words and bytes. Padding is explicit so
 * neither Kernel stack data nor compiler padding is exported accidentally. */
typedef struct UmicomKernelLaunchImage {
    UmicomProgramLaunchInfo info;
    UmicomU64 arguments[UMICOM_LAUNCH_ARGUMENT_LIMIT + 1U];
    UmicomU64 environment[UMICOM_LAUNCH_ENVIRONMENT_LIMIT + 1U];
    UmicomU8 text[UMICOM_LAUNCH_TEXT_BYTES];
    UmicomU8 padding[UMICOM_LAUNCH_BLOCK_BYTES - sizeof(UmicomProgramLaunchInfo) -
        (UMICOM_LAUNCH_ARGUMENT_LIMIT + UMICOM_LAUNCH_ENVIRONMENT_LIMIT + 2U) * 8U -
        UMICOM_LAUNCH_TEXT_BYTES];
} UmicomKernelLaunchImage;
_Static_assert(sizeof(UmicomKernelLaunchImage) == UMICOM_LAUNCH_BLOCK_BYTES,
    "The launch block has an exact, bounded user-memory extent");

/* Produce a detached value image. Invalid spans/strings leave outImage
 * unchanged. Pointers in the result are relocated USER addresses based on
 * virtualBase. All input/output storage is trusted, stable and non-overlapping. */
UmicomKernelLaunchStatus UmicomKernelProgramLaunchPack(
    const UmicomKernelProgramLaunchSpec *spec, UmicomAddress virtualBase,
    UmicomKernelLaunchImage *outImage);
/* Internal scheduler operation on an unstarted image. The complete write
 * region is checked before any chunk is copied. outFrame is published only
 * after the block is copied. The scheduler enforces fresh-task ownership. */
UmicomKernelLaunchStatus UmicomKernelProgramLaunchPrepare(
    const UmicomKernelUserMemory *memory, const UmicomKernelProgramLaunchSpec *spec,
    UmicomRiscvTrapFrame *outFrame);
const char *UmicomKernelLaunchStatusName(UmicomKernelLaunchStatus status);
void UmicomKernelProgramLaunchValidateExecution(void);
#endif /* UMICOM_KERNEL_PROGRAM_LAUNCH_H */
