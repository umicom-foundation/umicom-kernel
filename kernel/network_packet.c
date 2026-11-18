/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/network_packet.c
 *
 * PURPOSE:
 *   Validate Ethernet-II and fixed IPv4 headers without allocation or unaligned word access.
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
#include "umicom/kernel/network_udp.h"
#include "umicom/kernel/network_udp6.h"
#include "umicom/kernel/network_icmp6.h"
#include "umicom/kernel/network_tcp6.h"
#include "umicom/kernel/network_neighbor6.h"
#include "umicom/kernel/network_icmp.h"
#include "umicom/kernel/network_tcp.h"
#include "umicom/kernel/tcp_options.h"
#include "umicom/kernel/address.h"
#include "boot_memory.h"

static UmicomU16 ReadNetwork16(const UmicomU8 *p)
{
    return (UmicomU16)(((UmicomU16)p[0] << 8U) | (UmicomU16)p[1]);
}

/* The original untagged reader is retained for engineering review. Its fixed
 * IPv4 logic now lives in PacketIpv4PayloadRead; the active wrapper keeps the
 * original argument checks, status precedence and untagged EtherType policy. */
#if 0
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4Read(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4View *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV4_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;

    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv4View) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end))
        return UMICOM_NETWORK_PACKET_ARGUMENT;

    /* Decode bytes rather than casting a packed wire header. This remains
     * valid for unaligned captures and for either host byte order. No field
     * is read until the caller's span includes the complete minimum header. */
    const UmicomU8 *frame = (const UmicomU8 *)bytes;
    if (ReadNetwork16(frame + 12U) != 0x0800U)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    const UmicomU8 *ip = frame + 14U;
    if ((ip[0] >> 4U) != 4U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    const UmicomSize headerBytes = (UmicomSize)(ip[0] & 15U) * 4U;
    if (headerBytes < 20U) return UMICOM_NETWORK_PACKET_HEADER;
    if (headerBytes > accessibleBytes - 14U) return UMICOM_NETWORK_PACKET_LENGTH;
    if (headerBytes != 20U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    const UmicomU16 ipBytes = ReadNetwork16(ip + 2U);
    if (ipBytes < 20U) return UMICOM_NETWORK_PACKET_HEADER;
    if (ipBytes > 1500U) return UMICOM_NETWORK_PACKET_LIMIT;
    if ((UmicomSize)ipBytes > accessibleBytes - 14U)
        return UMICOM_NETWORK_PACKET_LENGTH;

    const UmicomU16 flagsAndOffset = ReadNetwork16(ip + 6U);
    if (flagsAndOffset & 0x8000U) return UMICOM_NETWORK_PACKET_HEADER;
    /* A complete transport payload cannot be inferred from a fragment.
     * Reassembly needs its own bounded ownership and timeout design. */
    if (flagsAndOffset & 0x3fffU) return UMICOM_NETWORK_PACKET_FRAGMENT;
    UmicomU32 sum = 0U;
    for (UmicomSize i = 0U; i < 20U; i += 2U)
        sum += ReadNetwork16(ip + i);
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;

    /* Stage the complete result before publishing it. Existing freestanding
     * RAM helpers avoid introducing a hosted memcpy/memset dependency. TTL and
     * protocol are observations; address/routing/transport policies come later. */
    UmicomEthernetIpv4View staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(staged.destinationMac, frame, 6U);
    UmicomBootMemoryCopy(staged.sourceMac, frame + 6U, 6U);
    UmicomBootMemoryCopy(staged.sourceAddress, ip + 12U, 4U);
    UmicomBootMemoryCopy(staged.destinationAddress, ip + 16U, 4U);
    staged.differentiatedServices = ip[1];
    staged.timeToLive = ip[8];
    staged.protocol = ip[9];
    staged.dontFragment = (flagsAndOffset & 0x4000U) ? UMICOM_TRUE : UMICOM_FALSE;
    staged.identification = ReadNetwork16(ip + 4U);
    staged.ipBytes = ipBytes;
    staged.payloadOffset = UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET;
    staged.payloadBytes = (UmicomSize)ipBytes - 20U;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}
#endif

/* Both callers prove RAM extents, output separation and the Ethernet header
 * before entering this shared decoder. Keeping one IPv4 owner prevents tagged
 * captures from acquiring different checksum or fragmentation policies. */
static UmicomNetworkPacketStatus PacketIpv4PayloadRead(const UmicomU8 *frame,
    UmicomSize accessibleBytes,UmicomSize ipOffset,UmicomEthernetIpv4View *out)
{
    if(ipOffset>accessibleBytes || accessibleBytes-ipOffset<20U)
        return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *ip = frame + ipOffset;
    if ((ip[0] >> 4U) != 4U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    const UmicomSize headerBytes = (UmicomSize)(ip[0] & 15U) * 4U;
    if (headerBytes < 20U) return UMICOM_NETWORK_PACKET_HEADER;
    if (headerBytes > accessibleBytes - ipOffset) return UMICOM_NETWORK_PACKET_LENGTH;
    if (headerBytes != 20U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    const UmicomU16 ipBytes = ReadNetwork16(ip + 2U);
    if (ipBytes < 20U) return UMICOM_NETWORK_PACKET_HEADER;
    if (ipBytes > 1500U) return UMICOM_NETWORK_PACKET_LIMIT;
    if ((UmicomSize)ipBytes > accessibleBytes - ipOffset)
        return UMICOM_NETWORK_PACKET_LENGTH;

    const UmicomU16 flagsAndOffset = ReadNetwork16(ip + 6U);
    if (flagsAndOffset & 0x8000U) return UMICOM_NETWORK_PACKET_HEADER;
    /* A complete transport payload cannot be inferred from a fragment.
     * Reassembly needs its own bounded ownership and timeout design. */
    if (flagsAndOffset & 0x3fffU) return UMICOM_NETWORK_PACKET_FRAGMENT;
    UmicomU32 sum = 0U;
    for (UmicomSize i = 0U; i < 20U; i += 2U)
        sum += ReadNetwork16(ip + i);
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;

    /* Stage the complete result before publishing it. Existing freestanding
     * RAM helpers avoid introducing a hosted memcpy/memset dependency. TTL and
     * protocol are observations; address/routing/transport policies come later. */
    UmicomEthernetIpv4View staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(staged.destinationMac, frame, 6U);
    UmicomBootMemoryCopy(staged.sourceMac, frame + 6U, 6U);
    UmicomBootMemoryCopy(staged.sourceAddress, ip + 12U, 4U);
    UmicomBootMemoryCopy(staged.destinationAddress, ip + 16U, 4U);
    staged.differentiatedServices = ip[1];
    staged.timeToLive = ip[8];
    staged.protocol = ip[9];
    staged.dontFragment = (flagsAndOffset & 0x4000U) ? UMICOM_TRUE : UMICOM_FALSE;
    staged.identification = ReadNetwork16(ip + 4U);
    staged.ipBytes = ipBytes;
    staged.payloadOffset = ipOffset + 20U;
    staged.payloadBytes = (UmicomSize)ipBytes - 20U;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}

UmicomNetworkPacketStatus UmicomKernelEthernetIpv4Read(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4View *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV4_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;

    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv4View) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end))
        return UMICOM_NETWORK_PACKET_ARGUMENT;

    /* Decode bytes rather than casting a packed wire header. This remains
     * valid for unaligned captures and for either host byte order. No field
     * is read until the caller's span includes the complete minimum header. */
    const UmicomU8 *frame = (const UmicomU8 *)bytes;
    if (ReadNetwork16(frame + 12U) != 0x0800U)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    return PacketIpv4PayloadRead(frame, accessibleBytes, 14U, out);
}

