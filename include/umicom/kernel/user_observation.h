/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/user_observation.h
 *
 * PURPOSE:
 *   Describe observations the built-in user acceptance payload writes into its
 *   own memory. These are test results, never trusted Kernel policy inputs.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_USER_OBSERVATION_H
#define UMICOM_KERNEL_USER_OBSERVATION_H
#include "umicom/kernel/types.h"

typedef struct UmicomKernelUserObservation {
    UmicomU64 identity; /* What the checked identity call returned. */
    UmicomU64 runs; /* Persistent in this one address space, not globally shared. */
    UmicomU64 stackResult; /* Computed through volatile user-stack locals. */
    UmicomU64 completed; /* Set only after every normal-case check passes. */
    UmicomU64 syscallChecks; /* Number of explicit buffer/refusal checks. */
    UmicomU64 registerCheck; /* Assembly verified several register classes. */
    UmicomAddress stackAddress; /* Observed sp while the user C frame is live. */
} UmicomKernelUserObservation;
#endif /* UMICOM_KERNEL_USER_OBSERVATION_H */
