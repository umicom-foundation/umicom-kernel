/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/bounds_checks.h
 *
 * PURPOSE:
 *   Qualify shared structural VM path budgets inside real freestanding user programs.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_CVM_BOUNDS_CHECKS_H
#define UMICOM_CVM_BOUNDS_CHECKS_H
#include "umicom_c/execution_bounds.h"

/* The guest is a thin caller. The shared runtime owns both graph admission and
 * path costs, and the existing process monitor checks exit and frame recovery. */
static UmicomU64 CheckExecutionBounds(UmicomU64 argument)
{
    static const UmcInstruction branch[] = {
        {UMC_PUSH,1,0},{UMC_JUMP_IF_ZERO,6,1},
        {UMC_PUSH,40,2},{UMC_PUSH,2,3},{UMC_ADD,0,4},{UMC_JUMP,7,5},
        {UMC_PUSH,7,6},{UMC_RETURN,0,7}
    };
    static const UmcInstruction faulty[] = {
        {UMC_PUSH,1,0},{UMC_PUSH,0,1},{UMC_DIVIDE,0,2},{UMC_RETURN,0,3}
    };
    static const UmcInstruction invalid[] = {{UMC_JUMP,0,17},{UMC_RETURN,0,18}};
    UmcExecutionBounds bounds;UmcResult result;UmcDiagnostic diagnostic;
    bounds.minimum_instructions=91U;bounds.maximum_instructions=92U;
    result.value=19;result.instructions_executed=23U;
    if(argument==72U){
        return umc_execution_bounds(invalid,2U,&bounds,&diagnostic)==UMC_BAD_BYTECODE &&
            diagnostic.source_offset==17U && bounds.minimum_instructions==91U &&
            bounds.maximum_instructions==92U ? 0xc048U : 0xe072U;
    }
    const UmcInstruction *program=argument==71U?faulty:branch;
    const size_t count=argument==71U?4U:8U;
    if(umc_execution_bounds(program,count,&bounds,&diagnostic)!=UMC_OK)return 0xe073U;
    if(argument==69U)
        return bounds.minimum_instructions==4U && bounds.maximum_instructions==7U ? 0xc045U : 0xe074U;
    if(argument==70U)
        return umc_execute(program,count,bounds.maximum_instructions,&result,&diagnostic)==UMC_OK &&
            result.value==42 && result.instructions_executed==7U ? 0xc046U : 0xe075U;
    return bounds.minimum_instructions==4U && bounds.maximum_instructions==4U &&
        umc_execute(program,count,bounds.maximum_instructions,&result,&diagnostic)==UMC_ARITHMETIC_ERROR &&
        diagnostic.source_offset==2U && result.value==19 && result.instructions_executed==23U ? 0xc047U : 0xe076U;
}
#endif
