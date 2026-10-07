/*-----------------------------------------------------------------------------
 * Umicom Kernel — host tests for the bounded firmware DTB copy adapter.
 * The original strict-reader suite is kept unchanged. These tests exercise
 * the real adapter and strict reader; the fixture builder only emits bytes.
 * Native protocol checks do not imply a successful firmware or QEMU boot.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/device_tree_firmware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); exit(1); \
} } while (0)
#define OK(expression) CHECK((expression) == UMICOM_TREE_OK)
#define BAD(expression) CHECK((expression) != UMICOM_TREE_OK)

typedef struct FirmwareFixture {
    UmicomU8 structure[4096], strings[1024], blob[8192];
    UmicomSize structureBytes, stringsBytes, bytes;
    UmicomSize padding[256], paddingCount, childPadding, firstValue;
} FirmwareFixture;

static FirmwareFixture fixture;
static UmicomU8 copyBuffer[8192], original[8192];
static UmicomKernelDeviceTreeReader reader;
static UmicomSize changed;
static const UmicomU8 binaryValue[] = {0U, 0U, 0U, 1U, 0xffU, 0U, 0U, 9U};

static void Put32(UmicomU8 *out, UmicomU32 value)
{
    out[0] = (UmicomU8)(value >> 24U); out[1] = (UmicomU8)(value >> 16U);
    out[2] = (UmicomU8)(value >> 8U); out[3] = (UmicomU8)value;
}
static UmicomU32 Get32(const UmicomU8 *in)
{
    return ((UmicomU32)in[0] << 24U) | ((UmicomU32)in[1] << 16U) |
        ((UmicomU32)in[2] << 8U) | in[3];
}
static void Token(UmicomU32 value)
{
    CHECK(fixture.structureBytes + 4U <= sizeof(fixture.structure));
    Put32(fixture.structure + fixture.structureBytes, value);
    fixture.structureBytes += 4U;
}
static void Begin(const char *name)
{
    Token(1U);
    const size_t bytes = strlen(name) + 1U;
    CHECK(fixture.structureBytes + bytes + 3U <= sizeof(fixture.structure));
    memcpy(fixture.structure + fixture.structureBytes, name, bytes);
    fixture.structureBytes += bytes;
    while (fixture.structureBytes & 3U) fixture.structure[fixture.structureBytes++] = 0U;
}
static void Property(const char *name, const void *value, UmicomSize bytes)
{
    const size_t nameBytes = strlen(name) + 1U;
    CHECK(fixture.stringsBytes + nameBytes <= sizeof(fixture.strings));
    Token(3U); Token((UmicomU32)bytes); Token((UmicomU32)fixture.stringsBytes);
    if (!fixture.firstValue) fixture.firstValue = fixture.structureBytes;
    memcpy(fixture.strings + fixture.stringsBytes, name, nameBytes);
    fixture.stringsBytes += nameBytes;
    CHECK(fixture.structureBytes + bytes + 3U <= sizeof(fixture.structure));
    if (bytes) memcpy(fixture.structure + fixture.structureBytes, value, (size_t)bytes);
    fixture.structureBytes += bytes;
    while (fixture.structureBytes & 3U) {
        CHECK(fixture.paddingCount < sizeof(fixture.padding) / sizeof(fixture.padding[0]));
        fixture.padding[fixture.paddingCount++] = fixture.structureBytes;
        fixture.structure[fixture.structureBytes++] = 0U;
    }
}
static void Finish(void)
{
    Token(9U);
    const UmicomSize structureOffset = 56U; /* Header and reservation sentinel. */
    const UmicomSize stringsOffset = structureOffset + fixture.structureBytes;
    fixture.bytes = stringsOffset + fixture.stringsBytes;
    CHECK(fixture.bytes <= sizeof(fixture.blob));
    Put32(fixture.blob, 0xd00dfeedU);
    Put32(fixture.blob + 4U, (UmicomU32)fixture.bytes);
    Put32(fixture.blob + 8U, (UmicomU32)structureOffset);
    Put32(fixture.blob + 12U, (UmicomU32)stringsOffset);
    Put32(fixture.blob + 16U, 40U);
    Put32(fixture.blob + 20U, 17U); Put32(fixture.blob + 24U, 16U);
    Put32(fixture.blob + 32U, (UmicomU32)fixture.stringsBytes);
    Put32(fixture.blob + 36U, (UmicomU32)fixture.structureBytes);
    memcpy(fixture.blob + structureOffset, fixture.structure, (size_t)fixture.structureBytes);
    memcpy(fixture.blob + stringsOffset, fixture.strings, (size_t)fixture.stringsBytes);
    for (UmicomSize i = 0U; i < fixture.paddingCount; ++i) fixture.padding[i] += structureOffset;
    fixture.firstValue += structureOffset;
    fixture.childPadding += structureOffset;
}
static void ResetOutputs(void)
{
    memset(copyBuffer, 0xa5, sizeof(copyBuffer));
    memset(&reader, 0xa6, sizeof(reader));
    changed = 999U;
}
static void Golden(void)
{
    memset(&fixture, 0, sizeof(fixture));
    Begin("");
    for (unsigned i = 0U; i < 8U; ++i) {
        char name[8]; CHECK(snprintf(name, sizeof(name), "p%u", i) > 0);
        Property(name, binaryValue, i);
    }
    Property("label", "persist", 8U);
    Begin("a"); fixture.childPadding = fixture.structureBytes - 1U;
    Property("marker", binaryValue + 4U, 1U);
    Token(2U); Token(2U); Finish(); ResetOutputs();
}
static void DirtyPadding(void)
{
    for (UmicomSize i = 0U; i < fixture.paddingCount; ++i)
        fixture.blob[fixture.padding[i]] = (UmicomU8)(0x81U + i % 100U);
}
static UmicomKernelTreeStatus Open(void)
{
    return UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        copyBuffer, sizeof(copyBuffer), &reader, &changed);
}
static void AllBytes(const void *data, UmicomSize bytes, UmicomU8 value)
{
    const UmicomU8 *p = data;
    for (UmicomSize i = 0U; i < bytes; ++i) CHECK(p[i] == value);
}
static void CheckUnpublished(void)
{
    AllBytes(&reader, sizeof(reader), 0U); CHECK(changed == 0U);
}
static void CheckSource(void)
{
    CHECK(memcmp(fixture.blob, original, sizeof(original)) == 0);
}
static void RejectCopied(void)
{
    memcpy(original, fixture.blob, sizeof(original));
    BAD(Open()); CheckUnpublished(); CheckSource();
    AllBytes(copyBuffer, fixture.bytes, 0U);
    AllBytes(copyBuffer + fixture.bytes, sizeof(copyBuffer) - fixture.bytes, 0xa5U);
}

