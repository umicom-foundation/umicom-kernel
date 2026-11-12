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
#include "umicom_c/profile.h"
#ifndef UMC_FORWARD_BRANCH_PROFILE
#error "CVM branch selectors require the forward-branch-capable canonical Umicom C runtime."
#endif
#ifndef UMC_LOCAL_INTEGER_PROFILE
#error "CVM local selectors require the local-capable canonical Umicom C runtime."
#endif
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


/* Integer slots are private VM values, not Kernel memory. Exercise the canonical
 * runtime after loading the native envelope into an isolated user address space. */
static UmicomU64 CheckLocals(UmicomU64 argument)
{
    static const UmcInstruction input[] = {
        {UMC_PUSH,6,10U},{UMC_STORE_LOCAL,31,11U},{UMC_LOAD_LOCAL,31,12U},
        {UMC_PUSH,7,13U},{UMC_MULTIPLY,0,14U},{UMC_STORE_LOCAL,31,15U},
        {UMC_LOAD_LOCAL,31,16U},{UMC_RETURN,0,17U}
    };
    static const UmcInstruction uninitialized[] = {
        {UMC_LOAD_LOCAL,31,4U},{UMC_RETURN,0,5U}
    };
    uint8_t bytes[UMC_BYTECODE_HEADER_SIZE+8U*UMC_BYTECODE_RECORD_SIZE];
    UmcInstruction decoded[8]; UmcTraceStep steps[8];
    UmcDiagnostic diagnostic; UmcResult result;
    result.value=19; result.instructions_executed=0U;
    size_t size=0U,count=99U,used=99U;
    if(umc_encode(input,8U,bytes,sizeof bytes,&size,&diagnostic)!=UMC_OK || bytes[4]!=3U)
        return 0xe012U;
    if(argument==17U)bytes[4]=2U;
    const UmcStatus decoded_status=umc_decode(bytes,size,decoded,8U,&count,&diagnostic);
    if(argument==17U)
        return decoded_status==UMC_BAD_BYTECODE && count==0U ? 0xc011U : 0xe013U;
    if(decoded_status!=UMC_OK || count!=8U)return 0xe014U;
    const UmcStatus status=umc_execute_trace(decoded,count,argument==16U ? 5U : 8U,
        &result,steps,8U,&used,&diagnostic);
    if(argument==16U)
        return status==UMC_FUEL_EXHAUSTED && used==6U && steps[5].opcode==UMC_STORE_LOCAL &&
            steps[5].top==42 && steps[5].stack_depth==1U && result.value==19 &&
            result.instructions_executed==0U ? 0xc010U : 0xe015U;
    if(status!=UMC_OK || result.value!=42 || result.instructions_executed!=8U || used!=8U ||
        steps[1].stack_depth!=0U || steps[1].top!=0 || steps[2].top!=6 || steps[6].top!=42)
        return 0xe016U;
    if(argument==15U) {
        /* Previous execution must not provide initialization for this program. */
        used=99U;steps[0].top=113;
        return umc_execute_trace(uninitialized,2U,0U,&result,steps,8U,&used,&diagnostic)==UMC_BAD_BYTECODE &&
            used==0U && steps[0].top==113 && result.value==42 && result.instructions_executed==8U &&
            diagnostic.source_offset==4U ? 0xc00fU : 0xe017U;
    }
    if(argument==18U) {
        /* Invalid indices must be rejected before a native shift or array access. */
        decoded[1].operand=32;
        return umc_execute(decoded,count,0U,&result,&diagnostic)==UMC_BAD_BYTECODE &&
            result.value==42 && diagnostic.source_offset==11U ? 0xc012U : 0xe018U;
    }
    return 0xc00eU;
}


/* The same portable runtime follows forward branches inside the native U-mode
 * envelope. No compiler, host callback or native address enters the bytecode. */
