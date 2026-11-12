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
Expected result: 46 test groups pass: 19 IPv4 groups and 27 UDP groups. This is the expected result of these commands, not a claim
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
ARP, fragmentation/reassembly, IPv6, TCP checksums, sockets and live networking each need their own supported contract and evidence.


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