static void TestCleanCopy(void)
{
    Golden(); memcpy(original, fixture.blob, sizeof(original));
    OK(Open()); CheckSource(); CHECK(changed == 0U);
    CHECK(reader.opened && reader.self == &reader && reader.blob == copyBuffer);
    CHECK(reader.bytes == fixture.bytes && reader.nodeCount == 2U && reader.propertyCount == 10U);
    CHECK(memcmp(copyBuffer, fixture.blob, (size_t)fixture.bytes) == 0);
    AllBytes(copyBuffer + fixture.bytes, sizeof(copyBuffer) - fixture.bytes, 0xa5U);
}
static void TestOnlyPropertyPaddingChanges(void)
{
    Golden(); DirtyPadding();
    /* Zero padding is already canonical and must not inflate the change count. */
    fixture.blob[fixture.padding[1]] = 0U;
    memcpy(original, fixture.blob, sizeof(original));
    CHECK(UmicomKernelDeviceTreeOpen(fixture.blob, fixture.bytes, &reader) == UMICOM_TREE_BAD_STRUCTURE);
    ResetOutputs(); OK(Open()); CheckSource(); CHECK(changed == fixture.paddingCount - 1U);
    for (UmicomSize i = 0U; i < fixture.bytes; ++i) {
        UmicomBoolean padding = UMICOM_FALSE;
        for (UmicomSize j = 0U; j < fixture.paddingCount; ++j)
            if (fixture.padding[j] == i) padding = UMICOM_TRUE;
        CHECK(copyBuffer[i] == (padding ? 0U : fixture.blob[i]));
    }
    UmicomKernelDeviceTreeReader strict = {0};
    OK(UmicomKernelDeviceTreeOpen(copyBuffer, fixture.bytes, &strict));
    CHECK(strict.nodeCount == reader.nodeCount && strict.propertyCount == reader.propertyCount);
    CHECK(UmicomKernelDeviceTreeOpen(fixture.blob, fixture.bytes, &strict) == UMICOM_TREE_BAD_STRUCTURE);
}
static void TestOwnedReaderLifetime(void)
{
    Golden(); DirtyPadding(); OK(Open());
    memset(fixture.blob, 0xdd, sizeof(fixture.blob));
    UmicomKernelTreeSpan span = {0};
    OK(UmicomKernelDeviceTreeProperty(&reader, 0U, "label", &span));
    CHECK(span.bytes == 8U && memcmp(span.data, "persist", 8U) == 0);
    CHECK(span.data >= copyBuffer && span.data + span.bytes <= copyBuffer + reader.bytes);
    UmicomU32 node = 999U; OK(UmicomKernelDeviceTreeFind(&reader, "/a", &node));
    OK(UmicomKernelDeviceTreeProperty(&reader, node, "marker", &span));
    CHECK(span.bytes == 1U && span.data[0] == 0xffU);
    char path[8]; OK(UmicomKernelDeviceTreePath(&reader, node, path, sizeof(path)));
    CHECK(strcmp(path, "/a") == 0);
}
static void TestPropertyDataPreserved(void)
{
    Golden(); DirtyPadding(); OK(Open());
    for (unsigned i = 0U; i < 8U; ++i) {
        char name[8]; CHECK(snprintf(name, sizeof(name), "p%u", i) > 0);
        UmicomKernelTreeSpan span = {0};
        OK(UmicomKernelDeviceTreeProperty(&reader, 0U, name, &span));
        CHECK(span.bytes == i && memcmp(span.data, binaryValue, i) == 0);
    }
}
static void TestUnalignedInputAndCopy(void)
{
    Golden(); DirtyPadding();
    UmicomU8 *source = malloc((size_t)fixture.bytes + 1U);
    UmicomU8 *owned = malloc((size_t)fixture.bytes + 2U);
    CHECK(source && owned); memcpy(source + 1U, fixture.blob, (size_t)fixture.bytes);
    memset(owned, 0x75, (size_t)fixture.bytes + 2U);
    OK(UmicomKernelDeviceTreeOpenFirmwareCopy(source + 1U, fixture.bytes,
        owned + 1U, fixture.bytes, &reader, &changed));
    CHECK(reader.blob == owned + 1U && changed == fixture.paddingCount);
    CHECK(owned[0] == 0x75U && owned[fixture.bytes + 1U] == 0x75U);
    CHECK(memcmp(source + 1U, fixture.blob, (size_t)fixture.bytes) == 0);
    free(source); free(owned);
}
static void TestExactCapacity(void)
{
    Golden(); DirtyPadding();
    OK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        copyBuffer, fixture.bytes, &reader, &changed));
    CHECK(changed == fixture.paddingCount);
    ResetOutputs();
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        copyBuffer, fixture.bytes - 1U, &reader, &changed) == UMICOM_TREE_OUTSIDE_BUFFER);
    CheckUnpublished(); AllBytes(copyBuffer, sizeof(copyBuffer), 0xa5U);
}
static void TestAccessibleTailIsNotCopied(void)
{
    Golden(); DirtyPadding();
    memset(fixture.blob + fixture.bytes, 0x6d, sizeof(fixture.blob) - (size_t)fixture.bytes);
    memcpy(original, fixture.blob, sizeof(original));
    OK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, sizeof(fixture.blob),
        copyBuffer, sizeof(copyBuffer), &reader, &changed));
    CheckSource(); CHECK(reader.bytes == fixture.bytes);
    AllBytes(copyBuffer + fixture.bytes, sizeof(copyBuffer) - fixture.bytes, 0xa5U);
}
static void TestEveryTruncatedPrefix(void)
{
    Golden(); DirtyPadding();
    for (UmicomSize bytes = 0U; bytes < fixture.bytes; ++bytes) {
        UmicomU8 *shortInput = malloc(bytes ? (size_t)bytes : 1U); CHECK(shortInput);
        if (bytes) memcpy(shortInput, fixture.blob, (size_t)bytes);
        ResetOutputs();
        BAD(UmicomKernelDeviceTreeOpenFirmwareCopy(shortInput, bytes, copyBuffer,
            sizeof(copyBuffer), &reader, &changed));
        CheckUnpublished(); AllBytes(copyBuffer, sizeof(copyBuffer), 0xa5U);
        if (bytes) CHECK(memcmp(shortInput, fixture.blob, (size_t)bytes) == 0);
        free(shortInput);
    }
}
static void TestNullAndPointerOverflow(void)
{
    Golden(); memcpy(original, fixture.blob, sizeof(original));
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(0, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, 0,
        sizeof(copyBuffer), &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), 0, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), &reader, 0) == UMICOM_TREE_INVALID_ARGUMENT);
    const UmicomUIntPtr nearEnd = ~(UmicomUIntPtr)0U - 3U;
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy((const void *)nearEnd, 8U, copyBuffer,
        sizeof(copyBuffer), &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, (void *)nearEnd,
        8U, &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    const UmicomUIntPtr alignedNearEnd = ~(UmicomUIntPtr)0U - 7U;
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), (UmicomKernelDeviceTreeReader *)alignedNearEnd, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), &reader, (UmicomSize *)alignedNearEnd) == UMICOM_TREE_INVALID_ARGUMENT);
    _Alignas(UmicomKernelDeviceTreeReader) UmicomU8 ownerStorage[sizeof(reader) + 8U];
    memset(ownerStorage, 0x59, sizeof(ownerStorage));
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), (UmicomKernelDeviceTreeReader *)(void *)(ownerStorage + 1U),
        &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes, copyBuffer,
        sizeof(copyBuffer), &reader, (UmicomSize *)(void *)(ownerStorage + 1U)) == UMICOM_TREE_INVALID_ARGUMENT);
    AllBytes(ownerStorage, sizeof(ownerStorage), 0x59U);
    CheckSource(); AllBytes(&reader, sizeof(reader), 0xa6U);
    AllBytes(copyBuffer, sizeof(copyBuffer), 0xa5U); CHECK(changed == 999U);
}
static void TestPairwiseOwnershipOverlap(void)
{
    Golden(); memcpy(original, fixture.blob, sizeof(original));
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        fixture.blob + 1U, fixture.bytes, &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        copyBuffer, sizeof(copyBuffer), (UmicomKernelDeviceTreeReader *)(void *)fixture.blob,
        &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        copyBuffer, sizeof(copyBuffer), &reader, (UmicomSize *)(void *)fixture.blob) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        &reader, sizeof(reader), &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        &changed, sizeof(changed), &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, fixture.bytes,
        copyBuffer, sizeof(copyBuffer), &reader, (UmicomSize *)(void *)&reader) == UMICOM_TREE_INVALID_ARGUMENT);
    /* Ownership covers declared capacity, including unused bytes beyond total. */
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(fixture.blob, sizeof(fixture.blob),
        fixture.blob + fixture.bytes, 8U, &reader, &changed) == UMICOM_TREE_INVALID_ARGUMENT);
    CheckSource(); AllBytes(&reader, sizeof(reader), 0xa6U);
    AllBytes(copyBuffer, sizeof(copyBuffer), 0xa5U); CHECK(changed == 999U);
}
static void TestNodePaddingRemainsStrict(void)
{
    Golden(); DirtyPadding(); fixture.blob[fixture.childPadding] = 0x71U;
    CHECK(UmicomKernelDeviceTreeOpen(fixture.blob, fixture.bytes, &reader) == UMICOM_TREE_BAD_STRUCTURE);
    ResetOutputs(); RejectCopied();
}
static void TestFinalValidationAndWipe(void)
{
    Golden(); DirtyPadding();
    /* A duplicate name is valid to walk, but the original reader must reject it. */
    const UmicomSize secondName = fixture.firstValue + 8U;
    Put32(fixture.blob + secondName, 0U); RejectCopied();
}
static void TestFailureReplacesPriorReader(void)
{
    Golden(); DirtyPadding(); OK(Open()); CHECK(reader.opened);
    fixture.blob[fixture.childPadding] = 1U;
    BAD(Open()); CheckUnpublished(); AllBytes(copyBuffer, fixture.bytes, 0U);
}
static void TestBadHeaderAndSizes(void)
{
    static const struct { UmicomSize offset; UmicomU32 value; } mutations[] = {
        {0U, 0U}, {4U, 0U}, {4U, 39U}, {4U, 0xffffffffU},
        {20U, 16U}, {24U, 18U}, {20U, 0U}
    };
    for (size_t i = 0U; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        Golden(); Put32(fixture.blob + mutations[i].offset, mutations[i].value);
        memcpy(original, fixture.blob, sizeof(original)); BAD(Open());
        CheckUnpublished(); CheckSource();
        AllBytes(copyBuffer + fixture.bytes, sizeof(copyBuffer) - fixture.bytes, 0xa5U);
    }
}
static void TestStructureBounds(void)
{
    static const struct { UmicomSize offset; UmicomU32 value; } mutations[] = {
        {8U, 0U}, {8U, 40U}, {8U, 57U}, {8U, 0xfffffffcU},
        {36U, 0U}, {36U, 8U}, {36U, 13U}, {36U, 0xfffffffcU},
        {12U, 39U}, {12U, 0xffffffffU}, {32U, 0xffffffffU}
    };
    for (size_t i = 0U; i < sizeof(mutations) / sizeof(mutations[0]); ++i) {
        Golden(); Put32(fixture.blob + mutations[i].offset, mutations[i].value);
        memcpy(original, fixture.blob, sizeof(original)); BAD(Open());
        CheckUnpublished(); CheckSource();
        AllBytes(copyBuffer + fixture.bytes, sizeof(copyBuffer) - fixture.bytes, 0xa5U);
    }
}
static void TestBlockOverlap(void)
{
    Golden(); DirtyPadding(); Put32(fixture.blob + 12U, 56U);
    memcpy(original, fixture.blob, sizeof(original)); BAD(Open()); CheckUnpublished(); CheckSource();
    Golden(); DirtyPadding(); Put32(fixture.blob + 16U, 56U);
    memcpy(original, fixture.blob, sizeof(original)); BAD(Open()); CheckUnpublished(); CheckSource();
    Golden(); Put32(fixture.blob + 16U, 32U);
    memcpy(original, fixture.blob, sizeof(original)); BAD(Open()); CheckUnpublished(); CheckSource();
}
static void TestMalformedTokensAndNames(void)
{
    Golden(); DirtyPadding(); Put32(fixture.blob + 56U, 7U); RejectCopied();
    Golden(); DirtyPadding(); Put32(fixture.blob + 56U, 2U); RejectCopied();
    Golden(); DirtyPadding();
    Put32(fixture.blob + 56U + fixture.structureBytes - 4U, 4U); RejectCopied();
    Golden(); DirtyPadding();
    memset(fixture.blob + 60U, 'a', (size_t)fixture.structureBytes - 4U); RejectCopied();
    Golden(); DirtyPadding();
    Put32(fixture.blob + fixture.firstValue - 4U, 0xffffffffU); RejectCopied();
}
static void TestMalformedPropertyExtent(void)
{
    Golden(); DirtyPadding(); Put32(fixture.blob + fixture.firstValue - 8U, 0xffffffffU); RejectCopied();
    Golden(); DirtyPadding();
    Put32(fixture.blob + fixture.firstValue - 8U,
        (UmicomU32)(56U + fixture.structureBytes - fixture.firstValue)); RejectCopied();
    Golden(); DirtyPadding();
    Put32(fixture.blob + 56U + fixture.structureBytes - 4U, 3U); RejectCopied();
}
static void TestReservationValidation(void)
{
    Golden(); DirtyPadding(); fixture.blob[47] = 1U; RejectCopied();
    Golden(); DirtyPadding();
    Put32(fixture.blob + 16U, (UmicomU32)(fixture.bytes - 8U));
    memcpy(original, fixture.blob, sizeof(original)); BAD(Open()); CheckUnpublished(); CheckSource();
}
static void TestBlobLimitAndMaximum(void)
{
    Golden(); DirtyPadding();
    const size_t bytes = (size_t)UMICOM_TREE_MAX_BYTES + 1U;
    UmicomU8 *source = malloc(bytes), *owned = malloc(bytes); CHECK(source && owned);
    memset(source, 0x79, bytes); memcpy(source, fixture.blob, (size_t)fixture.bytes);
    Put32(source + 4U, UMICOM_TREE_MAX_BYTES);
    memset(owned, 0xa5, bytes);
    OK(UmicomKernelDeviceTreeOpenFirmwareCopy(source, bytes, owned, bytes, &reader, &changed));
    CHECK(reader.bytes == UMICOM_TREE_MAX_BYTES && owned[UMICOM_TREE_MAX_BYTES] == 0xa5U);
    CHECK(owned[UMICOM_TREE_MAX_BYTES - 1U] == 0x79U);
    Put32(source + 4U, UMICOM_TREE_MAX_BYTES + 1U); ResetOutputs(); memset(owned, 0xa5, bytes);
    CHECK(UmicomKernelDeviceTreeOpenFirmwareCopy(source, bytes, owned, bytes, &reader, &changed) == UMICOM_TREE_LIMIT);
    CheckUnpublished(); AllBytes(owned, bytes, 0xa5U); free(source); free(owned);
}
static void TestFutureCompatibleVersion(void)
{
    Golden(); DirtyPadding(); Put32(fixture.blob + 20U, 20U); Put32(fixture.blob + 24U, 17U);
    OK(Open()); CHECK(changed == fixture.paddingCount);
}

