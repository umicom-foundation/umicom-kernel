/* Umicom Kernel scheduling diagnostic contract, not a general application ABI.
 * The harness opens a gate only after both programs have been timer-paused.
 * That avoids making register-preservation tests depend on emulator speed.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_KERNEL_USER_WORKLOAD_H
#define UMICOM_KERNEL_USER_WORKLOAD_H
#include "umicom/kernel/types.h"
#define UMICOM_SCHEDULED_OBSERVATION_ADDRESS ((UmicomAddress)0x00600000U)
#define UMICOM_SCHEDULED_OPERATION_BUSY 0xff01U
#define UMICOM_SCHEDULED_OPERATION_FAULT 0xff02U
#define UMICOM_SCHEDULED_OPERATION_CALLS 0xff03U
#define UMICOM_SCHEDULED_OPERATION_BAD_STACK 0xff04U
/* Assembly uses iterations at offset 8 and the release gate at offset 24. */
typedef struct UmicomKernelScheduledObservation {
    UmicomU64 identity;
    UmicomU64 iterations;
    UmicomU64 starts;
    UmicomU64 release;
    UmicomU64 completed;
    UmicomU64 localResult;
    UmicomU64 magic;
} UmicomKernelScheduledObservation;
_Static_assert(__builtin_offsetof(UmicomKernelScheduledObservation, iterations) == 8U,
    "Scheduled register probe needs the iterations offset");
_Static_assert(__builtin_offsetof(UmicomKernelScheduledObservation, release) == 24U,
    "Scheduled register probe needs the release offset");
#endif
