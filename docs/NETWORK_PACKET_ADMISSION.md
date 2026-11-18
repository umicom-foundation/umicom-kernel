![Umicom](../assets/branding/umicom-logo.svg)

# Read packet captures before building a network driver

The Kernel now has a small read-only capture parser in
[kernel/network_packet.c](../kernel/network_packet.c).
<!-- Original scope retained: It accepts one bounded Ethernet-II capture containing a fixed IPv4 header. -->
It accepts bounded Ethernet-II captures through separate IPv4, ARP and IPv6
profiles. Additive readers inspect UDP, echo and fixed-header TCP payloads.
It does not open a network interface, send traffic, implement sockets or make the Kernel network-ready.

## Before you begin

Use CMake 3.24 or later, Ninja and a C23-capable GCC or Clang.
The host checks run inside your current operating system. They need no administrator access,
packet capture permission or network adapter. Use a disposable build folder outside important data.

1. Open the Kernel repository.
2. Configure the isolated host checks.
3. Build them, then run CTest. Stop at the first failed command.

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S tests/network_packet -B build/host-network-packet -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DBUILD_TESTING=ON
cmake --build build/host-network-packet --parallel 2
ctest --test-dir build/host-network-packet --output-on-failure --no-tests=error
```

On Linux, omit the Windows compiler path and use a C23-capable compiler in PATH:

```sh
cmake -S tests/network_packet -B build/host-network-packet -G Ninja -DBUILD_TESTING=ON
cmake --build build/host-network-packet --parallel 2
ctest --test-dir build/host-network-packet --output-on-failure --no-tests=error
```

<!-- Earlier host suite contained 19 IPv4 groups, retained unchanged. -->
<!-- The previous expected total was 46: all 19 IPv4 and 27 UDP groups remain. -->
<!-- Previous expected result: 70 test groups pass: 19 IPv4, 27 UDP and 24 ICMP echo groups. All remain in the suite. -->
<!-- Previous expected total: 100 groups; all earlier IPv4 and transport cases remain. -->
<!-- Earlier expected total retained: 121 groups before IPv6; every group remains. -->
<!-- Earlier expected total retained: Expected result: 150 test groups pass: 19 IPv4, 27 UDP, 24 ICMP echo, 30 TCP, 21 ARP and 29 IPv6 groups. -->
<!-- Earlier expected total retained: 242 groups, including 33 IPv6 TCP groups. All remain. -->
<!-- Earlier expected total retained: 281 groups, including 39 IPv6 neighbor captures. -->
<!-- Earlier framing-suite expectation retained: 326 groups, the preceding 281 plus 45 generic Ethernet/VLAN capture cases. -->
Expected result: 363 test groups pass: the preceding 326 plus 37 tagged IPv4 capture cases. This is the expected result of these commands, not a claim
about every platform. A missing compiler or nonzero test exit needs investigation before proceeding.

## The capture contract

The caller supplies stable accessible RAM with the Ethernet preamble and frame checksum already removed.
Captures may be unaligned, but the output structure must be aligned and separate from the input.
The parser accepts 34 through 1514 capture bytes, IPv4 type 0x0800, a 20-byte IPv4 header
and an IP total length no greater than 1500 bytes.
Options, VLAN tags, IPv6 and fragments are outside this profile.

The network format's IP total length excludes Ethernet padding. See
[RFC 894](https://www.rfc-editor.org/rfc/rfc894.html).
The parser therefore reports payload length from the IP header, not from the end of the capture.
Its acceptance of short unpadded captures is an explicit software-input choice, not Ethernet transmitter validation.

## Follow a complete observation

```c
UmicomEthernetIpv4View view;
UmicomNetworkPacketStatus status =
    UmicomKernelEthernetIpv4Read(capture, capture_bytes, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Retain capture ownership before reading:
     * capture + view.payloadOffset, for view.payloadBytes bytes. */
}
```

This fragment assumes capture points to readable stable storage and capture_bytes is its accessible length.
The returned view copies MAC/IP addresses, protocol, TTL, identification and length fields.
It contains no borrowed pointer. Payload bytes remain in the original caller-owned capture.

The IPv4 header checksum covers the header; it does not authenticate a sender or validate a UDP/TCP payload.
Header layout, network byte order and the checksum are defined in
[RFC 791](https://www.rfc-editor.org/rfc/rfc791.html).
The parser observes TTL and protocol without implementing routing or transport policy.

## Failure without partial state

Every refusal leaves the output bytes unchanged. The implementation validates the input span,
header lengths, fragment flags and checksum before copying a staged result.
Checked address addition catches wraparound and overlap; it cannot establish that a fabricated address is mapped.

1. Read the fixed golden capture in tests/network_packet/packet_tests.c.
2. Find the truncation loop and predict why lengths 0 through 45 refuse that capture.
3. Change a header byte in a scratch copy and expect checksum refusal.
4. Change a payload byte and explain why header admission still succeeds.
5. Inspect the padding case: the capture has 60 bytes, while the admitted payload still has 12.

## Freestanding linkage

Hosted tests can hide a dependency on libc, the usual C runtime library.
The existing boot-memory link contracts also compile this parser at O0 and O2 without libc
and without discarding unused sections. After configuring the documented RISC-V build, run:

```powershell
cmake --build build/riscv64-clang-debug --target umicom-boot-memory-contracts
```

Use your actual configured cross-build directory if it differs.
These images check linkage only; they are not bootable guest tests.
The normal Kernel build also compiles the parser, but no NIC is bound by that addition.

## Recovery and next boundaries

If golden fails, compare the fixed header bytes and compiler diagnostics before changing expected results.
If a truncation or refusal test fails, inspect the failing length and verify output preservation.
Do not broaden the accepted packet profile simply to make a test pass.

A future driver must establish DMA ownership, buffer lifetime, bounded receive queues, shutdown and
error handling before it calls this parser. <!-- Earlier future-work list included all transport checksums. The bounded UDP reader below implements only its declared IPv4 transport checksum contract. -->
<!-- Earlier future boundary: ARP, fragmentation/reassembly, IPv6, TCP checksums, sockets and live networking each need their own supported contract and evidence. The fixed-header TCP checksum reader below now covers only its declared capture profile. -->
<!-- Earlier roadmap also listed all ARP work; the capture reader below adds only observation, not neighbor resolution. -->
ARP neighbor resolution, fragmentation/reassembly, IPv6, TCP connection state, sockets and live networking each need their own supported contract and evidence.


## Exercise the parser inside the diagnostic guest

The diagnostic Kernel now calls the same reader on synthetic RAM captures after hardware inventory checks. Normal startup remains separate. No virtual NIC is attached by this test, and no frame is transmitted or received.

After configuring the documented RISC-V build, build the current image before selecting tests:

~~~powershell
cmake --build build/riscv64-clang-debug --target umicom-kernel-evidence --parallel 2
ctest --test-dir build/riscv64-clang-debug -R "^kernel[.](qualification[.]identity|riscv64[.]packet_admission)$" --output-on-failure --no-tests=error
~~~

Use your actual configured build directory. Expected evidence includes seven ordered groups: golden capture, every truncated prefix, checksum refusal, padding exclusion, unaligned capture, unsupported Ethernet type and copied metadata lifetime. Refusal checks compare the entire output with its previous bytes. The wrapper also requires QEMU exit zero, the final Kernel completion marker and no explicit failure markers. Its transcript is packet-admission-guest.log in the build directory.

The CTest current-image fixture may add a build check to the selected tests. Keep that result distinct from the seven packet groups inside one guest test. If the test command's CMake module changes, regenerate the build before starting CTest: an already running CTest process has already read its test definitions.

<!-- Earlier guest scope excluded transport checksums. Four additive UDP groups now exercise the new transport reader; the seven IPv4 groups remain. -->
A passing guest check proves this synthetic parser path executes in the freestanding diagnostic image. It does not qualify a NIC, DMA synchronization, a receive queue or a network stack. The O0/O2 link-only contracts and the broader host parser cases remain complementary checks.

## Admit a UDP datagram through the same capture boundary

The additive API in [network_udp.h](../include/umicom/kernel/network_udp.h)
first calls the existing IPv4 reader, then validates the transport header.
It accepts protocol 17 and an eight-byte-or-larger UDP datagram whose length
exactly matches the admitted IPv4 payload. This deliberately restricted profile
excludes UDP options, IP options, fragments, IPv6 and jumbo packets.

[RFC 768](https://www.rfc-editor.org/rfc/rfc768) defines the UDP fields and the
IPv4 checksum convention. The checksum includes a pseudo-header containing IP
addresses, protocol and UDP length. Zero in the received checksum field denotes
absence; a computed zero is represented on the wire by all ones. These two
representations therefore have different meanings to admission.

~~~c
#include "umicom/kernel/network_udp.h"

UmicomEthernetIpv4UdpView view;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv4UdpRead(
    capture, capture_bytes, UMICOM_UDP_CHECKSUM_REQUIRE, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Read only while capture remains owned and stable:
     * capture + view.payloadOffset, for view.payloadBytes bytes. */
}
~~~

Use REQUIRE when this consumer needs a verified transport checksum. ALLOW_ABSENT
accepts the IPv4 no-checksum representation and reports checksumPresent false.
It still rejects a nonzero checksum that fails verification. Neither policy
authenticates a sender or provides encryption, replay protection or reliable delivery.
The reader observes ports without deciding whether a socket exists or a service may run.

The golden UDP fixture contains five payload bytes spelling hello. It has 47
capture bytes, 33 IP bytes and 13 UDP bytes. The returned application payload
starts at byte 42 and has five bytes. Extending the capture with Ethernet padding
does not extend any of those lengths or enter the checksum.

## Inspect checksum failures before widening admission

1. Run the complete host suite above, or select kernel.network_udp with CTest's -R option.
2. Inspect udp_tests.c: the fixed vector has IP checksum 3c61 and UDP checksum 9f5c.
3. Read corruption: each one-bit change to selected port, checksum or payload bytes must refuse.
4. Read source_address and destination_address: each repairs the IP checksum but deliberately leaves the UDP checksum stale.
5. Read zero_checksum_encoding: a fixed two-byte payload exercises the valid all-ones checksum representation.
6. Compare absent_allowed with absent_required. They use the same missing-checksum bytes and intentionally choose different policy.

The production code reads network words bytewise and adds an odd final byte as
the high byte of a conceptual zero-padded word. It does not read or write padding.
The bounded accumulator folds its carry after summation. See the arithmetic
background in [RFC 1071](https://www.rfc-editor.org/rfc/rfc1071).

The diagnostic guest now retains the seven IPv4 groups and appends udp-golden,
udp-refusals, udp-padding and udp-checksum-policy, followed by packet-admission.cases=11.
Use the same current-image fixture and guest commands above. A stale seven-group
transcript cannot satisfy the updated wrapper. The separate O0/O2 link contracts
include the transport function because it shares the production packet source.

If an output-preservation or bounds case fails, stop and investigate that exact
input before processing any real traffic. Host and guest success cover stable
synthetic RAM only. A future NIC integration must still establish DMA completion,
queue ownership and shutdown before handing a capture to either reader.


## Read an ICMP echo request or reply

The additive API in [network_icmp.h](../include/umicom/kernel/network_icmp.h)
reuses the existing IPv4 reader. Its fixed profile admits protocol 1, echo request
type 8 or echo reply type 0, code zero, and at least eight message bytes.
ICMP error messages and the datagrams they quote require a different parser.
This function neither generates a reply nor matches one to an outstanding request.

~~~c
#include "umicom/kernel/network_icmp.h"

UmicomEthernetIpv4IcmpEchoView echo;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv4IcmpEchoRead(
    capture, capture_bytes, &echo);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* echo.identifier and echo.sequenceNumber are copied observations.
     * Retain capture ownership before consuming its payload coordinates. */
}
~~~

The fragment assumes readable stable capture storage. The result contains copied
metadata and offsets; the payload remains in caller-owned RAM. A failed read leaves
the entire output unchanged, including padding bytes. Overlap with any part of the
accessible capture, including Ethernet padding, is refused before parsing.

### Understand the checksum difference

[RFC 792](https://www.rfc-editor.org/rfc/rfc792.html) defines the echo layout and
checksum. The message checksum covers the type byte through the final message byte,
with conceptual zero padding for an odd length. It has no UDP pseudo-header.
Consequently, changing an IP address and repairing the IPv4 header checksum does
not invalidate an otherwise unchanged echo message. Neither checksum authenticates
the address, identifier, sequence number or payload.

Zero in the ICMP checksum field never means optional checking. A zero field can be
valid when the complete folded sum is all ones; otherwise the reader refuses it.
The zero_checksum regression checks both situations. Do not copy UDP absence
policy into this reader.

### Work through the native checks

1. Run the complete host commands above; alternatively select kernel.network_icmp with CTest -R.
2. Open tests/network_packet/icmp_tests.c and read the independent golden bytes. They describe a 47-byte capture, a 33-byte IPv4 datagram and a 13-byte echo message.
3. Locate the five payload bytes at offset 42. The literal ICMP checksum is f62b for the request and fe2b for the corresponding reply.
4. Compare corruption, address_scope and zero_checksum. Explain the different checksum boundaries before modifying the fixture.
5. Inspect truncation, overlap and lifetime. Admission copies observations but does not extend the input storage lifetime.

The diagnostic guest preserves all eleven earlier groups and adds icmp-golden,
icmp-reply, icmp-refusals and icmp-padding-lifetime. Its final count at the ICMP checkpoint was
packet-admission.cases=15. The guest wrapper requires the complete ordered transcript
and successful process exit with -nic none. The existing O0/O2 no-libc link targets
also include the new function through the shared production source.

If a checksum case fails, inspect its exact bytes and length before changing the
expected result. If a guest marker is absent, first check the current-image fixture
and selected artifact identity. A host pass cannot substitute for a guest that
never ran. These checks establish synthetic capture behavior only; NIC drivers,
DMA completion, sockets, routing, live ping and physical hardware remain separate work.


## Inspect a fixed-header TCP capture

The additive [network_tcp.h](../include/umicom/kernel/network_tcp.h) API reuses
the bounded IPv4 reader and admits protocol 6 with a 20-byte TCP header.
A TCP data offset below five words is malformed. A header that exceeds the IP
payload is truncated; a fully present longer header is unsupported.
This profile does not parse options, establish connections, match acknowledgments,
reassemble streams, retransmit segments, implement sockets or attach a NIC.

~~~c
#include "umicom/kernel/network_tcp.h"

UmicomEthernetIpv4TcpView tcp;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv4TcpRead(
    capture, capture_bytes, &tcp);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Retain capture ownership before consuming tcp.payloadOffset and
     * tcp.payloadBytes. Copied sequence and flag values are observations,
     * not accepted connection state. */
}
~~~

The fragment assumes readable stable capture storage. The result copies ports,
sequence and acknowledgment numbers, flags, reserved bits, window and urgent
pointer. Reserved bits are observed rather than rejected. There is no state
machine to interpret these fields. Refusal leaves the complete output unchanged;
the output must be aligned and separate from the whole accessible capture.

### Check the mandatory transport checksum

TCP verifies a pseudo-header containing the IPv4 addresses, protocol and segment
length, together with the complete TCP header and payload. An odd final byte
gets conceptual zero padding. Ethernet padding is excluded by the IP length.
A zero stored checksum never disables verification, although it can be valid
when the complete folded sum is all ones. See
[RFC 9293, section 3.1](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.1).
A checksum is not sender authentication or a security policy.

1. Run the host commands above, or select "^kernel[.]network_tcp[.]" with CTest -R.
2. Inspect the independent golden vector in tests/network_packet/tcp_tests.c. It has 59 capture bytes, 45 IP bytes and 25 TCP bytes; the five-byte hello payload starts at offset 54.
3. Compare its literal IP checksum 0x3c60 and TCP checksum 0x77ed with the independent fixture helper.
4. Read source_address and destination_address: the IPv4 checksum is repaired after changing an address, but the stale TCP checksum must still refuse.
5. Compare the two checksum_zero outcomes. The valid zero-field fixture changes the window to 0x87ed, while merely clearing the original checksum refuses.
6. Inspect overlap, lifetime, output_alignment, truncation and data_offset before proposing a caller.

The guest preserves all fifteen earlier packet groups and adds tcp-golden,
tcp-refusals, tcp-zero-checksum and tcp-padding-lifetime. The current required
count is packet-admission.cases=19. Run the same evidence target and selected
guest commands above; the wrapper uses -nic none and requires normal completion.
The O0/O2 no-libc link targets compile the same production function.

If checksum verification fails, inspect the exact bytes, IP length and
pseudo-header before changing expectations. If a guest marker is absent,
establish the current ELF identity and fixture result first. These checks
cover synthetic RAM captures, not DMA synchronization, live traffic, TCP
interoperability or physical hardware.


## Learn address resolution by inspecting ARP captures

An IP address names a protocol endpoint. An Ethernet frame also needs a hardware
address for its next destination on the local link. Address Resolution Protocol
(ARP) carries requests and replies about that mapping. Parsing one message is
only the first step: deciding whether to trust it, maintaining a bounded neighbor
table and sending frames require separate services.

The additive [network_arp.h](../include/umicom/kernel/network_arp.h) API reads the
Ethernet/IPv4 profile of [RFC 826](https://www.rfc-editor.org/rfc/rfc826.html).
It accepts Ethernet type 0x0806, hardware type 1, protocol type 0x0800, six-byte
hardware addresses, four-byte protocol addresses, and request/reply opcodes 1/2.
Its minimum capture is 42 bytes; trailing Ethernet padding is ignored. The same
stable-RAM, overlap, alignment and output-preservation contracts apply.

| Capture offset | Field | Bytes |
| --- | --- | --- |
| 0 / 6 | Ethernet destination / source | 6 each |
| 12 | Ethernet type | 2 |
| 14 / 16 | ARP hardware / protocol types | 2 each |
| 18 / 19 | Address lengths | 1 each |
| 20 | Request or reply operation | 2 |
| 22 / 28 | Sender hardware / protocol address | 6 / 4 |
| 32 / 38 | Target hardware / protocol address | 6 / 4 |

These multibyte fields use network byte order: the most significant byte comes
first. The implementation reads bytes explicitly, so an unaligned capture does
not require an unaligned machine-word load. ARP has no payload checksum here;
this capture contract assumes the Ethernet preamble and frame checksum have
already been removed.

~~~c
#include "umicom/kernel/network_arp.h"

UmicomEthernetArpView observation;
UmicomNetworkPacketStatus status = UmicomKernelEthernetArpRead(
    capture, capture_bytes, &observation);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Inspect copied addresses and operation. No neighbor entry was learned. */
}
~~~

The fragment assumes readable, stable capture memory. Ethernet and ARP sender
addresses are returned separately, even if they differ. Zero protocol addresses,
unusual targets and a syntactically valid reply do not establish identity or
justify a neighbor-table update. The parser intentionally makes no such policy
decision and never generates a reply.

### Four practical lessons

1. **Follow the bytes.** Open tests/network_packet/arp_tests.c. Decode its 42-byte
   golden request using the table. Find the target IPv4 address 192.0.2.2 and
   explain why the target hardware address can be unknown.
2. **Separate representation from policy.** Compare sender_mismatch and addresses.
   Both can parse successfully. Write down what a future neighbor service would
   still need to know before accepting either observation.
3. **Preserve ownership.** Inspect truncation, overlap and lifetime. Every short
   prefix refuses without changing output; successful output owns copied values.
   No pointer into a device buffer survives the call.
4. **Compare host and guest evidence.** Run the complete host commands at the top
   of this guide. To select only ARP after building, use the command below. Then
   run the existing current-image guest command and retain its separate result.

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_arp[.]" --output-on-failure --no-tests=error
~~~

Expected ARP host result: 21 tests. The diagnostic guest retains its nineteen
existing groups and adds arp-golden, arp-reply, arp-refusals and
arp-padding-lifetime, for packet-admission.cases=23. The O0/O2 no-libc link
contracts compile the same production reader. A host pass is not a guest pass,
and neither proves live ARP resolution, DMA synchronization or NIC operation.

If a profile case fails, compare the literal type, length and opcode fields
before changing expectations. If a guest marker is absent, establish the actual
image identity and build-fixture result. Do not treat missing coverage as a pass.


## Course: inspect an IPv6 capture without building a network stack

IPv6 uses 128-bit addresses and a fixed forty-byte base header. Ethernet type
0x86dd identifies its payload. The base header has no header checksum; a
transport protocol such as UDP has its own requirements. Read the primary
[IPv6 specification](https://www.rfc-editor.org/rfc/rfc8200.html) and
[IPv6 over Ethernet](https://www.rfc-editor.org/rfc/rfc2464.html) beside this
course. Our reader supports a deliberately limited capture profile, not all
IPv6 features.

### Lesson 1: find the header fields

1. Open [ipv6_tests.c](../tests/network_packet/ipv6_tests.c).
2. Locate its literal 59-byte vector. The first fourteen bytes are Ethernet.
3. Decode the next four bytes, 6a bc de f0: version 6, traffic class 0xab,
   flow label 0xcdef0. These fields cross byte boundaries.
4. Read payload length 0x0005 in network byte order. The next-header value 17
   identifies UDP, but the five bytes that follow are deliberately not a
   valid UDP segment. Base-header inspection must not claim otherwise.

| Capture offset | IPv6 field | Bytes |
| --- | --- | --- |
| 14 | Version, traffic class and flow label | 4 |
| 18 | Payload length | 2 |
| 20 / 21 | Next header / hop limit | 1 each |
| 22 / 38 | Source / destination address | 16 each |
| 54 | Start of payload | Declared length |

### Lesson 2: use copied observations

~~~c
#include "umicom/kernel/network_ipv6.h"

UmicomEthernetIpv6View view;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6Read(
    capture, capture_bytes, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Addresses and header values are copied. Payload is described by offsets;
       it remains in the caller-owned capture, which must stay alive to read it. */
}
~~~

The fragment assumes stable accessible RAM. The caller must supply aligned,
nonoverlapping output. A refusal leaves that output unchanged. Do not consume
an uninitialized view after a refusal. An all-zero source, multicast destination
or zero hop limit is returned as an observation; a future networking service
must apply its own address and forwarding policy.

### Lesson 3: make unsupported behavior explicit

The reader accepts direct TCP, UDP and ICMPv6 next-header values and an empty
No Next Header packet. It does not validate their transport headers or checksums.
It refuses fragments and extension headers, including the hop-by-hop structure
used by jumbograms. Its maximum IPv6 packet is 1500 bytes. No Next Header with
nonempty payload is also refused by this narrower profile, although general
IPv6 permits trailing octets that a receiver ignores.

Compare the extensions and fragments tests. They check distinct refusal
statuses without changing the last observation. Compare padding and maximum:
the former ignores capture bytes beyond the declared payload, while the latter
uses a full 1460-byte IPv6 payload. Neither grants permission to read outside
the supplied capture span.

### Lesson 4: qualify host and guest separately

1. Run the complete configure/build/test commands at the beginning of this guide.
   The expected combined result is 150 groups; stop and investigate failures.
2. Select the new subset after building with the command below. It contains
   29 groups, including all short prefixes and all unsupported next-header values.
3. Use the existing diagnostic guest workflow. The guest runs without a NIC and
   now requires 27 ordered groups: the previous 23 plus ipv6-golden,
   ipv6-truncation, ipv6-extension-policy and ipv6-padding-lifetime.
4. Retain the build identity and separate host/guest results. The O0/O2 no-libc
   link contracts also compile this production reader.

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_ipv6[.]" --output-on-failure --no-tests=error
~~~

For a length failure, compare capture length, declared payload length and the
1500-byte IP limit. For a missing guest marker, check the image identity and
build fixture before changing expected output. A host or diagnostic guest pass
does not prove physical networking, IPv6 interoperability, neighbor discovery,
transport validation, DMA ownership or application connectivity.


## Course: ordinary IPv6 UDP capture admission

### Lesson 1: distinguish framing from a transport checksum

The base IPv6 reader establishes where its payload ends. The additive
UmicomKernelEthernetIpv6UdpRead service also requires direct UDP and validates
the datagram checksum. Include umicom/kernel/network_udp6.h and provide an
aligned UmicomEthernetIpv6UdpView separate from the capture.

[RFC 768](https://www.rfc-editor.org/rfc/rfc768.html) defines the eight-byte UDP
header and a length that includes both header and data.
[RFC 8200 section 8.1](https://www.rfc-editor.org/rfc/rfc8200.html#section-8.1)
defines IPv6's pseudo-header and ordinary mandatory UDP checksum. This reader
rejects a zero wire checksum. Specialized tunnel exceptions are outside its
profile; there is no IPv4-style ALLOW_ABSENT option.

1. Locate the IPv6 payload at capture offset 54.
2. Read UDP ports at offsets 54 and 56, its length at 58 and checksum at 60.
3. Require at least eight UDP bytes and equality with the IPv6 payload length.
4. Exclude trailing Ethernet padding before checking or exposing the payload.

### Lesson 2: construct an independent checksum fixture

The host golden vector in tests/network_packet/udp6_tests.c uses documentation
addresses 2001:db8::1 and 2001:db8::2, ports 12345 and 53, and the five bytes
hello. Its fixed checksum is 301f. The test mutation helper folds carries after
each word; production folds after summing the bounded input. This gives a
separate implementation for constructing changed fixtures.

The pseudo-header includes both 128-bit addresses, the upper-layer length and
protocol 17. Odd final bytes are conceptually followed by zero for arithmetic;
the parser neither reads nor writes a padding byte. A computed zero is encoded
as ffff. The independent two-byte payload 73f7 exercises this encoding in both
host and guest tests.

### Lesson 3: keep observations separate from policy

~~~c
#include "umicom/kernel/network_udp6.h"

UmicomEthernetIpv6UdpView view;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6UdpRead(
    capture, capture_bytes, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Metadata is copied. Payload bytes still belong to capture's owner. */
}
~~~

Change the source address without recomputing the checksum: admission must
refuse it. Change only the flow label or hop limit: the checksum still matches,
because those fields are outside the pseudo-header. This does not authorize
forwarding a zero-hop packet. Addresses and ports are observations for a future
policy owner, not socket or firewall decisions.

Failures leave the previous output intact. Input bytes must stay stable and
accessible during admission. Copied metadata outlives the capture, but the
payload offset does not retain its bytes. Never read a released DMA buffer
through an old offset.

### Lesson 4: qualify the checksum contract

1. Run this guide's host configure/build commands, then the complete host suite.
   The current combined expectation is 179 groups, including 29 IPv6 UDP groups.
2. Select the UDP subset below when diagnosing a failure. Its cases include all
   short prefixes, each address-bit corruption, maximum payload, absent checksum,
   negative-zero encoding, overlap, address wrap and preserved outputs.
3. Rebuild the current Kernel evidence and O0/O2 freestanding link contracts.
   The existing no-NIC guest now expects 31 ordered packet groups, adding
   udp6-golden, udp6-checksum, udp6-framing and udp6-padding-lifetime.
4. Keep host results and guest image identity together. Do not reinterpret a
   synthetic capture pass as physical networking or full IPv6 interoperability.

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_udp6[.]" --output-on-failure --no-tests=error
~~~

For LENGTH, compare capture size, IPv6 payload size and UDP length before
changing checksums. For CHECKSUM, inspect the addresses and declared datagram,
including odd-byte handling. For FRAGMENT or UNSUPPORTED, use a different
explicitly implemented profile; this reader does not reassemble fragments or
walk extension headers. No NIC, sockets, neighbor discovery, routing or packet
transmission is added by these lessons.


## Course: ICMPv6 echo capture inspection

This course extends the earlier packet exercises. The combined host suite now
contains 209 cases, and the no-NIC diagnostic guest requires 35 ordered groups.
Earlier counts above describe the preceding course checkpoints.

### Lesson 1: distinguish message formats and checksum scope

[RFC 4443](https://www.rfc-editor.org/rfc/rfc4443.html#section-2.3) specifies an
IPv6 pseudo-header for the ICMPv6 checksum. The echo profiles use types 128 and
129, code zero, and an eight-byte header containing checksum, identifier and
sequence number. The pseudo-header's protocol value is 58. Unlike UDP, ICMPv6
does not reserve a zero checksum field to mean that checking is absent.

The Kernel's UmicomKernelEthernetIpv6IcmpEchoRead reader admits only those echo
profiles after a fixed IPv6 header. It is a capture inspector, not a ping
command or an IPv6 host implementation. Neighbor discovery, error messages,
fragments and extension headers remain outside its supported profile.

1. Open tests/network_packet/icmp6_tests.c and identify the fixed request vector.
2. Locate its type at capture offset 54, checksum at 56, identifier at 58 and
   sequence number at 60. Data begins at 62.
3. Check the five-byte hello payload against the fixed cdfa checksum.
4. Inspect the separate reply with identifier abcd, sequence 1000 and payload
   6778. Its checksum field is legitimately zero. Rejecting every zero field
   would incorrectly apply the UDP policy to this message type.

### Lesson 2: inspect copied metadata safely

~~~c
#include "umicom/kernel/network_icmp6.h"

UmicomEthernetIpv6IcmpEchoView view;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6IcmpEchoRead(
    capture, capture_bytes, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Use copied type, identifier, sequence and IPv6 metadata here.
       Reading data through view.payloadOffset still requires capture to live. */
}
~~~

The input must be stable accessible RAM. The output must be aligned and separate
from the whole capture, including trailing padding. Refusal preserves the last
output, so always check status before consuming it. No memory is allocated and
no packet is transmitted. A valid reply does not prove it matches a request;
request ownership and matching belong to a future endpoint service.

### Lesson 3: design independent corruption checks

The fixture builder folds carries after each word, while production folds the
bounded total. Compare these implementations and the fixed vectors. Changing
any address bit must invalidate the checksum because both complete addresses
participate. Changing the flow label or hop limit does not change checksum
coverage; these remain observations for later policy decisions.

The ipv4-scope test deliberately omits the pseudo-header. The udp-scope test
uses the wrong protocol number. Both must fail. Other cases inspect every
unsupported message type, nonzero echo code, truncated prefix, odd payload,
maximum admitted size, overlapping output and address arithmetic wrap.

### Lesson 4: qualify the real production reader

1. Run the host configure/build commands at the start of this guide, then the
   complete suite. The expected current total is 209 cases.
2. Use the subset below to diagnose the 30 ICMPv6 cases after building.
3. Rebuild Kernel evidence and the O0/O2 freestanding link contracts. Run the
   existing packet diagnostic guest check with its source and image identity.
4. Require all previous 31 groups followed by icmp6-zero-reply, icmp6-checksum,
   icmp6-framing and icmp6-padding-lifetime. Do not weaken marker checks to hide
   a stale executable or a failed checksum.

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_icmp6[.]" --output-on-failure --no-tests=error
~~~

Investigate LENGTH through the captured and declared IPv6 sizes; inspect type
and code for HEADER or UNSUPPORTED; inspect message bytes, addresses, length
and protocol for CHECKSUM. A host test or no-NIC guest pass establishes this
bounded RAM contract only. It provides no evidence of a network interface,
physical-device operation, packet transmission or complete IPv6 conformance.


## Course: fixed-header TCP over IPv6

This checkpoint adds 33 TCP6 host cases. The combined expectation is now 242
host cases and 39 ordered no-NIC guest groups. Earlier course totals describe
historical checkpoints, not the current complete suite.

### Lesson 1: compose framing with a transport contract

Include umicom/kernel/network_tcp6.h and call
UmicomKernelEthernetIpv6TcpRead. The existing IPv6 reader first establishes a
bounded payload with direct next-header 6. TCP admission then requires a
20-byte header, verifies the complete segment checksum and copies metadata.
Payload data begins at capture offset 74: 14 Ethernet bytes, 40 IPv6 bytes and
20 TCP bytes. This function does not establish a connection or process a stream.

[RFC 8200 section 8.1](https://www.rfc-editor.org/rfc/rfc8200.html#section-8.1)
defines the IPv6 pseudo-header used by TCP. Both complete addresses, the
upper-layer length and protocol number participate. This profile admits no
extension headers, so the upper-layer length is the IPv6 payload length.
TCP uses protocol 6. A transmitted zero checksum is accepted only if the entire
sum verifies; the UDP rule that rejects a zero field does not apply to TCP.

1. Open tests/network_packet/tcp6_tests.c and find the documentation-address
   capture with ports 12345 and 80, payload hello and checksum 08b0.
2. Locate sequence at offset 58, acknowledgment at 62, data offset at 66,
   flags at 67, window at 68, checksum at 70 and urgent pointer at 72.
3. Read the high nibble of the data-offset byte as a count of four-byte words.
   Values below five are malformed. Values extending beyond the segment are
   truncated. A complete larger header is outside this fixed-header profile.
4. Inspect the shared Ipv6TransportChecksumMatches helper. UDP and ICMPv6 use
   its arithmetic but retain their own protocol and checksum-zero policies.

### Lesson 2: retain capture ownership

~~~c
#include "umicom/kernel/network_tcp6.h"

UmicomEthernetIpv6TcpView view;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6TcpRead(
    capture, capture_bytes, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* Ports, sequence, flags and IP fields are copied observations.
       Payload coordinates still borrow the original capture's storage. */
}
~~~

Keep the input accessible and stable during the call. Output must be aligned
and separate from the entire capture, including trailing padding. Refusal
preserves output; it does not grant permission to consume an uninitialized
view. A SYN/ACK flag combination is returned as metadata, not interpreted as
a valid handshake. Reserved bits and urgent pointers are similarly observed.

### Lesson 3: use independent vectors when sharing code

The host fixture helper constructs a conceptual 40-byte pseudo-header and
folds carries after each word. Production uses bounded addition and a final
fold. The guest's separate vector uses ports 443 and 49152, sequence 10203040,
acknowledgment 50607080, SYN/ACK flags and payload 4219. Window 4f46 makes its
valid transmitted checksum zero. This catches accidental reuse of UDP's
zero-field refusal.

Corrupt an address byte without updating the checksum and require CHECKSUM.
Change only flow label, hop limit or Ethernet metadata and observe that the
checksum remains valid. Those fields are outside its scope; their acceptance
does not authorize routing or physical-device use. Add a declared payload byte
without recomputing the checksum and require refusal: trailing capture padding
must never become unchecked TCP data.

### Lesson 4: qualify changes to the shared checksum primitive

1. Configure and build the isolated host tests using this guide's commands.
2. Run the whole 242-case suite, including earlier UDP6 and ICMPv6 groups. A new
   TCP6 pass alone does not establish that their shared arithmetic was preserved.
3. Use the subset below to diagnose the 33 new TCP6 cases after building.
4. Rebuild the Kernel evidence and O0/O2 freestanding link contracts. Run the
   existing packet diagnostic check with the current guest image identity.
5. Require the previous 35 guest groups followed by tcp6-zero-checksum,
   tcp6-corruption, tcp6-framing and tcp6-padding-lifetime. Keep the guest without
   a NIC and preserve all prior ordered marker checks.

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_tcp6[.]" --output-on-failure --no-tests=error
~~~

For HEADER, inspect the data-offset nibble. For LENGTH, compare the capture,
IPv6 payload and TCP header extents. For CHECKSUM, examine the complete segment,
addresses, length and protocol. For UNSUPPORTED or FRAGMENT, select an explicitly
implemented profile instead of weakening admission. Passing these synthetic
checks does not establish a TCP endpoint, retransmission, congestion control,
TCP options, neighbor discovery, network driver or physical interoperability.


## Inspect IPv6 neighbor messages without maintaining a neighbor cache

A neighbor solicitation asks about an IPv6 address. An advertisement describes an address and may include a link-layer address. The capture reader observes these messages; it does not decide that a neighbor is trusted, update a cache, assign an address or send a reply.

The new header is `umicom/kernel/network_neighbor6.h`. It builds on the existing Ethernet/IPv6 admission and ICMPv6 pseudo-header checksum. The supported profile uses a direct ICMPv6 payload with no extension headers. RFC 4861 describes the message checks: type 135 or 136, code zero, hop limit 255, at least 24 message bytes, a non-multicast target and nonzero bounded option lengths. An unspecified-source solicitation must use a solicited-node multicast destination and must not carry a source link-layer address. An advertisement to a multicast destination must clear its Solicited flag. Reserved fields and irrelevant options are ignored. See [RFC 4861 section 7.1](https://www.rfc-editor.org/rfc/rfc4861.html#section-7.1).

### Read a capture in C

1. Obtain a stable RAM capture from your caller. The storage must stay accessible throughout the call.
2. Keep the output structure aligned and separate from all input bytes, including padding.
3. Use the returned metadata only after `UMICOM_NETWORK_PACKET_OK`. Refusals leave the output untouched.

```c
#include "umicom/kernel/network_neighbor6.h"