/* Transport admission reuses the complete IPv4 reader, so fragmentation,
 * header checksum, capture bounds and byte-order rules have one owner. A
 * separate output preflight covers the larger transport view before reads. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4UdpRead(
    const void *bytes, UmicomSize accessibleBytes,
    UmicomUdpChecksumPolicy policy, UmicomEthernetIpv4UdpView *out)
{
    if (!bytes || !out || (policy != UMICOM_UDP_CHECKSUM_ALLOW_ABSENT &&
        policy != UMICOM_UDP_CHECKSUM_REQUIRE)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV4_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv4UdpView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;

    UmicomEthernetIpv4View ip;
    const UmicomNetworkPacketStatus status =
        UmicomKernelEthernetIpv4Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.protocol != 17U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 8U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *udp = (const UmicomU8 *)bytes + ip.payloadOffset;
    const UmicomU16 length = ReadNetwork16(udp + 4U);
    if (length < 8U) return UMICOM_NETWORK_PACKET_HEADER;
    /* Exact equality deliberately excludes transport options and surplus IP
     * data. Captured Ethernet padding remains outside ip.payloadBytes. */
    if ((UmicomSize)length != ip.payloadBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU16 checksum = ReadNetwork16(udp + 6U);
    if (!checksum && policy == UMICOM_UDP_CHECKSUM_REQUIRE)
        return UMICOM_NETWORK_PACKET_CHECKSUM;
    if (checksum) {
        UmicomU32 sum = 17U + (UmicomU32)length;
        for (UmicomSize i = 0U; i < 4U; i += 2U) {
            sum += ReadNetwork16(ip.sourceAddress + i);
            sum += ReadNetwork16(ip.destinationAddress + i);
        }
        for (UmicomSize i = 0U; i + 1U < length; i += 2U)
            sum += ReadNetwork16(udp + i);
        /* The last odd byte is the high half of a zero-padded word. The
         * conceptual padding is never read from the capture or written to it.
         * At most 746 words are summed, which fits the 32-bit accumulator. */
        if (length & 1U) sum += (UmicomU32)udp[length - 1U] << 8U;
        while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
        if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;
    }
    UmicomEthernetIpv4UdpView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv4, &ip, sizeof ip);
    staged.sourcePort = ReadNetwork16(udp);
    staged.destinationPort = ReadNetwork16(udp + 2U);
    staged.datagramBytes = length;
    staged.checksumPresent = checksum ? UMICOM_TRUE : UMICOM_FALSE;
    staged.payloadOffset = ip.payloadOffset + 8U;
    staged.payloadBytes = (UmicomSize)length - 8U;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}

/* Reuse IPv4's complete admission rather than independently interpreting its
 * length, fragmentation or header checksum. ICMP's checksum starts at its type
 * byte and has no UDP-style IPv4 pseudo-header. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4IcmpEchoRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4IcmpEchoView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV4_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv4IcmpEchoView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;

    UmicomEthernetIpv4View ip;
    const UmicomNetworkPacketStatus status =
        UmicomKernelEthernetIpv4Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.protocol != 1U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 8U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *icmp = (const UmicomU8 *)bytes + ip.payloadOffset;
    if (icmp[0] != UMICOM_ICMP_ECHO_REQUEST && icmp[0] != UMICOM_ICMP_ECHO_REPLY)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (icmp[1] != 0U) return UMICOM_NETWORK_PACKET_HEADER;
    UmicomU32 sum = 0U;
    for (UmicomSize i = 0U; i + 1U < ip.payloadBytes; i += 2U)
        sum += ReadNetwork16(icmp + i);
    /* Conceptual odd-byte padding never reads beyond the IP message or mutates
     * a capture. The admitted 1480-byte maximum fits the 32-bit accumulator. */
    if (ip.payloadBytes & 1U) sum += (UmicomU32)icmp[ip.payloadBytes - 1U] << 8U;
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;

    UmicomEthernetIpv4IcmpEchoView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv4, &ip, sizeof ip);
    staged.messageType = icmp[0];
    staged.identifier = ReadNetwork16(icmp + 4U);
    staged.sequenceNumber = ReadNetwork16(icmp + 6U);
    staged.messageBytes = (UmicomU16)ip.payloadBytes;
    staged.payloadOffset = ip.payloadOffset + 8U;
    staged.payloadBytes = ip.payloadBytes - 8U;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}


/* Decode sequence fields from bytes, preserving support for unaligned captures
 * and hosts whose native word order differs from the packet's network order. */
static UmicomU32 ReadNetwork32(const UmicomU8 *p)
{
    return ((UmicomU32)ReadNetwork16(p) << 16U) | (UmicomU32)ReadNetwork16(p + 2U);
}

UmicomNetworkPacketStatus UmicomKernelEthernetIpv4TcpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4TcpView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV4_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv4TcpView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;

    /* IPv4 owns capture/IP lengths, fragmentation and its header checksum.
     * Preflight above covers the larger transport output before any read. */
    UmicomEthernetIpv4View ip;
    const UmicomNetworkPacketStatus status =
        UmicomKernelEthernetIpv4Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.protocol != 6U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 20U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *tcp = (const UmicomU8 *)bytes + ip.payloadOffset;
    const UmicomSize headerBytes = (UmicomSize)(tcp[12] >> 4U) * 4U;
    if (headerBytes < 20U) return UMICOM_NETWORK_PACKET_HEADER;
    if (headerBytes > ip.payloadBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    if (headerBytes != 20U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;

    /* TCP has no checksum-absent policy. The pseudo-header binds its full
     * segment length, protocol and IPv4 addresses to the captured data. It is
     * conceptual input, never a prefix read before the caller's buffer. */
    UmicomU32 sum = 6U + (UmicomU32)ip.payloadBytes;
    for (UmicomSize i = 0U; i < 4U; i += 2U) {
        sum += ReadNetwork16(ip.sourceAddress + i);
        sum += ReadNetwork16(ip.destinationAddress + i);
    }
    for (UmicomSize i = 0U; i + 1U < ip.payloadBytes; i += 2U)
        sum += ReadNetwork16(tcp + i);
    if (ip.payloadBytes & 1U) sum += (UmicomU32)tcp[ip.payloadBytes - 1U] << 8U;
    /* At most 746 words are admitted. Final carry folding fits the 32-bit
     * accumulator; odd-byte zero padding never reads a captured pad byte. */
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;

    UmicomEthernetIpv4TcpView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv4, &ip, sizeof ip);
    staged.sourcePort = ReadNetwork16(tcp);
    staged.destinationPort = ReadNetwork16(tcp + 2U);
    staged.sequenceNumber = ReadNetwork32(tcp + 4U);
    staged.acknowledgmentNumber = ReadNetwork32(tcp + 8U);
    staged.window = ReadNetwork16(tcp + 14U);
    staged.urgentPointer = ReadNetwork16(tcp + 18U);
    staged.segmentBytes = (UmicomU16)ip.payloadBytes;
    staged.flags = tcp[13];
    staged.reservedBits = tcp[12] & 15U;
    staged.payloadOffset = ip.payloadOffset + headerBytes;
    staged.payloadBytes = ip.payloadBytes - headerBytes;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}

