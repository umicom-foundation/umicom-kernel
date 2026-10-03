/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/process.h
 *
 * PURPOSE:
 *   Own the frames, page tables and terminal report of a loaded native program.
 *   A process image has a lifetime even before a scheduler exists.
 *
 * EDUCATIONAL OVERVIEW:
 *   The executable file describes virtual addresses. This owner allocates the
 *   physical frames, records their rights, and retains them after execution so
 *   the caller can inspect an exit or a fault. Destroy removes the page tables
 *   before scrubbing and freeing their backing frames.
 *
 *   Keep this object at a stable, Kernel-owned address and do not copy it.
 *   Its user-memory view borrows pointers into the object itself. This is a
 *   single-hart, eager-loading foundation; it is not a process table, a handle
 *   manager, fork, demand paging, or a concurrent scheduler.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_PROCESS_H
#define UMICOM_KERNEL_PROCESS_H
#include "umicom/kernel/executable.h"
#include "umicom/kernel/riscv64/user_execution.h"

/* The existing checked-copy service has a bounded backing-page catalogue.
 * A change to the image or stack limits must respect that established boundary. */
_Static_assert(UMICOM_EXECUTABLE_MAX_IMAGE_PAGES + UMICOM_EXECUTABLE_STACK_PAGES <=
    UMICOM_USER_MEMORY_MAX_PAGES, "Executable pages must fit user ownership records");

typedef enum UmicomKernelProcessState {
    UMICOM_PROCESS_EMPTY,
    UMICOM_PROCESS_LOADING,
    UMICOM_PROCESS_READY,
    UMICOM_PROCESS_RUNNING,
    UMICOM_PROCESS_EXITED,
    UMICOM_PROCESS_FAULTED,
    UMICOM_PROCESS_TIMED_OUT,
    UMICOM_PROCESS_CALL_LIMIT,
    UMICOM_PROCESS_MONITOR_ERROR,
    UMICOM_PROCESS_CLEANUP_REQUIRED
} UmicomKernelProcessState;

typedef enum UmicomKernelProcessStatus {
    UMICOM_PROCESS_OK,
    UMICOM_PROCESS_INVALID_ARGUMENT,
    UMICOM_PROCESS_BAD_STATE,
    UMICOM_PROCESS_EXECUTABLE_REFUSED,
    UMICOM_PROCESS_OUT_OF_MEMORY,
    UMICOM_PROCESS_MAPPING_ERROR,
    UMICOM_PROCESS_CLEANUP_ERROR,
    UMICOM_PROCESS_ENTRY_REFUSED,
    UMICOM_PROCESS_MACHINE_STATE_ERROR
} UmicomKernelProcessStatus;

typedef struct UmicomKernelProcess {
    UmicomKernelVirtualAddressSpace space; /* Owns page-table frames, not data. */
    UmicomKernelUserPage pages[UMICOM_USER_MEMORY_MAX_PAGES]; /* Owns all listed backing frames. */
    UmicomSize pageCount; /* Includes any allocated page awaiting a successful mapping. */
    UmicomAddress entry;
    UmicomU64 identity; /* Chosen by a trusted caller, not read from the file. */
    UmicomKernelProcessState state;
    UmicomBoolean quiesced; /* True only when no invocation can still use this root. */
    UmicomKernelUserSession report; /* Terminal evidence remains valid until destruction. */
} UmicomKernelProcess;

/* Start with a zero-initialised, stable owner. The image is a non-overlapping,
 * Kernel-owned immutable span for this call. It may be released after success:
 * the running process has its own copies, not references into the ELF buffer.
 * A failed ordinary load rolls back to EMPTY. A cleanup error retains ownership
 * in CLEANUP_REQUIRED rather than pretending the leaked state is free. */
UmicomKernelProcessStatus UmicomKernelProcessCreate(
    UmicomKernelProcess *process,
    const UmicomU8 *image,
    UmicomSize imageBytes,
    UmicomU64 identity,
    UmicomKernelExecutableStatus *outExecutableStatus
);

/* Remove translations first, then scrub and return every backing frame.
 * A RUNNING process cannot be destroyed. No memory is freed merely because a
 * program exited; the Kernel caller makes that lifetime decision explicitly. */
UmicomKernelProcessStatus UmicomKernelProcessDestroy(UmicomKernelProcess *process);

/* Run READY exactly once, using the existing user execution monitor. The
 * argument is an application value, not a Kernel pointer. deadlineTicks is
 * bounded in platform timer ticks. The caller must be hart zero in M-mode,
 * with interrupts and MPRV clear. No process may run concurrently with this one. */
UmicomKernelProcessStatus UmicomKernelProcessRun(
    UmicomKernelProcess *process, UmicomU64 argument, UmicomU64 deadlineTicks
);

const char *UmicomKernelProcessStateName(UmicomKernelProcessState state);
const char *UmicomKernelProcessStatusName(UmicomKernelProcessStatus status);
void UmicomKernelExecutableLoadingValidate(void);
#endif /* UMICOM_KERNEL_PROCESS_H */
