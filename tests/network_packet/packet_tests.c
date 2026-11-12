/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/network_packet/packet_tests.c
 *
 * PURPOSE:
 *   Exercise the real packet reader against independent capture bytes and malformed boundaries.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/network_packet.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "line %d: %s\n", __LINE__, #test); return 1; } } while (0)

/* This fixed byte vector is independent of the reader. It is a synthetic
 * capture, not a valid UDP message: admission does not validate UDP payloads. */
static const unsigned char golden[] = {
    0x02,0,0,0,0,2, 0x02,0,0,0,0,1, 0x08,0,
    0x45,0,0,0x20,0x12,0x34,0x40,0,0x40,0x11,0x3c,0x62,
    0xc0,0,2,1, 0xc6,0x33,0x64,2,
    1,2,3,4,5,6,7,8,9,10,11,12
};
static void Put16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)(value >> 8U);
    p[1] = (unsigned char)value;
}
static void Checksum(unsigned char *frame)
{
    /* Fold each addition here, unlike the reader's final fold. This fixture
     * helper is used for changed headers only; golden's checksum is fixed. */
    unsigned sum = 0U;
    frame[24] = 0U; frame[25] = 0U;
    for (unsigned i = 14U; i < 34U; i += 2U) {
        sum += ((unsigned)frame[i] << 8U) | frame[i + 1U];
        sum = (sum & 65535U) + (sum >> 16U);
    }
    Put16(frame + 24U, (~sum) & 65535U);
}
static int Refused(const void *bytes, UmicomSize length, UmicomNetworkPacketStatus expected)
{
    UmicomEthernetIpv4View out;
    unsigned char before[sizeof out];
    memset(&out, 0xa5, sizeof out);
    memcpy(before, &out, sizeof out);
    CHECK(UmicomKernelEthernetIpv4Read(bytes, length, &out) == expected);
    CHECK(!memcmp(before, &out, sizeof out));
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *name = argv[1];
    unsigned char frame[1515];
    memset(frame, 0, sizeof frame);
    memcpy(frame, golden, sizeof golden);
    UmicomEthernetIpv4View out;
    if (!strcmp(name, "golden")) {
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(!memcmp(frame, golden, sizeof golden));
        CHECK(!memcmp(out.destinationMac, golden, 6U));
        CHECK(!memcmp(out.sourceMac, golden + 6U, 6U));
        CHECK(!memcmp(out.sourceAddress, golden + 26U, 4U));
        CHECK(!memcmp(out.destinationAddress, golden + 30U, 4U));
        CHECK(out.differentiatedServices == 0U && out.timeToLive == 64U && out.protocol == 17U);
        CHECK(out.identification == 0x1234U && out.dontFragment == UMICOM_TRUE);
        CHECK(out.ipBytes == 32U && out.payloadOffset == 34U && out.payloadBytes == 12U);
    } else if (!strcmp(name, "padding")) {
        memset(frame + sizeof golden, 0xcc, 60U - sizeof golden);
        CHECK(UmicomKernelEthernetIpv4Read(frame, 60U, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes == 12U && out.ipBytes == 32U);
    } else if (!strcmp(name, "unaligned")) {
        _Alignas(16) unsigned char storage[64];
        memcpy(storage + 1U, golden, sizeof golden);
        CHECK(UmicomKernelEthernetIpv4Read(storage + 1U, sizeof golden, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.identification == 0x1234U);
    } else if (!strcmp(name, "minimum")) {
        Put16(frame + 16U, 20U); Checksum(frame);
        CHECK(UmicomKernelEthernetIpv4Read(frame, 34U, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes == 0U && out.payloadOffset == 34U);
    } else if (!strcmp(name, "maximum")) {
        Put16(frame + 16U, 1500U); Checksum(frame);
        CHECK(UmicomKernelEthernetIpv4Read(frame, 1514U, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.payloadBytes == 1480U);
    } else if (!strcmp(name, "truncation")) {
        for (UmicomSize length = 0U; length < sizeof golden; ++length)
            CHECK(!Refused(frame, length, UMICOM_NETWORK_PACKET_LENGTH));
    } else if (!strcmp(name, "capture_limit")) {
        CHECK(!Refused(frame, 1515U, UMICOM_NETWORK_PACKET_LIMIT));
        CHECK(!Refused(frame, ~(UmicomSize)0U, UMICOM_NETWORK_PACKET_LIMIT));
    } else if (!strcmp(name, "ether_type")) {
        const unsigned types[] = {0x86ddU, 0x8100U, 0x88a8U, 0x0806U, 46U};
        for (unsigned i = 0U; i < sizeof types / sizeof types[0]; ++i) {
            Put16(frame + 12U, types[i]);
            CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_UNSUPPORTED));
        }
    } else if (!strcmp(name, "version")) {
        frame[14] = 0x65U;
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_UNSUPPORTED));
    } else if (!strcmp(name, "header_length")) {
        for (unsigned i = 0U; i < 5U; ++i) {
            frame[14] = (unsigned char)(0x40U + i);
            CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_HEADER));
        }
        frame[14] = 0x46U;
        CHECK(!Refused(frame, 34U, UMICOM_NETWORK_PACKET_LENGTH));
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_UNSUPPORTED));
    } else if (!strcmp(name, "total_length")) {
        Put16(frame + 16U, 19U);
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_HEADER));
        Put16(frame + 16U, 1501U);
        CHECK(!Refused(frame, 1514U, UMICOM_NETWORK_PACKET_LIMIT));
        Put16(frame + 16U, 33U);
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_LENGTH));
    } else if (!strcmp(name, "reserved_flag")) {
        Put16(frame + 20U, 0x8000U); Checksum(frame);
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_HEADER));
    } else if (!strcmp(name, "fragments")) {
        const unsigned flags[] = {0x2000U, 1U, 0x1fffU, 0x6000U};
        for (unsigned i = 0U; i < sizeof flags / sizeof flags[0]; ++i) {
            Put16(frame + 20U, flags[i]); Checksum(frame);
            CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_FRAGMENT));
        }
        Put16(frame + 20U, 0U); Checksum(frame);
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.dontFragment == UMICOM_FALSE);
    } else if (!strcmp(name, "checksum")) {
        frame[26] ^= 1U;
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_CHECKSUM));
        memcpy(frame, golden, sizeof golden); frame[24] ^= 1U;
        CHECK(!Refused(frame, sizeof golden, UMICOM_NETWORK_PACKET_CHECKSUM));
    } else if (!strcmp(name, "payload_policy")) {
        memset(frame + 34U, 0xff, 12U);
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden, &out) == UMICOM_NETWORK_PACKET_OK);
        frame[22] = 0U; frame[23] = 253U; frame[15] = 0xfcU; Checksum(frame);
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden, &out) == UMICOM_NETWORK_PACKET_OK);
        CHECK(out.timeToLive == 0U && out.protocol == 253U && out.differentiatedServices == 0xfcU);
    } else if (!strcmp(name, "overlap")) {
        union { unsigned char bytes[256]; UmicomEthernetIpv4View view; } storage;
        unsigned char before[sizeof storage];
        memset(&storage, 0, sizeof storage);
        memcpy(storage.bytes, golden, sizeof golden); memcpy(before, &storage, sizeof storage);
        CHECK(UmicomKernelEthernetIpv4Read(storage.bytes, sizeof golden, &storage.view) == UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before, &storage, sizeof storage));
        memcpy(storage.bytes + 8U, golden, sizeof golden); memcpy(before, &storage, sizeof storage);
        CHECK(UmicomKernelEthernetIpv4Read(storage.bytes + 8U, sizeof golden, &storage.view) == UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before, &storage, sizeof storage));
    } else if (!strcmp(name, "output_alignment")) {
        _Alignas(16) unsigned char storage[sizeof out + 16U];
        unsigned char before[sizeof storage];
        memset(storage, 0x5a, sizeof storage); memcpy(before, storage, sizeof storage);
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden,
            (UmicomEthernetIpv4View *)(void *)(storage + 1U)) == UMICOM_NETWORK_PACKET_ARGUMENT);
        CHECK(!memcmp(before, storage, sizeof storage));
    } else if (!strcmp(name, "address_wrap")) {
        const UmicomAddress maximum = ~(UmicomAddress)0U;
        CHECK(!Refused((const void *)(maximum - 15U), sizeof golden, UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden,
            (UmicomEthernetIpv4View *)(maximum - (_Alignof(UmicomEthernetIpv4View) - 1U))) == UMICOM_NETWORK_PACKET_ARGUMENT);
    } else if (!strcmp(name, "arguments")) {
        CHECK(!Refused(NULL, sizeof golden, UMICOM_NETWORK_PACKET_ARGUMENT));
        CHECK(UmicomKernelEthernetIpv4Read(frame, sizeof golden, NULL) == UMICOM_NETWORK_PACKET_ARGUMENT);
    } else return 2;
    return 0;
}
