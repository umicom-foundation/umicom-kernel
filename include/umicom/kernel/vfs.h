/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/vfs.h
 *
 * PURPOSE:
 *   Provide typed namespace operations and client-local file descriptors without
 *   making callers depend on how a filesystem stores its bytes.
 *
 * EDUCATIONAL OVERVIEW:
 *   A name, a node, an open description and a descriptor have different lives.
 *   Removing a name does not invalidate an open description. Duplicating a
 *   descriptor shares that description's position; opening the name again does
 *   not. Keeping these concepts separate makes cleanup and authority explicit.
 *
 *   These are trusted Kernel APIs, not user system calls. A client belongs to a
 *   Kernel-selected principal and has a fixed ceiling of rights. Buffer pointers
 *   must refer to non-overlapping Kernel-owned storage, never unchecked user RAM.
 *   One caller serialises operations; this interface is not IRQ-safe or SMP-safe.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_VFS_H
#define UMICOM_KERNEL_VFS_H
#include "umicom/kernel/types.h"

/* Bounds are part of admission, not claims about the eventual desktop profile. */
#define UMICOM_VFS_PATH_BYTES 256U
#define UMICOM_VFS_NAME_BYTES 64U
#define UMICOM_VFS_PATH_COMPONENTS 16U
#define UMICOM_VFS_DESCRIPTOR_LIMIT 16U
#define UMICOM_VFS_CLIENT_LIMIT 16U

typedef UmicomU64 UmicomKernelVfsNodeId;
typedef UmicomU64 UmicomKernelFileDescriptor;
typedef UmicomU32 UmicomKernelVfsRights;
#define UMICOM_VFS_RIGHT_READ ((UmicomKernelVfsRights)1U)
#define UMICOM_VFS_RIGHT_WRITE ((UmicomKernelVfsRights)2U)
#define UMICOM_VFS_RIGHT_QUERY ((UmicomKernelVfsRights)4U)
#define UMICOM_VFS_RIGHT_ENUMERATE ((UmicomKernelVfsRights)8U)
#define UMICOM_VFS_RIGHT_DUPLICATE ((UmicomKernelVfsRights)16U)
#define UMICOM_VFS_RIGHT_CREATE ((UmicomKernelVfsRights)32U)
#define UMICOM_VFS_RIGHT_REMOVE ((UmicomKernelVfsRights)64U)
#define UMICOM_VFS_RIGHT_ALL ((UmicomKernelVfsRights)127U)
#define UMICOM_VFS_DESCRIPTOR_RIGHTS ((UmicomKernelVfsRights)31U)

typedef enum UmicomKernelVfsStatus {
    UMICOM_VFS_OK,
    UMICOM_VFS_INVALID_ARGUMENT,
    UMICOM_VFS_BAD_STATE,
    UMICOM_VFS_UNSAFE_CONTEXT,
    UMICOM_VFS_INVALID_PATH,
    UMICOM_VFS_NOT_FOUND,
    UMICOM_VFS_EXISTS,
    UMICOM_VFS_NOT_DIRECTORY,
    UMICOM_VFS_NOT_FILE,
    UMICOM_VFS_NOT_EMPTY,
    UMICOM_VFS_ACCESS_DENIED,
    UMICOM_VFS_INVALID_DESCRIPTOR,
    UMICOM_VFS_CAPACITY,
    UMICOM_VFS_NO_MEMORY,
    UMICOM_VFS_RANGE,
    UMICOM_VFS_BUSY,
    UMICOM_VFS_END,
    UMICOM_VFS_CHANGED,
    UMICOM_VFS_EXHAUSTED,
    UMICOM_VFS_RELEASE_FAILED,
    /* Preserve the original final spelling for source review. The active
     * comma below appends backend outcomes without renumbering existing ABI
     * values or changing the established VFS behaviour. */
#if 0
    UMICOM_VFS_CORRUPT_STATE
#endif
    UMICOM_VFS_CORRUPT_STATE,
    UMICOM_VFS_IO_ERROR,
    UMICOM_VFS_READ_ONLY,
    UMICOM_VFS_UNSUPPORTED,
    UMICOM_VFS_INSPECTION_LIMIT,
    UMICOM_VFS_CORRUPT_FILESYSTEM
} UmicomKernelVfsStatus;
typedef enum UmicomKernelVfsKind {
    UMICOM_VFS_FILE = 1,
    UMICOM_VFS_DIRECTORY = 2
} UmicomKernelVfsKind;
typedef enum UmicomKernelVfsLifetime {
    UMICOM_VFS_UNUSED,
    UMICOM_VFS_OPEN,
    UMICOM_VFS_CLOSING,
    UMICOM_VFS_CLOSED,
    UMICOM_VFS_POISONED
} UmicomKernelVfsLifetime;

