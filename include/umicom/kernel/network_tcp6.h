/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_tcp6.h
 *
 * PURPOSE:
 *   Describe fixed-header IPv6 TCP capture admission without connection or device ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_TCP6_H
#define UMICOM_KERNEL_NETWORK_TCP6_H
#include "umicom/kernel/network_ipv6.h"
#include "umicom/kernel/network_tcp.h"
#define UMICOM_ETHERNET_IPV6_TCP_PAYLOAD_OFFSET 74U
typedef struct UmicomEthernetIpv6TcpView {
    UmicomEthernetIpv6View ipv6;
    UmicomU16 sourcePort, destinationPort;
    UmicomU32 sequenceNumber, acknowledgmentNumber;
    UmicomU16 window, urgentPointer, segmentBytes;
    UmicomU8 flags, reservedBits;
    UmicomSize payloadOffset, payloadBytes;
} UmicomEthernetIpv6TcpView;
/* Admit direct next-header 6 and exactly twenty TCP header bytes. Verify the
 * complete segment with the IPv6 pseudo-header: both sixteen-byte addresses,
 * upper-layer length and protocol 6. A zero checksum field is valid only when
 * the complete sum verifies; TCP has no checksum-absent interpretation.
 * Ethernet padding is excluded. No IPv6 extensions, fragments, TCP options,
 * jumbograms, stream/connection validation, NIC, allocation or I/O occurs.
 *
 * Ports, flags, low header nibble, sequence, acknowledgment, window and urgent
 * pointer remain observations, not authority to create or control connections.
 * Input may be unaligned but must remain accessible and stable. Output must be
 * aligned, writable and disjoint from the entire capture. Refusal preserves it.
 * Payload coordinates borrow the caller's capture; no pointer is retained.
 * Integer bounds checks cannot establish whether arbitrary RAM is mapped. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6TcpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6TcpView *out);
#endif
