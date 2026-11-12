/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/udp_tests.c
 *
 * PURPOSE:
 *   Exercise real UDP admission with independent checksums, corruption and borrowed-buffer boundaries.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/network_udp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)

/* Independent fixed vector: IPv4 checksum 3c61, UDP checksum 9f5c,
 * source 192.0.2.1:12345, destination 198.51.100.2:53, payload hello.
 * These documentation addresses and RAM bytes never reach a network. */
static const unsigned char golden[] = {
    2,0,0,0,0,2,2,0,0,0,0,1,8,0,
    0x45,0,0,33,0x12,0x34,0x40,0,64,17,0x3c,0x61,
    192,0,2,1,198,51,100,2,0x30,0x39,0,0x35,0,13,0x9f,0x5c,
    'h','e','l','l','o'
};
static void Put16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)(value >> 8U); p[1] = (unsigned char)value;
}
static unsigned Add(unsigned sum, unsigned value)
{
    sum += value; return (sum & 65535U) + (sum >> 16U);
}
static unsigned Sum(const unsigned char *p, size_t length, unsigned sum)
{
    for (size_t i = 0U; i < length; i += 2U)
        sum = Add(sum, ((unsigned)p[i] << 8U) | (i + 1U < length ? p[i + 1U] : 0U));
    return sum;
}
static void IpChecksum(unsigned char *frame)
{
    frame[24] = 0U; frame[25] = 0U;
    Put16(frame + 24U, ~Sum(frame + 14U, 20U, 0U) & 65535U);
}
static void UdpChecksum(unsigned char *frame, size_t length)
{
    /* Mutable vectors fold each word, independently of the production final
     * fold. The golden vector and negative-zero payload above are fixed. */
    frame[40] = 0U; frame[41] = 0U;
    unsigned sum = Sum(frame + 26U, 8U, 0U);
    sum = Add(Add(sum, 17U), (unsigned)length);
    unsigned checksum = ~Sum(frame + 34U, length, sum) & 65535U;
    Put16(frame + 40U, checksum ? checksum : 65535U);
}
static int Refused(const void *bytes, UmicomSize length, UmicomUdpChecksumPolicy policy,
    UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv4UdpView out;
    unsigned char before[sizeof out];
    memset(&out, 0xa5, sizeof out); memcpy(before, &out, sizeof out);
    CHECK(UmicomKernelEthernetIpv4UdpRead(bytes, length, policy, &out) == expected);
    CHECK(!memcmp(before, &out, sizeof out));
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *name = argv[1];
    unsigned char frame[1515];
    memset(frame, 0, sizeof frame); memcpy(frame, golden, sizeof golden);
    UmicomEthernetIpv4UdpView out;
    if (!strcmp(name, "golden")) {
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(!memcmp(frame, golden, sizeof golden));
        CHECK(out.sourcePort == 12345U && out.destinationPort == 53U);
        CHECK(out.datagramBytes == 13U && out.checksumPresent == UMICOM_TRUE);
        CHECK(out.payloadOffset == 42U && out.payloadBytes == 5U);
        CHECK(out.ipv4.ipBytes == 33U && out.ipv4.payloadBytes == 13U);
        CHECK(!memcmp(out.ipv4.sourceAddress, golden + 26U, 4U));
    } else if (!strcmp(name, "empty") || !strcmp(name, "even") || !strcmp(name, "maximum")) {
        unsigned length = !strcmp(name, "empty") ? 8U : !strcmp(name, "even") ? 12U : 1480U;
        Put16(frame + 16U, 20U + length); Put16(frame + 38U, length);
        if (length == 1480U) memset(frame + 42U, 0xff, 1472U);
        IpChecksum(frame); UdpChecksum(frame, length);
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, 34U + length, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes == length - 8U && out.datagramBytes == length);
    } else if (!strcmp(name, "padding")) {
        memset(frame + sizeof golden, 0xff, 100U - sizeof golden);
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, 100U, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes == 5U);
    } else if (!strcmp(name, "unaligned")) {
        _Alignas(16) unsigned char storage[sizeof golden + 1U];
        memcpy(storage + 1U, golden, sizeof golden);
        CHECK(UmicomKernelEthernetIpv4UdpRead(storage + 1U, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
    } else if (!strcmp(name, "truncation")) {
        for (UmicomSize size = 0U; size < sizeof golden; ++size)
            CHECK(!Refused(frame, size, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_LENGTH));
    } else if (!strcmp(name, "short_header")) {
        Put16(frame + 16U, 27U); IpChecksum(frame);
        CHECK(!Refused(frame, 41U, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_LENGTH));
    } else if (!strcmp(name, "length_min")) {
        for (unsigned length = 0U; length < 8U; ++length) {
            Put16(frame + 38U, length);
            CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_HEADER));
        }
    } else if (!strcmp(name, "short_length") || !strcmp(name, "long_length")) {
        Put16(frame + 38U, !strcmp(name, "short_length") ? 12U : 14U);
        CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_LENGTH));
    } else if (!strcmp(name, "protocol")) {
        frame[23] = 6U; IpChecksum(frame);
        CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_UNSUPPORTED));
    } else if (!strcmp(name, "ip_checksum")) {
        frame[24] ^= 1U;
        CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_CHECKSUM));
    } else if (!strcmp(name, "fragment")) {
        Put16(frame + 20U, 0x2000U); IpChecksum(frame);
        CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_FRAGMENT));
    } else if (!strcmp(name, "absent_allowed") || !strcmp(name, "absent_required")) {
        Put16(frame + 40U, 0U); frame[42] ^= 1U;
        if (!strcmp(name, "absent_required")) {
            CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_CHECKSUM));
        } else {
            CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_ALLOW_ABSENT, &out) == UMICOM_NETWORK_PACKET_OK);
            CHECK(out.checksumPresent == UMICOM_FALSE && out.payloadBytes == 5U);
        }
    } else if (!strcmp(name, "corruption")) {
        const unsigned positions[] = {34U,35U,36U,37U,40U,41U,42U,43U,44U,45U,46U};
        for (unsigned i = 0U; i < sizeof positions / sizeof positions[0]; ++i) {
            for (unsigned bit = 0U; bit < 8U; ++bit) {
                memcpy(frame, golden, sizeof golden); frame[positions[i]] ^= (unsigned char)(1U << bit);
                CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_ALLOW_ABSENT, UMICOM_NETWORK_PACKET_CHECKSUM));
            }
        }
    } else if (!strcmp(name, "source_address") || !strcmp(name, "destination_address")) {
        frame[!strcmp(name, "source_address") ? 26U : 30U] ^= 1U; IpChecksum(frame);
        CHECK(!Refused(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_CHECKSUM));
    } else if (!strcmp(name, "zero_checksum_encoding")) {
        Put16(frame + 16U, 30U); Put16(frame + 38U, 10U);
        Put16(frame + 40U, 65535U); Put16(frame + 42U, 0xe334U); IpChecksum(frame);
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, 44U, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.checksumPresent == UMICOM_TRUE && out.payloadBytes == 2U);
    } else if (!strcmp(name, "ports")) {
        Put16(frame + 34U, 0U); Put16(frame + 36U, 65535U); UdpChecksum(frame, 13U);
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.sourcePort == 0U && out.destinationPort == 65535U);
    } else if (!strcmp(name, "arguments")) {
        CHECK(!Refused(NULL, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(!Refused(frame, sizeof golden, (UmicomUdpChecksumPolicy)2, UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, NULL) == UMICOM_NETWORK_PACKET_ARGUMENT);
    } else if (!strcmp(name, "overlap")) {
        union { unsigned char bytes[256]; UmicomEthernetIpv4UdpView view; } storage;
        unsigned char before[sizeof storage];
        memset(&storage, 0, sizeof storage); memcpy(storage.bytes, golden, sizeof golden);
        memcpy(before, &storage, sizeof storage);
        CHECK(UmicomKernelEthernetIpv4UdpRead(storage.bytes, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, &storage.view) == UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before, &storage, sizeof storage));
        memcpy(storage.bytes + 8U, golden, sizeof golden); memcpy(before, &storage, sizeof storage);
        CHECK(UmicomKernelEthernetIpv4UdpRead(storage.bytes + 8U, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, &storage.view) == UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before, &storage, sizeof storage));
    } else if (!strcmp(name, "output_alignment")) {
        _Alignas(16) unsigned char storage[sizeof out + 16U];
        unsigned char before[sizeof storage];
        memset(storage, 0x5a, sizeof storage); memcpy(before, storage, sizeof storage);
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE,
            (UmicomEthernetIpv4UdpView *)(void *)(storage + 1U)) == UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before, storage, sizeof storage));
    } else if (!strcmp(name, "address_wrap")) {
        const UmicomAddress maximum = ~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum - 15U), sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE,
            (UmicomEthernetIpv4UdpView *)(maximum - (_Alignof(UmicomEthernetIpv4UdpView) - 1U))) == UMICOM_NETWORK_PACKET_ARGUMENT);
    } else if (!strcmp(name, "lifetime")) {
        CHECK(UmicomKernelEthernetIpv4UdpRead(frame, sizeof golden, UMICOM_UDP_CHECKSUM_REQUIRE, &out) == UMICOM_NETWORK_PACKET_OK);
        memset(frame, 0, sizeof golden);
        CHECK(out.sourcePort == 12345U && out.ipv4.sourceAddress[0] == 192U && out.payloadBytes == 5U);
    } else if (!strcmp(name, "capture_limit")) {
        CHECK(!Refused(frame, 1515U, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(frame, ~(UmicomSize)0U, UMICOM_UDP_CHECKSUM_REQUIRE, UMICOM_NETWORK_PACKET_LIMIT));
    } else return 2;
    return 0;
}
