/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/native_contract/contract_tests.c
 *
 * PURPOSE:
 *   Check ABI reply boundaries and output preservation with an observed call bridge.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static UmicomU64 replies[7] = {0x10000U, 31U, 64U, 1U, 4096U, 64U, 32U};
static UmicomSize calls;
static UmicomU64 Invoke(UmicomU64 operation, UmicomU64 selector, UmicomU64 first, UmicomU64 second)
{
    CHECK(operation == 3U && first == 0U && second == 0U);
    CHECK(selector == (UmicomU64)calls && selector < 7U);
    ++calls;
    return replies[selector];
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *name = argv[1];
    UmicomUserContract out, before;
    memset(&out, 0xa5, sizeof out);
    memcpy(&before, &out, sizeof before);
    UmicomUserContractStatus expected = UMICOM_USER_CONTRACT_OK;
    UmicomSize expectedCalls = 7U;
    UmicomUserInvoke bridge = Invoke;
    UmicomUserContract *target = &out;
    if (!strcmp(name, "current")) {}
    else if (!strcmp(name, "minimal")) replies[1] = 0U;
    else if (!strcmp(name, "future-minor")) replies[0] = 0x1ffffU;
    else if (!strcmp(name, "unknown-features")) replies[1] = (1ULL << 40U) | 1U;
    else if (!strcmp(name, "null-bridge")) {
        bridge = (UmicomUserInvoke)0; expected = UMICOM_USER_CONTRACT_INVALID_ARGUMENT; expectedCalls = 0U;
    } else if (!strcmp(name, "null-output")) {
        target = NULL; expected = UMICOM_USER_CONTRACT_INVALID_ARGUMENT; expectedCalls = 0U;
    } else if (!strcmp(name, "legacy")) {
        replies[0] = 1U; expected = UMICOM_USER_CONTRACT_UNAVAILABLE; expectedCalls = 1U;
    } else if (!strcmp(name, "absent")) {
        replies[0] = ~(UmicomU64)0U; expected = UMICOM_USER_CONTRACT_UNAVAILABLE; expectedCalls = 1U;
    } else if (!strcmp(name, "future-major")) {
        replies[0] = 0x20000U; expected = UMICOM_USER_CONTRACT_INCOMPATIBLE; expectedCalls = 1U;
    } else if (!strcmp(name, "version-overflow")) {
        replies[0] = (1ULL << 32U) | 0x10000U; expected = UMICOM_USER_CONTRACT_INVALID_REPLY; expectedCalls = 1U;
    } else if (!strcmp(name, "word-width")) {
        replies[2] = 32U; expected = UMICOM_USER_CONTRACT_INCOMPATIBLE;
    } else if (!strcmp(name, "byte-order")) {
        replies[3] = 2U; expected = UMICOM_USER_CONTRACT_INCOMPATIBLE;
    } else if (!strcmp(name, "page-size")) {
        replies[4] = 8192U; expected = UMICOM_USER_CONTRACT_INCOMPATIBLE;
    } else if (!strcmp(name, "copy-zero")) {
        replies[5] = 0U; expected = UMICOM_USER_CONTRACT_INVALID_REPLY;
    } else if (!strcmp(name, "copy-too-large")) {
        replies[5] = 4097U; expected = UMICOM_USER_CONTRACT_INVALID_REPLY;
    } else if (!strcmp(name, "call-budget")) {
        replies[6] = 6U; expected = UMICOM_USER_CONTRACT_INVALID_REPLY;
    } else if (!strncmp(name, "unavailable-", 12U) && strlen(name) == 13U &&
               name[12] >= '1' && name[12] <= '6') {
        replies[(unsigned)(name[12] - '0')] = ~(UmicomU64)0U;
        expected = UMICOM_USER_CONTRACT_UNAVAILABLE;
    } else return 2;
    CHECK(UmicomUserContractDiscover(bridge, target) == expected);
    CHECK(calls == expectedCalls);
    if (expected != UMICOM_USER_CONTRACT_OK)
        CHECK(memcmp(&out, &before, sizeof out) == 0);
    else {
        CHECK(out.version == replies[0] && out.features == replies[1]);
        CHECK(out.wordBits == 64U && out.byteOrder == 1U && out.pageBytes == 4096U);
        CHECK(out.copyLimit == 64U && out.callLimit == 32U);
    }
    return 0;
}
