/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/remainder_checks.h
 *
 * PURPOSE:
 *   Exercise the canonical remainder runtime through independent freestanding format-5 bytes.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_CVM_REMAINDER_CHECKS_H
#define UMICOM_CVM_REMAINDER_CHECKS_H
#ifndef UMC_REMAINDER_PROFILE
#error "CVM remainder selectors require the remainder-capable canonical Umicom C runtime."
#endif
/* Kept in the guest translation unit so hosted and real U-mode consumers run
 * exactly the same checks without copying the shared runtime implementation. */
static UmicomU64 CheckRemainder(UmicomU64 argument)
{
    uint8_t bytes[]={
        'U','M','C','B',5,0,4,0,
        1,7,0,0,0,10,0,0,0,
        1,3,0,0,0,20,0,0,0,
        18,0,0,0,0,30,0,0,0,
        7,0,0,0,0,40,0,0,0
    };
    UmcInstruction code[4];UmcTraceStep steps[4];UmcDiagnostic d;
    UmcResult result={19,23U};size_t count=99U,used=99U;
    if(argument==35U)bytes[4]=4U;
    UmcStatus status=umc_decode(bytes,sizeof bytes,code,4U,&count,&d);
    if(argument==35U)return status==UMC_BAD_BYTECODE && count==0U ? 0xc023U : 0xe030U;
    if(status!=UMC_OK || count!=4U)return 0xe031U;
    if(argument==32U)code[0].operand=-7;
    if(argument==33U)code[1].operand=0;
    if(argument==34U){code[0].operand=INT32_MIN;code[1].operand=-1;}
    if(argument==37U)code[2].operand=1;
    status=umc_execute_trace(code,count,argument==36U?2U:4U,&result,steps,4U,&used,&d);
    if(argument==37U)return status==UMC_BAD_BYTECODE && used==0U &&
        result.value==19 && result.instructions_executed==23U ? 0xc025U : 0xe032U;
    if(argument==36U)return status==UMC_FUEL_EXHAUSTED && used==3U &&
        steps[2].opcode==UMC_REMAINDER && steps[2].stack_depth==2U &&
        d.source_offset==30U && result.value==19 && result.instructions_executed==23U ? 0xc024U : 0xe033U;
    if(argument==33U || argument==34U)return status==UMC_ARITHMETIC_ERROR && used==3U &&
        steps[2].opcode==UMC_REMAINDER && steps[2].stack_depth==2U &&
        d.source_offset==30U && result.value==19 && result.instructions_executed==23U
        ? (argument==33U?0xc021U:0xc022U) : 0xe034U;
    int32_t expected=argument==32U?-1:1;
    return status==UMC_OK && used==4U && result.value==expected &&
        result.instructions_executed==4U && steps[2].opcode==UMC_REMAINDER &&
        steps[2].stack_depth==1U && steps[2].top==expected &&
        steps[3].opcode==UMC_RETURN ? (argument==32U?0xc020U:0xc01fU) : 0xe035U;
}
#endif
