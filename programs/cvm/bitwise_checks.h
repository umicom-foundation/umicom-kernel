/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/bitwise_checks.h
 *
 * PURPOSE:
 *   Exercise independent format-6 bitwise records through the shared runtime in hosted and freestanding consumers.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_CVM_BITWISE_CHECKS_H
#define UMICOM_CVM_BITWISE_CHECKS_H

/* Selector identities extend the native envelope; bytecode itself still has
 * no system calls, native addresses, allocation or host callbacks. */
static UmicomU64 CheckBitwise(UmicomU64 argument)
{
    uint8_t bytes[]={
        'U','M','C','B',6,0,4,0,
        1,255,255,255,255,10,0,0,0,
        1,42,0,0,0,20,0,0,0,
        19,0,0,0,0,30,0,0,0,
        7,0,0,0,0,40,0,0,0
    };
    UmcInstruction code[4]; UmcTraceStep steps[4]; UmcDiagnostic d;
    UmcResult result={19,23U}; size_t count=99U,used=99U;
    if(argument==44U)bytes[26]=20U;
    if(argument==45U)bytes[26]=21U;
    if(argument==47U)bytes[4]=5U;
    if(argument==48U)bytes[27]=1U;
    if(argument==51U)bytes[26]=2U; /* Old arithmetic cannot claim format 6. */
    UmcStatus status=umc_decode(bytes,sizeof bytes,code,4U,&count,&d);
    if(argument==47U || argument==48U || argument==51U)
        return status==UMC_BAD_BYTECODE && count==0U ?
            0xc000U+argument : 0xe040U;
    if(status!=UMC_OK || count!=4U)return 0xe041U;
    if(argument==49U)return
        (umc_runtime_profile(UMC_PROFILE_FEATURES)&UMC_FEATURE_BINARY_BITWISE) &&
        !(umc_runtime_profile(UMC_PROFILE_FEATURES)&UMC_FEATURE_BITWISE_COMPLEMENT) &&
/* The runtime now also admits format 7; this fixture retains its original encoding. The former implementation is retained for owner review. */
#if 0
        umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS)==126U ? 0xc031U : 0xe042U;
#endif
        umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS)==254U ? 0xc031U : 0xe042U;
    uint8_t encoded[sizeof bytes]; size_t size=0U;
    if(umc_encode(code,count,encoded,sizeof encoded,&size,&d)!=UMC_OK ||
        size!=sizeof bytes)return 0xe043U;
    for(size_t i=0U;i<size;++i)if(encoded[i]!=bytes[i])return 0xe044U;
    if(argument==50U){
        code[0].operand=INT32_MIN;
        code[1].operand=-1;
        code[2].opcode=UMC_BITWISE_XOR;
    }
    status=umc_execute_trace(code,count,argument==46U?2U:4U,
        &result,steps,4U,&used,&d);
    if(argument==46U)return status==UMC_FUEL_EXHAUSTED && used==3U &&
        steps[2].opcode==UMC_BITWISE_AND && steps[2].stack_depth==2U &&
        d.source_offset==30U && result.value==19 && result.instructions_executed==23U ?
            0xc02eU : 0xe045U;
    const int32_t expected=argument==44U?-43:(argument==45U?-1:
        (argument==50U?INT32_MAX:42));
    UmcProgramInfo info;
    if(umc_inspect(code,count,&info,&d)!=UMC_OK ||
        info.peak_stack!=2U || info.arithmetic_count!=0U)return 0xe046U;
    return status==UMC_OK && used==4U && result.value==expected &&
        result.instructions_executed==4U && steps[2].stack_depth==1U &&
        steps[2].top==expected && steps[3].opcode==UMC_RETURN ?
            0xc000U+argument : 0xe047U;
}
#endif
