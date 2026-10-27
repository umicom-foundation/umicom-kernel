/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/user/native_contract.h
 *
 * PURPOSE:
 *   Discover a native program contract through registers before using Kernel services.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_USER_NATIVE_CONTRACT_H
#define UMICOM_USER_NATIVE_CONTRACT_H
#include "umicom/kernel/types.h"

/* This callback is the program's Assembly ECALL bridge, not a Kernel function.
 * Host tests substitute replies without pretending that a privilege transition
 * took place. No context or Kernel address is exchanged with the monitor. */
typedef UmicomU64 (*UmicomUserInvoke)(UmicomU64 number, UmicomU64 first,
    UmicomU64 second, UmicomU64 third);

typedef struct UmicomUserContract {
    UmicomU64 version;
    UmicomU64 features;
    UmicomU64 wordBits;
    UmicomU64 byteOrder;
    UmicomU64 pageBytes;
    UmicomU64 copyLimit;
    UmicomU64 callLimit;
} UmicomUserContract;

typedef enum UmicomUserContractStatus {
    UMICOM_USER_CONTRACT_OK = 0,
    UMICOM_USER_CONTRACT_INVALID_ARGUMENT,
    UMICOM_USER_CONTRACT_UNAVAILABLE,
    UMICOM_USER_CONTRACT_INCOMPATIBLE,
    UMICOM_USER_CONTRACT_INVALID_REPLY
} UmicomUserContractStatus;

/* Query VERSION first: a legacy monitor's UNKNOWN_CALL must not be interpreted
 * as a feature bit. This reader supports major 1, any minor, RV64 little-endian
 * and 4096-byte pages. Unknown feature bits are preserved, not granted as rights.
 * At most seven ECALLs are used; these consume the ordinary invocation budget.
 * A failure leaves every output byte untouched. Output is caller-owned, aligned
 * writable storage in this program; the callback must not modify it or retain
 * any stack pointers. This is synchronous, allocation-free, and has no libc
 * dependency. It does not authenticate the monitor or bind service handles. */
UmicomUserContractStatus UmicomUserContractDiscover(
    UmicomUserInvoke invoke, UmicomUserContract *outContract);
/* Requirements are a local C interface, not a wire message or Kernel address.
 * structureBytes permits future trailing optional fields; all fields in this
 * declaration must exist. Unknown tails are ignored, not interpreted as policy.
 * Calls include discovery and any prior calls; callsNeeded includes final EXIT.
 * A successful check predicts compatibility, never authority or execution. */
typedef struct UmicomUserRequirements {
    UmicomU64 structureBytes;
    UmicomU64 minimumMinorVersion;
    UmicomU64 requiredFeatures;
    UmicomU64 minimumCopyBytes;
    UmicomU64 callsAlreadyUsed;
    UmicomU64 callsNeeded;
} UmicomUserRequirements;

typedef enum UmicomUserReadiness {
    UMICOM_USER_READY = 0,
    UMICOM_USER_REQUIREMENTS_INVALID,
    UMICOM_USER_CONTRACT_MALFORMED,
    UMICOM_USER_VERSION_TOO_OLD,
    UMICOM_USER_FEATURE_MISSING,
    UMICOM_USER_COPY_LIMIT_INSUFFICIENT,
    UMICOM_USER_CALL_LIMIT_INSUFFICIENT
} UmicomUserReadiness;

/* Both arguments borrow readable, aligned caller-owned storage for this call.
 * Only structureBytes is read when that size is too small. No input is changed,
 * no ECALL is made and no budget is reserved. The caller must track its own
 * calls accurately, use a fresh discovery result and still handle each service
 * refusal. Concurrent requests cannot use this estimate as synchronization. */
UmicomUserReadiness UmicomUserContractCheck(
    const UmicomUserContract *contract, const UmicomUserRequirements *requirements);

#endif
