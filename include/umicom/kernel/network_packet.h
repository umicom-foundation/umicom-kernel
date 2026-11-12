/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_packet.h
 *
 * PURPOSE:
 *   Describe bounded, read-only admission of Ethernet-II IPv4 capture bytes.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_NETWORK_PACKET_H
#define UMICOM_KERNEL_NETWORK_PACKET_H
#include "umicom/kernel/types.h"

#define UMICOM_ETHERNET_IPV4_CAPTURE_MAX_BYTES 1514U
#define UMICOM_ETHERNET_IPV4_PAYLOAD_OFFSET 34U

typedef enum UmicomNetworkPacketStatus {
    UMICOM_NETWORK_PACKET_OK = 0,
    UMICOM_NETWORK_PACKET_ARGUMENT,
    UMICOM_NETWORK_PACKET_LENGTH,
    UMICOM_NETWORK_PACKET_LIMIT,
    UMICOM_NETWORK_PACKET_UNSUPPORTED,
    UMICOM_NETWORK_PACKET_HEADER,
    UMICOM_NETWORK_PACKET_CHECKSUM,
    UMICOM_NETWORK_PACKET_FRAGMENT
} UmicomNetworkPacketStatus;

/* Metadata is copied so returning from admission does not retain a DMA or
 * device buffer. Payload offsets refer to the ORIGINAL capture; its owner must
 * retain it before any consumer reads those bytes. The parser owns no buffer. */
typedef struct UmicomEthernetIpv4View {
    UmicomU8 destinationMac[6];
    UmicomU8 sourceMac[6];
    UmicomU8 sourceAddress[4];
    UmicomU8 destinationAddress[4];
    UmicomU8 differentiatedServices;
    UmicomU8 timeToLive;
    UmicomU8 protocol;
    UmicomBoolean dontFragment;
    UmicomU16 identification;
    UmicomU16 ipBytes;
    UmicomSize payloadOffset;
    UmicomSize payloadBytes;
} UmicomEthernetIpv4View;

/* Read an accessible, stable RAM capture with preamble and FCS already removed.
 * Only Ethernet-II type 0x0800, IPv4's fixed 20-byte header and unfragmented
 * datagrams are supported. Short unpadded captures and trailing Ethernet
 * padding are accepted; this is a capture contract, not wire-frame validation.
 *
 * bytes may be unaligned. out must be aligned and must not overlap the input.
 * Refusal leaves out unchanged. No allocation, routing, reassembly, transport
 * checksum, address policy, DMA synchronization or network I/O occurs here.
 * Header arithmetic checks cannot prove that arbitrary memory is mapped. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4Read(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4View *out);

#endif
