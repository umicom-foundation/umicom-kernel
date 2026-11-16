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
    UmicomKernelConsoleWriteLine("packet-admission.cases=51");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PACKET_ADMISSION_READY");
}
