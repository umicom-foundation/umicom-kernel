![Umicom](../assets/branding/umicom-logo.svg)

# Read packet captures before building a network driver

The Kernel now has a small read-only capture parser in
[kernel/network_packet.c](../kernel/network_packet.c).
It accepts one bounded Ethernet-II capture containing a fixed IPv4 header.
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
Expected result: 100 test groups pass: 19 IPv4, 27 UDP, 24 ICMP echo and 30 TCP groups. This is the expected result of these commands, not a claim
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
ARP, fragmentation/reassembly, IPv6, TCP connection state, sockets and live networking each need their own supported contract and evidence.


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