/* ARP shares the byte-order and freestanding RAM helpers above, but is not
 * an IPv4 datagram. Keep address observation separate from neighbor ownership
 * so a future NIC/ARP service cannot mistake syntactic admission for trust. */
UmicomNetworkPacketStatus UmicomKernelEthernetArpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetArpView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_ARP_CAPTURE_MIN_BYTES) return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_ARP_CAPTURE_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetArpView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    const UmicomU8 *frame = (const UmicomU8 *)bytes;
    if (ReadNetwork16(frame + 12U) != 0x0806U ||
        ReadNetwork16(frame + 14U) != 1U || ReadNetwork16(frame + 16U) != 0x0800U)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (frame[18] != 6U || frame[19] != 4U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    const UmicomU16 operation = ReadNetwork16(frame + 20U);
    if (operation != UMICOM_ARP_REQUEST && operation != UMICOM_ARP_REPLY)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    UmicomEthernetArpView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(staged.destinationMac, frame, 6U);
    UmicomBootMemoryCopy(staged.sourceMac, frame + 6U, 6U);
    UmicomBootMemoryCopy(staged.senderHardwareAddress, frame + 22U, 6U);
    UmicomBootMemoryCopy(staged.senderProtocolAddress, frame + 28U, 4U);
    UmicomBootMemoryCopy(staged.targetHardwareAddress, frame + 32U, 6U);
    UmicomBootMemoryCopy(staged.targetProtocolAddress, frame + 38U, 4U);
    staged.operation = operation;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}

/* Keep IPv6 capture admission beside the existing freestanding packet readers.
 * Stage copied metadata only after all framing and supported-profile checks,
 * so a caller can retain its last observation when an input is refused. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6Read(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6View *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES) return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV6_CAPTURE_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv6View) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    const UmicomU8 *frame = (const UmicomU8 *)bytes, *ip = frame + 14U;
    if (ReadNetwork16(frame + 12U) != 0x86ddU) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if ((ip[0] >> 4U) != 6U) return UMICOM_NETWORK_PACKET_HEADER;
    const UmicomU16 payload = ReadNetwork16(ip + 4U);
    if (payload > UMICOM_IPV6_CAPTURE_MAX_PAYLOAD_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    if ((UmicomSize)payload > accessibleBytes - UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES)
        return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 next = ip[6];
    if (next == 44U) return UMICOM_NETWORK_PACKET_FRAGMENT;
    if (next != 6U && next != 17U && next != 58U && next != 59U)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (next == 59U && payload != 0U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    UmicomEthernetIpv6View staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(staged.destinationMac, frame, 6U);
    UmicomBootMemoryCopy(staged.sourceMac, frame + 6U, 6U);
    UmicomBootMemoryCopy(staged.sourceAddress, ip + 8U, 16U);
    UmicomBootMemoryCopy(staged.destinationAddress, ip + 24U, 16U);
    staged.trafficClass = (UmicomU8)(((ip[0] & 15U) << 4U) | (ip[1] >> 4U));
    staged.flowLabel = ((UmicomU32)(ip[1] & 15U) << 16U) | ((UmicomU32)ip[2] << 8U) | ip[3];
    staged.ipBytes = (UmicomU16)(40U + payload);
    staged.payloadOffset = UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES;
    staged.payloadBytes = payload;
    staged.nextHeader = next;
    staged.hopLimit = ip[7];
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}


/* All admitted IPv6 transports share the same pseudo-header arithmetic.
 * Protocol-specific rules (such as UDP's refused wire zero) remain in callers.
 * With no extensions and at most 1460 payload bytes, the high length word is
 * zero and at most 748 nonzero words fit safely in this 32-bit accumulator. */
static int Ipv6TransportChecksumMatches(const UmicomEthernetIpv6View *ip,
    const UmicomU8 *payload, UmicomU8 protocol)
{
    UmicomU32 sum = (UmicomU32)protocol + (UmicomU32)ip->payloadBytes;
    for (UmicomSize i = 0U; i < 16U; i += 2U) {
        sum += ReadNetwork16(ip->sourceAddress + i);
        sum += ReadNetwork16(ip->destinationAddress + i);
    }
    for (UmicomSize i = 0U; i + 1U < ip->payloadBytes; i += 2U)
        sum += ReadNetwork16(payload + i);
    if (ip->payloadBytes & 1U) sum += (UmicomU32)payload[ip->payloadBytes - 1U] << 8U;
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    return sum == 0xffffU;
}

/* UDP uses IPv6's existing capture admission; a transport consumer must not
 * mistake a base-header observation for a verified transport datagram. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6UdpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6UdpView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES) return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV6_CAPTURE_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv6UdpView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    UmicomEthernetIpv6View ip;
    const UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.nextHeader != 17U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 8U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *udp = (const UmicomU8 *)bytes + ip.payloadOffset;
    const UmicomU16 length = ReadNetwork16(udp + 4U);
    if (length < 8U) return UMICOM_NETWORK_PACKET_HEADER;
    if (length != ip.payloadBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    if (ReadNetwork16(udp + 6U) == 0U) return UMICOM_NETWORK_PACKET_CHECKSUM;
    /* No extension headers were admitted, so the upper-layer length is the
     * complete IPv6 payload. Its high 16 bits and the three reserved bytes are
     * zero. At most 748 nonzero words are summed: well within a 32-bit value. */
/* Retain the original UDP checksum loop for review. The shared IPv6 checksum primitive preserves its exact byte scope; the preceding zero-field refusal remains UDP-specific. The former implementation is retained for owner review. */
#if 0
    UmicomU32 sum = 17U + (UmicomU32)length;
    for (UmicomSize i = 0U; i < 16U; i += 2U) {
        sum += ReadNetwork16(ip.sourceAddress + i);
        sum += ReadNetwork16(ip.destinationAddress + i);
    }
    for (UmicomSize i = 0U; i + 1U < length; i += 2U) sum += ReadNetwork16(udp + i);
    if (length & 1U) sum += (UmicomU32)udp[length - 1U] << 8U;
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;
#endif
    if (!Ipv6TransportChecksumMatches(&ip, udp, 17U)) return UMICOM_NETWORK_PACKET_CHECKSUM;
    UmicomEthernetIpv6UdpView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv6, &ip, sizeof ip);
    staged.sourcePort = ReadNetwork16(udp);
    staged.destinationPort = ReadNetwork16(udp + 2U);
    staged.datagramBytes = length;
    staged.payloadOffset = (UmicomSize)ip.payloadOffset + 8U;
    staged.payloadBytes = (UmicomSize)length - 8U;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}

