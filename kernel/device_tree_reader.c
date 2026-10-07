/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/device_tree_reader.c
 *
 * A DTB is a byte protocol, not an array of native C structures. Every integer
 * is decoded after its enclosing span has been checked. The explicit stack
 * bounds nesting; fixed tables bound nodes, properties and duplicate searches.
 * No address stored inside the input is dereferenced. Reservation addresses
 * describe firmware ownership and are retained only as integer observations.
 *
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/device_tree_reader.h"
#include "umicom/kernel/device_tree.h"

static void UmicomTreeClear(void *memory, UmicomSize bytes)
{
    /* Volatile stores keep a freestanding compiler from emitting a libc call. */
    volatile UmicomU8 *out = (volatile UmicomU8 *)memory;
    for (UmicomSize i = 0U; i < bytes; ++i) out[i] = 0U;
}
static UmicomU32 UmicomTreeRead32(const UmicomU8 *p)
{
    return ((UmicomU32)p[0] << 24U) | ((UmicomU32)p[1] << 16U) |
        ((UmicomU32)p[2] << 8U) | (UmicomU32)p[3];
}
static UmicomU64 UmicomTreeRead64(const UmicomU8 *p)
{
    return ((UmicomU64)UmicomTreeRead32(p) << 32U) | UmicomTreeRead32(p + 4U);
}
static UmicomBoolean UmicomTreeFits(UmicomSize start, UmicomSize bytes, UmicomSize limit)
{
    /* Subtraction is safe after the start check; start+bytes need not wrap. */
    return start <= limit && bytes <= limit - start ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomTreeOverlap(UmicomSize a, UmicomSize an, UmicomSize b, UmicomSize bn)
{
    /* Callers already proved both intervals fit the same bounded blob. */
    return an && bn && a < b + bn && b < a + an ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomBoolean UmicomTreeEqual(const UmicomU8 *a, UmicomSize an, const UmicomU8 *b, UmicomSize bn)
{
    if (an != bn) return UMICOM_FALSE;
    for (UmicomSize i = 0U; i < an; ++i) if (a[i] != b[i]) return UMICOM_FALSE;
    return UMICOM_TRUE;
}
static UmicomBoolean UmicomTreeIdentifierByte(UmicomU8 c, UmicomBoolean node)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == ',' || c == '.' || c == '_' || c == '+' || c == '-' ||
        (node ? c == '@' : (c == '#' || c == '?')) ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelTreeStatus UmicomTreeName(const UmicomU8 *blob, UmicomSize start,
    UmicomSize end, UmicomSize maximum, UmicomBoolean node, UmicomU32 *length)
{
    for (UmicomSize i = 0U; i <= maximum; ++i) {
        if (start >= end || i >= end - start) return UMICOM_TREE_BAD_STRUCTURE;
        if (!blob[start + i]) { *length = (UmicomU32)i; return UMICOM_TREE_OK; }
        if (!UmicomTreeIdentifierByte(blob[start + i], node)) return UMICOM_TREE_BAD_VALUE;
    }
    return UMICOM_TREE_LIMIT;
}
static UmicomBoolean UmicomTreeReady(const UmicomKernelDeviceTreeReader *r)
{
    return r && r->self == r && r->opened && r->blob && r->bytes <= UMICOM_TREE_MAX_BYTES &&
        r->nodeCount && r->nodeCount <= UMICOM_TREE_NODE_LIMIT &&
        r->propertyCount <= UMICOM_TREE_PROPERTY_LIMIT ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelTreeStatus UmicomTreeParse(UmicomKernelDeviceTreeReader *r, UmicomSize available)
{
    const UmicomU8 *b = r->blob;
    if (available < UMICOM_KERNEL_FDT_MINIMUM_HEADER_BYTES) return UMICOM_TREE_OUTSIDE_BUFFER;
    if (UmicomTreeRead32(b) != UMICOM_KERNEL_FDT_MAGIC) return UMICOM_TREE_BAD_HEADER;
    /* totalsize includes every block and free-space gap. First bound the whole
     * object; only then interpret the offsets that select its internal spans. */
    const UmicomU32 total = UmicomTreeRead32(b + 4U);
    if (total < 40U || total > available) return UMICOM_TREE_OUTSIDE_BUFFER;
    if (total > UMICOM_TREE_MAX_BYTES) return UMICOM_TREE_LIMIT;
    const UmicomU32 structure = UmicomTreeRead32(b + 8U);
    const UmicomU32 strings = UmicomTreeRead32(b + 12U);
    const UmicomU32 reserve = UmicomTreeRead32(b + 16U);
    const UmicomU32 format = UmicomTreeRead32(b + 20U);
    const UmicomU32 compatible = UmicomTreeRead32(b + 24U);
    const UmicomU32 stringsBytes = UmicomTreeRead32(b + 32U);
    const UmicomU32 structureBytes = UmicomTreeRead32(b + 36U);
    if (format < 17U || compatible > 17U) return UMICOM_TREE_UNSUPPORTED_FORMAT;
    if (compatible > format || structure < 40U || strings < 40U || reserve < 40U ||
        (structure & 3U) || (reserve & 7U) || (structureBytes & 3U) || structureBytes < 12U)
        return UMICOM_TREE_BAD_HEADER;
    if (!UmicomTreeFits(structure, structureBytes, total) || !UmicomTreeFits(strings, stringsBytes, total) ||
        !UmicomTreeFits(reserve, 16U, total)) return UMICOM_TREE_OUTSIDE_BUFFER;
    if (UmicomTreeOverlap(structure, structureBytes, strings, stringsBytes)) return UMICOM_TREE_BAD_HEADER;
    r->bytes = total;

    /* The reservation table has a terminator, not a length field. Check each
     * pair against every other block before reading it, including its sentinel. */
    UmicomSize pos = reserve;
    for (;;) {
        if (!UmicomTreeFits(pos, 16U, total)) return UMICOM_TREE_OUTSIDE_BUFFER;
        if (UmicomTreeOverlap(pos, 16U, structure, structureBytes) ||
            UmicomTreeOverlap(pos, 16U, strings, stringsBytes)) return UMICOM_TREE_BAD_HEADER;
        const UmicomU64 address = UmicomTreeRead64(b + pos);
        const UmicomU64 bytes = UmicomTreeRead64(b + pos + 8U);
        pos += 16U;
        if (!address && !bytes) break;
        if (!bytes || address > ~(UmicomU64)0U - bytes) return UMICOM_TREE_BAD_VALUE;
        if (r->reservationCount == UMICOM_TREE_RESERVATION_LIMIT) return UMICOM_TREE_LIMIT;
        for (UmicomU32 i = 0U; i < r->reservationCount; ++i) {
            const UmicomKernelTreeReservation *old = &r->reservations[i];
            if (address < old->address + old->bytes && old->address < address + bytes)
                return UMICOM_TREE_BAD_VALUE;
        }
        r->reservations[r->reservationCount++] = (UmicomKernelTreeReservation){address, bytes};
    }

    /* The index is preorder: a parent is always present before its child.
     * Properties stay contiguous because the grammar forbids adding another
     * property after the first child node has begun. The separate children
     * flags preserve that rule even after we return from a deeply nested node. */
    UmicomU32 stack[UMICOM_TREE_DEPTH_LIMIT];

    UmicomTreeClear(stack, sizeof(stack));
    UmicomBoolean children[UMICOM_TREE_NODE_LIMIT];
    UmicomTreeClear(children, sizeof(children));
    UmicomU32 depth = 0U;
    const UmicomSize end = (UmicomSize)structure + structureBytes;
    const UmicomSize stringsEnd = (UmicomSize)strings + stringsBytes;
    pos = structure;
    while (pos < end) {
        if (!UmicomTreeFits(pos, 4U, end)) return UMICOM_TREE_BAD_STRUCTURE;
        const UmicomU32 token = UmicomTreeRead32(b + pos);
        pos += 4U;
        if (token == 4U) continue; /* NOP has no payload and cannot change depth. */
        if (token == 1U) {
            if ((!depth && r->nodeCount) || depth == UMICOM_TREE_DEPTH_LIMIT ||
                r->nodeCount == UMICOM_TREE_NODE_LIMIT) return UMICOM_TREE_LIMIT;
            UmicomU32 length = 0U;
            const UmicomKernelTreeStatus named = UmicomTreeName(b, pos, end,
                UMICOM_TREE_NAME_LIMIT, UMICOM_TRUE, &length);
            if (named != UMICOM_TREE_OK) return named;
            if ((depth == 0U && length) || (depth != 0U && !length)) return UMICOM_TREE_BAD_STRUCTURE;
            const UmicomU32 parent = depth ? stack[depth - 1U] : UMICOM_TREE_NO_NODE;
            for (UmicomU32 i = 0U; i < r->nodeCount; ++i)
                if (r->nodes[i].parent == parent && UmicomTreeEqual(b + r->nodes[i].nameOffset,
                        r->nodes[i].nameBytes, b + pos, length)) return UMICOM_TREE_DUPLICATE;
            const UmicomU32 index = r->nodeCount++;
            r->nodes[index] = (UmicomKernelTreeNode){parent, (UmicomU32)pos, length, r->propertyCount, 0U};
            if (depth) children[parent] = UMICOM_TRUE;
            stack[depth++] = index;
            pos += (UmicomSize)length + 1U;
        } else if (token == 2U) {
            if (!depth) return UMICOM_TREE_BAD_STRUCTURE;
            --depth;
            continue;
        } else if (token == 3U) {
            /* Property names live in a different block from their values.
             * Check the name offset and the value length independently; a
             * valid name never makes an out-of-bounds value safe to read. */
            if (!depth || children[stack[depth - 1U]]) return UMICOM_TREE_BAD_STRUCTURE;
            if (!UmicomTreeFits(pos, 8U, end)) return UMICOM_TREE_BAD_STRUCTURE;
            const UmicomU32 bytes = UmicomTreeRead32(b + pos);
            const UmicomU32 name = UmicomTreeRead32(b + pos + 4U);
            pos += 8U;
            if (!UmicomTreeFits(pos, bytes, end) || name >= stringsBytes) return UMICOM_TREE_OUTSIDE_BUFFER;
            UmicomU32 length = 0U;
            const UmicomKernelTreeStatus named = UmicomTreeName(b, (UmicomSize)strings + name,
                stringsEnd, UMICOM_TREE_PROPERTY_NAME_LIMIT, UMICOM_FALSE, &length);
            if (named != UMICOM_TREE_OK) return named;
            if (!length) return UMICOM_TREE_BAD_STRUCTURE;
            UmicomKernelTreeNode *node = &r->nodes[stack[depth - 1U]];
            for (UmicomU32 i = 0U; i < node->propertyCount; ++i) {
                const UmicomKernelTreeProperty *old = &r->properties[node->firstProperty + i];
                if (UmicomTreeEqual(b + old->nameOffset, old->nameBytes, b + strings + name, length))
                    return UMICOM_TREE_DUPLICATE;
            }
            if (r->propertyCount == UMICOM_TREE_PROPERTY_LIMIT) return UMICOM_TREE_LIMIT;
            r->properties[r->propertyCount++] = (UmicomKernelTreeProperty){strings + name, length, (UmicomU32)pos, bytes};
            ++node->propertyCount;
            pos += bytes;
        } else if (token == 9U) {
            /* END must follow one closed root and be the final structure token. */
            return !depth && r->nodeCount && pos == end ? UMICOM_TREE_OK : UMICOM_TREE_BAD_STRUCTURE;
        } else return UMICOM_TREE_BAD_STRUCTURE;

        /* Padding belongs to the encoding and must not hide another token. */
        while (pos & 3U) {
            if (pos >= end || b[pos] != 0U) return UMICOM_TREE_BAD_STRUCTURE;
            ++pos;
        }
    }
    return UMICOM_TREE_BAD_STRUCTURE; /* A size bound is not an END token. */
}

UmicomKernelTreeStatus UmicomKernelDeviceTreeOpen(const void *blob, UmicomSize available,
    UmicomKernelDeviceTreeReader *r)
{
    if (!blob || !r) return UMICOM_TREE_INVALID_ARGUMENT;
    const UmicomUIntPtr start = (UmicomUIntPtr)blob, output = (UmicomUIntPtr)r;
    if (available > ~(UmicomUIntPtr)0U - start || sizeof(*r) > ~(UmicomUIntPtr)0U - output ||
        (start < output + sizeof(*r) && output < start + available)) return UMICOM_TREE_INVALID_ARGUMENT;
    UmicomTreeClear(r, sizeof(*r));
    r->blob = (const UmicomU8 *)blob;
    const UmicomKernelTreeStatus status = UmicomTreeParse(r, available);
    if (status != UMICOM_TREE_OK) { UmicomTreeClear(r, sizeof(*r)); return status; }
    r->self = r;
    r->opened = UMICOM_TRUE; /* Publish only a completely indexed tree. */
    return UMICOM_TREE_OK;
}

UmicomKernelTreeStatus UmicomKernelDeviceTreeProperty(const UmicomKernelDeviceTreeReader *r,
    UmicomU32 index, const char *name, UmicomKernelTreeSpan *out)
{
    if (!UmicomTreeReady(r) || index >= r->nodeCount || !name || !out) return UMICOM_TREE_INVALID_ARGUMENT;
    UmicomSize length = 0U;
    while (length <= UMICOM_TREE_PROPERTY_NAME_LIMIT && name[length]) ++length;
    if (!length || length > UMICOM_TREE_PROPERTY_NAME_LIMIT) return UMICOM_TREE_INVALID_ARGUMENT;
    const UmicomKernelTreeNode *node = &r->nodes[index];
    for (UmicomU32 i = 0U; i < node->propertyCount; ++i) {
        const UmicomKernelTreeProperty *p = &r->properties[node->firstProperty + i];
        if (UmicomTreeEqual(r->blob + p->nameOffset, p->nameBytes, (const UmicomU8 *)name, length)) {
            *out = (UmicomKernelTreeSpan){r->blob + p->valueOffset, p->bytes};
            return UMICOM_TREE_OK;
        }
    }
    return UMICOM_TREE_NOT_FOUND; /* Failure does not publish an invented value. */
}
UmicomKernelTreeStatus UmicomKernelDeviceTreePath(const UmicomKernelDeviceTreeReader *r,
    UmicomU32 node, char *out, UmicomSize capacity)
{
    if (!UmicomTreeReady(r) || node >= r->nodeCount || !out) return UMICOM_TREE_INVALID_ARGUMENT;
    UmicomU32 chain[UMICOM_TREE_DEPTH_LIMIT];
    UmicomU32 depth = 0U;
    UmicomU32 cursor = node;
    while (cursor != 0U) {
        if (depth == UMICOM_TREE_DEPTH_LIMIT || cursor >= r->nodeCount) return UMICOM_TREE_BAD_STRUCTURE;
        chain[depth++] = cursor;
        const UmicomU32 parent = r->nodes[cursor].parent;
        if (parent >= cursor) return UMICOM_TREE_BAD_STRUCTURE;
        cursor = parent;
    }
    char path[UMICOM_TREE_PATH_BYTES];
    UmicomTreeClear(path, sizeof(path));
    UmicomSize used = 0U;
    if (!depth) path[used++] = '/';
    while (depth) {
        const UmicomKernelTreeNode *n = &r->nodes[chain[--depth]];
        if ((UmicomSize)n->nameBytes + 1U >= sizeof(path) - used) return UMICOM_TREE_LIMIT;
        path[used++] = '/';
        for (UmicomU32 i = 0U; i < n->nameBytes; ++i) path[used++] = (char)r->blob[n->nameOffset + i];
    }
    if (capacity <= used) return UMICOM_TREE_LIMIT;
    for (UmicomSize i = 0U; i <= used; ++i) out[i] = path[i];
    return UMICOM_TREE_OK;
}
UmicomKernelTreeStatus UmicomKernelDeviceTreeFind(const UmicomKernelDeviceTreeReader *r,
    const char *path, UmicomU32 *out)
{
    if (!UmicomTreeReady(r) || !path || !out || path[0] != '/') return UMICOM_TREE_INVALID_ARGUMENT;
    UmicomSize length = 0U;
    while (length < UMICOM_TREE_PATH_BYTES && path[length]) ++length;
    if (length == UMICOM_TREE_PATH_BYTES) return UMICOM_TREE_LIMIT;
    /* Exact paths keep ambiguity out of this small discovery interface.
     * Alias expansion and omitted unit-address matching need separate rules;
     * silently applying either here could select a different peripheral. */
    for (UmicomU32 i = 0U; i < r->nodeCount; ++i) {
        char candidate[UMICOM_TREE_PATH_BYTES];
        UmicomTreeClear(candidate, sizeof(candidate));
        const UmicomKernelTreeStatus status = UmicomKernelDeviceTreePath(r, i, candidate, sizeof(candidate));
        if (status != UMICOM_TREE_OK) return status;
        UmicomSize n = 0U; while (candidate[n]) ++n;
        if (UmicomTreeEqual((const UmicomU8 *)path, length, (const UmicomU8 *)candidate, n)) {
            *out = i; return UMICOM_TREE_OK;
        }
    }
    return UMICOM_TREE_NOT_FOUND;
}
UmicomKernelTreeStatus UmicomKernelTreeU32(UmicomKernelTreeSpan span, UmicomU32 *out)
{
    if (!span.data || !out || span.bytes != 4U) return UMICOM_TREE_BAD_VALUE;
    *out = UmicomTreeRead32(span.data); return UMICOM_TREE_OK;
}
UmicomKernelTreeStatus UmicomKernelTreeString(UmicomKernelTreeSpan span, char *out, UmicomSize capacity)
{
    if (!span.data || !span.bytes || !out) return UMICOM_TREE_BAD_VALUE;
    if (span.bytes > capacity) return UMICOM_TREE_LIMIT;
    if (span.data[span.bytes - 1U] != 0U) return UMICOM_TREE_BAD_VALUE;
    for (UmicomSize i = 0U; i + 1U < span.bytes; ++i)
        if (span.data[i] < 32U || span.data[i] > 126U) return UMICOM_TREE_BAD_VALUE;
    for (UmicomSize i = 0U; i < span.bytes; ++i) out[i] = (char)span.data[i];
    return UMICOM_TREE_OK;
}
UmicomKernelTreeStatus UmicomKernelTreeStringListContains(UmicomKernelTreeSpan span,
    const char *text, UmicomBoolean *out)
{
    if (!span.data || !span.bytes || !text || !out) return UMICOM_TREE_BAD_VALUE;
    UmicomSize wanted = 0U;
    while (wanted < UMICOM_TREE_PATH_BYTES && text[wanted]) ++wanted;
    if (!wanted || wanted == UMICOM_TREE_PATH_BYTES) return UMICOM_TREE_BAD_VALUE;
    UmicomSize pos = 0U;
    UmicomBoolean found = UMICOM_FALSE;
    while (pos < span.bytes) {
        const UmicomSize start = pos;
        while (pos < span.bytes && span.data[pos]) {
            if (span.data[pos] < 32U || span.data[pos] > 126U) return UMICOM_TREE_BAD_VALUE;
            ++pos;
        }
        if (pos == start || pos == span.bytes) return UMICOM_TREE_BAD_VALUE;
        if (UmicomTreeEqual(span.data + start, pos - start, (const UmicomU8 *)text, wanted)) found = UMICOM_TRUE;
        ++pos;
    }
    *out = found; /* Validate the entire list even if the first item matched. */
    return UMICOM_TREE_OK;
}
const char *UmicomKernelTreeStatusName(UmicomKernelTreeStatus status)
{
    switch (status) {
    case UMICOM_TREE_OK: return "ok";
    case UMICOM_TREE_INVALID_ARGUMENT: return "invalid-argument";
    case UMICOM_TREE_BAD_HEADER: return "bad-header";
    case UMICOM_TREE_UNSUPPORTED_FORMAT: return "unsupported-format";
    case UMICOM_TREE_OUTSIDE_BUFFER: return "outside-buffer";
    case UMICOM_TREE_LIMIT: return "capacity-limit";
    case UMICOM_TREE_BAD_STRUCTURE: return "bad-structure";
    case UMICOM_TREE_DUPLICATE: return "duplicate";
    case UMICOM_TREE_BAD_VALUE: return "bad-value";
    case UMICOM_TREE_NOT_FOUND: return "not-found";
    case UMICOM_TREE_UNTRANSLATED: return "untranslated";
    case UMICOM_TREE_UNSUPPORTED_CELLS: return "unsupported-cells";
    default: return "unknown-status";
    }
}
