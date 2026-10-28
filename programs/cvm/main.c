/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/main.c
 *
 * PURPOSE:
 *   Connect the bounded CVM runtime to the native user-program contract.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/types.h"
#include "umicom_c/umicom_c.h"
#include "umicom_c/comparison.h"

/* The loaded program owns all working buffers. The runtime never receives a
 * Kernel pointer and cannot issue a syscall or allocate executable memory. */
static const UmcInstruction arithmetic[] = {
    {UMC_PUSH, 7, 0U}, {UMC_PUSH, 6, 0U},
    {UMC_MULTIPLY, 0, 0U}, {UMC_RETURN, 0, 0U}
};
static const UmcInstruction zero_divisor[] = {
    {UMC_PUSH, 7, 0U}, {UMC_PUSH, 0, 0U},
    {UMC_DIVIDE, 0, 0U}, {UMC_RETURN, 0, 0U}
};
UmicomU64 UmicomProgramMain(UmicomU64 argument)
{
    if (argument > 3U) return 0xc004U;
    uint8_t bytes[UMC_BYTECODE_HEADER_SIZE + 4U * UMC_BYTECODE_RECORD_SIZE];
    UmcInstruction decoded[4];
    UmcDiagnostic diagnostic;
    UmcResult result;
    result.value = 19;
    result.instructions_executed = 0U;
    size_t size = 0U;
    size_t count = 0U;
    const UmcInstruction *input = argument == 2U ? zero_divisor : arithmetic;
    if (umc_encode(input, 4U, bytes, sizeof bytes, &size, &diagnostic) != UMC_OK)
        return 0xe001U;
    /* Corruption must be rejected before a partial decode is admitted to the VM. */
    if (argument == 3U) bytes[5] = 1U;
    const UmcStatus decode = umc_decode(bytes, size, decoded, 4U, &count, &diagnostic);
    if (argument == 3U)
        return decode == UMC_BAD_BYTECODE && count == 0U ? 0xc003U : 0xe002U;
    if (decode != UMC_OK || count != 4U) return 0xe003U;
    bool same = false;
    if (umc_program_same_operations(input, 4U, decoded, count, &same, &diagnostic) != UMC_OK || !same)
        return 0xe007U;
    const UmcStatus status = umc_execute(decoded, count,
        argument == 1U ? 3U : 4U, &result, &diagnostic);
    if (argument == 1U)
        return status == UMC_FUEL_EXHAUSTED && result.value == 19 &&
            result.instructions_executed == 0U ? 0xc001U : 0xe004U;
    if (argument == 2U)
        return status == UMC_ARITHMETIC_ERROR && result.value == 19 &&
            result.instructions_executed == 0U ? 0xc002U : 0xe005U;
    return status == UMC_OK && result.instructions_executed == 4U &&
        result.value == 42 ? 42U : 0xe006U;
}