/* ICMPv6 differs from IPv4 echo: its checksum includes an IPv6 pseudo-header.
 * Keep this distinction explicit rather than reusing the IPv4 checksum scope
 * or UDP's special zero-checksum rejection. Framing stays in the IPv6 reader. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6IcmpEchoRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6IcmpEchoView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES) return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV6_CAPTURE_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv6IcmpEchoView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    UmicomEthernetIpv6View ip;
    const UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.nextHeader != 58U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 8U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *icmp = (const UmicomU8 *)bytes + ip.payloadOffset;
    if (icmp[0] != UMICOM_ICMPV6_ECHO_REQUEST && icmp[0] != UMICOM_ICMPV6_ECHO_REPLY)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (icmp[1] != 0U) return UMICOM_NETWORK_PACKET_HEADER;
/* Retain the original ICMPv6 checksum loop for review. The shared primitive preserves address, message and protocol scope, including correctly computed zero fields. The former implementation is retained for owner review. */
#if 0
    UmicomU32 sum = 58U + (UmicomU32)ip.payloadBytes;
    for (UmicomSize i = 0U; i < 16U; i += 2U) {
        sum += ReadNetwork16(ip.sourceAddress + i);
        sum += ReadNetwork16(ip.destinationAddress + i);
    }
    for (UmicomSize i = 0U; i + 1U < ip.payloadBytes; i += 2U) sum += ReadNetwork16(icmp + i);
    if (ip.payloadBytes & 1U) sum += (UmicomU32)icmp[ip.payloadBytes - 1U] << 8U;
    /* Bounded capture length keeps the accumulator below 2^32. The high half
     * of the conceptual 32-bit upper-layer length is zero for this profile. */
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    if (sum != 0xffffU) return UMICOM_NETWORK_PACKET_CHECKSUM;
#endif
    if (!Ipv6TransportChecksumMatches(&ip, icmp, 58U)) return UMICOM_NETWORK_PACKET_CHECKSUM;
    UmicomEthernetIpv6IcmpEchoView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv6, &ip, sizeof ip);
    staged.messageType = icmp[0];
    staged.identifier = ReadNetwork16(icmp + 4U);
    staged.sequenceNumber = ReadNetwork16(icmp + 6U);
    staged.messageBytes = (UmicomU16)ip.payloadBytes;
    staged.payloadOffset = ip.payloadOffset + 8U;
    staged.payloadBytes = ip.payloadBytes - 8U;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}

/* Fixed-header TCP observations compose the same IPv6 admission and checksum
 * services as UDP and ICMPv6; connection state remains a separate future owner. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6TcpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6TcpView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV6_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv6TcpView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;

    /* IPv6 owns capture/IP lengths and extension/fragment policy.
     * Preflight above covers the larger transport output before any read. */
    UmicomEthernetIpv6View ip;
    const UmicomNetworkPacketStatus status =
        UmicomKernelEthernetIpv6Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.nextHeader != 6U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 20U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *tcp = (const UmicomU8 *)bytes + ip.payloadOffset;
    const UmicomSize headerBytes = (UmicomSize)(tcp[12] >> 4U) * 4U;
    if (headerBytes < 20U) return UMICOM_NETWORK_PACKET_HEADER;
    if (headerBytes > ip.payloadBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    if (headerBytes != 20U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;

    /* TCP has no absent-checksum field; all segment bytes and the full IPv6
     * pseudo-header must verify, including when the transmitted value is zero. */
    if (!Ipv6TransportChecksumMatches(&ip, tcp, 6U)) return UMICOM_NETWORK_PACKET_CHECKSUM;

    UmicomEthernetIpv6TcpView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv6, &ip, sizeof ip);
    staged.sourcePort = ReadNetwork16(tcp);
    staged.destinationPort = ReadNetwork16(tcp + 2U);
    staged.sequenceNumber = ReadNetwork32(tcp + 4U);
    staged.acknowledgmentNumber = ReadNetwork32(tcp + 8U);
    staged.window = ReadNetwork16(tcp + 14U);
    staged.urgentPointer = ReadNetwork16(tcp + 18U);
    staged.segmentBytes = (UmicomU16)ip.payloadBytes;
    staged.flags = tcp[13];
    staged.reservedBits = tcp[12] & 15U;
    staged.payloadOffset = ip.payloadOffset + headerBytes;
    staged.payloadBytes = ip.payloadBytes - headerBytes;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}


/* Neighbor captures reuse the IPv6 frame and checksum owners. The copied result
 * records what was admitted; a later neighbor-cache state machine must still
 * apply interface ownership, lifetime and reachability policy. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6NeighborRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6NeighborView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES) return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_IPV6_CAPTURE_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetIpv6NeighborView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    UmicomEthernetIpv6View ip;
    const UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6Read(bytes, accessibleBytes, &ip);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    if (ip.nextHeader != 58U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (ip.payloadBytes < 24U) return UMICOM_NETWORK_PACKET_LENGTH;
    const UmicomU8 *icmp = (const UmicomU8 *)bytes + ip.payloadOffset;
    if (icmp[0] != UMICOM_ICMPV6_NEIGHBOR_SOLICITATION &&
        icmp[0] != UMICOM_ICMPV6_NEIGHBOR_ADVERTISEMENT) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (icmp[1] != 0U || ip.hopLimit != 255U) return UMICOM_NETWORK_PACKET_HEADER;
    if (!Ipv6TransportChecksumMatches(&ip, icmp, 58U)) return UMICOM_NETWORK_PACKET_CHECKSUM;
    if (icmp[8] == 0xffU) return UMICOM_NETWORK_PACKET_HEADER;
    UmicomEthernetIpv6NeighborView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomBootMemoryCopy(&staged.ipv6, &ip, sizeof ip);
    UmicomBootMemoryCopy(staged.targetAddress, icmp + 8U, sizeof staged.targetAddress);
    staged.messageType = icmp[0];
    staged.messageBytes = ip.payloadBytes;
    const UmicomBoolean advertisement = icmp[0] == UMICOM_ICMPV6_NEIGHBOR_ADVERTISEMENT;
    if (advertisement) {
        staged.router = (icmp[4] & 0x80U) ? UMICOM_TRUE : UMICOM_FALSE;
        staged.solicited = (icmp[4] & 0x40U) ? UMICOM_TRUE : UMICOM_FALSE;
        staged.override = (icmp[4] & 0x20U) ? UMICOM_TRUE : UMICOM_FALSE;
        if (staged.solicited && ip.destinationAddress[0] == 0xffU) return UMICOM_NETWORK_PACKET_HEADER;
    }
    const UmicomU8 relevant = advertisement ? 2U : 1U;
    for (UmicomSize at = 24U; at < ip.payloadBytes;) {
        const UmicomSize remaining = (UmicomSize)ip.payloadBytes - at;
        if (remaining < 2U) return UMICOM_NETWORK_PACKET_LENGTH;
        const UmicomSize optionBytes = (UmicomSize)icmp[at + 1U] * 8U;
        if (!optionBytes) return UMICOM_NETWORK_PACKET_HEADER;
        if (optionBytes > remaining) return UMICOM_NETWORK_PACKET_LENGTH;
        ++staged.optionCount;
        if (icmp[at] == relevant) {
            if (optionBytes != 8U || staged.hasLinkLayerAddress) return UMICOM_NETWORK_PACKET_HEADER;
            UmicomBootMemoryCopy(staged.linkLayerAddress, icmp + at + 2U, sizeof staged.linkLayerAddress);
            staged.hasLinkLayerAddress = UMICOM_TRUE;
        } else ++staged.ignoredOptionCount;
        at += optionBytes;
    }
    if (!advertisement) {
        UmicomU8 sourceBits = 0U;
        for (UmicomSize i = 0U; i < 16U; ++i) sourceBits |= ip.sourceAddress[i];
        if (!sourceBits) {
            /* The solicited-node prefix is ff02::1:ff00:0/104. The target's
             * assignment to an interface and DAD progress are separate state. */
            static const UmicomU8 prefix[13] = {0xffU,2U,0U,0U,0U,0U,0U,0U,0U,0U,0U,1U,0xffU};
            if (staged.hasLinkLayerAddress) return UMICOM_NETWORK_PACKET_HEADER;
            for (UmicomSize i = 0U; i < sizeof prefix; ++i)
                if (ip.destinationAddress[i] != prefix[i]) return UMICOM_NETWORK_PACKET_HEADER;
        }
    }
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}


