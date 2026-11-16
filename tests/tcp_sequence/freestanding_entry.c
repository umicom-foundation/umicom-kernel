/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_sequence/freestanding_entry.c
 *
 * PURPOSE:
 *   Link every sequence helper into an RV64 freestanding image without a host runtime; this is not a boot test.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/tcp_sequence.h"
volatile UmicomU32 UmicomSequenceLinkResult;
void _start(void)
{
    UmicomTcpSequenceValue length=UmicomKernelTcpSequenceLength(4U,2U);
    UmicomTcpSequenceValue next=UmicomKernelTcpSequenceAdvance(0xfffffffeU,length.value);
    UmicomSequenceLinkResult=(UmicomU32)UmicomKernelTcpSequenceCompare(next.value,0xfffffffeU);
    UmicomSequenceLinkResult+=(UmicomU32)UmicomKernelTcpNewAcknowledgment(0xfffffffeU,next.value,0U);
    UmicomSequenceLinkResult+=(UmicomU32)UmicomKernelTcpReceiveSequence(0xfffffffeU,8U,0U,2U);
    for(;;) { }
}
