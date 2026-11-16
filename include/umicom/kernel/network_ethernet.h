/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_ethernet.h
 *
 * PURPOSE:
 *   Observe bounded Ethernet-II framing and up to two captured VLAN tags without network effects.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_ETHERNET_H
#define UMICOM_KERNEL_NETWORK_ETHERNET_H
#include "umicom/kernel/network_packet.h"

#define UMICOM_ETHERNET_FRAME_HEADER_BYTES 14U
#define UMICOM_ETHERNET_FRAME_PAYLOAD_MAX_BYTES 1500U
#define UMICOM_ETHERNET_FRAME_TAG_MAX_COUNT 2U
#define UMICOM_ETHERNET_FRAME_CAPTURE_MAX_BYTES 1522U

typedef struct UmicomEthernetTagView {
    UmicomU16 protocolIdentifier;
    UmicomU16 controlInformation;
    UmicomU16 vlanIdentifier;
    UmicomU8 priority;
    UmicomBoolean dropEligible;
} UmicomEthernetTagView;

typedef struct UmicomEthernetFrameView {
    UmicomU8 destinationMac[6], sourceMac[6];
    UmicomSize tagCount;
    UmicomEthernetTagView tags[UMICOM_ETHERNET_FRAME_TAG_MAX_COUNT];
    UmicomU16 etherType;
    UmicomSize payloadOffset, payloadBytes;
} UmicomEthernetFrameView;

/* The caller owns stable accessible RAM without preamble or FCS. This reader
 * recognizes TPIDs 0x8100 and 0x88a8, at most two tags in captured order. It
 * copies every TCI bit, including priority tags (VID 0) and reserved VID 4095,
 * as observations only. No tag authorizes a network or authenticates a peer.
 *
 * EtherTypes >=1536 are opaque observations. A length field <=1500 is an
 * unsupported LLC profile; 1501..1535 is a refused header gap. The payload
 * includes all remaining capture bytes, including padding, and is limited to
 * 1500. Header-only captures are allowed: this is not wire-frame validation.
 *
 * Output must be aligned and separate from the entire capture. Failure
 * preserves output; unused tag slots are zero on success. MAC/tag metadata is
 * copied, but payload coordinates borrow the original capture. No allocation,
 * pointer retention, offload reconstruction, FCS, NIC, DMA, bridge or transport
 * action occurs. Existing IPv4/IPv6/ARP readers still accept untagged profiles.
 * Checked extents cannot establish that an arbitrary address is readable RAM. */
UmicomNetworkPacketStatus UmicomKernelEthernetFrameRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetFrameView *out);
#endif
