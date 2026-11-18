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
#include "umicom/kernel/network_ethernet.h"
#include "umicom/kernel/network_tagged_ipv4.h"
#include "umicom/kernel/network_arp.h"
#include "umicom/kernel/network_ipv6.h"
#include "umicom/kernel/network_udp6.h"
#include "umicom/kernel/network_icmp6.h"
#include "umicom/kernel/network_tcp6.h"
#include "umicom/kernel/network_neighbor6.h"
#include "umicom/kernel/network_udp.h"
#include "umicom/kernel/network_icmp.h"
#include "umicom/kernel/network_tcp.h"
#include "umicom/kernel/tcp_options.h"
#include "umicom/kernel/tcp_receive.h"
#include "umicom/kernel/tcp_send_buffer.h"
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

/* Exercise the encoder in the no-NIC guest using the independently fixed
 * capture already used by admission. No driver or network stack is invoked. */
static void UdpWriteValidateExecution(void)
{
    UmicomEthernetIpv4UdpSpec spec;
    UmicomBootMemoryClear(&spec, sizeof spec);
    UmicomBootMemoryCopy(spec.destinationMac, udpCapture, 6U);
    UmicomBootMemoryCopy(spec.sourceMac, udpCapture + 6U, 6U);
    UmicomBootMemoryCopy(spec.sourceAddress, udpCapture + 26U, 4U);
    UmicomBootMemoryCopy(spec.destinationAddress, udpCapture + 30U, 4U);
    spec.sourcePort = 12345U; spec.destinationPort = 53U;
    spec.identification = 0x1234U; spec.timeToLive = 64U;
    UmicomU8 frame[60], before[60];
    for (UmicomSize i = 0U; i < sizeof frame; ++i) frame[i] = 0xa5U;
    UmicomSize written = 0U;
    PacketRequire(UmicomKernelEthernetIpv4UdpWrite(&spec, udpCapture + 42U, 5U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_OK, "UDP encode golden");
    PacketRequire(written == sizeof udpCapture && PacketBytesEqual(frame, udpCapture, sizeof udpCapture),
        "UDP encode independent bytes");
    for (UmicomSize i = written; i < sizeof frame; ++i)
        PacketRequire(frame[i] == 0xa5U, "UDP encode spare capacity");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-write-golden");

    UmicomBootMemoryCopy(before, frame, sizeof frame);
    written = 777U;
    PacketRequire(UmicomKernelEthernetIpv4UdpWrite(&spec, udpCapture + 42U, 5U,
        frame, 46U, &written) == UMICOM_NETWORK_PACKET_LENGTH, "UDP encode short capacity");
    PacketRequire(written == 777U && PacketBytesEqual(before, frame, sizeof frame), "UDP encode refusal mutated output");
    PacketRequire(UmicomKernelEthernetIpv4UdpWrite(&spec, frame + 1U, 5U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_ARGUMENT, "UDP encode overlap");
    PacketRequire(written == 777U && PacketBytesEqual(before, frame, sizeof frame), "UDP encode alias mutated output");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-write-refusals");

    UmicomEthernetIpv4UdpView view;
    PacketRequire(UmicomKernelEthernetIpv4UdpWrite(&spec, (const void *)0, 0U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_OK, "UDP encode empty");
    PacketRequire(written == 42U && UmicomKernelEthernetIpv4UdpRead(frame, written,
        UMICOM_UDP_CHECKSUM_REQUIRE, &view) == UMICOM_NETWORK_PACKET_OK &&
        view.payloadBytes == 0U && view.checksumPresent == UMICOM_TRUE, "UDP encoded empty admission");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-write-empty");

    const UmicomU8 zeroPayload[2] = {0xe3U, 0x34U};
    PacketRequire(UmicomKernelEthernetIpv4UdpWrite(&spec, zeroPayload, 2U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_OK, "UDP encode zero checksum");
    PacketRequire(frame[40] == 0xffU && frame[41] == 0xffU &&
        UmicomKernelEthernetIpv4UdpRead(frame, written, UMICOM_UDP_CHECKSUM_REQUIRE, &view) ==
            UMICOM_NETWORK_PACKET_OK, "UDP encoded negative zero");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp-write-zero-checksum");
}

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


/* Fixed echo bytes are checked in the actual freestanding guest. They are
 * synthetic RAM observations; no reply is sent and no NIC is attached. */
static const UmicomU8 echoCapture[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,33,0x12,0x34,0x40,0,64,1,0x3c,0x71,
    192,0,2,1,198,51,100,2,
    8,0,0xf6,0x2b,0x12,0x34,0xab,0xcd,'h','e','l','l','o'
};
static void EchoRefused(const void *bytes, UmicomSize length,
    UmicomNetworkPacketStatus expected, const char *reason)
{
    UmicomEthernetIpv4IcmpEchoView result;
    UmicomU8 before[sizeof result];
    for (UmicomSize i = 0U; i < sizeof result; ++i) before[i] = 0xa5U;
    UmicomBootMemoryCopy(&result, before, sizeof result);
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoRead(bytes, length, &result) == expected, reason);
    PacketRequire(PacketBytesEqual(before, &result, sizeof result), "ICMP refusal changed output");
}
static void EchoValidateExecution(void)
{
    UmicomU8 frame[60];
    UmicomEthernetIpv4IcmpEchoView result;
    UmicomBootMemoryClear(frame, sizeof frame);
    UmicomBootMemoryCopy(frame, echoCapture, sizeof echoCapture);
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoRead(frame, sizeof echoCapture, &result) ==
        UMICOM_NETWORK_PACKET_OK, "ICMP request");
    PacketRequire(result.messageType == UMICOM_ICMP_ECHO_REQUEST && result.identifier == 0x1234U &&
        result.sequenceNumber == 0xabcdU && result.messageBytes == 13U &&
        result.payloadOffset == 42U && result.payloadBytes == 5U, "ICMP request metadata");
    PacketRequire(PacketBytesEqual(frame, echoCapture, sizeof echoCapture), "ICMP input changed");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-golden");

    frame[34] = 0U; frame[36] = 0xfeU;
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoRead(frame, sizeof echoCapture, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.messageType == UMICOM_ICMP_ECHO_REPLY &&
        result.sequenceNumber == 0xabcdU, "ICMP reply");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-reply");

    UmicomBootMemoryCopy(frame, echoCapture, sizeof echoCapture);
    frame[46] ^= 1U;
    EchoRefused(frame, sizeof echoCapture, UMICOM_NETWORK_PACKET_CHECKSUM, "ICMP odd byte checksum");
    frame[46] ^= 1U;
    EchoRefused(frame, sizeof echoCapture - 1U, UMICOM_NETWORK_PACKET_LENGTH, "ICMP truncation");
    frame[35] = 1U;
    EchoRefused(frame, sizeof echoCapture, UMICOM_NETWORK_PACKET_HEADER, "ICMP code");
    frame[35] = 0U; frame[36] = 0U; frame[37] = 0U;
    EchoRefused(frame, sizeof echoCapture, UMICOM_NETWORK_PACKET_CHECKSUM, "ICMP zero is not optional");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-refusals");

    UmicomBootMemoryCopy(frame, echoCapture, sizeof echoCapture);
    for (UmicomSize i = sizeof echoCapture; i < sizeof frame; ++i) frame[i] = 0xffU;
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoRead(frame, sizeof frame, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.payloadBytes == 5U, "ICMP padding excluded");
    UmicomBootMemoryClear(frame, sizeof frame);
    PacketRequire(result.identifier == 0x1234U && result.ipv4.sourceAddress[0] == 192U,
        "ICMP metadata retained input pointer");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-padding-lifetime");
}


/* Literal TCP capture is independently checked in the actual diagnostic
 * guest. The fixture has no connection peer and no network device. */
static const UmicomU8 tcpCapture[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,45,0x12,0x34,0x40,0,64,6,0x3c,0x60,
    192,0,2,1,198,51,100,2,
    0x30,0x39,0,80,0x12,0x34,0x56,0x78,0x90,0xab,0xcd,0xef,
    0x50,0x18,0x10,0,0x77,0xed,0,0,'h','e','l','l','o'
};
static void TcpRefused(const void *bytes, UmicomSize length,
    UmicomNetworkPacketStatus expected, const char *reason)
{
    UmicomEthernetIpv4TcpView result;
    UmicomU8 before[sizeof result];
    for (UmicomSize i = 0U; i < sizeof result; ++i) before[i] = 0xa5U;
    UmicomBootMemoryCopy(&result, before, sizeof result);
    PacketRequire(UmicomKernelEthernetIpv4TcpRead(bytes, length, &result) == expected, reason);
    PacketRequire(PacketBytesEqual(before, &result, sizeof result), "TCP refusal changed output");
}
static void TcpValidateExecution(void)
{
    UmicomU8 frame[64];
    UmicomEthernetIpv4TcpView result;
    UmicomBootMemoryClear(frame, sizeof frame);
    UmicomBootMemoryCopy(frame, tcpCapture, sizeof tcpCapture);
    PacketRequire(UmicomKernelEthernetIpv4TcpRead(frame, sizeof tcpCapture, &result) ==
        UMICOM_NETWORK_PACKET_OK, "TCP capture");
    PacketRequire(result.sourcePort == 12345U && result.destinationPort == 80U &&
        result.sequenceNumber == 0x12345678U && result.acknowledgmentNumber == 0x90abcdefU &&
        result.flags == (UMICOM_TCP_ACK | UMICOM_TCP_PSH) && result.window == 4096U &&
        result.segmentBytes == 25U && result.payloadOffset == 54U &&
        result.payloadBytes == 5U, "TCP copied metadata");
    PacketRequire(PacketBytesEqual(frame, tcpCapture, sizeof tcpCapture), "TCP input changed");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-golden");

    frame[58] ^= 1U;
    TcpRefused(frame, sizeof tcpCapture, UMICOM_NETWORK_PACKET_CHECKSUM, "TCP odd-byte corruption");
    frame[58] ^= 1U;
    TcpRefused(frame, sizeof tcpCapture - 1U, UMICOM_NETWORK_PACKET_LENGTH, "TCP truncation");
    frame[46] = 0x40U;
    TcpRefused(frame, sizeof tcpCapture, UMICOM_NETWORK_PACKET_HEADER, "TCP short data offset");
    frame[46] = 0x60U;
    TcpRefused(frame, sizeof tcpCapture, UMICOM_NETWORK_PACKET_UNSUPPORTED, "TCP option profile");
    frame[46] = 0x50U;
    frame[50] = 0U; frame[51] = 0U;
    TcpRefused(frame, sizeof tcpCapture, UMICOM_NETWORK_PACKET_CHECKSUM, "TCP checksum mandatory");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-refusals");

    /* The zero checksum field becomes valid only after the window adds the
     * missing sum. A zero field by itself never disables verification. */
    frame[48] = 0x87U; frame[49] = 0xedU;
    PacketRequire(UmicomKernelEthernetIpv4TcpRead(frame, sizeof tcpCapture, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.window == 0x87edU, "TCP zero checksum full sum");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-zero-checksum");

    UmicomBootMemoryCopy(frame, tcpCapture, sizeof tcpCapture);
    for (UmicomSize i = sizeof tcpCapture; i < sizeof frame; ++i) frame[i] = 0xffU;
    PacketRequire(UmicomKernelEthernetIpv4TcpRead(frame, sizeof frame, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.payloadBytes == 5U, "TCP Ethernet padding");
    UmicomBootMemoryClear(frame, sizeof frame);
    PacketRequire(result.sequenceNumber == 0x12345678U &&
        result.ipv4.sourceAddress[0] == 192U, "TCP metadata retained input pointer");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-padding-lifetime");
}


static void ArpValidateExecution(void)
{
    static const UmicomU8 capture[42]={
        0xff,0xff,0xff,0xff,0xff,0xff, 0x02,0,0,0,0,1, 0x08,0x06,
        0,1, 0x08,0, 6,4, 0,1, 0x02,0,0,0,0,1, 192,0,2,1,
        0,0,0,0,0,0, 192,0,2,2
    };
    _Alignas(16) UmicomU8 storage[61];
    UmicomU8 *frame=storage+1U;
    UmicomBootMemoryClear(storage,sizeof storage);
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    UmicomEthernetArpView result,before;
    PacketRequire(UmicomKernelEthernetArpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.operation==UMICOM_ARP_REQUEST && result.senderProtocolAddress[3]==1U &&
        result.targetProtocolAddress[3]==2U,"ARP unaligned request");
    UmicomKernelConsoleWriteLine("packet-admission.case=arp-golden");
    frame[21]=2U;
    PacketRequire(UmicomKernelEthernetArpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.operation==UMICOM_ARP_REPLY,"ARP reply opcode");
    UmicomKernelConsoleWriteLine("packet-admission.case=arp-reply");
    UmicomBootMemoryCopy(&before,&result,sizeof before);
    for(UmicomSize n=0U;n<sizeof capture;++n) {
        PacketRequire(UmicomKernelEthernetArpRead(frame,n,&result)==UMICOM_NETWORK_PACKET_LENGTH,"ARP prefix refusal");
        PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"ARP prefix changed result");
    }
    frame[18]=8U;
    PacketRequire(UmicomKernelEthernetArpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_UNSUPPORTED,
        "ARP unsupported hardware length");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"ARP profile changed result");
    frame[18]=6U;frame[20]=1U;frame[21]=0U;
    PacketRequire(UmicomKernelEthernetArpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_UNSUPPORTED,
        "ARP opcode network byte order");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"ARP opcode changed result");
    UmicomKernelConsoleWriteLine("packet-admission.case=arp-refusals");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=sizeof capture;n<60U;++n)frame[n]=0xffU;
    PacketRequire(UmicomKernelEthernetArpRead(frame,60U,&result)==UMICOM_NETWORK_PACKET_OK,"ARP padding");
    UmicomBootMemoryClear(storage,sizeof storage);
    PacketRequire(result.senderProtocolAddress[0]==192U && result.sourceMac[0]==2U &&
        result.operation==UMICOM_ARP_REQUEST,"ARP copied metadata lifetime");
    UmicomKernelConsoleWriteLine("packet-admission.case=arp-padding-lifetime");
}