typedef struct UmicomKernelVfsNodeInfo {
    UmicomKernelVfsNodeId id;
    UmicomKernelVfsKind kind;
    UmicomSize bytes;
    UmicomSize maximumBytes;
} UmicomKernelVfsNodeInfo;
typedef struct UmicomKernelVfsDirectoryEntry {
    char name[UMICOM_VFS_NAME_BYTES];
    UmicomKernelVfsNodeInfo info;
} UmicomKernelVfsDirectoryEntry;

/* The first provider is RAMFS. A mount borrows these immutable operations and
 * their stable context until unmounted. Node IDs are meaningful only within
 * that provider. Pin/Unpin own open descriptions; they do not own pathnames.
 * No operation may yield while a namespace or byte operation is in progress. */
typedef struct UmicomKernelVfsOperations {
    UmicomKernelVfsStatus (*validate)(void *context);
    UmicomKernelVfsStatus (*root)(void *context, UmicomKernelVfsNodeId *outNode);
    UmicomKernelVfsStatus (*lookup)(void *context, UmicomKernelVfsNodeId directory,
        const char *name, UmicomKernelVfsNodeId *outNode);
    UmicomKernelVfsStatus (*create)(void *context, UmicomKernelVfsNodeId directory,
        const char *name, UmicomKernelVfsKind kind);
    UmicomKernelVfsStatus (*unlink)(void *context, UmicomKernelVfsNodeId directory,
        const char *name, UmicomKernelVfsKind kind);
    UmicomKernelVfsStatus (*stat)(void *context, UmicomKernelVfsNodeId node,
        UmicomKernelVfsNodeInfo *outInfo);
    UmicomKernelVfsStatus (*pin)(void *context, UmicomKernelVfsNodeId node);
    UmicomKernelVfsStatus (*unpin)(void *context, UmicomKernelVfsNodeId node);
    UmicomKernelVfsStatus (*read)(void *context, UmicomKernelVfsNodeId node,
        UmicomSize offset, void *destination, UmicomSize bytes, UmicomSize *outRead);
    UmicomKernelVfsStatus (*write)(void *context, UmicomKernelVfsNodeId node,
        UmicomSize offset, const void *source, UmicomSize bytes, UmicomSize *outWritten);
    UmicomKernelVfsStatus (*resize)(void *context, UmicomKernelVfsNodeId node, UmicomSize bytes);
    UmicomKernelVfsStatus (*enumerate)(void *context, UmicomKernelVfsNodeId node,
        UmicomU64 epoch, UmicomSize cursor, UmicomKernelVfsDirectoryEntry *outEntry,
        UmicomU64 *outEpoch, UmicomSize *outNext);
} UmicomKernelVfsOperations;

typedef struct UmicomKernelVfs {
    const struct UmicomKernelVfs *self;
    const UmicomKernelVfsOperations *operations;
    void *context;
    UmicomKernelVfsNodeId root;
    UmicomSize clients;
    UmicomKernelVfsLifetime state;
} UmicomKernelVfs;
typedef struct UmicomKernelVfsOpenDescription {
    UmicomKernelVfsNodeId node;
    UmicomSize position;
    UmicomSize references;
    UmicomU64 directoryEpoch;
    UmicomKernelVfsKind kind;
    UmicomBoolean append;
} UmicomKernelVfsOpenDescription;
typedef struct UmicomKernelVfsDescriptorRecord {
    UmicomU32 generation;
    UmicomSize description;
    UmicomKernelVfsRights rights;
    UmicomBoolean occupied;
    UmicomBoolean retired;
} UmicomKernelVfsDescriptorRecord;
typedef struct UmicomKernelVfsClient {
    const struct UmicomKernelVfsClient *self;
    UmicomKernelVfs *vfs;
    UmicomU64 principal;
    UmicomKernelVfsRights rights;
    UmicomKernelVfsLifetime state;
    UmicomKernelVfsOpenDescription descriptions[UMICOM_VFS_DESCRIPTOR_LIMIT];
    UmicomKernelVfsDescriptorRecord descriptors[UMICOM_VFS_DESCRIPTOR_LIMIT];
} UmicomKernelVfsClient;

