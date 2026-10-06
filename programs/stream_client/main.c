/*-----------------------------------------------------------------------------
 * Umicom native stream demonstration
 * File: programs/stream_client/main.c
 *
 * This independently linked program has no hosted stdio library. Its small
 * wrapper returns both status and byte count from the native stream boundary.
 * Locals stay alive while reads or writes block; starts detects an accidental
 * restart instead of resumption. Tests use a page-crossing private buffer.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/stream_abi.h"

UmicomU64 UmicomStreamProgramCall(UmicomU64 call, UmicomU64 selector, UmicomAddress address,
    UmicomSize bytes, UmicomU64 flags, UmicomSize *outBytes);
void UmicomStreamProgramFault(void);
static volatile UmicomU64 umicomStreamStarts;
alignas(4096) static UmicomU8 umicomStreamBuffer[8192];
static UmicomBoolean UmicomStreamPrint(UmicomU64 selector, const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    UmicomSize accepted = 0U;
    return UmicomStreamProgramCall(UMICOM_USER_CALL_STREAM_WRITE, selector,
        (UmicomAddress)text, bytes, 0U, &accepted) == UMICOM_STREAM_OK && accepted == bytes
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomU64 UmicomStreamReadLine(void)
{
    /* Distinct volatile locals must survive the suspension on an empty input.
     * This is a small integration check, not a substitute for the register suite. */
    volatile UmicomU64 before = 0x12345678U;
    volatile UmicomU64 after = 0x87654321U;
    UmicomU8 *const buffer = &umicomStreamBuffer[4096U - 16U];
    if (!UmicomStreamPrint(UMICOM_STREAM_OUTPUT, "Umicom input: type a line, or Ctrl-D for EOF.\n")) return 0xeb01U;
    for (UmicomSize reads = 0U; reads < 8U; ++reads) {
        UmicomSize received = 0U;
        const UmicomU64 status = UmicomStreamProgramCall(UMICOM_USER_CALL_STREAM_READ,
            UMICOM_STREAM_INPUT, (UmicomAddress)buffer, 64U, 0U, &received);
        if (status == UMICOM_STREAM_EOF && received == 0U) {
            if (!UmicomStreamPrint(UMICOM_STREAM_OUTPUT, "End of input.\n")) return 0xeb02U;
            return 0U;
        }
        if (status != UMICOM_STREAM_OK || received == 0U || received > 64U) return 0xeb03U;
        if (!UmicomStreamPrint(UMICOM_STREAM_OUTPUT, "received: ")) return 0xeb04U;
        UmicomSize sent = 0U;
        if (UmicomStreamProgramCall(UMICOM_USER_CALL_STREAM_WRITE, UMICOM_STREAM_OUTPUT,
                (UmicomAddress)buffer, received, 0U, &sent) != UMICOM_STREAM_OK || sent != received) return 0xeb05U;
        UmicomBoolean ended = UMICOM_FALSE;
        for (UmicomSize i = 0U; i < received; ++i) if (buffer[i] == 10U) ended = UMICOM_TRUE;
        /* After successful WRITE the Kernel must own its copy, not this buffer. */
        for (UmicomSize i = 0U; i < received; ++i) buffer[i] = 0xeeU;
        if (ended) break;
    }
    if (before != 0x12345678U || after != 0x87654321U || umicomStreamStarts != 1U) return 0xeb06U;
    return UmicomStreamPrint(UMICOM_STREAM_ERROR, "Input handled; this line came from stderr.\n") ? 0U : 0xeb07U;
}
UmicomU64 UmicomStreamProgramMain(UmicomU64 mode)
{
    ++umicomStreamStarts;
    if (mode == 0U) return UmicomStreamReadLine();
    if (mode == 1U) {
        /* More chunks than queue capacity forces output backpressure. The
         * final stdout/stderr sequence must remain ordered across resumptions. */
        for (UmicomSize i = 0U; i < 12U; ++i)
            if (!UmicomStreamPrint(i % 2U ? UMICOM_STREAM_ERROR : UMICOM_STREAM_OUTPUT,
                "Umicom ordered output\n")) return 0xeb08U;
        return umicomStreamStarts == 1U ? 0U : 0xeb09U;
    }
    if (mode == 2U) {
        UmicomSize bytes = 99U;
        if (UmicomStreamProgramCall(UMICOM_USER_CALL_STREAM_READ, UMICOM_STREAM_OUTPUT, 0U, 1U, 0U, &bytes)
            != UMICOM_STREAM_WRONG_DIRECTION || bytes != 0U) return 0xeb10U;
        if (UmicomStreamProgramCall(UMICOM_USER_CALL_STREAM_WRITE, UMICOM_STREAM_OUTPUT,
            ~(UmicomAddress)0U - 7U, 32U, 0U, &bytes) != UMICOM_STREAM_BAD_BUFFER || bytes != 0U) return 0xeb11U;
        if (UmicomStreamProgramCall(UMICOM_USER_CALL_STREAM_READ, UMICOM_STREAM_INPUT,
            (UmicomAddress)umicomStreamBuffer, 8U, UMICOM_STREAM_NONBLOCK, &bytes)
            != UMICOM_STREAM_WOULD_BLOCK || bytes != 0U) return 0xeb12U;
        return UmicomStreamPrint(UMICOM_STREAM_OUTPUT, "Refusal checks passed.\n") ? 0U : 0xeb13U;
    }
    if (!UmicomStreamPrint(UMICOM_STREAM_OUTPUT, "Output accepted before stopping.\n")) return 0xeb14U;
    if (mode == 3U) UmicomStreamProgramFault();
    if (mode == 4U) for (;;) { ++umicomStreamStarts; } /* Timer budget must still stop this program. */
    return mode == 5U ? 0U : 0xeb15U;
}
