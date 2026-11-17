/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/machine_checks.h
 *
 * PURPOSE:
 *   Qualify the shared resumable VM and explicit-root heap inside isolated user programs.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_CVM_MACHINE_CHECKS_H
#define UMICOM_CVM_MACHINE_CHECKS_H
#include "umicom_c/machine.h"
#include "umicom_c/managed_heap.h"
/* Static caller-owned workspaces keep the small user stack independent of
 * debugger program storage and the managed heap capacity. Each loaded process
 * receives its own writable data; no Kernel address is passed to the runtime. */
static UmcMachine machine_workspace;
static UmcManagedHeap managed_workspace;
static UmicomU64 CheckMachine(UmicomU64 argument)
{
    UmcDiagnostic d;UmcTraceStep step;UmcResult result;
    result.value=99;result.instructions_executed=0U;
    if(argument==55U) {
        UmcHandle a=0U,b=0U;size_t freed=99U;
        umc_heap_init(&managed_workspace);
        if(umc_heap_allocate(&managed_workspace,1U,&a)!=UMC_HEAP_OK ||
            umc_heap_allocate(&managed_workspace,1U,&b)!=UMC_HEAP_OK ||
            umc_heap_set_edge(&managed_workspace,a,0U,b)!=UMC_HEAP_OK ||
            umc_heap_set_edge(&managed_workspace,b,0U,a)!=UMC_HEAP_OK)return 0xe034U;
        if(umc_heap_collect(&managed_workspace,&a,1U,&freed)!=UMC_HEAP_OK || freed!=0U)return 0xe035U;
        if(umc_heap_collect(&managed_workspace,NULL,0U,&freed)!=UMC_HEAP_OK || freed!=2U)return 0xe036U;
        return 0xc037U;
    }
    const UmcInstruction *program=argument==53U?zero_divisor:arithmetic;
    if(umc_machine_init(&machine_workspace,program,4U,argument==54U?2U:4U,&d)!=UMC_OK)return 0xe037U;
    if(umc_machine_step(&machine_workspace,&step,&d)!=UMC_OK || machine_workspace.frame.executed!=1U || step.top!=7)return 0xe038U;
    if(umc_machine_step(&machine_workspace,&step,&d)!=UMC_OK || machine_workspace.frame.executed!=2U)return 0xe039U;
    if(argument==53U || argument==54U) {
        UmcStatus expected=argument==53U?UMC_ARITHMETIC_ERROR:UMC_FUEL_EXHAUSTED;
        if(umc_machine_step(&machine_workspace,&step,&d)!=expected || machine_workspace.frame.executed!=2U ||
            machine_workspace.frame.depth!=2U || machine_workspace.frame.pc!=2U)return 0xe03aU;
        if(umc_machine_result(&machine_workspace,&result,&d)!=UMC_INVALID_ARGUMENT || result.value!=99)return 0xe03bU;
        return argument==53U?0xc035U:0xc036U;
    }
    if(umc_machine_step(&machine_workspace,&step,&d)!=UMC_OK || step.top!=42 ||
        umc_machine_step(&machine_workspace,&step,&d)!=UMC_OK || !machine_workspace.frame.finished)return 0xe03cU;
    if(umc_machine_result(&machine_workspace,&result,&d)!=UMC_OK || result.value!=42 || result.instructions_executed!=4U)return 0xe03dU;
    return 0xc034U;
}
#endif