UmicomEthernetIpv6NeighborView observation;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv6NeighborRead(
    capture, capture_bytes, &observation);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* These fields are copies, independent of the capture storage. */
    unsigned message_type = observation.messageType;
    unsigned option_count = observation.optionCount;
    (void)message_type;
    (void)option_count;
    /* Read linkLayerAddress only when hasLinkLayerAddress is true. */
}
```

This is an integration fragment, not a standalone program: the caller supplies `capture` and `capture_bytes`. The return value records parsing acceptance, not peer identity.

### Understand the narrower Ethernet profile

The parser accepts at most one relevant Ethernet address option: source address for a solicitation, target address for an advertisement. That option must occupy exactly eight bytes, including its two-byte option header. Duplicate relevant options are refused instead of silently selecting one. Unknown and irrelevant options are counted and skipped only after their full declared extent fits. `optionCount` includes both kinds; `ignoredOptionCount` counts skipped options. This profile does not interpret arbitrary link-layer formats.

The DAD-shaped test fixture uses the unspecified source address, but accepting it does not implement Duplicate Address Detection. The reader recognizes the solicited-node multicast prefix; it does not check interface membership, address ownership or perform the host state machine. Likewise, the hop-limit check and checksum do not authenticate a neighbor.

### Run and diagnose the checks

Run the complete host sequence near the top of this lesson after any parser change. To isolate this extension after building, use:

```powershell
ctest --test-dir build/host-network-packet -R "neighbor6" --output-on-failure --no-tests=error
```

Expected result: 39 neighbor cases. They cover independently checksummed solicitation and advertisement captures, a valid zero checksum, malformed option extents, duplicate address options, unspecified-source restrictions, multicast advertisement flags, every refused code/hop value, truncation, output alignment/overlap and copied metadata lifetime.

The existing focused guest qualification now requires 43 ordered packet groups: the preceding 39 plus four neighbor groups. It runs with no NIC. Keep its normal identity/build checks enabled; a transcript from a different binary is not evidence for edited source. If a checksum test fails, compare the 16-byte addresses, message length and protocol byte included in the pseudo-header before changing parser policy. If an option test fails, confirm the length is expressed in eight-byte units and excludes neither the option header nor trailing data. A zero checksum field is valid when the complete ones-complement sum is correct.

Hardware receive/transmit, neighbor aging, address resolution retries, routing and transport connections remain separate work.


## Observe Ethernet and VLAN framing separately from IP

A link-layer frame identifies a destination and source with six-byte MAC
addresses. Its next two bytes are either an Ethernet-II protocol identifier
(an EtherType) or an IEEE 802.3 length field. The generic reader in
`umicom/kernel/network_ethernet.h` supports Ethernet-II identifiers from 1536
upward. It refuses length-field LLC frames and the gap between 1500 and 1536.
The [IANA IEEE 802 number registry](https://www.iana.org/assignments/ieee-802-numbers)
documents this distinction; it is a number registry, not a complete validation
specification.

### Lesson 1: retain what the capture actually contains

A VLAN tag inserts a protocol identifier and control information before the
inner protocol. The reader recognizes tag identifiers 0x8100 and 0x88a8 and
admits at most two captured tags. That depth is this API's bound, not a claim
that all Ethernet networks use at most two tags. The control information holds
priority (three bits), drop eligibility (one bit) and a VLAN identifier (twelve
bits). The [Linux Ethernet definitions](https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/if_ether.h)
and [VLAN definitions](https://raw.githubusercontent.com/torvalds/linux/master/include/linux/if_vlan.h)
provide primary implementation references for the field constants. Umicom's
reader is independently implemented in C.

The reader copies all tag bits, including VID zero and reserved VID 4095.
Observing these values is not permission to configure or join a VLAN. A NIC
may strip a tag before software receives a frame; absent bytes do not establish
that a packet was untagged on the wire. Offload metadata needs a separate
trusted contract and is not reconstructed here.

1. Keep the original capture in stable accessible RAM without preamble or FCS.
2. Supply an aligned output object outside the entire capture span.
3. Inspect the view only after an OK result.
4. Keep capture ownership while using its payload coordinates.

~~~c
#include "umicom/kernel/network_ethernet.h"

UmicomEthernetFrameView observation;
UmicomNetworkPacketStatus status = UmicomKernelEthernetFrameRead(
    capture, capture_bytes, &observation);
if (status == UMICOM_NETWORK_PACKET_OK) {
    for (UmicomSize i = 0; i < observation.tagCount; ++i) {
        UmicomU16 vlan = observation.tags[i].vlanIdentifier;
        (void)vlan; /* A copied observation, not a forwarding decision. */
    }
    /* Coordinates refer to the original capture, whose tags remain present. */
    UmicomSize offset = observation.payloadOffset;
    UmicomSize size = observation.payloadBytes;
    (void)offset;
    (void)size;
}
~~~

This integration fragment requires caller-owned `capture` and `capture_bytes`.
It performs no receive, transmit or bridge operation.

### Lesson 2: distinguish framing extent from protocol payload

An untagged header is 14 bytes; one tag makes it 18, two tags make it 22.
The generic reader returns every remaining capture byte as payload, including
padding. Its maximum is 1500 bytes after the actual header. A 1515-byte
untagged capture is therefore refused even though a two-tag capture can reach
1522 bytes. Header-only captures are accepted for inspection; this does not
qualify them as valid physical transmissions.

Unlike the IP readers, framing alone cannot remove Ethernet padding by reading
a protocol length. It also does not verify the payload's checksum or version.
An unknown EtherType is an opaque observation. Identifier 0x9100 is not treated
as a tag in this profile. Existing IPv4, IPv6, ARP and transport APIs retain
their untagged input requirements: generic tag support does not silently extend
their contracts.

### Lesson 3: qualify bounded parsing

Run the complete host sequence at the beginning of this guide. After building,
isolate the 45 new cases with:

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_ethernet[.]" --output-on-failure --no-tests=error
~~~

Expected coverage includes all four recognized two-tag orders, unaligned
captures, every truncated header prefix, the EtherType/length boundary,
priority/DEI/VID bits, three-tag refusal, each payload limit, invalid output
alignment and address wrap. Refusals must leave output bytes unchanged.
The Ethernet framing subset contains 47 ordered groups: all 43 earlier
groups followed by ethernet-plain, ethernet-tags, ethernet-refusals and
ethernet-lifetime. Keep the existing build and image-identity fixtures enabled.

For LENGTH, compare accessible bytes with each complete tag's four-byte extent.
For LIMIT, subtract the actual header before checking the payload. For
UNSUPPORTED, inspect the length-field profile or tag depth; do not remove a
bound merely to make a fixture pass. For ARGUMENT, check output alignment,
overlap and arithmetic. These tests use synthetic RAM and no NIC; they do not
establish hardware, FCS, VLAN offload, network isolation or tagged IP forwarding.


## Compose tagged Ethernet with fixed IPv4

### Lesson 1: keep one IPv4 decoder

The generic Ethernet reader accepts captured tags, while the original IPv4 and
transport entry points retain their untagged contract. The new
UmicomKernelEthernetTaggedIpv4Read entry composes framing with the same private
IPv4 decoder used by the original entry. The earlier full implementation remains
in an explained disabled block for review. No packet is stripped or copied into
a second temporary frame merely to reuse parsing.

~~~c
UmicomEthernetTaggedIpv4View view;
UmicomNetworkPacketStatus status = UmicomKernelEthernetTaggedIpv4Read(
    capture, capture_bytes, &view);
if (status == UMICOM_NETWORK_PACKET_OK) {
    UmicomSize ip_header_offset = view.frame.payloadOffset;
    UmicomSize ip_payload_offset = view.ipv4.payloadOffset;
    UmicomSize ip_payload_bytes = view.ipv4.payloadBytes;
    (void)ip_header_offset;
    (void)ip_payload_offset;
    (void)ip_payload_bytes;
}
~~~

This fragment assumes stable caller-owned RAM. It accepts zero, one or two
supported tags and requires the inner EtherType to be IPv4. It applies the
existing fixed 20-byte header, checksum, total-length and fragment rules.
It does not validate UDP/TCP data or allow the older transport APIs to accept
tagged frames. The IPv4 header checksum covers the IP header, not Ethernet tags;
see [RFC 791](https://www.rfc-editor.org/rfc/rfc791) for that protocol boundary.

### Lesson 2: retain original coordinates and padding scope

A double-tag header places the IP header at byte 22. Its fixed header places
IP payload at byte 42. These offsets always refer to the original capture.
For a 60-byte capture containing a 32-byte IPv4 datagram, frame.payloadBytes is
38, but ipv4.payloadBytes is 12. The difference includes the IP header and
Ethernet padding. Never use the larger framing count as a transport length.

Inspect a tag's VLAN identifier, priority and drop-eligible bit without treating
those observations as authorization. The decoder neither joins a network nor
validates offload metadata, FCS, forwarding policy or the physical interface.
Copied metadata remains usable after capture storage is released; payload access
does not. Output alignment and complete input/output separation are required.

### Lesson 3: qualify reuse and refusals

Run the complete host sequence from the beginning of this guide. It now registers
363 cases: 326 retained packet cases and 37 tagged IPv4 cases. Isolate the addition:

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_tagged_ipv4[.]" --output-on-failure --no-tests=error
~~~

Coverage includes recognized tag orders, padding, every truncated prefix,
maximum datagrams, unsupported options, fragments, checksum corruption,
unaligned input, output overlap/alignment, address wrap and unchanged legacy
scope. The focused guest expects 51 ordered groups, including four new
tagged-ipv4 observations after the earlier 47 groups. Keep its current-image
build and identity checks enabled. Authored fixtures alone are not execution
evidence; compare actual results from your selected source build.

On LENGTH, compare the capture remainder after its actual tag header with the
IPv4 total length. On CHECKSUM, inspect the 20 IP-header bytes, not tag bytes.
On FRAGMENT, use a separately designed reassembly service rather than passing a
partial datagram onward. These tests operate on synthetic RAM with no NIC and
cannot qualify physical VLAN reception or end-to-end networking.

## Encode a UDP frame in ordinary memory

The complementary `UmicomKernelEthernetIpv4UdpWrite` operation in
`umicom/kernel/network_udp.h` creates an untagged Ethernet-II / IPv4 / UDP
byte sequence. It does not send it. Supply explicit MAC addresses, IPv4
addresses, ports, identification, differentiated-services byte and nonzero TTL
in `UmicomEthernetIpv4UdpSpec`. The writer sets the fixed IPv4 header and DF
flag, then computes both IPv4 and UDP checksums. It always emits a UDP checksum;
a calculated zero is represented as 0xffff rather than "checksum absent".

1. Keep the specification and payload in stable accessible RAM.
2. Provide a separate destination with capacity for 42 plus the payload length.
   At most 1472 payload bytes fit this ordinary 1500-byte IPv4 profile.
3. Provide a separate aligned UmicomSize output counter. All complete spans
   must be pairwise disjoint, including unused destination capacity.
4. Check the returned status before reading a newly written count or frame.
5. If successful, use only the returned byte count. Bytes beyond it remain
   unchanged; they are not guaranteed zero-filled Ethernet padding.

~~~c
UmicomU8 frame[1514];
UmicomSize written = 0U;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv4UdpWrite(
    &spec, payload, payload_bytes, frame, sizeof frame, &written);
if (status == UMICOM_NETWORK_PACKET_OK) {
    /* frame[0..written) is an encoded RAM object, not a transmitted packet. */
}
~~~

This fragment assumes initialized `spec` and valid `payload` objects. A NULL
payload is allowed only for zero bytes. Destination/spec/counter overlap,
misaligned typed objects, arithmetic wraparound, excessive payload, short
capacity and zero TTL are refused before output publication. Destination and
written remain unchanged on refusal. No allocator or hosted C library is used.
The API cannot prove that a supplied address is mapped, accessible or coherent
with a device. Its buffer contract remains the caller's responsibility.

The returned size is 42 plus payload bytes, without preamble, FCS or padding.
A device adapter must handle the Ethernet minimum-frame requirement and FCS;
passing spare uninitialized memory as padding would disclose memory contents.
The encoder does not validate routing, ownership of an address, socket binding,
firewall policy, ARP discovery, MTU variation or hardware offload settings.
Options, fragments, VLAN tags, IPv6 and jumbo packets require other contracts.
See [UDP, RFC 768](https://www.rfc-editor.org/rfc/rfc768) for the pseudo-header
and zero-checksum representation.

The host sequence above now contains 376 cases, retaining all 363 admission
cases and adding 13 encoding groups. The all-lengths group covers payload
lengths 0 through 1472. Other groups check fixed independent bytes, short
capacities, checksum-zero encoding, unaligned byte buffers, lifetime, invalid
arguments, overlap and address wrap. To isolate encoding:

~~~powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_udp_write[.]" --output-on-failure --no-tests=error
~~~

The diagnostic guest keeps the 51 prior admission groups and appends four
encoding groups, for 55 ordered markers. Run the current-image and identity
fixtures with the packet guest. The debug and optimized no-libc link targets
also include the writer. Passing these checks establishes the declared RAM
encoding behavior on those builds, not a physical network driver or live UDP.

## Encode ICMP echo messages in owned RAM

`UmicomKernelEthernetIpv4IcmpEchoWrite` constructs an explicit type-8 request or type-0 reply. It shares complete-span admission and fixed Ethernet/IPv4 header composition with the UDP writer. The ICMP checksum remains protocol-specific: it covers the eight-byte ICMP header and payload, without a UDP pseudo-header. A computed zero stays zero and must still validate when read.

1. Include `umicom/kernel/network_icmp.h` and initialize an `UmicomEthernetIpv4IcmpEchoSpec`. Supply destination/source MAC and IPv4 addresses explicitly. The writer does not derive a reply policy or swap addresses from a received capture.
2. Set a nonzero TTL, an IPv4 identification value, echo identifier, sequence number and supported message type. IPv4 identification and echo identifier are separate fields even when an example gives them the same value. The code byte is always zero.
3. Supply zero through 1472 payload bytes and an independent writable destination with at least 42 plus payload length bytes. A null payload is valid only at zero length.
4. Keep the complete spec, destination capacity, payload and aligned output counter stable, accessible and pairwise disjoint. Byte buffers can be unaligned; typed spec and counter objects cannot.
5. Check status before consuming the new count. Rejected calls preserve destination and count. Successful calls preserve spare destination capacity. The writer retains no pointers.

```c
UmicomU8 frame[1514];
UmicomSize written = 0U;
UmicomNetworkPacketStatus status = UmicomKernelEthernetIpv4IcmpEchoWrite(
    &spec, payload, payload_bytes, frame, sizeof frame, &written);
/* Use frame[0..written) only after status == UMICOM_NETWORK_PACKET_OK. */
```

The fragment assumes initialized spec and valid payload storage. The result excludes Ethernet padding, preamble and FCS. A later device adapter must supply valid padding and the required checksum/offload contract. There is no routing, neighbor discovery, request correlation, timeout handling, rate limiting or network I/O here. An encoded reply is not permission to answer every incoming request, and an echoed identifier is not authentication. This is not a complete ping implementation.

The host suite adds sixteen ICMP encoding groups to the earlier 376 groups, for 392 registrations. It includes independent request/reply bytes, every payload length, valid zero checksums, corruption detection, complete-span overlap and address-wrap rejection. The guest retains its earlier 55 groups and appends four more ordered groups, for 59. Registration counts describe coverage to execute; actual results belong to the selected build evidence.

```powershell
ctest --test-dir build/host-network-packet -R "^kernel[.]network_icmp_write[.]" --output-on-failure --no-tests=error
```

See [ICMP, RFC 792](https://www.rfc-editor.org/rfc/rfc792) for echo message fields and checksums. Live networking and physical hardware remain separate qualification work.
