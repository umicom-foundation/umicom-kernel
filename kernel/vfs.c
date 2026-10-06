/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/vfs.c
 *
 * PURPOSE:
 *   Resolve bounded paths and manage client-local descriptors over a typed
 *   filesystem provider. Storage allocation belongs to that provider.
 *
 * EDUCATIONAL OVERVIEW:
 *   A descriptor grants operations on one already-open object. Namespace
 *   authority is separate: being able to read a file does not grant permission
 *   to remove its name. Multiple descriptors may share one description, whose
 *   position survives until the last reference closes it.
 *
 *   The fixed tables make capacity and cleanup visible before a general process
 *   descriptor service exists. This layer does not add a syscall or trust a
 *   program-supplied client pointer. The future syscall adapter must derive its
 *   client from execution state and validate user buffers before these calls.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/vfs.h"
#include "vfs_internal.h"

typedef struct UmicomVfsPath {
    UmicomSize count;
    char names[UMICOM_VFS_PATH_COMPONENTS][UMICOM_VFS_NAME_BYTES];
} UmicomVfsPath;
static UmicomKernelVfsStatus UmicomVfsParse(const char *path, UmicomVfsPath *parsed)
{
    if (path == (const char *)0 || path[0] != '/') return UMICOM_VFS_INVALID_PATH;
    /* Bound the scan before splitting components. A later invalid component
     * must not cause an earlier part of the path to create a directory. */
    UmicomSize bytes = 0U;
    while (bytes < UMICOM_VFS_PATH_BYTES && path[bytes] != '\0') ++bytes;
    if (bytes == UMICOM_VFS_PATH_BYTES) return UMICOM_VFS_INVALID_PATH;
    parsed->count = 0U;
    if (bytes == 1U) return UMICOM_VFS_OK;
    if (path[bytes - 1U] == '/') return UMICOM_VFS_INVALID_PATH;
    UmicomSize position = 1U;
    while (position < bytes) {
        if (parsed->count == UMICOM_VFS_PATH_COMPONENTS) return UMICOM_VFS_INVALID_PATH;
        UmicomSize length = 0U;
        while (position < bytes && path[position] != '/') {
            if (length == UMICOM_VFS_NAME_BYTES - 1U) return UMICOM_VFS_INVALID_PATH;
            parsed->names[parsed->count][length++] = path[position++];
        }
        parsed->names[parsed->count][length] = '\0';
        if (!UmicomVfsNameValid(parsed->names[parsed->count])) return UMICOM_VFS_INVALID_PATH;
        ++parsed->count;
        if (position < bytes) ++position;
    }
    return UMICOM_VFS_OK;
}
static UmicomBoolean UmicomVfsOperationsValid(const UmicomKernelVfsOperations *ops)
{
    return ops != (const UmicomKernelVfsOperations *)0 && ops->validate && ops->root &&
        ops->lookup && ops->create && ops->unlink && ops->stat && ops->pin && ops->unpin &&
        ops->read && ops->write && ops->resize && ops->enumerate ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomKernelVfsStatus UmicomVfsReady(UmicomKernelVfs *vfs)
{
    if (vfs == (UmicomKernelVfs *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    if (vfs->self != vfs || vfs->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    if (!UmicomVfsOperationsValid(vfs->operations) || vfs->context == (void *)0 ||
        vfs->root == 0U || vfs->clients > UMICOM_VFS_CLIENT_LIMIT) return UMICOM_VFS_CORRUPT_STATE;
    return vfs->operations->validate(vfs->context);
}
UmicomKernelVfsStatus UmicomKernelVfsMount(UmicomKernelVfs *vfs,
    const UmicomKernelVfsOperations *operations, void *context)
{
    if (vfs == (UmicomKernelVfs *)0 || context == (void *)0 || !UmicomVfsOperationsValid(operations)) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!UmicomVfsZero(vfs, sizeof(*vfs))) return UMICOM_VFS_BAD_STATE;
    UmicomKernelVfsNodeId root = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomVfsClear(&info, sizeof(info));
    UmicomKernelVfsStatus status = operations->root(context, &root);
    if (status != UMICOM_VFS_OK) return status;
    status = operations->stat(context, root, &info);
    if (status != UMICOM_VFS_OK) return status;
    if (root == 0U || info.kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_NOT_DIRECTORY;
    status = operations->pin(context, root);
    if (status != UMICOM_VFS_OK) return status;
    /* Publication follows the successful root pin. The provider cannot close
     * while this mount, or any client attached to it, still depends on it. */
    vfs->self = vfs;
    vfs->operations = operations;
    vfs->context = context;
    vfs->root = root;
    vfs->state = UMICOM_VFS_OPEN;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsUnmount(UmicomKernelVfs *vfs)
{
    UmicomKernelVfsStatus status = UmicomVfsReady(vfs);
    if (status != UMICOM_VFS_OK) return status;
    if (vfs->clients != 0U) return UMICOM_VFS_BUSY;
    status = vfs->operations->unpin(vfs->context, vfs->root);
    if (status == UMICOM_VFS_OK) vfs->state = UMICOM_VFS_CLOSED;
    return status;
}
UmicomKernelVfsStatus UmicomKernelVfsClientOpen(UmicomKernelVfsClient *client,
    UmicomKernelVfs *vfs, UmicomU64 principal, UmicomKernelVfsRights rights)
{
    if (client == (UmicomKernelVfsClient *)0 || principal == 0U || (rights & ~UMICOM_VFS_RIGHT_ALL) != 0U) return UMICOM_VFS_INVALID_ARGUMENT;
    if (!UmicomVfsZero(client, sizeof(*client))) return UMICOM_VFS_BAD_STATE;
    UmicomKernelVfsStatus status = UmicomVfsReady(vfs);
    if (status != UMICOM_VFS_OK) return status;
    if (vfs->clients == UMICOM_VFS_CLIENT_LIMIT) return UMICOM_VFS_CAPACITY;
    client->self = client;
    client->vfs = vfs;
    client->principal = principal;
    client->rights = rights;
    client->state = UMICOM_VFS_OPEN;
    ++vfs->clients;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsClientValidate(UmicomKernelVfsClient *client)
{
    if (client == (UmicomKernelVfsClient *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    if (client->self != client || client->state != UMICOM_VFS_OPEN) return UMICOM_VFS_BAD_STATE;
    UmicomKernelVfsStatus status = UmicomVfsReady(client->vfs);
    if (status != UMICOM_VFS_OK) return status;
    if (client->principal == 0U || client->vfs->clients == 0U ||
        (client->rights & ~UMICOM_VFS_RIGHT_ALL) != 0U) return UMICOM_VFS_CORRUPT_STATE;
    /* Recount references from descriptors rather than trusting the stored
     * count. A missed duplicate or double close must be detected before it can
     * release the provider pin while another descriptor still uses it. */
    UmicomSize references[UMICOM_VFS_DESCRIPTOR_LIMIT];
    UmicomVfsClear(references, sizeof(references));
    for (UmicomSize i = 0U; i < UMICOM_VFS_DESCRIPTOR_LIMIT; ++i) {
        const UmicomKernelVfsDescriptorRecord *const record = &client->descriptors[i];
        if ((record->occupied != UMICOM_FALSE && record->occupied != UMICOM_TRUE) ||
            (record->retired != UMICOM_FALSE && record->retired != UMICOM_TRUE)) return UMICOM_VFS_CORRUPT_STATE;
        if (!record->occupied) {
            if (record->rights != 0U || record->description != 0U ||
                (record->retired && record->generation != ~(UmicomU32)0U)) return UMICOM_VFS_CORRUPT_STATE;
            continue;
        }
        if (record->retired || record->generation == 0U || record->description >= UMICOM_VFS_DESCRIPTOR_LIMIT ||
            (record->rights & ~client->rights) != 0U || (record->rights & ~UMICOM_VFS_DESCRIPTOR_RIGHTS) != 0U) return UMICOM_VFS_CORRUPT_STATE;
        ++references[record->description];
    }
    for (UmicomSize i = 0U; i < UMICOM_VFS_DESCRIPTOR_LIMIT; ++i) {
        const UmicomKernelVfsOpenDescription *const description = &client->descriptions[i];
        if (description->references != references[i]) return UMICOM_VFS_CORRUPT_STATE;
        if (references[i] == 0U) {
            if (!UmicomVfsZero(description, sizeof(*description))) return UMICOM_VFS_CORRUPT_STATE;
        } else if (description->node == 0U ||
            (description->kind != UMICOM_VFS_FILE && description->kind != UMICOM_VFS_DIRECTORY) ||
            (description->append != UMICOM_FALSE && description->append != UMICOM_TRUE)) return UMICOM_VFS_CORRUPT_STATE;
    }
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomVfsDescriptor(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor token, UmicomKernelVfsRights required,
    UmicomKernelVfsDescriptorRecord **outRecord, UmicomKernelVfsOpenDescription **outDescription)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsClientValidate(client);
    if (status != UMICOM_VFS_OK) return status;
    /* A numeric token is local to this client. Bounds and generation checks
     * happen before indexing, then rights are checked on that exact reference. */
    const UmicomU32 slot = (UmicomU32)token;
    const UmicomU32 generation = (UmicomU32)(token >> 32U);
    if (slot == 0U || slot > UMICOM_VFS_DESCRIPTOR_LIMIT || generation == 0U) return UMICOM_VFS_INVALID_DESCRIPTOR;
    UmicomKernelVfsDescriptorRecord *const record = &client->descriptors[slot - 1U];
    if (!record->occupied || record->generation != generation) return UMICOM_VFS_INVALID_DESCRIPTOR;
    if ((record->rights & required) != required) return UMICOM_VFS_ACCESS_DENIED;
    *outRecord = record;
    *outDescription = &client->descriptions[record->description];
    return UMICOM_VFS_OK;
}
static UmicomSize UmicomVfsFreeDescriptor(UmicomKernelVfsClient *client)
{
    for (UmicomSize i = 0U; i < UMICOM_VFS_DESCRIPTOR_LIMIT; ++i) {
        if (!client->descriptors[i].occupied && !client->descriptors[i].retired) return i;
    }
    return UMICOM_VFS_DESCRIPTOR_LIMIT;
}
static UmicomKernelFileDescriptor UmicomVfsPublishDescriptor(UmicomKernelVfsClient *client,
    UmicomSize slot, UmicomSize description, UmicomKernelVfsRights rights)
{
    UmicomKernelVfsDescriptorRecord *const record = &client->descriptors[slot];
    /* Publication is the last step of Open/Duplicate. No other callback can
     * see a half-built description because this API never yields mid-call. */
    if (record->generation == 0U) record->generation = 1U;
    record->description = description;
    record->rights = rights;
    record->occupied = UMICOM_TRUE;
    ++client->descriptions[description].references;
    return ((UmicomU64)record->generation << 32U) | (slot + 1U);
}
static UmicomKernelVfsStatus UmicomVfsResolve(UmicomKernelVfs *vfs,
    const UmicomVfsPath *path, UmicomSize count, UmicomKernelVfsNodeId *outNode)
{
    UmicomKernelVfsNodeId node = vfs->root;
    for (UmicomSize i = 0U; i < count; ++i) {
        UmicomKernelVfsNodeId next = 0U;
        UmicomKernelVfsStatus status = vfs->operations->lookup(vfs->context, node, path->names[i], &next);
        if (status != UMICOM_VFS_OK) return status;
        node = next;
    }
    *outNode = node;
    return UMICOM_VFS_OK;
}
static UmicomKernelVfsStatus UmicomVfsChangeName(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsKind kind, UmicomBoolean removing)
{
    UmicomKernelVfsStatus status = UmicomKernelVfsClientValidate(client);
    if (status != UMICOM_VFS_OK) return status;
    if (kind != UMICOM_VFS_FILE && kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_INVALID_ARGUMENT;
    const UmicomKernelVfsRights required = removing ? UMICOM_VFS_RIGHT_REMOVE : UMICOM_VFS_RIGHT_CREATE;
    if ((client->rights & required) == 0U) return UMICOM_VFS_ACCESS_DENIED;
    UmicomVfsPath parsed;
    status = UmicomVfsParse(path, &parsed);
    if (status != UMICOM_VFS_OK) return status;
    if (parsed.count == 0U) return UMICOM_VFS_BUSY; /* Root is a mount, not a removable child. */
    UmicomKernelVfsNodeId parent = 0U;
    status = UmicomVfsResolve(client->vfs, &parsed, parsed.count - 1U, &parent);
    if (status != UMICOM_VFS_OK) return status;
    const UmicomKernelVfsOperations *const ops = client->vfs->operations;
    return removing ? ops->unlink(client->vfs->context, parent, parsed.names[parsed.count - 1U], kind) :
        ops->create(client->vfs->context, parent, parsed.names[parsed.count - 1U], kind);
}
UmicomKernelVfsStatus UmicomKernelVfsCreate(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsKind kind)
{
    return UmicomVfsChangeName(client, path, kind, UMICOM_FALSE);
}
UmicomKernelVfsStatus UmicomKernelVfsRemove(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsKind kind)
{
    return UmicomVfsChangeName(client, path, kind, UMICOM_TRUE);
}
UmicomKernelVfsStatus UmicomKernelVfsOpen(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsRights rights, UmicomBoolean append,
    UmicomKernelFileDescriptor *outDescriptor)
{
    if (outDescriptor == (UmicomKernelFileDescriptor *)0 || (rights & ~UMICOM_VFS_DESCRIPTOR_RIGHTS) != 0U ||
        (append != UMICOM_FALSE && append != UMICOM_TRUE) || (append && !(rights & UMICOM_VFS_RIGHT_WRITE))) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsStatus status = UmicomKernelVfsClientValidate(client);
    if (status != UMICOM_VFS_OK) return status;
    if ((rights & ~client->rights) != 0U) return UMICOM_VFS_ACCESS_DENIED;
    /* Check capacity before pinning a node; a full descriptor table has no
     * side effect on provider ownership or an existing open position. */
    const UmicomSize slot = UmicomVfsFreeDescriptor(client);
    UmicomSize description = 0U;
    while (description < UMICOM_VFS_DESCRIPTOR_LIMIT && client->descriptions[description].references != 0U) ++description;
    if (slot == UMICOM_VFS_DESCRIPTOR_LIMIT || description == UMICOM_VFS_DESCRIPTOR_LIMIT) return UMICOM_VFS_CAPACITY;
    UmicomVfsPath parsed;
    status = UmicomVfsParse(path, &parsed);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelVfsNodeId node = 0U;
    status = UmicomVfsResolve(client->vfs, &parsed, parsed.count, &node);
    if (status != UMICOM_VFS_OK) return status;
    UmicomKernelVfsNodeInfo info;
    UmicomVfsClear(&info, sizeof(info));
    status = client->vfs->operations->stat(client->vfs->context, node, &info);
    if (status != UMICOM_VFS_OK) return status;
    if (info.kind == UMICOM_VFS_DIRECTORY && (rights & (UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE))) return UMICOM_VFS_NOT_FILE;
    if (info.kind == UMICOM_VFS_FILE && (rights & UMICOM_VFS_RIGHT_ENUMERATE)) return UMICOM_VFS_NOT_DIRECTORY;
    status = client->vfs->operations->pin(client->vfs->context, node);
    if (status != UMICOM_VFS_OK) return status;
    client->descriptions[description].node = node;
    client->descriptions[description].kind = info.kind;
    client->descriptions[description].append = append;
    *outDescriptor = UmicomVfsPublishDescriptor(client, slot, description, rights);
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsClose(UmicomKernelVfsClient *client, UmicomKernelFileDescriptor descriptor)
{
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, 0U, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    /* Only the last descriptor releases the shared description's node pin.
     * Earlier closes must preserve both its position and its backing object. */
    if (description->references == 1U) {
        status = client->vfs->operations->unpin(client->vfs->context, description->node);
        if (status != UMICOM_VFS_OK) return status; /* A refused unpin retains the handle for diagnosis. */
        UmicomVfsClear(description, sizeof(*description));
    } else --description->references;
    record->occupied = UMICOM_FALSE;
    record->description = 0U;
    record->rights = 0U;
    /* Retire at exhaustion. Wrapping would let a remembered token address a
     * different file after enough open/close cycles. */
    if (record->generation == ~(UmicomU32)0U) record->retired = UMICOM_TRUE;
    else ++record->generation;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsClientClose(UmicomKernelVfsClient *client, UmicomSize *outClosed)
{
    if (outClosed == (UmicomSize *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsStatus status = UmicomKernelVfsClientValidate(client);
    if (status != UMICOM_VFS_OK) return status;
    *outClosed = 0U;
    for (UmicomSize i = 0U; i < UMICOM_VFS_DESCRIPTOR_LIMIT; ++i) {
        const UmicomKernelVfsDescriptorRecord *const record = &client->descriptors[i];
        if (!record->occupied) continue;
        status = UmicomKernelVfsClose(client, ((UmicomU64)record->generation << 32U) | (i + 1U));
        if (status != UMICOM_VFS_OK) return status;
        ++*outClosed;
    }
    /* The mount remains pinned by this client until every close succeeds.
     * On an earlier error, the caller can retry the remaining descriptors. */
    --client->vfs->clients;
    client->state = UMICOM_VFS_CLOSED;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsDuplicate(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomKernelVfsRights rights,
    UmicomKernelFileDescriptor *outDescriptor)
{
    if (outDescriptor == (UmicomKernelFileDescriptor *)0 || (rights & ~UMICOM_VFS_DESCRIPTOR_RIGHTS) != 0U) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_DUPLICATE, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    if ((rights & ~record->rights) != 0U) return UMICOM_VFS_ACCESS_DENIED;
    const UmicomSize slot = UmicomVfsFreeDescriptor(client);
    if (slot == UMICOM_VFS_DESCRIPTOR_LIMIT) return UMICOM_VFS_CAPACITY;
    *outDescriptor = UmicomVfsPublishDescriptor(client, slot, record->description, rights);
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsQuery(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomKernelVfsNodeInfo *outInfo)
{
    if (outInfo == (UmicomKernelVfsNodeInfo *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_QUERY, &record, &description);
    return status == UMICOM_VFS_OK ? client->vfs->operations->stat(client->vfs->context, description->node, outInfo) : status;
}
UmicomKernelVfsStatus UmicomKernelVfsRead(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, void *destination, UmicomSize bytes, UmicomSize *outTransferred)
{
    if (outTransferred == (UmicomSize *)0 || (bytes != 0U && destination == (void *)0)) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_READ, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    if (description->kind != UMICOM_VFS_FILE) return UMICOM_VFS_NOT_FILE;
    *outTransferred = 0U;
    UmicomSize transferred = 0U;
    status = client->vfs->operations->read(client->vfs->context, description->node,
        description->position, destination, bytes, &transferred);
    if (transferred > bytes || description->position > ~(UmicomSize)0U - transferred) {
        client->vfs->state = UMICOM_VFS_POISONED;
        return UMICOM_VFS_CORRUPT_STATE;
    }
    description->position += transferred;
    *outTransferred = transferred;
    return status;
}
UmicomKernelVfsStatus UmicomKernelVfsWrite(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, const void *source, UmicomSize bytes, UmicomSize *outTransferred)
{
    if (outTransferred == (UmicomSize *)0 || (bytes != 0U && source == (const void *)0)) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_WRITE, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    if (description->kind != UMICOM_VFS_FILE) return UMICOM_VFS_NOT_FILE;
    *outTransferred = 0U;
    UmicomKernelVfsNodeInfo info;
    UmicomVfsClear(&info, sizeof(info));
    status = client->vfs->operations->stat(client->vfs->context, description->node, &info);
    if (status != UMICOM_VFS_OK) return status;
    /* APPEND chooses the current end at each write, not merely at Open.
     * This observation and the write are serial; no concurrent writer runs
     * between them under the current Kernel-side contract. */
    const UmicomSize offset = description->append ? info.bytes : description->position;
    if (offset > info.maximumBytes || bytes > info.maximumBytes - offset) return UMICOM_VFS_RANGE;
    UmicomSize transferred = 0U;
    status = client->vfs->operations->write(client->vfs->context, description->node, offset, source, bytes, &transferred);
    if (transferred > bytes) {
        client->vfs->state = UMICOM_VFS_POISONED;
        return UMICOM_VFS_CORRUPT_STATE;
    }
    /* A partial write commits only its actual prefix. Retrying starts at that
     * new position; callers must not resend the already accepted bytes. */
    if (transferred != 0U) description->position = offset + transferred;
    *outTransferred = transferred;
    return status;
}
UmicomKernelVfsStatus UmicomKernelVfsSeek(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomSize absoluteOffset)
{
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, 0U, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    if (!(record->rights & (UMICOM_VFS_RIGHT_READ | UMICOM_VFS_RIGHT_WRITE))) return UMICOM_VFS_ACCESS_DENIED;
    if (description->kind != UMICOM_VFS_FILE) return UMICOM_VFS_NOT_FILE;
    UmicomKernelVfsNodeInfo info;
    UmicomVfsClear(&info, sizeof(info));
    status = client->vfs->operations->stat(client->vfs->context, description->node, &info);
    if (status != UMICOM_VFS_OK) return status;
    if (absoluteOffset > info.maximumBytes) return UMICOM_VFS_RANGE;
    description->position = absoluteOffset;
    return UMICOM_VFS_OK;
}
UmicomKernelVfsStatus UmicomKernelVfsResize(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomSize bytes)
{
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_WRITE, &record, &description);
    return status == UMICOM_VFS_OK ? client->vfs->operations->resize(client->vfs->context, description->node, bytes) : status;
}
UmicomKernelVfsStatus UmicomKernelVfsReadDirectory(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomKernelVfsDirectoryEntry *outEntry)
{
    if (outEntry == (UmicomKernelVfsDirectoryEntry *)0) return UMICOM_VFS_INVALID_ARGUMENT;
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_ENUMERATE, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    if (description->kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_NOT_DIRECTORY;
    UmicomU64 epoch = 0U;
    UmicomSize next = 0U;
    status = client->vfs->operations->enumerate(client->vfs->context, description->node,
        description->directoryEpoch, description->position, outEntry, &epoch, &next);
    /* A changed namespace leaves the old cursor intact so the caller can
     * decide to rewind. Silently restarting here could repeat or skip names. */
    if (status == UMICOM_VFS_OK || status == UMICOM_VFS_END) {
        description->directoryEpoch = epoch;
        description->position = next;
    }
    return status;
}
UmicomKernelVfsStatus UmicomKernelVfsRewindDirectory(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor)
{
    UmicomKernelVfsDescriptorRecord *record = (UmicomKernelVfsDescriptorRecord *)0;
    UmicomKernelVfsOpenDescription *description = (UmicomKernelVfsOpenDescription *)0;
    UmicomKernelVfsStatus status = UmicomVfsDescriptor(client, descriptor, UMICOM_VFS_RIGHT_ENUMERATE, &record, &description);
    if (status != UMICOM_VFS_OK) return status;
    if (description->kind != UMICOM_VFS_DIRECTORY) return UMICOM_VFS_NOT_DIRECTORY;
    description->position = 0U;
    description->directoryEpoch = 0U;
    return UMICOM_VFS_OK;
}
const char *UmicomKernelVfsStatusName(UmicomKernelVfsStatus status)
{
    switch (status) {
        case UMICOM_VFS_OK: return "ok";
        case UMICOM_VFS_INVALID_ARGUMENT: return "invalid-argument";
        case UMICOM_VFS_BAD_STATE: return "bad-state";
        case UMICOM_VFS_UNSAFE_CONTEXT: return "unsafe-context";
        case UMICOM_VFS_INVALID_PATH: return "invalid-path";
        case UMICOM_VFS_NOT_FOUND: return "not-found";
        case UMICOM_VFS_EXISTS: return "exists";
        case UMICOM_VFS_NOT_DIRECTORY: return "not-directory";
        case UMICOM_VFS_NOT_FILE: return "not-file";
        case UMICOM_VFS_NOT_EMPTY: return "not-empty";
        case UMICOM_VFS_ACCESS_DENIED: return "access-denied";
        case UMICOM_VFS_INVALID_DESCRIPTOR: return "invalid-descriptor";
        case UMICOM_VFS_CAPACITY: return "capacity";
        case UMICOM_VFS_NO_MEMORY: return "no-memory";
        case UMICOM_VFS_RANGE: return "range";
        case UMICOM_VFS_BUSY: return "busy";
        case UMICOM_VFS_END: return "end";
        case UMICOM_VFS_CHANGED: return "changed";
        case UMICOM_VFS_EXHAUSTED: return "exhausted";
        case UMICOM_VFS_RELEASE_FAILED: return "release-failed";
        case UMICOM_VFS_CORRUPT_STATE: return "corrupt-state";
        default: return "unknown-status";
    }
}