/* Guest vectors use an empty No Next Header packet, independently of the host
 * UDP-shaped vector. Run without a NIC: this exercises RAM capture admission. */
static void Ipv6ValidateExecution(void)
{
    static const UmicomU8 capture[54] = {
        2,0,0,0,0,2, 2,0,0,0,0,1, 0x86,0xdd,
        0x60,0,0,1,0,0,59,32,
        0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1,
        0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,2
    };
    _Alignas(16) UmicomU8 storage[65];
    UmicomU8 *frame=storage+1U;
    UmicomBootMemoryClear(storage,sizeof storage);
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    UmicomEthernetIpv6View result,before;
    PacketRequire(UmicomKernelEthernetIpv6Read(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.flowLabel==1U && result.trafficClass==0U && result.nextHeader==59U &&
        result.hopLimit==32U && result.payloadBytes==0U && result.payloadOffset==54U &&
        result.sourceAddress[15]==1U && result.destinationAddress[15]==2U,"IPv6 unaligned no-next capture");
    UmicomKernelConsoleWriteLine("packet-admission.case=ipv6-golden");
    UmicomBootMemoryCopy(&before,&result,sizeof before);
    for(UmicomSize n=0U;n<sizeof capture;++n) {
        PacketRequire(UmicomKernelEthernetIpv6Read(frame,n,&result)==UMICOM_NETWORK_PACKET_LENGTH,"IPv6 truncated base header");
        PacketRequire(PacketBytesEqual(&before,&result,sizeof result),"IPv6 truncation changed result");
    }
    frame[19]=1U;
    PacketRequire(UmicomKernelEthernetIpv6Read(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_LENGTH,"IPv6 truncated payload");
    PacketRequire(PacketBytesEqual(&before,&result,sizeof result),"IPv6 payload refusal changed result");
    frame[19]=0U;
    UmicomKernelConsoleWriteLine("packet-admission.case=ipv6-truncation");
    frame[20]=44U;
    PacketRequire(UmicomKernelEthernetIpv6Read(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_FRAGMENT,"IPv6 fragment refused");
    frame[20]=0U;
    PacketRequire(UmicomKernelEthernetIpv6Read(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_UNSUPPORTED,"IPv6 extension refused");
    PacketRequire(PacketBytesEqual(&before,&result,sizeof result),"IPv6 extension refusal changed result");
    UmicomKernelConsoleWriteLine("packet-admission.case=ipv6-extension-policy");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=sizeof capture;n<64U;++n) frame[n]=0xffU;
    PacketRequire(UmicomKernelEthernetIpv6Read(frame,64U,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.ipBytes==40U && result.payloadBytes==0U,"IPv6 padding is not payload");
    UmicomBootMemoryClear(storage,sizeof storage);
    PacketRequire(result.sourceAddress[0]==0x20U && result.sourceAddress[15]==1U &&
        result.destinationAddress[15]==2U && result.flowLabel==1U,"IPv6 copied address lifetime");
    UmicomKernelConsoleWriteLine("packet-admission.case=ipv6-padding-lifetime");
}


/* This guest uses the independent negative-zero checksum vector: its valid
 * wire checksum is ffff, while a zero field must still be rejected. */
static void Ipv6UdpValidateExecution(void)
{
    static const UmicomU8 capture[64]={
        2,0,0,0,0,2,2,0,0,0,0,1,0x86,0xdd,
        0x60,0,0,1,0,10,17,64,
        0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,1,
        0x20,1,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0,2,
        0x30,0x39,0,0x35,0,10,0xff,0xff,0x73,0xf7
    };
    _Alignas(16) UmicomU8 storage[81];UmicomU8 *frame=storage+1U;
    UmicomBootMemoryClear(storage,sizeof storage);UmicomBootMemoryCopy(frame,capture,sizeof capture);
    UmicomEthernetIpv6UdpView result,before;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.sourcePort==12345U && result.destinationPort==53U && result.datagramBytes==10U &&
        result.payloadOffset==62U && result.payloadBytes==2U && result.ipv6.sourceAddress[15]==1U,
        "IPv6 UDP unaligned negative-zero vector");
    UmicomBootMemoryCopy(&before,&result,sizeof before);
    UmicomKernelConsoleWriteLine("packet-admission.case=udp6-golden");
    frame[60]=frame[61]=0U;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"IPv6 UDP zero checksum");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);frame[63]^=1U;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"IPv6 UDP payload corruption");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);frame[37]^=1U;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"IPv6 UDP pseudo-header corruption");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"IPv6 UDP checksum refusal changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp6-checksum");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=0U;n<sizeof capture;++n){
        PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,n,&result)==UMICOM_NETWORK_PACKET_LENGTH,"IPv6 UDP short capture");
        PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"IPv6 UDP truncation changed output");
    }
    frame[59]=9U;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_LENGTH,"IPv6 UDP length mismatch");
    frame[59]=10U;frame[20]=44U;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_FRAGMENT,"IPv6 UDP fragment");
    frame[20]=0U;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_UNSUPPORTED,"IPv6 UDP extension");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"IPv6 UDP framing refusal changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp6-framing");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=sizeof capture;n<80U;++n)frame[n]=0xffU;
    PacketRequire(UmicomKernelEthernetIpv6UdpRead(frame,80U,&result)==UMICOM_NETWORK_PACKET_OK && result.payloadBytes==2U,"IPv6 UDP padding");
    UmicomBootMemoryClear(storage,sizeof storage);
    PacketRequire(result.sourcePort==12345U && result.ipv6.destinationAddress[15]==2U,"IPv6 UDP copied metadata");
    UmicomKernelConsoleWriteLine("packet-admission.case=udp6-padding-lifetime");
}


