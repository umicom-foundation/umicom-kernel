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
#include "umicom/kernel/network_udp.h"
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
