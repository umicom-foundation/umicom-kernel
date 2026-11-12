/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/network_packet_validation.c
 *
 * PURPOSE:
 *   Exercise the production packet reader on synthetic RAM captures inside the real diagnostic guest.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/network_packet.h"
#include "umicom/kernel/network_udp.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "boot_memory.h"

/* The payload is synthetic; protocol 17 does not imply a valid UDP message.
 * No NIC is bound and no receive queue, DMA buffer or host interface is used. */
static const UmicomU8 packetCapture[] = {
    0x02,0,0,0,0,2, 0x02,0,0,0,0,1, 0x08,0,
    0x45,0,0,0x20,0x12,0x34,0x40,0,0x40,0x11,0x3c,0x62,
    0xc0,0,2,1, 0xc6,0x33,0x64,2,
    1,2,3,4,5,6,7,8,9,10,11,12
};
static void PacketRequire(int condition, const char *reason)
{
    if (condition) return;
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomKernelConsoleWrite("packet-admission.reason=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomPlatformFinishFailure(0xd4U);
    UmicomPlatformHalt();
}
static int PacketBytesEqual(const void *left, const void *right, UmicomSize bytes)
{
    const UmicomU8 *a = left, *b = right;
    for (UmicomSize i = 0U; i < bytes; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void PacketRefused(const void *bytes, UmicomSize length,
    UmicomNetworkPacketStatus expected, const char *reason)
{
    UmicomEthernetIpv4View result;
    UmicomU8 before[sizeof result];
    for (UmicomSize i = 0U; i < sizeof result; ++i) before[i] = 0xa5U;
    UmicomBootMemoryCopy(&result, before, sizeof result);
    PacketRequire(UmicomKernelEthernetIpv4Read(bytes, length, &result) == expected, reason);
    PacketRequire(PacketBytesEqual(before, &result, sizeof result), "refusal changed output");
}

/* UDP fixture is independent of the IPv4-only capture above. Its odd payload
 * checksum includes the pseudo-header; changing a payload byte must fail. */
static const UmicomU8 udpCapture[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,33,0x12,0x34,0x40,0,64,17,0x3c,0x61,
    192,0,2,1,198,51,100,2,0x30,0x39,0,0x35,0,13,0x9f,0x5c,
    'h','e','l','l','o'
};
static void UdpRefused(const void *bytes, UmicomSize length,
    UmicomNetworkPacketStatus expected, const char *reason)
{
    UmicomEthernetIpv4UdpView result;
    UmicomU8 before[sizeof result];
    for (UmicomSize i = 0U; i < sizeof result; ++i) before[i] = 0xa5U;
    UmicomBootMemoryCopy(&result, before, sizeof result);
    PacketRequire(UmicomKernelEthernetIpv4UdpRead(bytes, length,
        UMICOM_UDP_CHECKSUM_REQUIRE, &result) == expected, reason);
    PacketRequire(PacketBytesEqual(before, &result, sizeof result), "UDP refusal changed output");
}
static void UdpValidateExecution(void)
{
    UmicomU8 frame[60];
    UmicomEthernetIpv4UdpView result;
    UmicomBootMemoryClear(frame, sizeof frame);
    UmicomBootMemoryCopy(frame, udpCapture, sizeof udpCapture);
    PacketRequire(UmicomKernelEthernetIpv4UdpRead(frame, sizeof udpCapture,
        UMICOM_UDP_CHECKSUM_REQUIRE, &result) == UMICOM_NETWORK_PACKET_OK, "UDP golden");
    PacketRequire(result.sourcePort == 12345U && result.destinationPort == 53U &&
        result.payloadOffset == 42U && result.payloadBytes == 5U &&
        result.checksumPresent == UMICOM_TRUE, "UDP metadata");
    PacketRequire(PacketBytesEqual(frame, udpCapture, sizeof udpCapture), "UDP input changed");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-golden");

    frame[46] ^= 1U;
    UdpRefused(frame, sizeof udpCapture, UMICOM_NETWORK_PACKET_CHECKSUM, "UDP odd byte checksum");
    frame[46] ^= 1U;
    frame[39] = 12U;
    UdpRefused(frame, sizeof udpCapture, UMICOM_NETWORK_PACKET_LENGTH, "UDP length mismatch");
    frame[39] = 13U;
    UdpRefused(frame, sizeof udpCapture - 1U, UMICOM_NETWORK_PACKET_LENGTH, "UDP truncation");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-refusals");

    for (UmicomSize i = sizeof udpCapture; i < sizeof frame; ++i) frame[i] = 0xffU;
    PacketRequire(UmicomKernelEthernetIpv4UdpRead(frame, sizeof frame,
        UMICOM_UDP_CHECKSUM_REQUIRE, &result) == UMICOM_NETWORK_PACKET_OK &&
        result.payloadBytes == 5U, "UDP padding excluded");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-padding");

    frame[40] = 0U; frame[41] = 0U;
    UdpRefused(frame, sizeof frame, UMICOM_NETWORK_PACKET_CHECKSUM, "UDP checksum required");
    PacketRequire(UmicomKernelEthernetIpv4UdpRead(frame, sizeof frame,
        UMICOM_UDP_CHECKSUM_ALLOW_ABSENT, &result) == UMICOM_NETWORK_PACKET_OK &&
        result.checksumPresent == UMICOM_FALSE, "UDP checksum absence reported");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-checksum-policy");
}

void UmicomKernelPacketAdmissionValidateExecution(void)
{
    UmicomU8 frame[60];
    UmicomEthernetIpv4View result;
    UmicomBootMemoryClear(frame, sizeof frame);
    UmicomBootMemoryCopy(frame, packetCapture, sizeof packetCapture);
    UmicomKernelConsoleWriteLine("packet-admission-test=begin");

    PacketRequire(UmicomKernelEthernetIpv4Read(frame, sizeof packetCapture, &result) ==
        UMICOM_NETWORK_PACKET_OK, "golden capture");
    PacketRequire(result.identification == 0x1234U && result.protocol == 17U &&
        result.timeToLive == 64U && result.dontFragment == UMICOM_TRUE, "copied metadata");
    PacketRequire(result.ipBytes == 32U && result.payloadOffset == 34U &&
        result.payloadBytes == 12U, "payload extent");
    PacketRequire(PacketBytesEqual(frame, packetCapture, sizeof packetCapture), "input changed");
    UmicomKernelConsoleWriteLine("packet-admission.case=golden");

    for (UmicomSize size = 0U; size < sizeof packetCapture; ++size)
        PacketRefused(frame, size, UMICOM_NETWORK_PACKET_LENGTH, "truncation");
    UmicomKernelConsoleWriteLine("packet-admission.case=truncation");

    frame[22] ^= 1U;
    PacketRefused(frame, sizeof packetCapture, UMICOM_NETWORK_PACKET_CHECKSUM, "checksum");
    frame[22] ^= 1U;
    UmicomKernelConsoleWriteLine("packet-admission.case=checksum");

    for (UmicomSize i = sizeof packetCapture; i < sizeof frame; ++i) frame[i] = 0xccU;
    PacketRequire(UmicomKernelEthernetIpv4Read(frame, sizeof frame, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.payloadBytes == 12U, "padding is not payload");
    UmicomKernelConsoleWriteLine("packet-admission.case=padding");

    _Alignas(16) UmicomU8 unaligned[sizeof packetCapture + 1U];
    UmicomBootMemoryCopy(unaligned + 1U, packetCapture, sizeof packetCapture);
    PacketRequire(UmicomKernelEthernetIpv4Read(unaligned + 1U, sizeof packetCapture, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.identification == 0x1234U, "unaligned capture");
    UmicomKernelConsoleWriteLine("packet-admission.case=unaligned");

    frame[12] = 0x81U;
    PacketRefused(frame, sizeof frame, UMICOM_NETWORK_PACKET_UNSUPPORTED, "tagged frame");
    frame[12] = 0x08U;
    UmicomKernelConsoleWriteLine("packet-admission.case=unsupported");

    /* Output metadata is copied, while payload coordinates borrow the capture.
     * Altering the input after return must not retroactively alter metadata. */
    PacketRequire(UmicomKernelEthernetIpv4Read(frame, sizeof frame, &result) ==
        UMICOM_NETWORK_PACKET_OK, "metadata lifetime admission");
    frame[26] = 0U;
    PacketRequire(result.sourceAddress[0] == 192U && result.sourceAddress[3] == 1U,
        "metadata retained input pointer");
    UmicomKernelConsoleWriteLine("packet-admission.case=metadata-lifetime");
    /* Preserve the earlier transcript count for review. The same seven IPv4
     * groups remain above; four UDP groups extend the current acceptance. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=7");
#endif
    UdpValidateExecution();
    UmicomKernelConsoleWriteLine("packet-admission.cases=11");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PACKET_ADMISSION_READY");
}