static UmicomU64 CheckBranches(UmicomU64 argument)
{
    static const UmcInstruction guarded[]={
        {UMC_PUSH,0,10U},{UMC_JUMP_IF_ZERO,6,11U},
        {UMC_PUSH,1,12U},{UMC_PUSH,0,13U},{UMC_DIVIDE,0,14U},
        {UMC_JUMP,7,15U},{UMC_PUSH,0,16U},{UMC_RETURN,0,17U}
    };
    static const UmcInstruction incomplete_local[]={
        {UMC_PUSH,1,0U},{UMC_JUMP_IF_ZERO,5,1U},
        {UMC_PUSH,41,2U},{UMC_STORE_LOCAL,30,3U},{UMC_JUMP,7,4U},
        {UMC_PUSH,42,5U},{UMC_STORE_LOCAL,31,6U},
        {UMC_LOAD_LOCAL,31,7U},{UMC_RETURN,0,8U}
    };
    uint8_t bytes[UMC_BYTECODE_HEADER_SIZE+8U*UMC_BYTECODE_RECORD_SIZE];
    UmcInstruction decoded[8];UmcTraceStep steps[8];UmcDiagnostic d;UmcResult r;
    r.value=19;r.instructions_executed=0U;size_t size=0U,count=99U,used=99U;
    if(umc_encode(guarded,8U,bytes,sizeof bytes,&size,&d)!=UMC_OK || bytes[4]!=4U)return 0xe019U;
    if(argument==22U)bytes[4]=3U;
    UmcStatus status=umc_decode(bytes,size,decoded,8U,&count,&d);
    if(argument==22U)return status==UMC_BAD_BYTECODE && count==0U ? 0xc016U : 0xe01aU;
    if(status!=UMC_OK || count!=8U)return 0xe01bU;
    steps[0].top=113;
    if(argument==23U){
        return umc_execute_trace(incomplete_local,9U,0U,&r,steps,8U,&used,&d)==UMC_BAD_BYTECODE &&
            used==0U && steps[0].top==113 && r.value==19 ? 0xc017U : 0xe01cU;
    }
    if(argument==24U || argument==25U){
        if(argument==24U)decoded[5].operand=1;
        else decoded[4].opcode=UMC_NEGATE;
        return umc_execute_trace(decoded,count,0U,&r,steps,8U,&used,&d)==UMC_BAD_BYTECODE &&
            used==0U && steps[0].top==113 && r.value==19 ?
            (argument==24U ? 0xc018U : 0xc019U) : 0xe01dU;
    }
    if(argument==20U)decoded[0].operand=1;
    status=umc_execute_trace(decoded,count,argument==21U ? 3U : 8U,&r,steps,8U,&used,&d);
    if(argument==20U)return status==UMC_ARITHMETIC_ERROR && used==5U &&
        steps[4].opcode==UMC_DIVIDE && d.source_offset==14U && r.value==19 &&
        r.instructions_executed==0U ? 0xc014U : 0xe01eU;
    if(argument==21U)return status==UMC_FUEL_EXHAUSTED && used==4U &&
        steps[3].instruction_index==7U && steps[3].opcode==UMC_RETURN &&
        steps[3].status==UMC_FUEL_EXHAUSTED && r.value==19 &&
        r.instructions_executed==0U ? 0xc015U : 0xe01fU;
    return status==UMC_OK && r.value==0 && r.instructions_executed==4U && used==4U &&
        steps[1].instruction_index==1U && steps[1].stack_depth==0U &&
        steps[2].instruction_index==6U && steps[3].instruction_index==7U ? 0xc013U : 0xe020U;
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
    /* Preserve the prior range; selector 13 now verifies linked runtime
     * discovery in real U-mode at both optimization settings. */
#if 0
    if (argument == 4U || argument > 12U) return 0xc004U;
#endif
    if (argument == 13U) {
        const uint32_t features = umc_runtime_profile(UMC_PROFILE_FEATURES);
        const uint32_t required = UMC_FEATURE_ARITHMETIC | UMC_FEATURE_COMPARISON |
            UMC_FEATURE_EXECUTION_TRACE | UMC_FEATURE_PROGRAM_INSPECTION | UMC_FEATURE_LOCAL_INTEGERS |
            UMC_FEATURE_FORWARD_BRANCHES;
        return umc_runtime_profile(UMC_PROFILE_API) == UMC_API_VERSION &&
            (features & required) == required && !(features & (UMC_FEATURE_LOGICAL_NOT | UMC_FEATURE_LOGICAL_CONNECTIVES |
                UMC_FEATURE_CONDITIONAL_EXPRESSIONS)) &&
            /* Literal spellings belong to the hosted compiler. A freestanding
             * runtime must not advertise parser features it does not contain. */
            !(features & (UMC_FEATURE_HEXADECIMAL_LITERALS | UMC_FEATURE_BINARY_LITERALS)) &&
            /* Retain the prior wire set; the appended local profile is now required. */
#if 0
            umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS) == 6U &&
#endif
            /* Forward control flow adds format 4; retain the earlier local set. */
#if 0
            umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS) == 14U &&
#endif
            umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS) == 30U &&
            umc_runtime_profile(UMC_PROFILE_LOCAL_VALUES) == 32U &&
            umc_runtime_profile(UMC_PROFILE_IDENTIFIER_BYTES) == 0U &&
            umc_runtime_profile(UMC_PROFILE_PROGRAM_INSTRUCTIONS) == UMC_PROGRAM_LIMIT &&
            umc_runtime_profile(UMC_PROFILE_STACK_VALUES) == UMC_STACK_LIMIT &&
            umc_runtime_profile(UMC_PROFILE_SOURCE_BYTES) == 0U &&
            umc_runtime_profile((UmcProfileProperty)999) == 0U ? 0xc00dU : 0xe011U;
    }
    /* Preserve the earlier selector bound; local cases append 14..18. */
#if 0
    if (argument == 4U || argument > 13U) return 0xc004U;
#endif
    /* Preserve earlier selector bounds; branch cases append 19..25. */
#if 0
    if (argument == 4U || argument > 18U) return 0xc004U;
#endif
    if (argument == 4U || argument > 25U) return 0xc004U;
    if (argument >= 19U) return CheckBranches(argument);
    if (argument >= 14U) return CheckLocals(argument);
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
