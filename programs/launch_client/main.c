/*-----------------------------------------------------------------------------
 * Umicom native argument demonstration
 * File: programs/launch_client/main.c
 *
 * This is a separately linked program. The Kernel copies a launch block into
 * its stack, then the ordinary saved-frame entry supplies argc/argv/envp. The
 * program checks the bounded layout before following the pointers and prints
 * through the existing copied stream service. It has no hosted C library.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/launch_abi.h"
#include "umicom/kernel/stream_abi.h"

UmicomU64 UmicomLaunchProgramWrite(UmicomAddress address, UmicomSize bytes, UmicomSize *outBytes);
static volatile UmicomU64 umicomLaunchStarts;
static UmicomU8 umicomLaunchOutput[UMICOM_STREAM_TRANSFER_BYTES];
static UmicomSize umicomLaunchUsed;
static UmicomBoolean UmicomLaunchFlush(void)
{
    if (umicomLaunchUsed == 0U) return UMICOM_TRUE;
    UmicomSize copied = 0U;
    const UmicomU64 status = UmicomLaunchProgramWrite((UmicomAddress)umicomLaunchOutput,
        umicomLaunchUsed, &copied);
    if (status != UMICOM_STREAM_OK || copied != umicomLaunchUsed) return UMICOM_FALSE;
    umicomLaunchUsed = 0U;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomLaunchByte(UmicomU8 byte)
{
    if (umicomLaunchUsed == sizeof(umicomLaunchOutput) && !UmicomLaunchFlush()) return UMICOM_FALSE;
    umicomLaunchOutput[umicomLaunchUsed++] = byte;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomLaunchText(const char *text)
{
    for (UmicomSize i = 0U; text[i]; ++i)
        if (!UmicomLaunchByte((UmicomU8)text[i])) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomLaunchNumber(UmicomU64 value)
{
    char digits[20];
    UmicomSize used = 0U;
    do { digits[used++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (used) if (!UmicomLaunchByte((UmicomU8)digits[--used])) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomLaunchStringValid(const char *text, const UmicomProgramLaunchInfo *info)
{
    const UmicomAddress address = (UmicomAddress)text;
    if (address < info->text || address >= info->text + info->textBytes) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i <= UMICOM_LAUNCH_STRING_LIMIT && i < info->text + info->textBytes - address; ++i)
        if (text[i] == '\0') return UMICOM_TRUE;
    return UMICOM_FALSE;
}
UmicomU64 UmicomLaunchProgramMain(UmicomU64 argc, const char *const *argv,
    const char *const *envp, const UmicomProgramLaunchInfo *info)
{
    ++umicomLaunchStarts;
    /* A legacy numeric launch normally has null argv/envp. Refuse it before
     * dereferencing metadata rather than treating an integer as a pointer. */
    if (argc == 0U || argc > UMICOM_LAUNCH_ARGUMENT_LIMIT || !argv || !envp || !info) return 0xec01U;
    volatile UmicomU64 local = 0x162534U;
    const UmicomAddress base = (UmicomAddress)info;
    if ((base & 15U) != 0U || base > ~(UmicomAddress)0U - UMICOM_LAUNCH_BLOCK_BYTES) return 0xec02U;
    if (info->cookie != UMICOM_LAUNCH_COOKIE || info->bytes != UMICOM_LAUNCH_BLOCK_BYTES ||
        info->argumentCount != argc || info->environmentCount > UMICOM_LAUNCH_ENVIRONMENT_LIMIT ||
        info->arguments != base + sizeof(*info) ||
        info->environment != info->arguments + (UMICOM_LAUNCH_ARGUMENT_LIMIT + 1U) * 8U ||
        info->text != info->environment + (UMICOM_LAUNCH_ENVIRONMENT_LIMIT + 1U) * 8U ||
        info->textBytes > UMICOM_LAUNCH_TEXT_BYTES || info->text + info->textBytes > base + info->bytes ||
        (UmicomAddress)argv != info->arguments || (UmicomAddress)envp != info->environment)
        return 0xec03U;
    if (argv[argc] || envp[info->environmentCount]) return 0xec04U;
    /* Validate every string before producing the success transcript. The
     * application owns these bytes, but an invalid startup contract is still
     * worth diagnosing instead of printing unrelated memory. */
    for (UmicomSize i = 0U; i < argc; ++i) if (!UmicomLaunchStringValid(argv[i], info)) return 0xec05U;
    for (UmicomSize i = 0U; i < info->environmentCount; ++i)
        if (!UmicomLaunchStringValid(envp[i], info)) return 0xec06U;
    if (!UmicomLaunchText("Umicom structured launch\nargc=") || !UmicomLaunchNumber(argc) ||
        !UmicomLaunchText("\n")) return 0xec07U;
    for (UmicomSize i = 0U; i < argc; ++i)
        if (!UmicomLaunchText("argv[") || !UmicomLaunchNumber(i) || !UmicomLaunchText("]=<") ||
            !UmicomLaunchText(argv[i]) || !UmicomLaunchText(">\n")) return 0xec08U;
    for (UmicomSize i = 0U; i < info->environmentCount; ++i)
        if (!UmicomLaunchText("env[") || !UmicomLaunchNumber(i) || !UmicomLaunchText("]=<") ||
            !UmicomLaunchText(envp[i]) || !UmicomLaunchText(">\n")) return 0xec09U;
    if (!UmicomLaunchText("launch-context=valid\n") || !UmicomLaunchFlush()) return 0xec10U;
    /* Stream backpressure may have suspended this C stack. Neither the launch
     * block above sp nor the local below it should have been recreated. */
    return umicomLaunchStarts == 1U && local == 0x162534U ? 0U : 0xec11U;
}
