/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_icmp.h
 *
 * PURPOSE:
 *   Describe borrowed-buffer admission of fixed-profile IPv4 ICMP echo captures.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_NETWORK_ICMP_H
#define UMICOM_KERNEL_NETWORK_ICMP_H
#include "umicom/kernel/network_packet.h"

#define UMICOM_ICMP_ECHO_REPLY 0U
#define UMICOM_ICMP_ECHO_REQUEST 8U
#define UMICOM_ETHERNET_IPV4_ICMP_ECHO_PAYLOAD_OFFSET 42U

typedef struct UmicomEthernetIpv4IcmpEchoView {
    UmicomEthernetIpv4View ipv4;
    UmicomU8 messageType;
    UmicomU16 identifier;
    UmicomU16 sequenceNumber;
    UmicomU16 messageBytes;
    UmicomSize payloadOffset;
    UmicomSize payloadBytes;
} UmicomEthernetIpv4IcmpEchoView;

/* Compose fixed-header IPv4 admission with protocol 1, echo type 0 or 8,
 * code zero and an eight-byte-or-longer message. Verify the checksum over
 * the entire admitted IP payload; Ethernet padding is not message data.
 * A checksum field of zero is not an opt-out: the complete sum must validate.
 *
 * Metadata is copied and payload coordinates refer to the original capture.
 * Keep the accessible RAM bytes stable during the call and owned until the
 * payload has been consumed. out must be aligned, writable and disjoint from
 * the whole capture; refusal leaves it unchanged. No input pointer is stored.
 *
 * This is not ping or a reply builder. Identifier, sequence and addresses are
 * observations, not proof of an outstanding request, sender identity or trust.
 * ICMP errors, IPv6, IP options and fragments are outside this bounded profile.
 * No allocation, NIC access, DMA synchronization, routing or I/O occurs. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4IcmpEchoRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4IcmpEchoView *out);

#endif