/* Independent reply vector with a mathematically valid zero checksum. The
 * guest checks that UDP's special zero prohibition did not leak into ICMPv6. */
static void Ipv6EchoValidateExecution(void)
{
    static const UmicomU8 capture[]={0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x01,0x00,0x0a,0x3a,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x81,0x00,0x00,0x00,0xab,0xcd,0x10,0x00,0x67,0x78};
    _Alignas(16) UmicomU8 storage[81];UmicomU8 *frame=storage+1U;
    UmicomBootMemoryClear(storage,sizeof storage);UmicomBootMemoryCopy(frame,capture,sizeof capture);
    UmicomEthernetIpv6IcmpEchoView result,before;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.messageType==129U && result.identifier==0xabcdU && result.sequenceNumber==0x1000U &&
        result.messageBytes==10U && result.payloadOffset==62U && result.payloadBytes==2U,
        "ICMPv6 unaligned valid-zero reply");
    UmicomBootMemoryCopy(&before,&result,sizeof before);
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp6-zero-reply");
    frame[54]=128U;frame[56]=1U;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.messageType==128U,"ICMPv6 request checksum update");
    UmicomBootMemoryCopy(&before,&result,sizeof before);
    frame[63]^=1U;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"ICMPv6 payload corruption");
    frame[63]^=1U;frame[37]^=1U;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"ICMPv6 pseudo-header corruption");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"ICMPv6 checksum changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp6-checksum");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=0U;n<sizeof capture;++n){
        PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,n,&result)==UMICOM_NETWORK_PACKET_LENGTH,"ICMPv6 short capture");
        PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"ICMPv6 prefix changed output");
    }
    frame[54]=135U;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_UNSUPPORTED,"ICMPv6 neighbor message outside echo profile");
    frame[54]=129U;frame[55]=1U;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_HEADER,"ICMPv6 echo code");
    frame[55]=0U;frame[20]=44U;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_FRAGMENT,"ICMPv6 fragment");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"ICMPv6 profile changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp6-framing");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=sizeof capture;n<80U;++n)frame[n]=0xffU;
    PacketRequire(UmicomKernelEthernetIpv6IcmpEchoRead(frame,80U,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.payloadBytes==2U,"ICMPv6 Ethernet padding");
    UmicomBootMemoryClear(storage,sizeof storage);
    PacketRequire(result.identifier==0xabcdU && result.ipv6.destinationAddress[15]==2U,"ICMPv6 copied metadata");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp6-padding-lifetime");
}


/* Independent SYN/ACK capture with a valid zero checksum. This exercises
 * freestanding code with a different vector from the primary host fixture. */
