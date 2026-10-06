/* Umicom Kernel console-local byte operations.
 * Aggregate initialisation/copy can make a freestanding compiler emit hosted
 * memset/memcpy calls. Explicit volatile byte operations keep these bounded
 * metadata/staging actions self-contained without adding a second C runtime.
 * The caller supplies trusted non-overlapping spans for Copy.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#ifndef UMICOM_CONSOLE_INTERNAL_H
#define UMICOM_CONSOLE_INTERNAL_H
#include "umicom/kernel/types.h"
static inline void UmicomConsoleClear(void *target, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static inline void UmicomConsoleCopy(void *target, const void *source, UmicomSize bytes)
{
    volatile UmicomU8 *out = (volatile UmicomU8 *)target;
    const volatile UmicomU8 *in = (const volatile UmicomU8 *)source;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = in[i];
}
#endif /* UMICOM_CONSOLE_INTERNAL_H */