/* Mount one provider at /. Mounts/clients use entirely zero-filled stable
 * storage once. The pinned root keeps the provider alive until Unmount.
 * This foundation has no mount crossing, symlink resolution or working directory.
 * Paths are canonical absolute printable-ASCII paths, at most 255 bytes, with
 * at most sixteen components. Empty components, trailing slash (except /),
 * backslash, dot and dot-dot are refused rather than silently normalised. */
UmicomKernelVfsStatus UmicomKernelVfsMount(UmicomKernelVfs *vfs,
    const UmicomKernelVfsOperations *operations, void *context);
UmicomKernelVfsStatus UmicomKernelVfsUnmount(UmicomKernelVfs *vfs);
UmicomKernelVfsStatus UmicomKernelVfsClientOpen(UmicomKernelVfsClient *client,
    UmicomKernelVfs *vfs, UmicomU64 principal, UmicomKernelVfsRights rights);
UmicomKernelVfsStatus UmicomKernelVfsClientClose(UmicomKernelVfsClient *client, UmicomSize *outClosed);
UmicomKernelVfsStatus UmicomKernelVfsClientValidate(UmicomKernelVfsClient *client);

/* CREATE/REMOVE are namespace authority on this whole mounted domain, not
 * filesystem ACLs. Give separate domains or no such authority to untrusted
 * clients until credential/path-capability policy exists. No user identities
 * or descriptor tables are accepted directly from a syscall argument. */
UmicomKernelVfsStatus UmicomKernelVfsCreate(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsKind kind);
UmicomKernelVfsStatus UmicomKernelVfsRemove(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsKind kind);
UmicomKernelVfsStatus UmicomKernelVfsOpen(UmicomKernelVfsClient *client,
    const char *path, UmicomKernelVfsRights rights, UmicomBoolean append,
    UmicomKernelFileDescriptor *outDescriptor);
UmicomKernelVfsStatus UmicomKernelVfsClose(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor);
/* Duplicate shares position/append/iteration state, but may only reduce rights.
 * Separate Open calls receive independent descriptions and positions. Tokens
 * are client-local, not globally unique secrets or cross-client capabilities. */
UmicomKernelVfsStatus UmicomKernelVfsDuplicate(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomKernelVfsRights rights,
    UmicomKernelFileDescriptor *outDescriptor);
UmicomKernelVfsStatus UmicomKernelVfsQuery(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomKernelVfsNodeInfo *outInfo);
/* On validated I/O requests outTransferred reports the actual prefix, including
 * on backend failure. Partial writes advance only by that prefix. EOF is OK/0;
 * an error is never converted into a fabricated complete transfer. */
UmicomKernelVfsStatus UmicomKernelVfsRead(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, void *destination, UmicomSize bytes,
    UmicomSize *outTransferred);
UmicomKernelVfsStatus UmicomKernelVfsWrite(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, const void *source, UmicomSize bytes,
    UmicomSize *outTransferred);
UmicomKernelVfsStatus UmicomKernelVfsSeek(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomSize absoluteOffset);
UmicomKernelVfsStatus UmicomKernelVfsResize(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomSize bytes);
/* A structural namespace change makes an existing iteration return CHANGED.
 * Rewind starts a new iteration. A result is a value snapshot, not a node pin. */
UmicomKernelVfsStatus UmicomKernelVfsReadDirectory(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor, UmicomKernelVfsDirectoryEntry *outEntry);
UmicomKernelVfsStatus UmicomKernelVfsRewindDirectory(UmicomKernelVfsClient *client,
    UmicomKernelFileDescriptor descriptor);
const char *UmicomKernelVfsStatusName(UmicomKernelVfsStatus status);
void UmicomKernelVfsValidateExecution(void);
#endif /* UMICOM_KERNEL_VFS_H */
