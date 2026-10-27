/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/support/native_contract.c
 *
 * PURPOSE:
 *   Validate versioned register replies and publish only a complete compatible contract.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/user/native_contract.h"
#include "umicom/kernel/user_abi.h"

UmicomUserContractStatus UmicomUserContractDiscover(
    UmicomUserInvoke invoke, UmicomUserContract *outContract)
{
    if (invoke == (UmicomUserInvoke)0 || outContract == (UmicomUserContract *)0)
        return UMICOM_USER_CONTRACT_INVALID_ARGUMENT;

    /* Keep untrusted replies local until all shape checks succeed. In
     * particular, legacy UNKNOWN_CALL is 1, which is also a valid feature bit. */
    UmicomUserContract value;
    value.version = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_VERSION, 0U, 0U);
    if (value.version == UMICOM_USER_ABI_UNAVAILABLE ||
        value.version == UMICOM_USER_RESULT_UNKNOWN_CALL)
        return UMICOM_USER_CONTRACT_UNAVAILABLE;
    if ((value.version >> 32U) != 0U)
        return UMICOM_USER_CONTRACT_INVALID_REPLY;
    if ((value.version >> 16U) != 1U)
        return UMICOM_USER_CONTRACT_INCOMPATIBLE;

    value.features = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_FEATURES, 0U, 0U);
    value.wordBits = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_WORD_BITS, 0U, 0U);
    value.byteOrder = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_BYTE_ORDER, 0U, 0U);
    value.pageBytes = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_PAGE_BYTES, 0U, 0U);
    value.copyLimit = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_COPY_LIMIT, 0U, 0U);
    value.callLimit = invoke(UMICOM_USER_CALL_ABI_QUERY, UMICOM_USER_ABI_QUERY_CALL_LIMIT, 0U, 0U);
    if (value.features == UMICOM_USER_ABI_UNAVAILABLE ||
        value.wordBits == UMICOM_USER_ABI_UNAVAILABLE ||
        value.byteOrder == UMICOM_USER_ABI_UNAVAILABLE ||
        value.pageBytes == UMICOM_USER_ABI_UNAVAILABLE ||
        value.copyLimit == UMICOM_USER_ABI_UNAVAILABLE ||
        value.callLimit == UMICOM_USER_ABI_UNAVAILABLE)
        return UMICOM_USER_CONTRACT_UNAVAILABLE;
    if (value.wordBits != 64U || value.byteOrder != UMICOM_USER_ABI_LITTLE_ENDIAN ||
        value.pageBytes != UMICOM_USER_PAGE_BYTES)
        return UMICOM_USER_CONTRACT_INCOMPATIBLE;
    if (value.copyLimit == 0U || value.copyLimit > value.pageBytes || value.callLimit < 7U)
        return UMICOM_USER_CONTRACT_INVALID_REPLY;

    /* Explicit scalar publication avoids an implicit freestanding memcpy.
     * No callbacks or fallible work occur after publication starts. */
    outContract->version = value.version;
    outContract->features = value.features;
    outContract->wordBits = value.wordBits;
    outContract->byteOrder = value.byteOrder;
    outContract->pageBytes = value.pageBytes;
    outContract->copyLimit = value.copyLimit;
    outContract->callLimit = value.callLimit;
    return UMICOM_USER_CONTRACT_OK;
}

UmicomUserReadiness UmicomUserContractCheck(
    const UmicomUserContract *contract, const UmicomUserRequirements *requirements)
{
    if (contract == (const UmicomUserContract *)0 ||
        requirements == (const UmicomUserRequirements *)0)
        return UMICOM_USER_REQUIREMENTS_INVALID;
    /* Check the shared prefix before reading policy fields. An extended caller
     * can retain additional optional metadata without this reader accessing it. */
    if (requirements->structureBytes < (UmicomU64)sizeof(UmicomUserRequirements))
        return UMICOM_USER_REQUIREMENTS_INVALID;
    if (requirements->minimumMinorVersion > 0xffffU)
        return UMICOM_USER_REQUIREMENTS_INVALID;
    /* A copied or edited result is not automatically a discovery result. Reject
     * malformed machine shape before comparing application requirements. */
    if ((contract->version >> 16U) != 1U ||
        contract->wordBits != 64U || contract->byteOrder != UMICOM_USER_ABI_LITTLE_ENDIAN ||
        contract->pageBytes != UMICOM_USER_PAGE_BYTES ||
        contract->features == UMICOM_USER_ABI_UNAVAILABLE ||
        contract->copyLimit == 0U || contract->copyLimit > contract->pageBytes ||
        contract->callLimit == UMICOM_USER_ABI_UNAVAILABLE || contract->callLimit < 7U)
        return UMICOM_USER_CONTRACT_MALFORMED;
    if ((contract->version & 0xffffU) < requirements->minimumMinorVersion)
        return UMICOM_USER_VERSION_TOO_OLD;
    if ((contract->features & requirements->requiredFeatures) != requirements->requiredFeatures)
        return UMICOM_USER_FEATURE_MISSING;
    if (contract->copyLimit < requirements->minimumCopyBytes)
        return UMICOM_USER_COPY_LIMIT_INSUFFICIENT;
    /* Subtraction after proving the bound avoids overflow in used + needed.
     * This is a local estimate, not a mutable reservation in the monitor. */
    if (requirements->callsAlreadyUsed > contract->callLimit ||
        requirements->callsNeeded > contract->callLimit - requirements->callsAlreadyUsed)
        return UMICOM_USER_CALL_LIMIT_INSUFFICIENT;
    return UMICOM_USER_READY;
}