/* Framing has its own observation boundary: do not change the established
 * untagged transport readers or pretend that tag parsing adds routing policy.
 * Every read is byte-based and checked against the original capture extent. */
UmicomNetworkPacketStatus UmicomKernelEthernetFrameRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetFrameView *out)
{
    if (!bytes || !out) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (accessibleBytes < UMICOM_ETHERNET_FRAME_HEADER_BYTES)
        return UMICOM_NETWORK_PACKET_LENGTH;
    if (accessibleBytes > UMICOM_ETHERNET_FRAME_CAPTURE_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start = (UmicomAddress)bytes;
    const UmicomAddress output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomEthernetFrameView) ||
        !UmicomKernelAddressAddChecked(start, accessibleBytes, &end) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (start < outputEnd && output < end))
        return UMICOM_NETWORK_PACKET_ARGUMENT;

    const UmicomU8 *frame = bytes;
    UmicomEthernetFrameView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    UmicomSize offset = UMICOM_ETHERNET_FRAME_HEADER_BYTES;
    UmicomU16 type = ReadNetwork16(frame + 12U);
    while (type == 0x8100U || type == 0x88a8U) {
        if (staged.tagCount == UMICOM_ETHERNET_FRAME_TAG_MAX_COUNT)
            return UMICOM_NETWORK_PACKET_UNSUPPORTED;
        if (accessibleBytes - offset < 4U)
            return UMICOM_NETWORK_PACKET_LENGTH;
        UmicomEthernetTagView *tag = &staged.tags[staged.tagCount];
        tag->protocolIdentifier = type;
        tag->controlInformation = ReadNetwork16(frame + offset);
        tag->priority = (UmicomU8)(tag->controlInformation >> 13U);
        tag->dropEligible = (tag->controlInformation & 0x1000U) ?
            UMICOM_TRUE : UMICOM_FALSE;
        tag->vlanIdentifier = (UmicomU16)(tag->controlInformation & 0x0fffU);
        type = ReadNetwork16(frame + offset + 2U);
        offset += 4U;
        ++staged.tagCount;
    }
    if (type <= 1500U) return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    if (type < 1536U) return UMICOM_NETWORK_PACKET_HEADER;
    if (accessibleBytes - offset > UMICOM_ETHERNET_FRAME_PAYLOAD_MAX_BYTES)
        return UMICOM_NETWORK_PACKET_LIMIT;

    /* Publish one complete value only after all extents and profile limits
     * pass. The input is never rewritten or stripped of its captured tags. */
    UmicomBootMemoryCopy(staged.destinationMac, frame, 6U);
    UmicomBootMemoryCopy(staged.sourceMac, frame + 6U, 6U);
    staged.etherType = type;
    staged.payloadOffset = offset;
    staged.payloadBytes = accessibleBytes - offset;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}


/* Framing owns tag traversal; IPv4 owns its own header and payload lengths.
 * Publish both observations together only after every shared admission passes. */
UmicomNetworkPacketStatus UmicomKernelEthernetTaggedIpv4Read(
    const void *bytes,UmicomSize accessibleBytes,UmicomEthernetTaggedIpv4View *out)
{
    if(!bytes || !out)return UMICOM_NETWORK_PACKET_ARGUMENT;
    if(accessibleBytes<UMICOM_ETHERNET_FRAME_HEADER_BYTES)return UMICOM_NETWORK_PACKET_LENGTH;
    if(accessibleBytes>UMICOM_ETHERNET_FRAME_CAPTURE_MAX_BYTES)return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomAddress start=(UmicomAddress)bytes,output=(UmicomAddress)out;
    UmicomAddress end=0U,outputEnd=0U;
    if(output%_Alignof(UmicomEthernetTaggedIpv4View) ||
       !UmicomKernelAddressAddChecked(start,accessibleBytes,&end) ||
       !UmicomKernelAddressAddChecked(output,sizeof *out,&outputEnd) ||
       (start<outputEnd && output<end))return UMICOM_NETWORK_PACKET_ARGUMENT;
    UmicomEthernetTaggedIpv4View staged;UmicomBootMemoryClear(&staged,sizeof staged);
    UmicomNetworkPacketStatus status=UmicomKernelEthernetFrameRead(bytes,accessibleBytes,&staged.frame);
    if(status!=UMICOM_NETWORK_PACKET_OK)return status;
    if(staged.frame.etherType!=0x0800U)return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    status=PacketIpv4PayloadRead(bytes,accessibleBytes,staged.frame.payloadOffset,&staged.ipv4);
    if(status!=UMICOM_NETWORK_PACKET_OK)return status;
    UmicomBootMemoryCopy(out,&staged,sizeof staged);return UMICOM_NETWORK_PACKET_OK;
}

/* Encoding uses the same byte-order primitive as admission. Sum a bounded
 * sequence of network words; an odd final byte occupies the high half-word.
 * The largest UDP profile is small enough that the U32 sum cannot overflow. */
static UmicomU32 PacketWordSum(const UmicomU8 *bytes, UmicomSize length, UmicomU32 sum)
{
    UmicomSize i = 0U;
    for (; length - i >= 2U; i += 2U) sum += ReadNetwork16(bytes + i);
    if (i < length) sum += (UmicomU32)bytes[i] << 8U;
    return sum;
}
static UmicomU16 PacketChecksum(UmicomU32 sum)
{
    while (sum >> 16U) sum = (sum & 0xffffU) + (sum >> 16U);
    return (UmicomU16)(~sum & 0xffffU);
}
static void PacketPut16(UmicomU8 *bytes, UmicomU16 value)
{
    bytes[0] = (UmicomU8)(value >> 8U);
    bytes[1] = (UmicomU8)(value & 0xffU);
}

