/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/slice_checks.h
 *
 * PURPOSE:
 *   Exercise cooperative slices and refused work inside the isolated CVM diagnostic.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_CVM_SLICE_CHECKS_H
#define UMICOM_CVM_SLICE_CHECKS_H
#include "umicom_c/machine.h"
#ifndef UMC_MACHINE_SLICE_PROFILE
#error "The CVM slice diagnostic requires matching Umicom C slice headers and runtime."
#endif
static UmcMachine slice_workspace;
static UmicomU64 CheckSlices(UmicomU64 selector)
{
    UmcDiagnostic diagnostic;
    UmcSliceResult report;
    UmcResult result;
    const UmcInstruction *program = selector == 57U ? zero_divisor : arithmetic;
    if (umc_machine_init(&slice_workspace,program,4U,selector==58U?2U:4U,&diagnostic)!=UMC_OK)
        return 0xe040U;
    if(selector==59U) {
        report.instructions_executed=99U;report.finished=7U;
        if(umc_machine_run_slice(&slice_workspace,0U,&report,0,&diagnostic)!=UMC_INVALID_ARGUMENT ||
            report.instructions_executed!=99U || report.finished!=7U || slice_workspace.frame.executed!=0U)
            return 0xe041U;
        return 0xc03bU;
    }
    if(umc_machine_run_slice(&slice_workspace,2U,&report,0,&diagnostic)!=UMC_OK ||
        report.instructions_executed!=2U || report.finished!=0U || slice_workspace.frame.pc!=2U)
        return 0xe042U;
    const UmcStatus status=umc_machine_run_slice(&slice_workspace,2U,&report,0,&diagnostic);
    if(selector==57U || selector==58U) {
        if(status!=(selector==57U?UMC_ARITHMETIC_ERROR:UMC_FUEL_EXHAUSTED) ||
            report.instructions_executed!=0U || report.finished!=0U ||
            slice_workspace.frame.executed!=2U || slice_workspace.frame.pc!=2U)
            return 0xe043U;
        return selector==57U?0xc039U:0xc03aU;
    }
    if(status!=UMC_OK || report.instructions_executed!=2U || report.finished!=1U ||
        umc_machine_result(&slice_workspace,&result,&diagnostic)!=UMC_OK ||
        result.value!=42 || result.instructions_executed!=4U)return 0xe044U;
    return 0xc038U;
}
#endif
