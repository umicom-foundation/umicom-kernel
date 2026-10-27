/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/native_contract/requirements_tests.c
 *
 * PURPOSE:
 *   Verify native requirements reject missing services and exhausted budgets
 *   without mutating caller state or overflowing arithmetic.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/user/native_contract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *name = argv[1];
    UmicomUserContract contract = {0x10000U, 31U, 64U, 1U, 4096U, 64U, 32U};
    UmicomUserRequirements requirements = {sizeof requirements, 0U, 4U, 64U, 9U, 23U};
    const UmicomUserContract *input = &contract;
    const UmicomUserRequirements *policy = &requirements;
    UmicomUserReadiness expected = UMICOM_USER_READY;
    if (!strcmp(name, "exact-limits")) {}
    else if (!strcmp(name, "null-contract")) { input = NULL; expected = UMICOM_USER_REQUIREMENTS_INVALID; }
    else if (!strcmp(name, "null-requirements")) { policy = NULL; expected = UMICOM_USER_REQUIREMENTS_INVALID; }
    else if (!strcmp(name, "short-prefix")) { requirements.structureBytes = sizeof requirements - 1U; expected = UMICOM_USER_REQUIREMENTS_INVALID; }
    else if (!strcmp(name, "unknown-tail")) { requirements.structureBytes = sizeof requirements + 16U; }
    else if (!strcmp(name, "minor-invalid")) { requirements.minimumMinorVersion = 0x10000U; expected = UMICOM_USER_REQUIREMENTS_INVALID; }
    else if (!strcmp(name, "minor-newer")) { requirements.minimumMinorVersion = 1U; expected = UMICOM_USER_VERSION_TOO_OLD; }
    else if (!strcmp(name, "minor-supported")) { contract.version = 0x10002U; requirements.minimumMinorVersion = 2U; }
    else if (!strcmp(name, "missing-feature")) { contract.features = 3U; expected = UMICOM_USER_FEATURE_MISSING; }
    else if (!strcmp(name, "unknown-feature-needed")) { requirements.requiredFeatures = 1ULL << 40U; expected = UMICOM_USER_FEATURE_MISSING; }
    else if (!strcmp(name, "unknown-feature-present")) { requirements.requiredFeatures = 1ULL << 40U; contract.features |= requirements.requiredFeatures; }
    else if (!strcmp(name, "copy-too-large")) { requirements.minimumCopyBytes = 65U; expected = UMICOM_USER_COPY_LIMIT_INSUFFICIENT; }
    else if (!strcmp(name, "no-copy-needed")) { requirements.minimumCopyBytes = 0U; }
    else if (!strcmp(name, "used-too-many")) { requirements.callsAlreadyUsed = 33U; expected = UMICOM_USER_CALL_LIMIT_INSUFFICIENT; }
    else if (!strcmp(name, "one-too-many")) { requirements.callsNeeded = 24U; expected = UMICOM_USER_CALL_LIMIT_INSUFFICIENT; }
    else if (!strcmp(name, "sum-overflow")) { requirements.callsNeeded = ~(UmicomU64)0U; expected = UMICOM_USER_CALL_LIMIT_INSUFFICIENT; }
    else if (!strcmp(name, "used-overflow")) { requirements.callsAlreadyUsed = ~(UmicomU64)0U; expected = UMICOM_USER_CALL_LIMIT_INSUFFICIENT; }
    else if (!strcmp(name, "budget-zero-remaining")) { requirements.callsAlreadyUsed = 32U; requirements.callsNeeded = 0U; }
    else if (!strcmp(name, "exit-still-needed")) { requirements.callsAlreadyUsed = 32U; requirements.callsNeeded = 1U; expected = UMICOM_USER_CALL_LIMIT_INSUFFICIENT; }
    else if (!strcmp(name, "malformed-version")) { contract.version = (1ULL << 32U) | 0x10000U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-features")) { contract.features = ~(UmicomU64)0U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-machine")) { contract.wordBits = 32U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-page")) { contract.pageBytes = 8192U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-order")) { contract.byteOrder = 2U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-copy")) { contract.copyLimit = 4097U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-zero-copy")) { contract.copyLimit = 0U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-budget")) { contract.callLimit = 6U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else if (!strcmp(name, "malformed-unavailable-budget")) { contract.callLimit = ~(UmicomU64)0U; expected = UMICOM_USER_CONTRACT_MALFORMED; }
    else return 2;
    unsigned char beforeContract[sizeof contract], beforeRequirements[sizeof requirements];
    memcpy(beforeContract, &contract, sizeof contract);
    memcpy(beforeRequirements, &requirements, sizeof requirements);
    CHECK(UmicomUserContractCheck(input, policy) == expected);
    CHECK(memcmp(beforeContract, &contract, sizeof contract) == 0);
    CHECK(memcmp(beforeRequirements, &requirements, sizeof requirements) == 0);
    return 0;
}
