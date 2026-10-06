/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/program_launch.c
 *
 * Build a bounded startup block in the pages already owned by a fresh task.
 * Its upper stack area contains the block; initial sp points just below it so
 * ordinary downward-growing C frames cannot immediately overwrite the strings.
 * No physical allocation, second loader or alternative syscall path is added.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/program_launch.h"
#include "umicom/kernel/executable.h"
#include "umicom/kernel/object_cache.h"

static void UmicomLaunchClear(void *target, UmicomSize bytes)
{
    /* Volatile byte stores avoid a freestanding dependency on hosted memset. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static void UmicomLaunchCopy(void *target, const void *source, UmicomSize bytes)
{
    UmicomU8 *out = (UmicomU8 *)target;
    const UmicomU8 *in = (const UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
static UmicomBoolean UmicomLaunchNameByte(UmicomU8 byte, UmicomBoolean first)
{
    /* Environment names have one deliberate grammar. Values are not parsed
     * as paths, commands or authority, and may contain spaces or '='. */
    return byte == '_' || (byte >= 'A' && byte <= 'Z') ||
        (byte >= 'a' && byte <= 'z') || (!first && byte >= '0' && byte <= '9')
        ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelLaunchStatus UmicomLaunchAppend(UmicomKernelLaunchImage *image,
    const UmicomKernelLaunchString *string, UmicomAddress base, UmicomU64 *outAddress)
{
    if (string->bytes > UMICOM_LAUNCH_STRING_LIMIT) return UMICOM_LAUNCH_TOO_LARGE;
    if (string->bytes != 0U && !string->data) return UMICOM_LAUNCH_INVALID_ARGUMENT;
    /* Reserve the terminator too; exact-capacity strings must remain terminated. */
    if (string->bytes + 1U > UMICOM_LAUNCH_TEXT_BYTES - image->info.textBytes)
        return UMICOM_LAUNCH_TOO_LARGE;
    const UmicomSize start = image->info.textBytes;
    for (UmicomSize i = 0U; i < string->bytes; ++i) {
        const UmicomU8 byte = (UmicomU8)string->data[i];
        if (byte == 0U) return UMICOM_LAUNCH_INVALID_ARGUMENT;
        image->text[start + i] = byte;
    }
    image->text[start + string->bytes] = 0U;
    *outAddress = base + __builtin_offsetof(UmicomKernelLaunchImage, text) + start;
    image->info.textBytes += string->bytes + 1U;
    return UMICOM_LAUNCH_OK;
}
UmicomKernelLaunchStatus UmicomKernelProgramLaunchPack(
    const UmicomKernelProgramLaunchSpec *spec, UmicomAddress virtualBase,
    UmicomKernelLaunchImage *outImage)
{
    if (!spec || !outImage || !spec->arguments || spec->argumentCount == 0U ||
        (spec->environmentCount != 0U && !spec->environment)) return UMICOM_LAUNCH_INVALID_ARGUMENT;
    if (spec->argumentCount > UMICOM_LAUNCH_ARGUMENT_LIMIT ||
        spec->environmentCount > UMICOM_LAUNCH_ENVIRONMENT_LIMIT) return UMICOM_LAUNCH_TOO_LARGE;
    if ((virtualBase & 15U) != 0U || virtualBase == 0U ||
        virtualBase > ~(UmicomAddress)0U - (UMICOM_LAUNCH_BLOCK_BYTES - 1U) ||
        !UmicomKernelVirtualMemoryIsCanonical(virtualBase) ||
        !UmicomKernelVirtualMemoryIsCanonical(virtualBase + UMICOM_LAUNCH_BLOCK_BYTES - 1U))
        return UMICOM_LAUNCH_INVALID_ARGUMENT;
    if (spec->arguments[0].bytes == 0U) return UMICOM_LAUNCH_INVALID_ARGUMENT;
    /* Nothing reaches the caller or its user pages until the entire plan is
     * valid. This also snapshots input strings before later copying begins. */
    UmicomKernelLaunchImage image;
    UmicomLaunchClear(&image, sizeof(image));
    image.info.cookie = UMICOM_LAUNCH_COOKIE;
    image.info.bytes = sizeof(image);
    image.info.argumentCount = spec->argumentCount;
    image.info.environmentCount = spec->environmentCount;
    image.info.arguments = virtualBase + __builtin_offsetof(UmicomKernelLaunchImage, arguments);
    image.info.environment = virtualBase + __builtin_offsetof(UmicomKernelLaunchImage, environment);
    image.info.text = virtualBase + __builtin_offsetof(UmicomKernelLaunchImage, text);
    for (UmicomSize i = 0U; i < spec->argumentCount; ++i) {
        const UmicomKernelLaunchStatus status = UmicomLaunchAppend(&image, &spec->arguments[i],
            virtualBase, &image.arguments[i]);
        if (status != UMICOM_LAUNCH_OK) return status;
    }
    for (UmicomSize i = 0U; i < spec->environmentCount; ++i) {
        const UmicomKernelLaunchString *entry = &spec->environment[i];
        if (!entry->data || entry->bytes < 2U) return UMICOM_LAUNCH_INVALID_ENVIRONMENT;
        if (entry->bytes > UMICOM_LAUNCH_STRING_LIMIT) return UMICOM_LAUNCH_TOO_LARGE;
        UmicomSize name = 0U;
        while (name < entry->bytes && entry->data[name] != '=') {
            if (!UmicomLaunchNameByte((UmicomU8)entry->data[name], name == 0U ? UMICOM_TRUE : UMICOM_FALSE))
                return UMICOM_LAUNCH_INVALID_ENVIRONMENT;
            ++name;
        }
        if (name == 0U || name == entry->bytes) return UMICOM_LAUNCH_INVALID_ENVIRONMENT;
        /* Compare copied earlier names, not input aliases. An unambiguous
         * environment cannot select different values depending on search order. */
        for (UmicomSize earlier = 0U; earlier < i; ++earlier) {
            const UmicomSize offset = image.environment[earlier] - image.info.text;
            UmicomSize matched = 0U;
            while (matched < name && image.text[offset + matched] == (UmicomU8)entry->data[matched]) ++matched;
            if (matched == name && image.text[offset + name] == '=') return UMICOM_LAUNCH_DUPLICATE_ENVIRONMENT;
        }
        const UmicomKernelLaunchStatus status = UmicomLaunchAppend(&image, entry, virtualBase, &image.environment[i]);
        if (status != UMICOM_LAUNCH_OK) return status;
    }
    /* Unused pointers, both array sentinels and all padding remain zero. */
    UmicomLaunchCopy(outImage, &image, sizeof(image));
    return UMICOM_LAUNCH_OK;
}
UmicomKernelLaunchStatus UmicomKernelProgramLaunchPrepare(
    const UmicomKernelUserMemory *memory, const UmicomKernelProgramLaunchSpec *spec,
    UmicomRiscvTrapFrame *outFrame)
{
    if (!memory || !outFrame) return UMICOM_LAUNCH_INVALID_ARGUMENT;
    if (!UmicomKernelObjectCacheAccessAllowed()) return UMICOM_LAUNCH_UNSAFE;
    const UmicomAddress base = UMICOM_EXECUTABLE_STACK_TOP - UMICOM_LAUNCH_BLOCK_BYTES;
    _Static_assert(UMICOM_EXECUTABLE_STACK_PAGES * UMICOM_EXECUTABLE_PAGE_BYTES > UMICOM_LAUNCH_BLOCK_BYTES,
        "The startup block must leave usable C stack space");
    UmicomKernelLaunchImage image;
    const UmicomKernelLaunchStatus packed = UmicomKernelProgramLaunchPack(spec, base, &image);
    if (packed != UMICOM_LAUNCH_OK) return packed;
    /* Preflight the complete block, not merely its endpoints. All mappings stay
     * stable during this serial, pre-first-instruction setup operation. */
    if (UmicomKernelUserMemoryCheck(memory, base, sizeof(image), UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE)
        != UMICOM_USER_RESULT_OK) return UMICOM_LAUNCH_BAD_MEMORY;
    for (UmicomSize done = 0U; done < sizeof(image);) {
        const UmicomSize left = sizeof(image) - done;
        const UmicomSize bytes = left < UMICOM_USER_COPY_LIMIT ? left : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryWrite(memory, base + done, (const UmicomU8 *)&image + done, bytes)
            != UMICOM_USER_RESULT_OK) return UMICOM_LAUNCH_BAD_MEMORY;
        done += bytes;
    }
    /* Publish only the argument registers and initial stack. Entry PC, image
     * ownership and privilege state still belong to the existing scheduler. */
    outFrame->x2_sp = base;
    outFrame->x10_a0 = image.info.argumentCount;
    outFrame->x11_a1 = image.info.arguments;
    outFrame->x12_a2 = image.info.environment;
    outFrame->x13_a3 = base;
    return UMICOM_LAUNCH_OK;
}
const char *UmicomKernelLaunchStatusName(UmicomKernelLaunchStatus status)
{
    switch (status) {
        case UMICOM_LAUNCH_OK: return "ok";
        case UMICOM_LAUNCH_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_LAUNCH_TOO_LARGE: return "too-large";
        case UMICOM_LAUNCH_INVALID_ENVIRONMENT: return "invalid-environment";
        case UMICOM_LAUNCH_DUPLICATE_ENVIRONMENT: return "duplicate-environment";
        case UMICOM_LAUNCH_BAD_MEMORY: return "bad-memory";
        case UMICOM_LAUNCH_UNSAFE: return "unsafe-context";
        default: return "invalid-status";
    }
}
