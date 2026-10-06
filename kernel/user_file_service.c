/*-----------------------------------------------------------------------------
 * Umicom Kernel checked file-request boundary
 * File: kernel/user_file_service.c
 *
 * User addresses describe the caller's virtual memory. They are never cast
 * into machine pointers. We copy the request once, preflight every output, and
 * snapshot WRITE/path bytes before returning to the machine dispatcher. That
 * keeps filesystem allocation outside the borrowed user timer/root context.
 *
 * These guarantees rely on serial Kernel work and immutable mappings during
 * each checked copy. They are not a concurrent mapper or DMA protection scheme.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "user_files_internal.h"

UmicomU64 UmicomFileReadUser(const UmicomKernelUserMemory *memory, UmicomAddress address,
    void *destination, UmicomSize bytes)
{
    if (UmicomKernelUserMemoryCheck(memory, address, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_READ)
        != UMICOM_USER_RESULT_OK) return UMICOM_FILE_BAD_USER_BUFFER;
    for (UmicomSize offset = 0U; offset < bytes;) {
        /* The established copy primitive remains bounded to 64 bytes. Reuse
         * it in chunks without changing its public contract or implementation. */
        const UmicomSize chunk = bytes - offset < UMICOM_USER_COPY_LIMIT ? bytes - offset : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryRead(memory, address + offset, (UmicomU8 *)destination + offset,
            chunk) != UMICOM_USER_RESULT_OK) return UMICOM_FILE_BAD_USER_BUFFER;
        offset += chunk;
    }
    return UMICOM_VFS_OK;
}
UmicomU64 UmicomFileWriteUser(const UmicomKernelUserMemory *memory, UmicomAddress address,
    const void *source, UmicomSize bytes)
{
    if (UmicomKernelUserMemoryCheck(memory, address, bytes, UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE)
        != UMICOM_USER_RESULT_OK) return UMICOM_FILE_BAD_USER_BUFFER;
    for (UmicomSize offset = 0U; offset < bytes;) {
        const UmicomSize chunk = bytes - offset < UMICOM_USER_COPY_LIMIT ? bytes - offset : UMICOM_USER_COPY_LIMIT;
        if (UmicomKernelUserMemoryWrite(memory, address + offset, (const UmicomU8 *)source + offset,
            chunk) != UMICOM_USER_RESULT_OK) return UMICOM_FILE_BAD_USER_BUFFER;
        offset += chunk;
    }
    return UMICOM_VFS_OK;
}
static UmicomBoolean UmicomFileOverlap(UmicomAddress a, UmicomSize bytesA, UmicomAddress b, UmicomSize bytesB)
{
    /* Subtractions avoid forming an overflowing end pointer. Zero-length
     * operations touch no byte and therefore cannot overlap an output record. */
    if (!bytesA || !bytesB) return UMICOM_FALSE;
    return a <= b ? (b - a < bytesA ? UMICOM_TRUE : UMICOM_FALSE)
                  : (a - b < bytesB ? UMICOM_TRUE : UMICOM_FALSE);
}
static UmicomU64 UmicomFileRequestShape(const UmicomKernelFileRequest *r)
{
    switch (r->operation) {
        case UMICOM_FILE_OPEN:
        case UMICOM_FILE_CREATE:
        case UMICOM_FILE_REMOVE:
            if (r->descriptor || !r->bytes || r->bytes >= UMICOM_VFS_PATH_BYTES) return UMICOM_VFS_INVALID_ARGUMENT;
            if (r->operation == UMICOM_FILE_OPEN)
                return (r->argument & ~(UmicomU64)UMICOM_VFS_DESCRIPTOR_RIGHTS) || r->options > 1U
                    ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_VFS_OK;
            return (r->argument != UMICOM_VFS_FILE && r->argument != UMICOM_VFS_DIRECTORY) || r->options
                ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_VFS_OK;
        case UMICOM_FILE_READ:
        case UMICOM_FILE_WRITE:
            if (r->bytes > UMICOM_FILE_IO_LIMIT) return UMICOM_FILE_TOO_LARGE;
            return r->argument || r->options ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_VFS_OK;
        case UMICOM_FILE_QUERY:
        case UMICOM_FILE_READ_DIRECTORY:
            if (r->bytes != (r->operation == UMICOM_FILE_QUERY ? sizeof(UmicomKernelFileInfo)
                : sizeof(UmicomKernelFileEntry))) return UMICOM_VFS_INVALID_ARGUMENT;
            return r->argument || r->options ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_VFS_OK;
        case UMICOM_FILE_DUPLICATE:
            if (r->argument & ~(UmicomU64)UMICOM_VFS_DESCRIPTOR_RIGHTS) return UMICOM_VFS_INVALID_ARGUMENT;
            /* Fall through to the scalar-operation reserved-field checks. */
            [[fallthrough]];
        case UMICOM_FILE_SEEK:
        case UMICOM_FILE_RESIZE:
            return r->address || r->bytes || r->options ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_VFS_OK;
        case UMICOM_FILE_CLOSE:
        case UMICOM_FILE_REWIND_DIRECTORY:
            return r->address || r->bytes || r->argument || r->options ? UMICOM_VFS_INVALID_ARGUMENT : UMICOM_VFS_OK;
        default: return UMICOM_VFS_INVALID_ARGUMENT;
    }
}
static UmicomBoolean UmicomFileHasOutput(const UmicomKernelFileRequest *r)
{
    return r->operation == UMICOM_FILE_READ || r->operation == UMICOM_FILE_QUERY ||
        r->operation == UMICOM_FILE_READ_DIRECTORY ? UMICOM_TRUE : UMICOM_FALSE;
}
static UmicomU64 UmicomFileCheckOutput(const UmicomKernelUserMemory *memory,
    const UmicomKernelFileRequest *r, UmicomAddress reply)
{
    /* Validate the result before any backend operation can open a handle or
     * move a position. Otherwise a failed copy-out could hide committed work. */
    if (UmicomKernelUserMemoryCheck(memory, reply, sizeof(UmicomKernelFileResult),
        UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != UMICOM_USER_RESULT_OK) return UMICOM_FILE_BAD_USER_BUFFER;
    if (UmicomFileHasOutput(r)) {
        if (UmicomFileOverlap((UmicomAddress)r->address, r->bytes, reply, sizeof(UmicomKernelFileResult)))
            return UMICOM_VFS_INVALID_ARGUMENT;
        if (UmicomKernelUserMemoryCheck(memory, (UmicomAddress)r->address, r->bytes,
            UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE) != UMICOM_USER_RESULT_OK) return UMICOM_FILE_BAD_USER_BUFFER;
    }
    return UMICOM_VFS_OK;
}
static UmicomU64 UmicomFileReject(UmicomKernelUserSession *session, UmicomRiscvTrapFrame *frame,
    UmicomAddress nextPc, UmicomU64 status)
{
    /* Admission refusals have no backend effect. The caller always gets a0,
     * even when its requested result record is itself inaccessible. */
    if (umicomFileBoundTask && &umicomFileBoundTask->process.report == session) {
        UmicomKernelUserFileRecord *record = UmicomFileRecord(umicomFileBoundOwner, umicomFileBoundTask);
        if (record && !record->pending) UmicomFileClear(record->payload, sizeof(record->payload));
    }
    ++session->rejectedCalls;
    frame->x10_a0 = status;
    frame->mepc = nextPc;
    return 1U;
}
UmicomU64 UmicomKernelUserFileDispatch(UmicomKernelUserSession *session,
    UmicomRiscvTrapFrame *frame, UmicomAddress nextPc)
{
    if (!session || !frame) return 0U;
    UmicomKernelUserFiles *files = umicomFileBoundOwner;
    UmicomKernelUserTask *task = umicomFileBoundTask;
    if (!files || !task || files->self != files || files->state != UMICOM_VFS_OPEN ||
        files->scheduler->files != files || !files->scheduler->active ||
        task->state != UMICOM_USER_TASK_RUNNING || &task->process.report != session ||
        !session->identity || task->process.identity != session->identity)
        return UmicomFileReject(session, frame, nextPc, UMICOM_FILE_SERVICE_UNBOUND);
    UmicomKernelUserFileRecord *record = UmicomFileRecord(files, task);
    if (!record || !record->opened || record->identity != session->identity || !record->client.address)
        return UmicomFileReject(session, frame, nextPc, UMICOM_FILE_SERVICE_UNBOUND);
    if (record->pending || frame->x17_a7 != UMICOM_USER_CALL_FILE ||
        frame->x12_a2 != sizeof(UmicomKernelFileRequest) || frame->x13_a3 != sizeof(UmicomKernelFileResult))
        return UmicomFileReject(session, frame, nextPc, UMICOM_VFS_INVALID_ARGUMENT);
    UmicomKernelFileRequest request = {0};
    UmicomU64 status = UmicomFileReadUser(&session->memory, (UmicomAddress)frame->x10_a0,
        &request, sizeof(request));
    if (status == UMICOM_VFS_OK) status = UmicomFileRequestShape(&request);
    if (status == UMICOM_VFS_OK) status = UmicomFileCheckOutput(&session->memory, &request, (UmicomAddress)frame->x11_a1);
    if (status != UMICOM_VFS_OK) return UmicomFileReject(session, frame, nextPc, status);
    if (request.operation == UMICOM_FILE_OPEN || request.operation == UMICOM_FILE_CREATE ||
        request.operation == UMICOM_FILE_REMOVE || request.operation == UMICOM_FILE_WRITE) {
        /* WRITE and path bytes become Kernel-owned before returning to the
         * dispatcher. Neither input pointer is followed a second time later. */
        status = UmicomFileReadUser(&session->memory, (UmicomAddress)request.address,
            record->payload, request.bytes);
        if (status != UMICOM_VFS_OK) return UmicomFileReject(session, frame, nextPc, status);
        if (request.operation != UMICOM_FILE_WRITE) {
            for (UmicomSize i = 0U; i < request.bytes; ++i)
                if (record->payload[i] == 0U) return UmicomFileReject(session, frame, nextPc, UMICOM_VFS_INVALID_PATH);
            record->payload[request.bytes] = 0U; /* A bounded private terminator for the existing VFS. */
        }
    }
    record->request = request;
    record->reply = (UmicomAddress)frame->x11_a1;
    record->nextPc = nextPc;
    record->pending = UMICOM_TRUE; /* Publish only after all input/output checks. */
    frame->mepc = nextPc;          /* Advance the ECALL once; completion never repeats it. */
    return 0U;                    /* Existing Assembly retains the full frame and restores M-mode. */
}
static UmicomKernelFileInfo UmicomFileWireInfo(const UmicomKernelVfsNodeInfo *info)
{
    /* Copy fields, not native padding. No physical address or implementation
     * pointer belongs in an application's metadata buffer. */
    return (UmicomKernelFileInfo){info->id, (UmicomU64)info->kind, info->bytes, info->maximumBytes};
}
UmicomU64 UmicomFileExecute(UmicomKernelUserFileRecord *record,
    UmicomKernelVfsClient *client, const UmicomKernelUserMemory *memory)
{
    const UmicomKernelFileRequest *r = &record->request;
    UmicomKernelFileResult result = {0};
    result.status = UmicomFileCheckOutput(memory, r, record->reply);
    if (result.status != UMICOM_VFS_OK) return result.status; /* No file position or namespace changed. */
    UmicomKernelVfsNodeInfo info = {0};
    UmicomKernelVfsDirectoryEntry entry;
    UmicomFileClear(&entry, sizeof(entry));
    UmicomKernelFileDescriptor descriptor = 0U;
    UmicomSize transferred = 0U;
    switch (r->operation) {
        case UMICOM_FILE_OPEN:
            result.status = UmicomKernelVfsOpen(client, (const char *)record->payload,
                (UmicomKernelVfsRights)r->argument, r->options ? UMICOM_TRUE : UMICOM_FALSE, &descriptor);
            if (result.status == UMICOM_VFS_OK) result.value = descriptor;
            break;
        case UMICOM_FILE_CREATE:
            result.status = UmicomKernelVfsCreate(client, (const char *)record->payload, (UmicomKernelVfsKind)r->argument);
            break;
        case UMICOM_FILE_REMOVE:
            result.status = UmicomKernelVfsRemove(client, (const char *)record->payload, (UmicomKernelVfsKind)r->argument);
            break;
        case UMICOM_FILE_CLOSE:
            result.status = UmicomKernelVfsClose(client, r->descriptor);
            break;
        case UMICOM_FILE_DUPLICATE:
            result.status = UmicomKernelVfsDuplicate(client, r->descriptor, (UmicomKernelVfsRights)r->argument, &descriptor);
            if (result.status == UMICOM_VFS_OK) result.value = descriptor;
            break;
        case UMICOM_FILE_SEEK:
            result.status = UmicomKernelVfsSeek(client, r->descriptor, r->argument);
            break;
        case UMICOM_FILE_RESIZE:
            result.status = UmicomKernelVfsResize(client, r->descriptor, r->argument);
            break;
        case UMICOM_FILE_WRITE:
            result.status = UmicomKernelVfsWrite(client, r->descriptor, record->payload, r->bytes, &transferred);
            result.value = transferred; /* Preserve a real prefix even when the backend reports an error. */
            break;
        case UMICOM_FILE_READ:
            result.status = UmicomKernelVfsRead(client, r->descriptor, record->payload, r->bytes, &transferred);
            result.value = transferred;
            if (transferred && UmicomFileWriteUser(memory, (UmicomAddress)r->address,
                record->payload, transferred) != UMICOM_VFS_OK) return UMICOM_FILE_BAD_USER_BUFFER;
            break;
        case UMICOM_FILE_QUERY:
            result.status = UmicomKernelVfsQuery(client, r->descriptor, &info);
            if (result.status == UMICOM_VFS_OK) {
                const UmicomKernelFileInfo wire = UmicomFileWireInfo(&info);
                result.value = sizeof(wire);
                if (UmicomFileWriteUser(memory, (UmicomAddress)r->address, &wire, sizeof(wire)) != UMICOM_VFS_OK)
                    return UMICOM_FILE_BAD_USER_BUFFER;
            }
            break;
        case UMICOM_FILE_READ_DIRECTORY:
            result.status = UmicomKernelVfsReadDirectory(client, r->descriptor, &entry);
            if (result.status == UMICOM_VFS_OK) {
                UmicomKernelFileEntry wire;
                UmicomFileClear(&wire, sizeof(wire));
                for (UmicomSize i = 0U; i < sizeof(wire.name); ++i) wire.name[i] = entry.name[i];
                wire.info = UmicomFileWireInfo(&entry.info);
                result.value = sizeof(wire);
                if (UmicomFileWriteUser(memory, (UmicomAddress)r->address, &wire, sizeof(wire)) != UMICOM_VFS_OK)
                    return UMICOM_FILE_BAD_USER_BUFFER;
            }
            break;
        case UMICOM_FILE_REWIND_DIRECTORY:
            result.status = UmicomKernelVfsRewindDirectory(client, r->descriptor);
            break;
        default: result.status = UMICOM_VFS_INVALID_ARGUMENT; break;
    }
    /* There is no yield or concurrent mapper between preflight, the backend
     * operation and this copy-out. A copy failure here indicates broken Kernel
     * ownership, not an ordinary partial write that should be called success. */
    if (UmicomFileWriteUser(memory, record->reply, &result, sizeof(result)) != UMICOM_VFS_OK)
        return UMICOM_FILE_BAD_USER_BUFFER;
    return result.status;
}