typedef void (*FirmwareTest)(void);
typedef struct FirmwareCase { const char *name; FirmwareTest run; } FirmwareCase;
static const FirmwareCase cases[] = {
    {"clean_copy", TestCleanCopy}, {"only_property_padding_changes", TestOnlyPropertyPaddingChanges},
    {"owned_reader_lifetime", TestOwnedReaderLifetime}, {"property_data_preserved", TestPropertyDataPreserved},
    {"unaligned_input_and_copy", TestUnalignedInputAndCopy}, {"exact_capacity", TestExactCapacity},
    {"accessible_tail_not_copied", TestAccessibleTailIsNotCopied}, {"every_truncated_prefix", TestEveryTruncatedPrefix},
    {"null_and_pointer_overflow", TestNullAndPointerOverflow}, {"pairwise_ownership_overlap", TestPairwiseOwnershipOverlap},
    {"node_padding_remains_strict", TestNodePaddingRemainsStrict}, {"final_validation_and_wipe", TestFinalValidationAndWipe},
    {"failure_replaces_prior_reader", TestFailureReplacesPriorReader}, {"bad_header_and_sizes", TestBadHeaderAndSizes},
    {"structure_bounds", TestStructureBounds}, {"block_overlap", TestBlockOverlap},
    {"malformed_tokens_and_names", TestMalformedTokensAndNames}, {"malformed_property_extent", TestMalformedPropertyExtent},
    {"reservation_validation", TestReservationValidation}, {"blob_limit_and_maximum", TestBlobLimitAndMaximum},
    {"future_compatible_version", TestFutureCompatibleVersion}
};

