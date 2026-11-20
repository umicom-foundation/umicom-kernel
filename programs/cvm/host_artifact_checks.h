/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/host_artifact_checks.h
 *
 * PURPOSE:
 *   Exercise exact compiler-produced bytes and truncated input in U-mode
 *   using the existing freestanding decoder and execution service.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_CVM_HOST_ARTIFACT_CHECKS_H
#define UMICOM_KERNEL_CVM_HOST_ARTIFACT_CHECKS_H

#include "umicom/kernel/types.h"
#include "umicom_c/umicom_c.h"

extern const uint8_t UmicomHostCvmArtifactStart[], UmicomHostCvmArtifactEnd[];

static UmicomU64 CheckHostArtifact(UmicomU64 selector)
{
    const size_t bytes = (size_t)((UmicomAddress)UmicomHostCvmArtifactEnd -
        (UmicomAddress)UmicomHostCvmArtifactStart);
    UmcInstruction program[16];
    UmcDiagnostic diagnostic = {0};
    size_t count = 0U;
    if (bytes == 0U) return 0xe04aU;
    if (selector == 74U) {
        /* A truncated artifact never publishes even its first instruction. */
        program[0].opcode = UMC_NEGATE;
        const UmcStatus status = umc_decode(UmicomHostCvmArtifactStart, bytes - 1U,
            program, sizeof program / sizeof program[0], &count, &diagnostic);
        return status == UMC_BAD_BYTECODE && count == 0U && program[0].opcode == UMC_NEGATE ? 0xc04aU : 0xe04aU;
    }
    if (umc_decode(UmicomHostCvmArtifactStart, bytes, program,
            sizeof program / sizeof program[0], &count, &diagnostic) != UMC_OK)
        return 0xe049U;
    UmcResult result = {0};
    /* The test sequence has six operations, including its final RETURN. */
    return umc_execute(program, count, 16U, &result, &diagnostic) == UMC_OK &&
        count == 6U && result.instructions_executed == 6U && result.value == 42 ? 0xc049U : 0xe049U;
}

#endif
