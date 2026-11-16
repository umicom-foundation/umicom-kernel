/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_tagged_ipv4.h
 *
 * PURPOSE:
 *   Compose bounded Ethernet/VLAN framing with fixed IPv4 admission using original capture coordinates.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_TAGGED_IPV4_H
#define UMICOM_KERNEL_NETWORK_TAGGED_IPV4_H
#include "umicom/kernel/network_ethernet.h"
typedef struct UmicomEthernetTaggedIpv4View {
    UmicomEthernetFrameView frame;
    UmicomEthernetIpv4View ipv4;
} UmicomEthernetTaggedIpv4View;
/* Accept zero, one or two observed 0x8100/0x88a8 tags through the generic frame
 * reader, then require inner EtherType 0x0800 and the existing fixed 20-byte
 * IPv4 profile. Header checksum, fragment and total-length rules share the
 * untagged decoder. The IPv4 payload offset always refers to the ORIGINAL
 * capture; frame.payloadBytes may additionally include Ethernet padding.
 *
 * Stable accessible RAM only, with no preamble/FCS. Output must be aligned and
 * separate from the complete capture. Refusal preserves all output bytes.
 * Metadata is copied; reading payload later still requires the caller's capture
 * lifetime. No packet stripping/copy, allocation, VLAN authorization, transport
 * checksum, forwarding, reassembly, NIC or DMA access occurs. Existing untagged
 * transport readers still refuse VLAN frames; this API does not widen them. */
UmicomNetworkPacketStatus UmicomKernelEthernetTaggedIpv4Read(
    const void *bytes,UmicomSize accessibleBytes,UmicomEthernetTaggedIpv4View *out);
#endif
