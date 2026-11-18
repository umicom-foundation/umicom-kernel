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

/* Explicit addresses and identifiers describe an outgoing RAM message. They
 * are not inferred from an untrusted capture and do not authorize a reply. */
typedef struct UmicomEthernetIpv4IcmpEchoSpec {
    UmicomU8 destinationMac[6], sourceMac[6];
    UmicomU8 sourceAddress[4], destinationAddress[4];
    UmicomU16 identification, echoIdentifier, sequenceNumber;
    UmicomU8 differentiatedServices, timeToLive, messageType;
} UmicomEthernetIpv4IcmpEchoSpec;

/* Encode type 8 (echo request) or type 0 (echo reply), code zero, inside an
 * untagged fixed IPv4 header. Payload is 0..1472 bytes; TTL must be nonzero.
 * DF is set and both IPv4 and ICMP checksums are generated. An ICMP checksum
 * of zero is kept as zero: unlike UDP it does not mean checksum absent.
 * Result length is 42 + payloadBytes without preamble, padding or Ethernet FCS.
 *
 * spec and written require declared alignment. All complete spans (including
 * destination's unused capacity) must be accessible, stable and pairwise
 * disjoint. NULL payload is allowed only with zero length. Checked address
 * arithmetic does not establish mapping, permissions or device coherence.
 * Refusal preserves the destination and written. Success preserves spare
 * capacity and retains no input pointers. No allocation, NIC, routing, request
 * matching, automatic address swapping or I/O occurs. This is not live ping.
 */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4IcmpEchoWrite(
    const UmicomEthernetIpv4IcmpEchoSpec *spec, const void *payload,
    UmicomSize payloadBytes, void *destination, UmicomSize capacity,
    UmicomSize *written);

#endif
