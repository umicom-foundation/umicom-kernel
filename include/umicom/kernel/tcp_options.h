/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/tcp_options.h
 *
 * PURPOSE:
 *   Inspect bounded TCP option structure and copy coordinates without connection policy or retained pointers.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#ifndef UMICOM_KERNEL_TCP_OPTIONS_H
#define UMICOM_KERNEL_TCP_OPTIONS_H
#include "umicom/kernel/network_packet.h"
#define UMICOM_TCP_OPTIONS_MAX_BYTES 40U
/* offset and length refer to the original option area, not an Ethernet frame.
 * length includes the kind byte and, except NOP, the length byte itself.
 * A NOP entry has kind=1 and length=1. EOL and padding are not entries. */
typedef struct UmicomTcpOptionView {
    UmicomU8 kind, length, offset;
} UmicomTcpOptionView;
typedef struct UmicomTcpOptionsView {
    UmicomSize count, consumedBytes, paddingBytes;
    UmicomBoolean endSeen;
    UmicomTcpOptionView entries[UMICOM_TCP_OPTIONS_MAX_BYTES];
} UmicomTcpOptionsView;

/* Inspect an explicitly supplied complete TCP options area: 0..40 bytes,
 * including padding, with a length divisible by four. NULL input is allowed
 * only when the area is empty. The caller first admits the enclosing header
 * and computes this area from its data offset; this function does not read
 * an Ethernet/IP/TCP header, validate its checksum or authenticate a peer.
 * Existing fixed-header TCP readers keep their no-options profile.
 *
 * Kind 0 ends the list and requires all remaining area bytes to be zero.
 * Kind 1 occupies one byte. Every other kind has a length byte >=2 that must
 * fit the remaining area. Unknown kinds and repeated kinds are retained as
 * opaque coordinates, not rejected solely for being unknown or duplicated.
 * Known-option payload lengths, MSS values, flags, SACK blocks, timestamps,
 * negotiation and connection-state rules remain a separate semantic layer.
 * In particular, structural success does not make an MSS option valid on ACK.
 *
 * consumedBytes includes EOL when present; paddingBytes counts bytes after
 * EOL. Without EOL, consumedBytes equals areaBytes and paddingBytes is zero.
 * All unused entries are zeroed. The result owns copied metadata only; keep
 * the original input alive and stable before reading an entry's payload.
 * Input may be unaligned. Output must be aligned and disjoint from the whole
 * nonempty input. Checked address extents do not prove that RAM is mapped.
 * No allocation, I/O, retained pointer, reset or connection mutation occurs.
 * Every refusal preserves the entire output object. LIMIT means area >40;
 * HEADER covers non-four-byte area, lengths <2 and nonzero EOL padding;
 * LENGTH covers missing length bytes and options extending past the area.
 */
UmicomNetworkPacketStatus UmicomKernelTcpOptionsRead(const void *bytes,
    UmicomSize areaBytes, UmicomTcpOptionsView *out);

/* Owned field values extracted from recognized option kinds. Zero MSS and
 * zero timestamps are preserved wire values, not defaults or clock readings.
 * hasWindowScale distinguishes an absent option from a raw zero shift.
 * unknownCount excludes NOP, EOL and the four recognized kinds below. */
typedef struct UmicomTcpOptionValues {
    UmicomBoolean hasMss, hasWindowScale, sackPermitted, hasTimestamp;
    UmicomU16 mss;
    UmicomU8 windowScaleRaw, windowScaleBounded;
    UmicomU32 timestampValue, timestampEcho;
    UmicomSize unknownCount;
} UmicomTcpOptionValues;

/* Decode one complete option area after the same structural admission as
 * UmicomKernelTcpOptionsRead. Recognized kind/length pairs are MSS 2/4,
 * window scale 3/3, SACK-permitted 4/2 and timestamp 8/10. Repeating any of
 * these four kinds, or giving it another length, returns HEADER. This is an
 * explicit unambiguous extraction profile, not a claim that the structural
 * reader rejects duplicate or unknown options. Other structurally admitted
 * kinds are counted but not interpreted, including SACK block kind 5.
 *
 * Network integers are copied in host-value order using byte reads. The raw
 * window shift is retained; windowScaleBounded is min(raw,14), as required
 * when interpreting that offer. No receive window is scaled here. No SYN,
 * ACK, negotiated state or timestamp clock is supplied: the caller must
 * separately decide whether an option applies to the connection. Merely
 * observing SACK-permitted does not enable SACK. No MSS fallback is invented.
 *
 * Input/output storage has the same stability, accessibility and disjoint
 * requirements as the structural reader; out must have its own alignment.
 * An empty area publishes an all-absent result. Every refusal preserves all
 * output bytes. No input pointer is retained and no connection is changed.
 */
UmicomNetworkPacketStatus UmicomKernelTcpOptionValuesRead(const void *bytes,
    UmicomSize areaBytes, UmicomTcpOptionValues *out);

#endif