/* Optional probe of the supplied QEMU 8 fixture. No binary is copied into the source
 * tree, and normal CTest cases remain deterministic and self-contained. */
static void TestFirmwareFile(const char *path)
{
    FILE *file = fopen(path, "rb"); CHECK(file);
    CHECK(fseek(file, 0L, SEEK_END) == 0); const long length = ftell(file);
    CHECK(length > 0L && (UmicomSize)length <= UMICOM_TREE_MAX_BYTES);
    CHECK(fseek(file, 0L, SEEK_SET) == 0);
    UmicomU8 *source = malloc((size_t)length), *owned = malloc((size_t)length);
    UmicomU8 *saved = malloc((size_t)length); CHECK(source && owned && saved);
    CHECK(fread(source, 1U, (size_t)length, file) == (size_t)length); CHECK(fclose(file) == 0);
    memcpy(saved, source, (size_t)length);
    CHECK(UmicomKernelDeviceTreeOpen(source, (UmicomSize)length, &reader) == UMICOM_TREE_BAD_STRUCTURE);
    OK(UmicomKernelDeviceTreeOpenFirmwareCopy(source, (UmicomSize)length, owned,
        (UmicomSize)length, &reader, &changed));
    CHECK(changed == 26U && reader.nodeCount == 30U && reader.propertyCount == 117U);
    CHECK(reader.blob == owned && memcmp(source, saved, (size_t)length) == 0);
    UmicomSize differences = 0U;
    for (UmicomSize i = 0U; i < reader.bytes; ++i) {
        if (owned[i] == source[i]) continue;
        CHECK(owned[i] == 0U && source[i] != 0U);
        UmicomBoolean padding = UMICOM_FALSE;
        for (UmicomU32 j = 0U; j < reader.propertyCount; ++j) {
            const UmicomKernelTreeProperty *p = &reader.properties[j];
            const UmicomSize end = (UmicomSize)p->valueOffset + p->bytes;
            if (i >= end && i < ((end + 3U) & ~(UmicomSize)3U)) padding = UMICOM_TRUE;
        }
        CHECK(padding); ++differences;
    }
    CHECK(differences == changed);
    UmicomKernelDeviceTreeReader strict = {0};
    OK(UmicomKernelDeviceTreeOpen(owned, reader.bytes, &strict));
    CHECK(Get32(source + 4U) == reader.bytes);
    printf("firmware-file: changed=%llu nodes=%u properties=%u\n", changed, reader.nodeCount, reader.propertyCount);
    free(saved); free(source); free(owned);
}
int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "firmware_file") == 0) { TestFirmwareFile(argv[2]); return 0; }
    CHECK(argc == 2);
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (strcmp(argv[1], cases[i].name) == 0) { cases[i].run(); return 0; }
    fprintf(stderr, "Unknown firmware-copy case: %s\n", argv[1]); return 2;
}
