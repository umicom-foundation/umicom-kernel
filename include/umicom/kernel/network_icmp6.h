/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_icmp6.h
 *
 * PURPOSE:
 *   Define bounded ICMPv6 echo observations without creating a network endpoint.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_ICMP6_H
#define UMICOM_KERNEL_NETWORK_ICMP6_H
#include "umicom/kernel/network_ipv6.h"
#define UMICOM_ICMPV6_ECHO_REQUEST 128U
#define UMICOM_ICMPV6_ECHO_REPLY 129U
#define UMICOM_ETHERNET_IPV6_ECHO_PAYLOAD_OFFSET 62U
typedef struct UmicomEthernetIpv6IcmpEchoView {
    UmicomEthernetIpv6View ipv6;
    UmicomU8 messageType;
    UmicomU16 identifier, sequenceNumber, messageBytes;
    UmicomSize payloadOffset, payloadBytes;
} UmicomEthernetIpv6IcmpEchoView;
/* Admit direct next-header 58 after the existing bounded IPv6 base header.
 * Accept echo request/reply with code zero and at least an eight-byte message.
 * Verify the entire message with the IPv6 pseudo-header (both addresses,
 * message length and protocol 58). A correctly computed zero checksum is
 * valid: ICMPv6 has no UDP-style checksum-absent field. Odd payloads use
 * conceptual zero padding; captured Ethernet padding is never checksummed.
 *
 * No allocation, socket, request/reply matching, transmit, NIC, neighbor
 * discovery, error-message handling, extensions, fragments or retained input.
 * This capture reader does not implement an IPv6 host. Address and hop values
 * are metadata, not policy. Stable RAM input may be unaligned; writable output
 * must be aligned and separate from the entire accessible capture. Refusal
 * preserves output. Bounds arithmetic does not establish mapped memory. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6IcmpEchoRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6IcmpEchoView *out);
#endif
