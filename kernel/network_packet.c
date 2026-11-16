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
#include "umicom/kernel/network_arp.h"
#include "umicom/kernel/network_ipv6.h"
#include "umicom/kernel/network_udp.h"
#include "umicom/kernel/network_udp6.h"
#include "umicom/kernel/network_icmp6.h"
#include "umicom/kernel/network_tcp6.h"
#include "umicom/kernel/network_neighbor6.h"
#include "umicom/kernel/network_icmp.h"
#include "umicom/kernel/network_tcp.h"
#include "umicom/kernel/address.h"
#include "boot_memory.h"

static UmicomU16 ReadNetwork16(const UmicomU8 *p)
{
    return (UmicomU16)(((UmicomU16)p[0] << 8U) | (UmicomU16)p[1]);
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
