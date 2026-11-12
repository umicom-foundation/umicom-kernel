/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_udp.h
 *
 * PURPOSE:
 *   Describe bounded UDP admission over the existing Ethernet-II and IPv4 reader.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_NETWORK_UDP_H
#define UMICOM_KERNEL_NETWORK_UDP_H
#include "umicom/kernel/network_packet.h"

#define UMICOM_ETHERNET_IPV4_UDP_PAYLOAD_OFFSET 42U

typedef enum UmicomUdpChecksumPolicy {
    UMICOM_UDP_CHECKSUM_ALLOW_ABSENT = 0,
    UMICOM_UDP_CHECKSUM_REQUIRE = 1
} UmicomUdpChecksumPolicy;

typedef struct UmicomEthernetIpv4UdpView {
    UmicomEthernetIpv4View ipv4;
    UmicomU16 sourcePort;
    UmicomU16 destinationPort;
    UmicomU16 datagramBytes;
    UmicomBoolean checksumPresent;
    UmicomSize payloadOffset;
    UmicomSize payloadBytes;
} UmicomEthernetIpv4UdpView;

/* Compose the existing fixed-header IPv4 admission with UDP protocol 17.
 * UDP length must equal the admitted IP payload; UDP options, IP options,
 * fragments, IPv6 and jumbo packets are outside this profile. Ethernet
 * padding is excluded from both payload and checksum.
 *
 * A nonzero checksum is always verified, including the IPv4 pseudo-header.
 * An IPv4 zero checksum is accepted only with ALLOW_ABSENT and is reported
 * as absent, never as verified. REQUIRE refuses it with CHECKSUM status.
 * Port values are observations, not a socket-binding or firewall decision.
 *
 * The stable, accessible capture is borrowed; offsets refer to its beginning.
 * out must be aligned, writable and disjoint from the whole capture. Refusal
 * leaves it unchanged. Metadata is copied; no payload or input pointer is kept.
 * No allocation, device access, DMA synchronization, delivery or I/O occurs. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4UdpRead(
    const void *bytes, UmicomSize accessibleBytes,
    UmicomUdpChecksumPolicy policy, UmicomEthernetIpv4UdpView *out);

#endif