/* Shared span admission and Ethernet/IPv4 header composition replace the UDP-local copies so ICMP echo can reuse the same memory and framing boundary. UDP wire behavior is retained and the original writer remains for review. The former implementation is retained for owner review. */
#if 0
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4UdpWrite(
    const UmicomEthernetIpv4UdpSpec *spec, const void *payload,
    UmicomSize payloadBytes, void *destination, UmicomSize capacity,
    UmicomSize *written)
{
    if (!spec || !destination || !written || (!payload && payloadBytes))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (payloadBytes > 1472U) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomSize resultBytes = 42U + payloadBytes;
    if (capacity < resultBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    /* Check every complete span before dereferencing typed objects. A caller
     * cannot use spare destination capacity to overwrite input or the result
     * counter. The existing address owner checks arithmetic wraparound. */
    const UmicomAddress starts[4] = {(UmicomAddress)spec, (UmicomAddress)destination,
        (UmicomAddress)written, (UmicomAddress)payload};
    const UmicomSize lengths[4] = {sizeof *spec, capacity, sizeof *written, payloadBytes};
    UmicomAddress ends[4];
    if (starts[0] % _Alignof(UmicomEthernetIpv4UdpSpec) || starts[2] % _Alignof(UmicomSize))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    for (UmicomSize i = 0U; i < 4U; ++i) {
        if (!UmicomKernelAddressAddChecked(starts[i], lengths[i], &ends[i]))
            return UMICOM_NETWORK_PACKET_ARGUMENT;
        for (UmicomSize j = 0U; j < i; ++j)
            if (lengths[i] && lengths[j] && starts[i] < ends[j] && starts[j] < ends[i])
                return UMICOM_NETWORK_PACKET_ARGUMENT;
    }
    if (spec->timeToLive == 0U) return UMICOM_NETWORK_PACKET_HEADER;
    UmicomU8 header[42];
    UmicomBootMemoryClear(header, sizeof header);
    UmicomBootMemoryCopy(header, spec->destinationMac, 6U);
    UmicomBootMemoryCopy(header + 6U, spec->sourceMac, 6U);
    PacketPut16(header + 12U, 0x0800U);
    header[14] = 0x45U;
    header[15] = spec->differentiatedServices;
    PacketPut16(header + 16U, (UmicomU16)(28U + payloadBytes));
    PacketPut16(header + 18U, spec->identification);
    PacketPut16(header + 20U, 0x4000U);
    header[22] = spec->timeToLive;
    header[23] = 17U;
    UmicomBootMemoryCopy(header + 26U, spec->sourceAddress, 4U);
    UmicomBootMemoryCopy(header + 30U, spec->destinationAddress, 4U);
    PacketPut16(header + 24U, PacketChecksum(PacketWordSum(header + 14U, 20U, 0U)));
    PacketPut16(header + 34U, spec->sourcePort);
    PacketPut16(header + 36U, spec->destinationPort);
    const UmicomU16 udpBytes = (UmicomU16)(8U + payloadBytes);
    PacketPut16(header + 38U, udpBytes);
    /* Pseudo-header inputs are checksum material, not bytes added to UDP. */
    UmicomU32 sum = PacketWordSum(header + 26U, 8U, 17U + (UmicomU32)udpBytes);
    sum = PacketWordSum(header + 34U, 8U, sum);
    sum = PacketWordSum((const UmicomU8 *)payload, payloadBytes, sum);
    UmicomU16 checksum = PacketChecksum(sum);
    PacketPut16(header + 40U, checksum ? checksum : (UmicomU16)0xffffU);
    /* All admission and computation precedes publication. These ordinary RAM
     * copies do not fail under the accessible/stable buffer contract. */
    UmicomBootMemoryCopy(destination, header, sizeof header);
    if (payloadBytes) UmicomBootMemoryCopy((UmicomU8 *)destination + 42U, payload, payloadBytes);
    *written = resultBytes;
    return UMICOM_NETWORK_PACKET_OK;
}

#endif
/* The two fixed-profile writers share complete-span admission before any
 * typed dereference. Keep protocol-specific checksum rules in their writers;
 * sharing memory checks must not make ICMP inherit UDP's zero convention. */
static int PacketWriteSpans(const void *spec, UmicomSize specBytes,
    UmicomSize specAlignment, const void *payload, UmicomSize payloadBytes,
    void *destination, UmicomSize capacity, UmicomSize *written)
{
    const UmicomAddress starts[4] = {(UmicomAddress)spec, (UmicomAddress)destination,
        (UmicomAddress)written, (UmicomAddress)payload};
    const UmicomSize lengths[4] = {specBytes, capacity, sizeof *written, payloadBytes};
    UmicomAddress ends[4];
    if (starts[0] % specAlignment || starts[2] % _Alignof(UmicomSize)) return 0;
    for (UmicomSize i = 0U; i < 4U; ++i) {
        if (!UmicomKernelAddressAddChecked(starts[i], lengths[i], &ends[i])) return 0;
        for (UmicomSize j = 0U; j < i; ++j)
            if (lengths[i] && lengths[j] && starts[i] < ends[j] && starts[j] < ends[i]) return 0;
    }
    return 1;
}

/* Only common Ethernet/IPv4 bytes are authored here. Neither struct layout
 * nor a transport's checksum policy is reused as another protocol's format. */
/* Generalize only the shared IPv4 envelope length so TCP and the existing eight-byte transports use one header/checksum owner. The compatibility helper retains UDP and ICMP behavior. The former implementation is retained for owner review. */
#if 0
static void PacketIpv4Header(UmicomU8 header[42], const UmicomU8 destinationMac[6],
    const UmicomU8 sourceMac[6], const UmicomU8 sourceAddress[4],
    const UmicomU8 destinationAddress[4], UmicomU16 identification,
    UmicomU8 differentiatedServices, UmicomU8 timeToLive,
    UmicomU8 protocol, UmicomSize payloadBytes)
{
    UmicomBootMemoryClear(header, 42U);
    UmicomBootMemoryCopy(header, destinationMac, 6U);
    UmicomBootMemoryCopy(header + 6U, sourceMac, 6U);
    PacketPut16(header + 12U, 0x0800U);
    header[14] = 0x45U;
    header[15] = differentiatedServices;
    PacketPut16(header + 16U, (UmicomU16)(28U + payloadBytes));
    PacketPut16(header + 18U, identification);
    PacketPut16(header + 20U, 0x4000U);
    header[22] = timeToLive;
    header[23] = protocol;
    UmicomBootMemoryCopy(header + 26U, sourceAddress, 4U);
    UmicomBootMemoryCopy(header + 30U, destinationAddress, 4U);
    PacketPut16(header + 24U, PacketChecksum(PacketWordSum(header + 14U, 20U, 0U)));
}

#endif
static void PacketIpv4Envelope(UmicomU8 header[34], const UmicomU8 destinationMac[6],
    const UmicomU8 sourceMac[6], const UmicomU8 sourceAddress[4],
    const UmicomU8 destinationAddress[4], UmicomU16 identification,
    UmicomU8 differentiatedServices, UmicomU8 timeToLive,
    UmicomU8 protocol, UmicomSize transportBytes)
{
    UmicomBootMemoryClear(header, 34U);
    UmicomBootMemoryCopy(header, destinationMac, 6U);
    UmicomBootMemoryCopy(header + 6U, sourceMac, 6U);
    PacketPut16(header + 12U, 0x0800U);
    header[14] = 0x45U;
    header[15] = differentiatedServices;
    PacketPut16(header + 16U, (UmicomU16)(20U + transportBytes));
    PacketPut16(header + 18U, identification);
    PacketPut16(header + 20U, 0x4000U);
    header[22] = timeToLive;
    header[23] = protocol;
    UmicomBootMemoryCopy(header + 26U, sourceAddress, 4U);
    UmicomBootMemoryCopy(header + 30U, destinationAddress, 4U);
    PacketPut16(header + 24U, PacketChecksum(PacketWordSum(header + 14U, 20U, 0U)));
}

/* Preserve the established eight-byte transport profile for UDP/ICMP callers.
 * TCP passes its full segment length to the same envelope owner instead of
 * treating its longer header as payload or duplicating IPv4 checksum logic. */
static void PacketIpv4Header(UmicomU8 header[42], const UmicomU8 destinationMac[6],
    const UmicomU8 sourceMac[6], const UmicomU8 sourceAddress[4],
    const UmicomU8 destinationAddress[4], UmicomU16 identification,
    UmicomU8 differentiatedServices, UmicomU8 timeToLive,
    UmicomU8 protocol, UmicomSize payloadBytes)
{
    UmicomBootMemoryClear(header, 42U);
    PacketIpv4Envelope(header, destinationMac, sourceMac, sourceAddress,
        destinationAddress, identification, differentiatedServices, timeToLive,
        protocol, 8U + payloadBytes);
}

UmicomNetworkPacketStatus UmicomKernelEthernetIpv4UdpWrite(
    const UmicomEthernetIpv4UdpSpec *spec, const void *payload,
    UmicomSize payloadBytes, void *destination, UmicomSize capacity,
    UmicomSize *written)
{
    if (!spec || !destination || !written || (!payload && payloadBytes))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (payloadBytes > 1472U) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomSize resultBytes = 42U + payloadBytes;
    if (capacity < resultBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    if (!PacketWriteSpans(spec, sizeof *spec, _Alignof(UmicomEthernetIpv4UdpSpec),
        payload, payloadBytes, destination, capacity, written))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (spec->timeToLive == 0U) return UMICOM_NETWORK_PACKET_HEADER;
    UmicomU8 header[42];
    PacketIpv4Header(header, spec->destinationMac, spec->sourceMac,
        spec->sourceAddress, spec->destinationAddress, spec->identification,
        spec->differentiatedServices, spec->timeToLive, 17U, payloadBytes);
    PacketPut16(header + 34U, spec->sourcePort);
    PacketPut16(header + 36U, spec->destinationPort);
    const UmicomU16 udpBytes = (UmicomU16)(8U + payloadBytes);
    PacketPut16(header + 38U, udpBytes);
    /* Pseudo-header inputs are checksum material, not bytes added to UDP. */
    UmicomU32 sum = PacketWordSum(header + 26U, 8U, 17U + (UmicomU32)udpBytes);
    sum = PacketWordSum(header + 34U, 8U, sum);
    sum = PacketWordSum((const UmicomU8 *)payload, payloadBytes, sum);
    UmicomU16 checksum = PacketChecksum(sum);
    PacketPut16(header + 40U, checksum ? checksum : (UmicomU16)0xffffU);
    /* All admission and computation precedes publication. These ordinary RAM
     * copies do not fail under the accessible/stable buffer contract. */
    UmicomBootMemoryCopy(destination, header, sizeof header);
    if (payloadBytes) UmicomBootMemoryCopy((UmicomU8 *)destination + 42U, payload, payloadBytes);
    *written = resultBytes;
    return UMICOM_NETWORK_PACKET_OK;
}

UmicomNetworkPacketStatus UmicomKernelEthernetIpv4IcmpEchoWrite(
    const UmicomEthernetIpv4IcmpEchoSpec *spec, const void *payload,
    UmicomSize payloadBytes, void *destination, UmicomSize capacity,
    UmicomSize *written)
{
    if (!spec || !destination || !written || (!payload && payloadBytes))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (payloadBytes > 1472U) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomSize resultBytes = 42U + payloadBytes;
    if (capacity < resultBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    if (!PacketWriteSpans(spec, sizeof *spec, _Alignof(UmicomEthernetIpv4IcmpEchoSpec),
        payload, payloadBytes, destination, capacity, written))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (spec->timeToLive == 0U) return UMICOM_NETWORK_PACKET_HEADER;
    if (spec->messageType != UMICOM_ICMP_ECHO_REQUEST &&
        spec->messageType != UMICOM_ICMP_ECHO_REPLY)
        return UMICOM_NETWORK_PACKET_UNSUPPORTED;
    UmicomU8 header[42];
    PacketIpv4Header(header, spec->destinationMac, spec->sourceMac,
        spec->sourceAddress, spec->destinationAddress, spec->identification,
        spec->differentiatedServices, spec->timeToLive, 1U, payloadBytes);
    header[34] = spec->messageType;
    PacketPut16(header + 38U, spec->echoIdentifier);
    PacketPut16(header + 40U, spec->sequenceNumber);
    UmicomU32 sum = PacketWordSum(header + 34U, 8U, 0U);
    sum = PacketWordSum((const UmicomU8 *)payload, payloadBytes, sum);
    PacketPut16(header + 36U, PacketChecksum(sum));
    /* Publication follows every refusal path. The checksum covers ICMP only:
     * there is no IPv4 UDP-style pseudo-header and no absent-checksum marker. */
    UmicomBootMemoryCopy(destination, header, sizeof header);
    if (payloadBytes) UmicomBootMemoryCopy((UmicomU8 *)destination + 42U, payload, payloadBytes);
    *written = resultBytes;
    return UMICOM_NETWORK_PACKET_OK;
}

/* Byte-order output is explicit; C structure padding never becomes wire data. */
static void PacketPut32(UmicomU8 *bytes, UmicomU32 value)
{
    PacketPut16(bytes, (UmicomU16)(value >> 16U));
    PacketPut16(bytes + 2U, (UmicomU16)(value & 0xffffU));
}
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4TcpWrite(
    const UmicomEthernetIpv4TcpSpec *spec, const void *payload,
    UmicomSize payloadBytes, void *destination, UmicomSize capacity,
    UmicomSize *written)
{
    if (!spec || !destination || !written || (!payload && payloadBytes))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (payloadBytes > 1460U) return UMICOM_NETWORK_PACKET_LIMIT;
    const UmicomSize resultBytes = UMICOM_ETHERNET_IPV4_TCP_PAYLOAD_OFFSET + payloadBytes;
    if (capacity < resultBytes) return UMICOM_NETWORK_PACKET_LENGTH;
    if (!PacketWriteSpans(spec, sizeof *spec, _Alignof(UmicomEthernetIpv4TcpSpec),
        payload, payloadBytes, destination, capacity, written))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (spec->timeToLive == 0U) return UMICOM_NETWORK_PACKET_HEADER;
    UmicomU8 header[UMICOM_ETHERNET_IPV4_TCP_PAYLOAD_OFFSET];
    UmicomBootMemoryClear(header, sizeof header);
    const UmicomU16 segmentBytes = (UmicomU16)(20U + payloadBytes);
    PacketIpv4Envelope(header, spec->destinationMac, spec->sourceMac,
        spec->sourceAddress, spec->destinationAddress, spec->identification,
        spec->differentiatedServices, spec->timeToLive, 6U, segmentBytes);
    PacketPut16(header + 34U, spec->sourcePort);
    PacketPut16(header + 36U, spec->destinationPort);
    PacketPut32(header + 38U, spec->sequenceNumber);
    PacketPut32(header + 42U, spec->acknowledgmentNumber);
    header[46] = 0x50U;
    header[47] = spec->flags;
    PacketPut16(header + 48U, spec->window);
    PacketPut16(header + 52U, spec->urgentPointer);
    /* Even-sized pseudo/header spans allow the payload's odd final byte to
     * remain the high half of its final word. Unlike UDP, zero is transmitted
     * literally: TCP always validates the complete mandatory checksum sum. */
    UmicomU32 sum = PacketWordSum(header + 26U, 8U, 6U + (UmicomU32)segmentBytes);
    sum = PacketWordSum(header + 34U, 20U, sum);
    sum = PacketWordSum((const UmicomU8 *)payload, payloadBytes, sum);
    PacketPut16(header + 50U, PacketChecksum(sum));
    /* Publish only after every refusal and computation. The caller owns stable
     * accessible RAM during these infallible copies; no connection is mutated. */
    UmicomBootMemoryCopy(destination, header, sizeof header);
    if (payloadBytes) UmicomBootMemoryCopy((UmicomU8 *)destination + sizeof header, payload, payloadBytes);
    *written = resultBytes;
    return UMICOM_NETWORK_PACKET_OK;
}

UmicomNetworkPacketStatus UmicomKernelTcpOptionsRead(const void *bytes,
    UmicomSize areaBytes, UmicomTcpOptionsView *out)
{
    if (!out || (!bytes && areaBytes)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (areaBytes > UMICOM_TCP_OPTIONS_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    if (areaBytes % 4U) return UMICOM_NETWORK_PACKET_HEADER;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomTcpOptionsView) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (areaBytes && (!UmicomKernelAddressAddChecked(start, areaBytes, &end) ||
            (start < outputEnd && output < end))))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    UmicomTcpOptionsView staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    const UmicomU8 *options = (const UmicomU8 *)bytes;
    UmicomSize at = 0U;
    /* Every accepted item consumes at least one byte, so a forty-byte area
     * cannot exceed the fixed forty-entry result. Stage all metadata before
     * publication; a malformed final option must not expose a partial list. */
    while (at < areaBytes) {
        const UmicomU8 kind = options[at];
        if (kind == 0U) {
            staged.endSeen = UMICOM_TRUE;
            staged.consumedBytes = at + 1U;
            staged.paddingBytes = areaBytes - staged.consumedBytes;
            for (UmicomSize padding = at + 1U; padding < areaBytes; ++padding)
                if (options[padding] != 0U) return UMICOM_NETWORK_PACKET_HEADER;
            UmicomBootMemoryCopy(out, &staged, sizeof staged);
            return UMICOM_NETWORK_PACKET_OK;
        }
        UmicomSize length = 1U;
        if (kind != 1U) {
            if (areaBytes - at < 2U) return UMICOM_NETWORK_PACKET_LENGTH;
            length = options[at + 1U];
            if (length < 2U) return UMICOM_NETWORK_PACKET_HEADER;
            if (length > areaBytes - at) return UMICOM_NETWORK_PACKET_LENGTH;
        }
        staged.entries[staged.count].kind = kind;
        staged.entries[staged.count].length = (UmicomU8)length;
        staged.entries[staged.count].offset = (UmicomU8)at;
        ++staged.count;
        at += length;
    }
    staged.consumedBytes = areaBytes;
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}


/* Reuse the complete structural walk, then stage the narrower known-value
 * interpretation. The caller receives neither a half-decoded record nor a
 * borrowed payload pointer if a late known option is malformed. */
UmicomNetworkPacketStatus UmicomKernelTcpOptionValuesRead(const void *bytes,
    UmicomSize areaBytes, UmicomTcpOptionValues *out)
{
    if (!out || (!bytes && areaBytes)) return UMICOM_NETWORK_PACKET_ARGUMENT;
    if (areaBytes > UMICOM_TCP_OPTIONS_MAX_BYTES) return UMICOM_NETWORK_PACKET_LIMIT;
    if (areaBytes % 4U) return UMICOM_NETWORK_PACKET_HEADER;
    const UmicomAddress start = (UmicomAddress)bytes, output = (UmicomAddress)out;
    UmicomAddress end = 0U, outputEnd = 0U;
    if (output % _Alignof(UmicomTcpOptionValues) ||
        !UmicomKernelAddressAddChecked(output, sizeof *out, &outputEnd) ||
        (areaBytes && (!UmicomKernelAddressAddChecked(start, areaBytes, &end) ||
            (start < outputEnd && output < end))))
        return UMICOM_NETWORK_PACKET_ARGUMENT;
    UmicomTcpOptionsView structure;
    const UmicomNetworkPacketStatus status =
        UmicomKernelTcpOptionsRead(bytes, areaBytes, &structure);
    if (status != UMICOM_NETWORK_PACKET_OK) return status;
    UmicomTcpOptionValues staged;
    UmicomBootMemoryClear(&staged, sizeof staged);
    const UmicomU8 *options = (const UmicomU8 *)bytes;
    for (UmicomSize i = 0U; i < structure.count; ++i) {
        const UmicomTcpOptionView *entry = &structure.entries[i];
        const UmicomU8 *item = options + entry->offset;
        switch (entry->kind) {
        case 1U:
            break;
        case 2U:
            if (entry->length != 4U || staged.hasMss)
                return UMICOM_NETWORK_PACKET_HEADER;
            staged.hasMss = UMICOM_TRUE;
            staged.mss = ReadNetwork16(item + 2U);
            break;
        case 3U:
            if (entry->length != 3U || staged.hasWindowScale)
                return UMICOM_NETWORK_PACKET_HEADER;
            staged.hasWindowScale = UMICOM_TRUE;
            staged.windowScaleRaw = item[2];
            staged.windowScaleBounded = item[2] > 14U ? 14U : item[2];
            break;
        case 4U:
            if (entry->length != 2U || staged.sackPermitted)
                return UMICOM_NETWORK_PACKET_HEADER;
            staged.sackPermitted = UMICOM_TRUE;
            break;
        case 8U:
            if (entry->length != 10U || staged.hasTimestamp)
                return UMICOM_NETWORK_PACKET_HEADER;
            staged.hasTimestamp = UMICOM_TRUE;
            staged.timestampValue = ReadNetwork32(item + 2U);
            staged.timestampEcho = ReadNetwork32(item + 6U);
            break;
        default:
            ++staged.unknownCount;
            break;
        }
    }
    UmicomBootMemoryCopy(out, &staged, sizeof staged);
    return UMICOM_NETWORK_PACKET_OK;
}
