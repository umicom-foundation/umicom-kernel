/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/complement_checks.h
 *
 * PURPOSE:
 *   Exercise source-complement lowering through independent format-1 bytes in hosted and freestanding consumers.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_CVM_COMPLEMENT_CHECKS_H
#define UMICOM_CVM_COMPLEMENT_CHECKS_H
/* These selectors validate the shared arithmetic execution contract. They do
 * not claim that a freestanding runtime contains the hosted source compiler. */
static UmicomU64 CheckComplement(UmicomU64 argument)
{
    const uint8_t bytes[]={
        'U','M','C','B',1,0,4,0,
        1,255,255,255,255,10,0,0,0,
        1,0,0,0,128,20,0,0,0,
        3,0,0,0,0,10,0,0,0,
        7,0,0,0,0,30,0,0,0
    };
    UmcInstruction code[4];UmcTraceStep steps[4];UmcDiagnostic d;
    UmcResult result={19,23U};size_t count=99U,used=99U;
    if(umc_decode(bytes,sizeof bytes,code,4U,&count,&d)!=UMC_OK || count!=4U)return 0xe036U;
    if(argument==39U)code[1].operand=INT32_MAX;
    if(argument==40U)code[1].operand=0;
    if(argument==42U) {
        uint8_t encoded[sizeof bytes];size_t size=0U;
        if(umc_encode(code,count,encoded,sizeof encoded,&size,&d)!=UMC_OK || size!=sizeof bytes)return 0xe037U;
        for(size_t i=0U;i<size;++i)if(encoded[i]!=bytes[i])return 0xe038U;
        return !(umc_runtime_profile(UMC_PROFILE_FEATURES)&UMC_FEATURE_BITWISE_COMPLEMENT) &&
            /* Complement still uses format 1; the runtime also admits format 6. */
#if 0
            umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS)==62U ? 0xc02aU : 0xe039U;
#else
/* The runtime now also admits format 7; this fixture retains its original encoding. The former implementation is retained for owner review. */
#if 0
            umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS)==126U ? 0xc02aU : 0xe039U;
#endif
            umc_runtime_profile(UMC_PROFILE_WIRE_VERSIONS)==254U ? 0xc02aU : 0xe039U;
#endif
    }
    UmcStatus status=umc_execute_trace(code,count,argument==41U?2U:4U,&result,steps,4U,&used,&d);
    if(argument==41U)return status==UMC_FUEL_EXHAUSTED && used==3U &&
        steps[2].opcode==UMC_SUBTRACT && steps[2].stack_depth==2U &&
        d.source_offset==10U && result.value==19 && result.instructions_executed==23U ? 0xc029U : 0xe03aU;
    const int32_t expected=argument==38U?INT32_MAX:(argument==39U?INT32_MIN:-1);
    return status==UMC_OK && used==4U && result.value==expected &&
        result.instructions_executed==4U && steps[2].opcode==UMC_SUBTRACT &&
        steps[2].stack_depth==1U && steps[2].top==expected &&
        steps[3].opcode==UMC_RETURN ? 0xc000U+argument : 0xe03bU;
}
#endif
