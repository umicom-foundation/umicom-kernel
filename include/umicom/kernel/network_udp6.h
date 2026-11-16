/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_udp6.h
 *
 * PURPOSE:
 *   Describe bounded ordinary UDP admission over the existing IPv6 base-header reader.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_UDP6_H
#define UMICOM_KERNEL_NETWORK_UDP6_H
#include "umicom/kernel/network_ipv6.h"
#define UMICOM_ETHERNET_IPV6_UDP_PAYLOAD_OFFSET 62U
typedef struct UmicomEthernetIpv6UdpView {
    UmicomEthernetIpv6View ipv6;
    UmicomU16 sourcePort, destinationPort, datagramBytes;
    UmicomSize payloadOffset, payloadBytes;
} UmicomEthernetIpv6UdpView;
/* Compose fixed IPv6 admission with direct UDP protocol 17. The UDP length
 * includes its eight-byte header and must equal the IPv6 payload length.
 * Verify a mandatory nonzero UDP checksum, including the IPv6 pseudo-header.
 * A computed zero is encoded as 0xffff; a wire zero is refused. Specialized
 * zero-checksum tunnel profiles are not supported. Captured Ethernet padding
 * is outside both UDP payload and checksum. Empty UDP payloads are supported.
 *
 * No extensions, fragments, jumbograms, NIC, socket, allocation, transmission
 * or retained input pointer. Port/address/hop values are observations, not
 * authorization or routing policy. Input may be unaligned; output must be
 * aligned, writable and separate from the entire stable accessible capture.
 * Refusal preserves output. Bounds checks cannot establish mapped memory. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6UdpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6UdpView *out);
#endif
