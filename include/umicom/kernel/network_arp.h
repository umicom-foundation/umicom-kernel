/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_arp.h
 *
 * PURPOSE:
 *   Describe read-only Ethernet/IPv4 ARP request and reply observations without neighbor-table effects.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_ARP_H
#define UMICOM_KERNEL_NETWORK_ARP_H
#include "umicom/kernel/network_packet.h"
#define UMICOM_ETHERNET_ARP_CAPTURE_MIN_BYTES 42U
#define UMICOM_ETHERNET_ARP_CAPTURE_MAX_BYTES 1514U
#define UMICOM_ARP_REQUEST 1U
#define UMICOM_ARP_REPLY 2U
typedef struct UmicomEthernetArpView {
    UmicomU8 destinationMac[6], sourceMac[6];
    UmicomU8 senderHardwareAddress[6], senderProtocolAddress[4];
    UmicomU8 targetHardwareAddress[6], targetProtocolAddress[4];
    UmicomU16 operation;
} UmicomEthernetArpView;
/* Admit Ethernet-II 0x0806, hardware type 1, protocol type 0x0800,
 * hardware length 6, protocol length 4 and operation request/reply only.
 * Accessible RAM captures exclude preamble and FCS; 42..1514 bytes permit
 * absent or trailing Ethernet padding. Captures may be unaligned; out must
 * be aligned and separate from the entire capture. Refusals preserve out.
 *
 * All addresses are copied observations, including mismatched Ethernet/ARP
 * sender addresses, zero addresses and unusual target hardware values. This
 * does not authenticate a sender, learn neighbors, detect conflicts, apply
 * reply policy or transmit anything. No checksum is present in this ARP
 * payload. VLAN, other address sizes and other opcodes are unsupported.
 * No allocation, retained pointer or DMA access. Bounds cannot prove that
 * arbitrary supplied addresses are mapped readable RAM. */
UmicomNetworkPacketStatus UmicomKernelEthernetArpRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetArpView *out);
#endif
