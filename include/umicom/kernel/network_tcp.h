/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_tcp.h
 *
 * PURPOSE:
 *   Describe bounded read-only inspection of fixed-header IPv4 TCP captures without connection state.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#ifndef UMICOM_KERNEL_NETWORK_TCP_H
#define UMICOM_KERNEL_NETWORK_TCP_H
#include "umicom/kernel/network_packet.h"

#define UMICOM_ETHERNET_IPV4_TCP_PAYLOAD_OFFSET 54U
#define UMICOM_TCP_FIN 0x01U
#define UMICOM_TCP_SYN 0x02U
#define UMICOM_TCP_RST 0x04U
#define UMICOM_TCP_PSH 0x08U
#define UMICOM_TCP_ACK 0x10U
#define UMICOM_TCP_URG 0x20U
#define UMICOM_TCP_ECE 0x40U
#define UMICOM_TCP_CWR 0x80U

typedef struct UmicomEthernetIpv4TcpView {
    UmicomEthernetIpv4View ipv4;
    UmicomU16 sourcePort;
    UmicomU16 destinationPort;
    UmicomU32 sequenceNumber;
    UmicomU32 acknowledgmentNumber;
    UmicomU16 window;
    UmicomU16 urgentPointer;
    UmicomU16 segmentBytes;
    UmicomU8 flags;
    UmicomU8 reservedBits;
    UmicomSize payloadOffset;
    UmicomSize payloadBytes;
} UmicomEthernetIpv4TcpView;

/* Compose existing IPv4 admission with protocol 6 and a fixed 20-byte TCP
 * header. Verify the complete IPv4 pseudo-header plus TCP header and data.
 * The checksum is mandatory, including when its transmitted field is zero.
 * Ethernet padding is never included in the segment or its checksum.
 *
 * This is a narrow RAM-capture profile, not a TCP endpoint. TCP/IP options,
 * fragments and IPv6 are unsupported. Ports, flags, reserved bits, sequence,
 * acknowledgment, window and urgent pointer are observations, not authority
 * to establish, acknowledge, reset or close a connection. Reserved bits are
 * copied without interpretation. No option, stream or state-machine validation,
 * routing, NIC access, DMA synchronization, allocation or I/O occurs.
 *
 * Keep accessible bytes stable during the call and owned while consuming the
 * borrowed payload coordinates. out must be aligned, writable and disjoint
 * from the whole capture. Refusal leaves it unchanged; no input pointer is
 * retained. Bounds checks cannot establish that arbitrary RAM is mapped. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv4TcpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv4TcpView *out);
#endif
