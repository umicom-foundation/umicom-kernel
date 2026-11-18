/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/shift_checks.h
 *
 * PURPOSE:
 *   Qualify checked format-7 shifts through the canonical freestanding runtime.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_CVM_SHIFT_CHECKS_H
#define UMICOM_CVM_SHIFT_CHECKS_H
/* These original literal bytes are independent of hosted source compilation.
 * Guest code delegates arithmetic, tracing and admission to the shared VM. */
static UmicomU64 CheckShifts(UmicomU64 argument)
{
    uint8_t bytes[]={
        'U','M','C','B',7,0,4,0,
        1,21,0,0,0,10,0,0,0,
        1,1,0,0,0,20,0,0,0,
        22,0,0,0,0,30,0,0,0,
        7,0,0,0,0,40,0,0,0
    };
    if(argument==61U)bytes[26]=23U;
    if(argument==65U)bytes[4]=6U;
    if(argument==67U)bytes[27]=1U;
    UmcInstruction program[4];UmcDiagnostic d;size_t count=99U;
    UmcStatus status=umc_decode(bytes,sizeof bytes,program,4U,&count,&d);
    if(argument==65U || argument==67U)
        return status==UMC_BAD_BYTECODE && count==0U ? 0xc000U+argument : 0xe060U;
    if(status!=UMC_OK || count!=4U)return 0xe061U;
    uint8_t encoded[sizeof bytes];size_t size=0U;
    if(umc_encode(program,count,encoded,sizeof encoded,&size,&d)!=UMC_OK ||
        size!=sizeof bytes)return 0xe062U;
    for(size_t i=0U;i<size;++i)if(bytes[i]!=encoded[i])return 0xe063U;
    if(argument==62U)program[1].operand=-1;
    if(argument==63U){program[0].operand=1;program[1].operand=31;}
    if(argument==64U)program[0].operand=-1;
    if(argument==68U)program[1].operand=32;
    UmcResult result={19,23U};UmcTraceStep steps[4];size_t used=99U;
    status=umc_execute_trace(program,count,argument==66U?2U:4U,
        &result,steps,4U,&used,&d);
    if(argument==62U || argument==63U || argument==64U || argument==68U ||
        argument==66U) {
        const UmcStatus expected=argument==66U?UMC_FUEL_EXHAUSTED:UMC_ARITHMETIC_ERROR;
        return status==expected && used==3U && result.value==19 &&
            result.instructions_executed==23U && d.source_offset==30U &&
            steps[2].opcode==UMC_SHIFT_LEFT && steps[2].stack_depth==2U &&
            steps[2].status==expected ? 0xc000U+argument : 0xe064U;
    }
    const int32_t expected=argument==61U?10:42;
    return status==UMC_OK && used==4U && result.value==expected &&
        result.instructions_executed==4U && steps[2].top==expected &&
        steps[2].stack_depth==1U ? 0xc000U+argument : 0xe065U;
}
#endif