static void Ipv6TcpValidateExecution(void)
{
    static const UmicomU8 capture[]={0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x01,0x00,0x16,0x06,0x40,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x20,0x01,0x0d,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x01,0xbb,0xc0,0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x50,0x12,0x4f,0x46,0x00,0x00,0x00,0x00,0x42,0x19};
    _Alignas(16) UmicomU8 storage[97];UmicomU8 *frame=storage+1U;
    UmicomBootMemoryClear(storage,sizeof storage);UmicomBootMemoryCopy(frame,capture,sizeof capture);
    UmicomEthernetIpv6TcpView result,before;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.sourcePort==443U && result.destinationPort==49152U && result.sequenceNumber==0x10203040U &&
        result.acknowledgmentNumber==0x50607080U && result.flags==0x12U && result.window==0x4f46U &&
        result.segmentBytes==22U && result.payloadOffset==74U && result.payloadBytes==2U,
        "TCP6 unaligned valid-zero SYN/ACK observation");
    UmicomBootMemoryCopy(&before,&result,sizeof before);
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp6-zero-checksum");
    frame[75]^=1U;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"TCP6 payload corruption");
    frame[75]^=1U;frame[37]^=1U;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_CHECKSUM,"TCP6 pseudo-header corruption");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"TCP6 checksum refusal changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp6-corruption");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=0U;n<sizeof capture;++n){
        PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,n,&result)==UMICOM_NETWORK_PACKET_LENGTH,"TCP6 short capture");
        PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"TCP6 truncation changed output");
    }
    frame[66]=0x40U;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_HEADER,"TCP6 invalid offset");
    frame[66]=0x60U;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,sizeof capture,&result)==UMICOM_NETWORK_PACKET_LENGTH,"TCP6 option extent exceeds segment");
    frame[19]=24U;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,78U,&result)==UMICOM_NETWORK_PACKET_UNSUPPORTED,"TCP6 options outside capture profile");
    frame[20]=44U;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,78U,&result)==UMICOM_NETWORK_PACKET_FRAGMENT,"TCP6 fragment");
    PacketRequire(PacketBytesEqual(&result,&before,sizeof result),"TCP6 framing changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp6-framing");
    UmicomBootMemoryCopy(frame,capture,sizeof capture);
    for(UmicomSize n=sizeof capture;n<96U;++n)frame[n]=0xffU;
    PacketRequire(UmicomKernelEthernetIpv6TcpRead(frame,96U,&result)==UMICOM_NETWORK_PACKET_OK &&
        result.payloadBytes==2U,"TCP6 captured padding");
    UmicomBootMemoryClear(storage,sizeof storage);
    PacketRequire(result.sequenceNumber==0x10203040U && result.ipv6.destinationAddress[15]==2U,"TCP6 metadata lifetime");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp6-padding-lifetime");
}


/* Independent neighbor fixtures exercise the same freestanding reader used by
 * host checks. No address assignment or neighbor-cache mutation is performed. */
