/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_ipv6.h
 *
 * PURPOSE:
 *   Describe bounded IPv6 base-header observations without extension traversal or network side effects.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_IPV6_H
#define UMICOM_KERNEL_NETWORK_IPV6_H
#include "umicom/kernel/network_packet.h"
#define UMICOM_ETHERNET_IPV6_CAPTURE_MIN_BYTES 54U
#define UMICOM_ETHERNET_IPV6_CAPTURE_MAX_BYTES 1514U
#define UMICOM_IPV6_CAPTURE_MAX_PAYLOAD_BYTES 1460U
typedef struct UmicomEthernetIpv6View {
    UmicomU8 destinationMac[6], sourceMac[6];
    UmicomU8 sourceAddress[16], destinationAddress[16];
    UmicomU32 flowLabel;
    UmicomU16 ipBytes, payloadOffset, payloadBytes;
    UmicomU8 trafficClass, nextHeader, hopLimit;
} UmicomEthernetIpv6View;
/* Read Ethernet-II type 0x86dd with a forty-byte IPv6 base header. Captures
 * exclude preamble and FCS. Payload length is bounded by the ordinary Ethernet
 * MTU and the accessible span; trailing capture padding is not IPv6 payload.
 * Direct TCP(6), UDP(17), ICMPv6(58), and empty No Next Header(59) observations
 * are supported. A fragment header returns FRAGMENT; all other next headers,
 * including extension headers, return UNSUPPORTED. Jumbograms are unsupported.
 * No transport header or checksum is verified by this base-header reader.
 *
 * Addresses, traffic class and hop limit are observations, not routing or
 * acceptance policy. An empty No Next Header payload is a deliberately narrower
 * capture profile than general IPv6. No address configuration, neighbor cache,
 * NIC, transmission, allocation or retained capture pointer is involved.
 * Unaligned input is permitted. Output must be aligned, separate from the
 * entire capture and writable. Every refusal preserves output. Checked bounds
 * cannot prove that arbitrary caller addresses are mapped accessible RAM. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6Read(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6View *out);
#endif
