/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/network_neighbor6.h
 *
 * PURPOSE:
 *   Describe copied IPv6 neighbor solicitation and advertisement captures without neighbor-cache or device effects.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_NETWORK_NEIGHBOR6_H
#define UMICOM_KERNEL_NETWORK_NEIGHBOR6_H
#include "umicom/kernel/network_ipv6.h"
#define UMICOM_ICMPV6_NEIGHBOR_SOLICITATION 135U
#define UMICOM_ICMPV6_NEIGHBOR_ADVERTISEMENT 136U
typedef struct UmicomEthernetIpv6NeighborView {
    UmicomEthernetIpv6View ipv6;
    UmicomU8 targetAddress[16], linkLayerAddress[6], messageType;
    UmicomU16 messageBytes, optionCount, ignoredOptionCount;
    UmicomBoolean hasLinkLayerAddress, router, solicited, override;
} UmicomEthernetIpv6NeighborView;
/* Admit direct ICMPv6 NS/NA with code zero, hop limit 255, at least 24 bytes,
 * valid IPv6 pseudo-header checksum and a nonmulticast target address.
 * Unspecified-source NS requires a solicited-node multicast destination and
 * no source link-layer option. Multicast-destination NA cannot be solicited.
 * These are capture checks, not interface ownership or authentication checks.
 *
 * Options use bounded eight-byte length units; zero/truncated lengths refuse.
 * For NS copy type-1 source address, for NA type-2 target address. This narrow
 * Ethernet profile requires exactly one unit for a relevant option and refuses
 * duplicates, rather than choosing an arbitrary address. No address option is
 * also valid. Ignore reserved bits and nonrelevant option contents after their
 * lengths are checked; count ignored options. No retained capture pointers.
 *
 * No NIC, DMA, transmit, cache update, DAD state machine, router discovery,
 * extension traversal, fragments, allocation or proof that a neighbor exists.
 * Stable RAM input may be unaligned. Aligned writable output must not overlap
 * any accessible capture bytes. Every refusal preserves output. Address bounds
 * do not establish that arbitrary caller pointers refer to mapped RAM. */
UmicomNetworkPacketStatus UmicomKernelEthernetIpv6NeighborRead(
    const void *bytes, UmicomSize accessibleBytes, UmicomEthernetIpv6NeighborView *out);
#endif