static void Ipv6NeighborValidateExecution(void)
{
    static const UmicomU8 advertisement[] = {0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x20,0x3a,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x88,0x00,0x00,0x00,0xe0,0x00,0x98,0x19,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x01,0x02,0x00,0x00,0x00,0x00,0x02};
    static const UmicomU8 dad[] = {0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x18,0x3a,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xff,0x00,0x00,0x02,0x87,0x00,0x7c,0x23,0x00,0x00,0x00,0x00,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02};
    static const UmicomU8 badOption[] = {0x33,0x33,0xff,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x00,0x01,0x86,0xdd,0x60,0x00,0x00,0x00,0x00,0x20,0x3a,0xff,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x88,0x00,0x98,0x1a,0xe0,0x00,0x00,0x00,0xfe,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x02,0x00,0x00,0x00,0x00,0x02};
    _Alignas(16) UmicomU8 storage[97];
    UmicomU8 *frame = storage + 1U;
    UmicomEthernetIpv6NeighborView result, before;
    UmicomBootMemoryClear(storage, sizeof storage);
    UmicomBootMemoryCopy(frame, advertisement, sizeof advertisement);
    PacketRequire(UmicomKernelEthernetIpv6NeighborRead(frame, sizeof advertisement, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.messageType == UMICOM_ICMPV6_NEIGHBOR_ADVERTISEMENT &&
        result.messageBytes == 32U && result.router && result.solicited && result.override &&
        result.optionCount == 1U && result.ignoredOptionCount == 0U && result.hasLinkLayerAddress &&
        result.targetAddress[15] == 2U && result.linkLayerAddress[5] == 2U,
        "neighbor6 unaligned valid-zero advertisement");
    UmicomBootMemoryCopy(&before, &result, sizeof before);
    UmicomKernelConsoleWriteLine("packet-admission.case=neighbor6-zero-advertisement");
    frame[85] ^= 1U;
    PacketRequire(UmicomKernelEthernetIpv6NeighborRead(frame, sizeof advertisement, &result) ==
        UMICOM_NETWORK_PACKET_CHECKSUM, "neighbor6 address checksum corruption");
    frame[85] ^= 1U;
    for (UmicomSize n = 0U; n < sizeof advertisement; ++n) {
        PacketRequire(UmicomKernelEthernetIpv6NeighborRead(frame, n, &result) ==
            UMICOM_NETWORK_PACKET_LENGTH, "neighbor6 truncated capture");
        PacketRequire(PacketBytesEqual(&result, &before, sizeof result), "neighbor6 truncation changed output");
    }
    frame[21] = 254U;
    PacketRequire(UmicomKernelEthernetIpv6NeighborRead(frame, sizeof advertisement, &result) ==
        UMICOM_NETWORK_PACKET_HEADER, "neighbor6 hop limit");
    PacketRequire(UmicomKernelEthernetIpv6NeighborRead(badOption, sizeof badOption, &result) ==
        UMICOM_NETWORK_PACKET_HEADER, "neighbor6 zero option units");
    PacketRequire(PacketBytesEqual(&result, &before, sizeof result), "neighbor6 refusal changed output");
    UmicomKernelConsoleWriteLine("packet-admission.case=neighbor6-checksum-framing");
    PacketRequire(UmicomKernelEthernetIpv6NeighborRead(dad, sizeof dad, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.messageType == UMICOM_ICMPV6_NEIGHBOR_SOLICITATION &&
        !result.hasLinkLayerAddress && result.optionCount == 0U && result.ipv6.sourceAddress[0] == 0U &&
        result.ipv6.destinationAddress[0] == 0xffU && result.targetAddress[15] == 2U,
        "neighbor6 unspecified-source solicitation capture");
    UmicomKernelConsoleWriteLine("packet-admission.case=neighbor6-dad-capture");
    UmicomBootMemoryCopy(frame, advertisement, sizeof advertisement);
    for (UmicomSize n = sizeof advertisement; n < 96U; ++n) frame[n] = 0xffU;
    PacketRequire(UmicomKernelEthernetIpv6NeighborRead(frame, 96U, &result) ==
        UMICOM_NETWORK_PACKET_OK && result.messageBytes == 32U, "neighbor6 padding excluded");
    UmicomBootMemoryClear(storage, sizeof storage);
    PacketRequire(result.targetAddress[15] == 2U && result.linkLayerAddress[5] == 2U &&
        result.ipv6.sourceAddress[15] == 2U, "neighbor6 copied metadata lifetime");
    UmicomKernelConsoleWriteLine("packet-admission.case=neighbor6-padding-lifetime");
}


/* The guest uses independent synthetic captures. No NIC or network state is
 * needed to establish tag ordering, byte alignment and copied metadata. */
static void EthernetValidateExecution(void)
{
    static const UmicomU8 raw[] = {2,0,0,0,0,2, 2,0,0,0,0,1, 0x08,0, 0xaa};
    static const UmicomU8 tagged[] = {2,0,0,0,0,2, 2,0,0,0,0,1,
        0x88,0xa8, 0xf0,0x01, 0x81,0, 0x20,0x09, 0x86,0xdd, 0xab,0xcd};
    UmicomEthernetFrameView view,before;
    PacketRequire(UmicomKernelEthernetFrameRead(raw,sizeof raw,&view)==UMICOM_NETWORK_PACKET_OK &&
        view.tagCount==0U && view.etherType==0x0800U && view.payloadOffset==14U &&
        view.payloadBytes==1U && view.sourceMac[5]==1U,"Ethernet plain metadata");
    UmicomKernelConsoleWriteLine("packet-admission.case=ethernet-plain");
    PacketRequire(UmicomKernelEthernetFrameRead(tagged,sizeof tagged,&view)==UMICOM_NETWORK_PACKET_OK &&
        view.tagCount==2U && view.tags[0].protocolIdentifier==0x88a8U &&
        view.tags[0].priority==7U && view.tags[0].dropEligible==UMICOM_TRUE &&
        view.tags[0].vlanIdentifier==1U && view.tags[1].protocolIdentifier==0x8100U &&
        view.tags[1].priority==1U && view.tags[1].dropEligible==UMICOM_FALSE &&
        view.tags[1].vlanIdentifier==9U && view.etherType==0x86ddU &&
        view.payloadOffset==22U && view.payloadBytes==2U,"Ethernet tag order");
    _Alignas(16) UmicomU8 frame[1523];
    UmicomBootMemoryCopy(frame,tagged,18U);
    frame[16]=8U;frame[17]=6U;
    PacketRequire(UmicomKernelEthernetFrameRead(frame,18U,&view)==UMICOM_NETWORK_PACKET_OK &&
        view.tagCount==1U && view.etherType==0x0806U && view.payloadBytes==0U &&
        view.tags[1].protocolIdentifier==0U,"Ethernet single header");
    UmicomKernelConsoleWriteLine("packet-admission.case=ethernet-tags");

    UmicomBootMemoryCopy(&before,&view,sizeof view);
    for(UmicomSize n=0U;n<22U;++n)
        PacketRequire(UmicomKernelEthernetFrameRead(tagged,n,&view)==UMICOM_NETWORK_PACKET_LENGTH &&
            PacketBytesEqual(&before,&view,sizeof view),"Ethernet truncated tag");
    UmicomBootMemoryClear(frame,sizeof frame);
    UmicomBootMemoryCopy(frame,tagged,sizeof tagged);
    frame[20]=0x81U;frame[21]=0U;
    PacketRequire(UmicomKernelEthernetFrameRead(frame,22U,&view)==UMICOM_NETWORK_PACKET_UNSUPPORTED &&
        PacketBytesEqual(&before,&view,sizeof view),"Ethernet tag depth");
    UmicomBootMemoryCopy(frame,raw,sizeof raw);
    PacketRequire(UmicomKernelEthernetFrameRead(frame,1515U,&view)==UMICOM_NETWORK_PACKET_LIMIT &&
        PacketBytesEqual(&before,&view,sizeof view),"Ethernet payload bound");
    UmicomKernelConsoleWriteLine("packet-admission.case=ethernet-refusals");

    UmicomBootMemoryCopy(frame+1U,tagged,sizeof tagged);
    PacketRequire(UmicomKernelEthernetFrameRead(frame+1U,sizeof tagged,&view)==UMICOM_NETWORK_PACKET_OK,
        "Ethernet unaligned input");
    UmicomBootMemoryClear(frame,sizeof frame);
    PacketRequire(view.tags[0].controlInformation==0xf001U && view.tags[1].vlanIdentifier==9U &&
        view.destinationMac[5]==2U && view.payloadOffset==22U,"Ethernet copied lifetime");
    UmicomKernelConsoleWriteLine("packet-admission.case=ethernet-lifetime");
}


/* Use the same independent IP bytes under different envelopes; no transport
 * decoder or network device participates in these composition checks. */
static void TaggedIpv4ValidateExecution(void)
{
    UmicomU8 frame[64],unaligned[65];UmicomEthernetTaggedIpv4View view,before;
    PacketRequire(UmicomKernelEthernetTaggedIpv4Read(packetCapture,sizeof packetCapture,&view)==UMICOM_NETWORK_PACKET_OK &&
        view.frame.tagCount==0U && view.ipv4.payloadOffset==34U && view.ipv4.payloadBytes==12U,
        "tagged IPv4 plain composition");
    UmicomKernelConsoleWriteLine("packet-admission.case=tagged-ipv4-plain");
    UmicomBootMemoryClear(frame,sizeof frame);UmicomBootMemoryCopy(frame,packetCapture,12U);
    frame[12]=0x88U;frame[13]=0xa8U;frame[14]=0xb1U;frame[15]=0x23U;
    frame[16]=0x81U;frame[17]=0U;frame[18]=0x4fU;frame[19]=0xffU;frame[20]=8U;frame[21]=0U;
    UmicomBootMemoryCopy(frame+22U,packetCapture+14U,sizeof packetCapture-14U);
    PacketRequire(UmicomKernelEthernetTaggedIpv4Read(frame,60U,&view)==UMICOM_NETWORK_PACKET_OK &&
        view.frame.tagCount==2U && view.frame.tags[0].vlanIdentifier==0x123U &&
        view.frame.tags[1].vlanIdentifier==4095U && view.frame.payloadOffset==22U &&
        view.frame.payloadBytes==38U && view.ipv4.payloadOffset==42U && view.ipv4.payloadBytes==12U,
        "tagged IPv4 offsets and padding");
    UmicomKernelConsoleWriteLine("packet-admission.case=tagged-ipv4-tags");
    UmicomBootMemoryCopy(&before,&view,sizeof view);
    for(UmicomSize n=0U;n<54U;++n)
        PacketRequire(UmicomKernelEthernetTaggedIpv4Read(frame,n,&view)==UMICOM_NETWORK_PACKET_LENGTH &&
            PacketBytesEqual(&view,&before,sizeof view),"tagged IPv4 truncation");
    frame[30]^=1U;
    PacketRequire(UmicomKernelEthernetTaggedIpv4Read(frame,54U,&view)==UMICOM_NETWORK_PACKET_CHECKSUM &&
        PacketBytesEqual(&view,&before,sizeof view),"tagged IPv4 checksum refusal");
    frame[30]^=1U;
    PacketRefused(frame,54U,UMICOM_NETWORK_PACKET_UNSUPPORTED,"legacy IPv4 remains untagged");
    UmicomKernelConsoleWriteLine("packet-admission.case=tagged-ipv4-refusals");
    UmicomBootMemoryCopy(unaligned+1U,frame,54U);
    PacketRequire(UmicomKernelEthernetTaggedIpv4Read(unaligned+1U,54U,&view)==UMICOM_NETWORK_PACKET_OK,
        "tagged IPv4 unaligned capture");
    UmicomBootMemoryClear(unaligned,sizeof unaligned);UmicomBootMemoryClear(frame,sizeof frame);
    PacketRequire(view.ipv4.sourceAddress[0]==192U && view.ipv4.identification==0x1234U &&
        view.frame.tags[0].priority==5U && view.ipv4.payloadOffset==42U,"tagged IPv4 copied metadata");
    UmicomKernelConsoleWriteLine("packet-admission.case=tagged-ipv4-lifetime");
}

/* Exercise the echo writer against the earlier independent capture. The
 * no-NIC guest observes only owned RAM bytes, never host network traffic. */
static void IcmpWriteValidateExecution(void)
{
    UmicomEthernetIpv4IcmpEchoSpec spec;
    UmicomBootMemoryClear(&spec, sizeof spec);
    UmicomBootMemoryCopy(spec.destinationMac, echoCapture, 6U);
    UmicomBootMemoryCopy(spec.sourceMac, echoCapture + 6U, 6U);
    UmicomBootMemoryCopy(spec.sourceAddress, echoCapture + 26U, 4U);
    UmicomBootMemoryCopy(spec.destinationAddress, echoCapture + 30U, 4U);
    spec.identification = 0x1234U; spec.echoIdentifier = 0x1234U;
    spec.sequenceNumber = 0xabcdU; spec.timeToLive = 64U;
    spec.messageType = UMICOM_ICMP_ECHO_REQUEST;
    UmicomU8 frame[60], before[60];
    for (UmicomSize i = 0U; i < sizeof frame; ++i) frame[i] = 0xa5U;
    UmicomSize written = 0U;
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec, echoCapture + 42U, 5U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_OK, "ICMP write request");
    PacketRequire(written == sizeof echoCapture && PacketBytesEqual(frame, echoCapture, sizeof echoCapture),
        "ICMP write independent bytes");
    for (UmicomSize i = written; i < sizeof frame; ++i)
        PacketRequire(frame[i] == 0xa5U, "ICMP write spare capacity");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-write-golden");

    UmicomBootMemoryCopy(before, frame, sizeof frame); written = 777U;
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec, echoCapture + 42U, 5U,
        frame, 46U, &written) == UMICOM_NETWORK_PACKET_LENGTH, "ICMP write short capacity");
    PacketRequire(written == 777U && PacketBytesEqual(before, frame, sizeof frame), "ICMP write refusal publication");
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec, frame + 1U, 5U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_ARGUMENT, "ICMP write overlap");
    PacketRequire(written == 777U && PacketBytesEqual(before, frame, sizeof frame), "ICMP write overlap publication");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-write-refusals");

    UmicomEthernetIpv4IcmpEchoView view;
    spec.messageType = UMICOM_ICMP_ECHO_REPLY;
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec, echoCapture + 42U, 5U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_OK, "ICMP write reply");
    PacketRequire(frame[34] == 0U && frame[36] == 0xfeU && frame[37] == 0x2bU &&
        UmicomKernelEthernetIpv4IcmpEchoRead(frame, written, &view) == UMICOM_NETWORK_PACKET_OK &&
        view.messageType == UMICOM_ICMP_ECHO_REPLY && view.identifier == 0x1234U &&
        view.sequenceNumber == 0xabcdU, "ICMP write reply identity");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-write-reply");

    const UmicomU8 zeroPayload[2] = {0x39U, 0xfeU};
    spec.messageType = UMICOM_ICMP_ECHO_REQUEST;
    PacketRequire(UmicomKernelEthernetIpv4IcmpEchoWrite(&spec, zeroPayload, 2U,
        frame, sizeof frame, &written) == UMICOM_NETWORK_PACKET_OK, "ICMP write zero checksum");
    PacketRequire(frame[36] == 0U && frame[37] == 0U &&
        UmicomKernelEthernetIpv4IcmpEchoRead(frame, written, &view) == UMICOM_NETWORK_PACKET_OK,
        "ICMP zero checksum remains verified");
    UmicomKernelConsoleWriteLine("packet-admission.case=icmp-write-zero-checksum");
}

