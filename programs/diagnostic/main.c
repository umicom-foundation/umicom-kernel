/*-----------------------------------------------------------------------------
 * Umicom Kernel native diagnostic program
 * File: programs/diagnostic/main.c
 *
 * PURPOSE:
 *   Prove that a separate ELF executable receives working text, read-only data,
 *   initialised data, zero-filled storage, a user stack and native system calls.
 *
 * EDUCATIONAL OVERVIEW:
 *   This file is linked into diagnostic.elf, not directly into Kernel text.
 *   Its globals force the loader to handle the different PT_LOAD segments.
 *   Volatile keeps the compiler from folding away the observations we need
 *   to make about actual loaded bytes. No hosted library is used.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/types.h"
#include "umicom/kernel/user_abi.h"

/* Definitions are deliberately in separate permission classes. The zero array
 * crosses a page boundary so an incomplete BSS mapping cannot pass by accident. */
static const volatile UmicomU8 umicomProgramLabel[16] = "Umicom Kernel";
static volatile UmicomU64 umicomProgramCounter = 32U;
static volatile UmicomU8 umicomProgramZero[4097];

UmicomU64 UmicomProgramSystemCall(UmicomU64 number, UmicomU64 first,
    UmicomU64 second, UmicomU64 third);
void UmicomProgramWriteReadOnly(const volatile UmicomU8 *address);
void UmicomProgramBusyLoop(void);

UmicomU64 UmicomProgramMain(UmicomU64 argument)
{
    /* Verify that the loader cleared every byte absent from the file. The
     * entry Assembly intentionally did not initialise this storage for us. */
    for (UmicomSize index = 0U; index < sizeof(umicomProgramZero); ++index) {
        if (umicomProgramZero[index] != 0U) {
            return 0xe001U;
        }
    }
    /* Both RO and RW initialisers must have come from this ELF's own bytes. */
    if (umicomProgramLabel[0] != 'U' || umicomProgramCounter != 32U) {
        return 0xe002U;
    }
    const UmicomU64 identity = UmicomProgramSystemCall(UMICOM_USER_CALL_IDENTITY, 0U, 0U, 0U);
    /* COPY must use the existing ownership checks for our newly loaded pages. */
    const UmicomU64 copyResult = UmicomProgramSystemCall(UMICOM_USER_CALL_COPY,
        (UmicomU64)(UmicomUIntPtr)&umicomProgramZero[0],
        (UmicomU64)(UmicomUIntPtr)&umicomProgramLabel[0], 16U);
    if (copyResult != UMICOM_USER_RESULT_OK || identity == 0U) {
        return 0xe003U;
    }
    for (UmicomSize index = 0U; index < 16U; ++index) {
        if (umicomProgramZero[index] != umicomProgramLabel[index]) {
            return 0xe004U;
        }
    }
    /* Touch the far BSS page and writable data. Each process owns its copies. */
    umicomProgramZero[4096] = 0x5aU;
    ++umicomProgramCounter;
    /* Two argument values select negative acceptance cases, not privileges. */
    if (argument == 2U) {
        UmicomProgramWriteReadOnly(umicomProgramLabel);
        return 0xe005U; /* Reaching this means read-only protection failed. */
    }
    if (argument == 3U) {
        UmicomProgramBusyLoop();
        return 0xe006U;
    }
    /* A caller can predict this value from its assigned identity and argument.
     * Both the data initialiser and a successful write contribute to it. */
    return identity + argument + umicomProgramCounter;
}
