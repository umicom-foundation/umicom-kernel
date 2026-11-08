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
#include "umicom_c/trace.h"
#ifndef UMC_COMPARISON_PROFILE
#error "CVM comparison selectors require the comparison-capable canonical Umicom C runtime."
#endif

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
/* Comparisons use the same canonical runtime and portable encoder as arithmetic.
 * Separate selectors keep the earlier envelope result contract unchanged. */
static UmicomU64 CheckComparison(UmicomU64 argument)
{
    /* Static immutable fixtures avoid an aggregate copy that a freestanding
     * compiler could lower to a hosted memcpy dependency at O0. */
    static const UmcInstruction true_input[4] = {
        {UMC_PUSH, INT32_MIN, 0U}, {UMC_PUSH, INT32_MAX, 2U},
        {UMC_LESS, 0, 1U}, {UMC_RETURN, 0, 3U}
    };
    static const UmcInstruction false_input[4] = {
        {UMC_PUSH, INT32_MAX, 0U}, {UMC_PUSH, INT32_MIN, 2U},
        {UMC_LESS, 0, 1U}, {UMC_RETURN, 0, 3U}
    };
    const UmcInstruction *input = argument == 10U ? false_input : true_input;
    uint8_t bytes[UMC_BYTECODE_HEADER_SIZE + 4U * UMC_BYTECODE_RECORD_SIZE];
    UmcInstruction decoded[4];
    UmcTraceStep steps[4];
    UmcDiagnostic diagnostic;
    UmcResult result;
    result.value = 19;
    result.instructions_executed = 0U;
    size_t size = 0U, count = 99U, used = 99U;
    if (umc_encode(input, 4U, bytes, sizeof bytes, &size, &diagnostic) != UMC_OK || bytes[4] != 2U)
        return 0xe00cU;
    if (argument == 11U) bytes[4] = 1U;
    const UmcStatus decoded_status = umc_decode(bytes, size, decoded, 4U, &count, &diagnostic);
    if (argument == 11U)
        return decoded_status == UMC_BAD_BYTECODE && count == 0U ? 0xc00bU : 0xe00dU;
    if (decoded_status != UMC_OK || count != 4U) return 0xe00eU;
    const UmcStatus status = umc_execute_trace(decoded, count, argument == 12U ? 2U : 4U,
        &result, steps, 4U, &used, &diagnostic);
    if (argument == 12U)
        return status == UMC_FUEL_EXHAUSTED && used == 3U && steps[2].opcode == UMC_LESS &&
            steps[2].status == UMC_FUEL_EXHAUSTED && steps[2].stack_depth == 2U &&
            steps[2].top == INT32_MAX && result.value == 19 && result.instructions_executed == 0U ?
            0xc00cU : 0xe00fU;
    const int32_t expected = argument == 10U ? 0 : 1;
    if (status != UMC_OK || used != 4U || result.value != expected || result.instructions_executed != 4U ||
        steps[2].opcode != UMC_LESS || steps[2].top != expected || steps[2].stack_depth != 1U ||
        steps[3].opcode != UMC_RETURN || steps[3].status != UMC_OK)
        return 0xe010U;
    return argument == 10U ? 0xc00aU : 0xc009U;
}

UmicomU64 UmicomProgramMain(UmicomU64 argument)
{
    /* Keep selectors 0..4 compatible. New trace checks use separate selectors;
     * the earlier bound is retained to make the extension reviewable. */
#if 0
    if (argument > 3U) return 0xc004U;
#endif
    /* Retain the trace-only bound; comparison checks now occupy selectors 9..12. */
#if 0
    if (argument == 4U || argument > 8U) return 0xc004U;
#endif
    if (argument == 4U || argument > 12U) return 0xc004U;
    if (argument >= 9U) return CheckComparison(argument);
    uint8_t bytes[UMC_BYTECODE_HEADER_SIZE + 4U * UMC_BYTECODE_RECORD_SIZE];
    UmcInstruction decoded[4];
    UmcDiagnostic diagnostic;
    UmcResult result;
    result.value = 19;
    result.instructions_executed = 0U;
    size_t size = 0U;
    size_t count = 0U;
    /* Selector 7 exercises the same fault through traced execution. Preserve
     * the previous source choice; ordinary selectors retain their bytecode. */
#if 0
    const UmcInstruction *input = argument == 2U ? zero_divisor : arithmetic;
#endif
    const UmcInstruction *input = (argument == 2U || argument == 7U) ? zero_divisor : arithmetic;
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
    if (argument >= 5U) {
        /* These buffers remain user-program owned. A trace is data, with no
         * callback, syscall, allocation or direct Kernel dependency in the VM. */
        UmcTraceStep steps[4];
        size_t used = 99U;
        steps[0].instruction_index = 113U;
        steps[0].top = 17;
        const UmcStatus traced = umc_execute_trace(decoded, count,
            argument == 6U ? 3U : 4U, &result, steps,
            argument == 8U ? 3U : 4U, &used, &diagnostic);
        if (argument == 8U)
            return traced == UMC_BUFFER_TOO_SMALL && used == 0U &&
                steps[0].instruction_index == 113U && steps[0].top == 17 &&
                result.value == 19 && result.instructions_executed == 0U ? 0xc008U : 0xe00bU;
        if (argument == 6U)
            return traced == UMC_FUEL_EXHAUSTED && used == 4U &&
                steps[3].opcode == UMC_RETURN && steps[3].status == UMC_FUEL_EXHAUSTED &&
                steps[3].stack_depth == 1U && steps[3].top == 42 &&
                result.value == 19 && result.instructions_executed == 0U ? 0xc006U : 0xe009U;
        if (argument == 7U)
            return traced == UMC_ARITHMETIC_ERROR && used == 3U &&
                steps[2].opcode == UMC_DIVIDE && steps[2].status == UMC_ARITHMETIC_ERROR &&
                steps[2].stack_depth == 2U && steps[2].top == 0 &&
                result.value == 19 && result.instructions_executed == 0U ? 0xc007U : 0xe00aU;
        return traced == UMC_OK && used == 4U && result.value == 42 &&
            result.instructions_executed == 4U && steps[2].opcode == UMC_MULTIPLY &&
            steps[2].stack_depth == 1U && steps[2].top == 42 &&
            steps[3].opcode == UMC_RETURN && steps[3].status == UMC_OK ? 0xc005U : 0xe008U;
    }
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