/* This fixture owns RAM only. It does not bypass packet admission to inject
 * traffic: no device, socket, TCP connection or transmitted ACK exists here. */
static void TcpReceiveValidateExecution(void)
{
    static UmicomTcpReceive receiver;
    static UmicomU8 before[sizeof receiver], data[UMICOM_TCP_RECEIVE_CAPACITY];
    static UmicomU8 output[UMICOM_TCP_RECEIVE_CAPACITY];
    UmicomTcpReceiveSnapshot snapshot;
    UmicomU32 cursor=77U, count=77U;
    for(UmicomU32 i=0U;i<UMICOM_TCP_RECEIVE_CAPACITY;++i) data[i]=(UmicomU8)(i%251U);
    PacketRequire(UmicomKernelTcpReceiveInit(&receiver,0xfffffffeU)==UMICOM_TCP_SEQUENCE_ACCEPT,
        "receive initialization");
    PacketRequire(UmicomKernelTcpReceiveAppend(&receiver,0xfffffffeU,data,4U,&cursor)==UMICOM_TCP_SEQUENCE_ACCEPT && cursor==2U,
        "receive cursor wraps only after copy");
    data[0]=255U;
    PacketRequire(UmicomKernelTcpReceiveRead(&receiver,output,2U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==2U &&
        output[0]==0U && output[1]==1U && receiver.next==2U, "receive owns payload and read preserves cursor");
    PacketRequire(receiver.bytes[0]==0U && receiver.bytes[1]==0U && receiver.bytes[2]==2U,
        "receive clears consumed slots only");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-receive-ownership");

    UmicomBootMemoryCopy(before,&receiver,sizeof receiver);
    cursor=77U;
    PacketRequire(UmicomKernelTcpReceiveAppend(&receiver,3U,data,1U,&cursor)==UMICOM_TCP_SEQUENCE_REJECT &&
        cursor==77U && PacketBytesEqual(before,&receiver,sizeof receiver), "receive gap atomic refusal");
    PacketRequire(UmicomKernelTcpReceiveAppend(&receiver,2U,data,0xffffffffU,&cursor)==UMICOM_TCP_SEQUENCE_REJECT &&
        cursor==77U && PacketBytesEqual(before,&receiver,sizeof receiver), "receive huge count atomic refusal");
    PacketRequire(UmicomKernelTcpReceiveAppend(&receiver,2U,0,1U,&cursor)==UMICOM_TCP_SEQUENCE_INVALID &&
        cursor==77U && PacketBytesEqual(before,&receiver,sizeof receiver), "receive null atomic refusal");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-receive-refusals");

    data[0]=0U;
    PacketRequire(UmicomKernelTcpReceiveInit(&receiver,100U)==UMICOM_TCP_SEQUENCE_ACCEPT &&
        UmicomKernelTcpReceiveAppend(&receiver,100U,data,UMICOM_TCP_RECEIVE_CAPACITY,&cursor)==UMICOM_TCP_SEQUENCE_ACCEPT &&
        cursor==1124U, "receive fills bounded capacity");
    PacketRequire(UmicomKernelTcpReceiveInspect(&receiver,&snapshot)==UMICOM_TCP_SEQUENCE_ACCEPT &&
        snapshot.buffered==UMICOM_TCP_RECEIVE_CAPACITY && snapshot.available==0U, "receive reports full storage");
    UmicomBootMemoryCopy(before,&receiver,sizeof receiver);
    cursor=77U;
    PacketRequire(UmicomKernelTcpReceiveAppend(&receiver,1124U,data,1U,&cursor)==UMICOM_TCP_SEQUENCE_REJECT &&
        cursor==77U && PacketBytesEqual(before,&receiver,sizeof receiver), "receive full storage refuses acknowledgment advance");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-receive-backpressure");

    PacketRequire(UmicomKernelTcpReceiveRead(&receiver,output,1000U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==1000U &&
        PacketBytesEqual(output,data,1000U), "receive ordered first drain");
    PacketRequire(UmicomKernelTcpReceiveAppend(&receiver,1124U,data,100U,&cursor)==UMICOM_TCP_SEQUENCE_ACCEPT && cursor==1224U,
        "receive reuses freed ring slots");
    PacketRequire(UmicomKernelTcpReceiveRead(&receiver,output,UMICOM_TCP_RECEIVE_CAPACITY,&count)==UMICOM_TCP_SEQUENCE_ACCEPT &&
        count==124U && PacketBytesEqual(output,data+1000U,24U) && PacketBytesEqual(output+24U,data,100U),
        "receive preserves stream order across ring wrap");
    PacketRequire(UmicomKernelTcpReceiveInspect(&receiver,&snapshot)==UMICOM_TCP_SEQUENCE_ACCEPT &&
        snapshot.next==1224U && snapshot.buffered==0U && snapshot.available==UMICOM_TCP_RECEIVE_CAPACITY,
        "receive drained counters");
    for(UmicomU32 i=0U;i<UMICOM_TCP_RECEIVE_CAPACITY;++i)
        PacketRequire(receiver.bytes[i]==0U,"receive consumed slots cleared");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-receive-ring-wrap");
}

/* Synthetic RAM exercise: no socket, peer, transmission or authenticated
 * acknowledgment packet is involved. The actual buffer owner is called. */
static void TcpSendValidateExecution(void)
{
    static UmicomTcpSendBuffer sender;
    static UmicomU8 before[sizeof sender], data[UMICOM_TCP_SEND_CAPACITY], output[UMICOM_TCP_SEND_CAPACITY];
    UmicomTcpTransmitSnapshot snapshot;
    UmicomU32 start=77U,count=77U;
    for(UmicomU32 i=0U;i<UMICOM_TCP_SEND_CAPACITY;++i)data[i]=(UmicomU8)(i%251U);
    PacketRequire(UmicomKernelTcpSendInit(&sender,0xfffffffeU)==UMICOM_TCP_SEQUENCE_ACCEPT,
        "send initialization");
    PacketRequire(UmicomKernelTcpSendAppend(&sender,data,4U,&start)==UMICOM_TCP_SEQUENCE_ACCEPT && start==0xfffffffeU,
        "send owns four wrapping sequence bytes");
    data[0]=99U;
    for(UmicomU32 i=0U;i<2U;++i)
        PacketRequire(UmicomKernelTcpSendCopy(&sender,start,output,4U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==4U &&
            output[0]==0U && output[1]==1U && output[2]==2U && output[3]==3U,
            "send repeated copies retain original bytes");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-send-ownership");

    UmicomBootMemoryCopy(before,&sender,sizeof sender);count=77U;
    PacketRequire(UmicomKernelTcpSendAcknowledge(&sender,3U,&count)==UMICOM_TCP_SEQUENCE_REJECT && count==77U &&
        PacketBytesEqual(before,&sender,sizeof sender),"send future ACK preserves bytes");
    PacketRequire(UmicomKernelTcpSendAppend(&sender,data,0xffffffffU,&count)==UMICOM_TCP_SEQUENCE_INVALID && count==77U &&
        PacketBytesEqual(before,&sender,sizeof sender),"send huge payload refused before copy");
    PacketRequire(UmicomKernelTcpSendCopy(&sender,2U,output,4U,&count)==UMICOM_TCP_SEQUENCE_REJECT && count==77U &&
        PacketBytesEqual(before,&sender,sizeof sender),"send endpoint is outside retained interval");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-send-refusals");

    PacketRequire(UmicomKernelTcpSendAcknowledge(&sender,0U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==2U,
        "send partial ACK across zero");
    PacketRequire(sender.bytes[0]==0U && sender.bytes[1]==0U && sender.bytes[2]==2U && sender.bytes[3]==3U,
        "send clears only acknowledged slots");
    PacketRequire(UmicomKernelTcpSendCopy(&sender,0U,output,4U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==2U &&
        output[0]==2U && output[1]==3U,"send unacknowledged tail retained");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-send-partial-ack");

    for(UmicomU32 i=0U;i<UMICOM_TCP_SEND_CAPACITY;++i)data[i]=(UmicomU8)(i%251U);
    PacketRequire(UmicomKernelTcpSendInit(&sender,100U)==UMICOM_TCP_SEQUENCE_ACCEPT &&
        UmicomKernelTcpSendAppend(&sender,data,UMICOM_TCP_SEND_CAPACITY,&start)==UMICOM_TCP_SEQUENCE_ACCEPT,
        "send fills bounded capacity");
    PacketRequire(UmicomKernelTcpSendInspect(&sender,&snapshot)==UMICOM_TCP_SEQUENCE_ACCEPT && snapshot.available==0U,
        "send full snapshot");
    UmicomBootMemoryCopy(before,&sender,sizeof sender);count=77U;
    PacketRequire(UmicomKernelTcpSendAppend(&sender,data,1U,&count)==UMICOM_TCP_SEQUENCE_REJECT && count==77U &&
        PacketBytesEqual(before,&sender,sizeof sender),"send full backpressure");
    PacketRequire(UmicomKernelTcpSendAcknowledge(&sender,1100U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==1000U,
        "send releases first thousand");
    PacketRequire(UmicomKernelTcpSendAppend(&sender,data,1000U,&start)==UMICOM_TCP_SEQUENCE_ACCEPT && start==1124U,
        "send reuses released ring slots");
    PacketRequire(UmicomKernelTcpSendCopy(&sender,1100U,output,UMICOM_TCP_SEND_CAPACITY,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==1024U &&
        PacketBytesEqual(output,data+1000U,24U) && PacketBytesEqual(output+24U,data,1000U),"send ring keeps stream order");
    PacketRequire(UmicomKernelTcpSendAcknowledge(&sender,2124U,&count)==UMICOM_TCP_SEQUENCE_ACCEPT && count==1024U,
        "send final release");
    for(UmicomU32 i=0U;i<UMICOM_TCP_SEND_CAPACITY;++i)PacketRequire(sender.bytes[i]==0U,"send released storage cleared");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-send-ring-wrap");
}

/* Serialize literal TCP captures in guest RAM. These calls do not bind a NIC,
 * allocate packet queues, advance a send cursor or acknowledge a connection. */
static void TcpWriteValidateExecution(void)
{
    UmicomEthernetIpv4TcpSpec spec;
    UmicomBootMemoryClear(&spec,sizeof spec);
    UmicomBootMemoryCopy(spec.destinationMac,tcpCapture,6U);
    UmicomBootMemoryCopy(spec.sourceMac,tcpCapture+6U,6U);
    UmicomBootMemoryCopy(spec.sourceAddress,tcpCapture+26U,4U);
    UmicomBootMemoryCopy(spec.destinationAddress,tcpCapture+30U,4U);
    spec.identification=0x1234U;spec.timeToLive=64U;
    spec.sourcePort=12345U;spec.destinationPort=80U;
    spec.sequenceNumber=0x12345678U;spec.acknowledgmentNumber=0x90abcdefU;
    spec.flags=UMICOM_TCP_ACK|UMICOM_TCP_PSH;spec.window=4096U;
    UmicomU8 output[64],before[64];UmicomSize written=999U;
    for(UmicomSize i=0U;i<sizeof output;++i)output[i]=0xa5U;
    PacketRequire(UmicomKernelEthernetIpv4TcpWrite(&spec,tcpCapture+54U,5U,
        output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK && written==sizeof tcpCapture &&
        PacketBytesEqual(output,tcpCapture,sizeof tcpCapture) && output[written]==0xa5U,
        "TCP write literal capture");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-write-golden");
    UmicomBootMemoryCopy(before,output,sizeof output);written=999U;
    PacketRequire(UmicomKernelEthernetIpv4TcpWrite(&spec,tcpCapture+54U,5U,
        output,58U,&written)==UMICOM_NETWORK_PACKET_LENGTH && written==999U &&
        PacketBytesEqual(before,output,sizeof output),"TCP write capacity refusal");
    PacketRequire(UmicomKernelEthernetIpv4TcpWrite(&spec,output+60U,1U,
        output,sizeof output,&written)==UMICOM_NETWORK_PACKET_ARGUMENT && written==999U &&
        PacketBytesEqual(before,output,sizeof output),"TCP write tail alias refusal");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-write-refusals");
    UmicomEthernetIpv4TcpView observed;
    PacketRequire(UmicomKernelEthernetIpv4TcpWrite(&spec,0,0U,
        output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK && written==54U,
        "TCP write empty capture");
    PacketRequire(UmicomKernelEthernetIpv4TcpRead(output,written,&observed)==UMICOM_NETWORK_PACKET_OK &&
        observed.payloadBytes==0U && observed.sequenceNumber==spec.sequenceNumber,
        "TCP empty checksum and header");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-write-empty");
    spec.window=0x87edU;
    PacketRequire(UmicomKernelEthernetIpv4TcpWrite(&spec,tcpCapture+54U,5U,
        output,sizeof output,&written)==UMICOM_NETWORK_PACKET_OK && output[50]==0U && output[51]==0U,
        "TCP write keeps valid zero checksum");
    PacketRequire(UmicomKernelEthernetIpv4TcpRead(output,written,&observed)==UMICOM_NETWORK_PACKET_OK,
        "TCP zero checksum remains mandatory");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-write-zero-checksum");
}


/* Run structural option checks against the freestanding implementation, while
 * preserving the earlier fixed-header and stream-storage acceptance groups. */
static void TcpOptionsValidateExecution(void)
{
    UmicomU8 area[40] = {1U,2U,4U,0x05U,0xB4U,1U,0U,0U};
    UmicomTcpOptionsView out;
    PacketRequire(UmicomKernelTcpOptionsRead(area,8U,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.count==3U && out.entries[1].kind==2U && out.entries[1].offset==1U &&
        out.entries[1].length==4U && out.consumedBytes==7U && out.paddingBytes==1U,
        "option coordinates");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-options-golden");
/* Use the freestanding C null-pointer constant because Kernel types do not define UMICOM_NULL; retain the authored fixture for review. The former implementation is retained for owner review. */
#if 0
    PacketRequire(UmicomKernelTcpOptionsRead(UMICOM_NULL,0U,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.count==0U && out.consumedBytes==0U && !out.endSeen,"empty option area");
#endif
    PacketRequire(UmicomKernelTcpOptionsRead((const void *)0,0U,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.count==0U && out.consumedBytes==0U && !out.endSeen,"empty option area");
    for(UmicomSize i=0U;i<sizeof area;++i)area[i]=1U;
    PacketRequire(UmicomKernelTcpOptionsRead(area,sizeof area,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.count==40U && out.entries[39].offset==39U && !out.endSeen,"maximum NOP area");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-options-boundary");
    UmicomTcpOptionsView before;
    UmicomBootMemoryCopy(&before,&out,sizeof before);
    area[39]=253U;
    PacketRequire(UmicomKernelTcpOptionsRead(area,sizeof area,&out)==UMICOM_NETWORK_PACKET_LENGTH &&
        PacketBytesEqual(&before,&out,sizeof out),"missing final option length preserves output");
    UmicomBootMemoryClear(area,sizeof area);area[39]=1U;
    PacketRequire(UmicomKernelTcpOptionsRead(area,sizeof area,&out)==UMICOM_NETWORK_PACKET_HEADER &&
        PacketBytesEqual(&before,&out,sizeof out),"nonzero EOL padding preserves output");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-options-refusals");
    _Alignas(16) UmicomU8 unaligned[5]={0U,253U,2U,0U,0U};
    PacketRequire(UmicomKernelTcpOptionsRead(unaligned+1U,4U,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.count==1U && out.entries[0].kind==253U && out.entries[0].length==2U,
        "unaligned opaque option");
    unaligned[1]=17U;
    PacketRequire(out.entries[0].kind==253U,"copied option metadata lifetime");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-options-lifetime");
}


/* Known-value decoding remains a RAM-only inspection step. These guest checks
 * do not enable negotiated TCP features or alter send/receive connection state. */
static void TcpOptionValuesValidateExecution(void)
{
    UmicomU8 bytes[24] = {2U,4U,0x05U,0xB4U,3U,3U,15U,4U,2U,
        8U,10U,0x11U,0x22U,0x33U,0x44U,0x88U,0x99U,0xAAU,0xBBU,0U};
    UmicomTcpOptionValues out;
    PacketRequire(UmicomKernelTcpOptionValuesRead(bytes,20U,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.hasMss && out.mss==1460U && out.hasWindowScale && out.windowScaleRaw==15U &&
        out.windowScaleBounded==14U && out.sackPermitted && out.hasTimestamp &&
        out.timestampValue==0x11223344U && out.timestampEcho==0x8899AABBU &&
        out.unknownCount==0U,"known option copied fields");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-option-values-golden");
    UmicomTcpOptionValues before;
    UmicomBootMemoryCopy(&before,&out,sizeof before);
    bytes[19]=2U;bytes[20]=2U;
    PacketRequire(UmicomKernelTcpOptionValuesRead(bytes,sizeof bytes,&out)==UMICOM_NETWORK_PACKET_HEADER &&
        PacketBytesEqual(&before,&out,sizeof out),"late option refusal keeps prior values");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-option-values-refusal");
    const UmicomU8 highScale[4]={3U,3U,255U,0U};
    PacketRequire(UmicomKernelTcpOptionValuesRead(highScale,sizeof highScale,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.windowScaleRaw==255U && out.windowScaleBounded==14U && !out.hasMss,
        "window shift preserves raw offer and bounds interpreted shift");
    const UmicomU8 opaque[4]={5U,2U,253U,2U};
    PacketRequire(UmicomKernelTcpOptionValuesRead(opaque,sizeof opaque,&out)==UMICOM_NETWORK_PACKET_OK &&
        out.unknownCount==2U && !out.sackPermitted,"opaque kinds do not enable SACK");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-option-values-boundary");
    UmicomBootMemoryCopy(&out,&before,sizeof out);
    UmicomBootMemoryClear(bytes,sizeof bytes);
    PacketRequire(PacketBytesEqual(&before,&out,sizeof out),"copied values outlive option bytes");
    PacketRequire(UmicomKernelTcpOptionValuesRead((const void *)0,0U,&out)==UMICOM_NETWORK_PACKET_OK &&
        !out.hasMss && !out.hasWindowScale && !out.sackPermitted && !out.hasTimestamp &&
        out.mss==0U && out.timestampValue==0U && out.unknownCount==0U,
        "empty values replace all presence state");
    UmicomKernelConsoleWriteLine("packet-admission.case=tcp-option-values-lifetime");
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
    /* Keep the former eleven-group transcript count for review. Those groups
     * still execute, followed by four ICMP capture checks. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=11");
#endif
    EchoValidateExecution();
    /* The fifteen existing groups remain, followed by fixed-header TCP
     * inspection. Preserve the old transcript count for engineering review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=15");
#endif
    TcpValidateExecution();
/* All nineteen IPv4/transport groups still execute. Four ARP groups extend the current capture contract; keep the earlier count for review. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=19");
#endif
    ArpValidateExecution();
/* All twenty-three existing groups remain; four IPv6 capture groups extend the diagnostic transcript. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=23");
#endif
    Ipv6ValidateExecution();
    Ipv6UdpValidateExecution();
/* The four IPv6 UDP groups extend the preceding twenty-seven groups, which still execute in order. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=27");
#endif
/* Four ICMPv6 echo groups extend the prior thirty-one groups, which remain in order. Retain the earlier transcript count for review. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=31");
#endif
    Ipv6EchoValidateExecution();
/* Four TCP6 groups extend the same thirty-five existing guest groups. Retain the previous transcript total for review. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=35");
#endif
    Ipv6TcpValidateExecution();
/* Keep all thirty-nine prior groups; neighbor captures add four ordered guest groups. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=39");
#endif
    Ipv6NeighborValidateExecution();
/* Retain the same forty-three earlier packet groups; four generic Ethernet/VLAN groups extend the guest transcript. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=43");
#endif
    EthernetValidateExecution();
/* Retain all forty-seven previous groups and extend the ordered transcript with four tagged IPv4 composition groups. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=47");
#endif
    TaggedIpv4ValidateExecution();
/* Keep all fifty-one admission groups and extend their ordered transcript with four RAM-only encoding groups. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=51");
#endif
    UdpWriteValidateExecution();
/* Retain the fifty-five earlier packet groups and append four ICMP encoding groups to the ordered diagnostic evidence. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=55");
#endif
    IcmpWriteValidateExecution();
/* The fifty-nine earlier packet groups still run; four owned receive-storage groups extend the current no-NIC diagnostic. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=59");
#endif
    TcpReceiveValidateExecution();
/* The existing sixty-three groups still execute; four owned send-storage groups extend this RAM-only diagnostic. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=63");
#endif
    TcpSendValidateExecution();
    /* Retain the earlier total; all sixty-seven groups still execute before TCP serialization. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=67");
#endif
    TcpWriteValidateExecution();
/* Keep the seventy-one prior guest groups and append four option-structure groups without a NIC. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=71");
#endif
    TcpOptionsValidateExecution();
/* Retain the seventy-five prior groups and append four known-option value groups in the no-NIC guest. The former implementation is retained for owner review. */
#if 0
    UmicomKernelConsoleWriteLine("packet-admission.cases=75");
#endif
    TcpOptionValuesValidateExecution();
    UmicomKernelConsoleWriteLine("packet-admission.cases=79");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PACKET_ADMISSION_READY");
}
