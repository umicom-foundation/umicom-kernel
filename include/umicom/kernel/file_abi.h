/*-----------------------------------------------------------------------------
 * Umicom Kernel native file-service contract
 * File: include/umicom/kernel/file_abi.h
 *
 * A file request is a copied value, not a pointer to a Kernel descriptor table.
 * The VFS vocabulary supplies the existing rights, kinds and ordinary statuses;
 * the explicit wire records below do not expose its implementation structures.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_FILE_ABI_H
#define UMICOM_KERNEL_FILE_ABI_H
#include "umicom/kernel/vfs.h"

/* The original process, copy and message service numbers are unchanged. */
#define UMICOM_USER_CALL_FILE 32U
#define UMICOM_FILE_IO_LIMIT 4096U
#define UMICOM_FILE_BAD_USER_BUFFER 0x200U
#define UMICOM_FILE_SERVICE_UNBOUND 0x201U
#define UMICOM_FILE_TOO_LARGE 0x202U

/* Unknown operations and unused nonzero arguments are refused. A malformed
 * request must not accidentally select additional authority or a new feature. */
typedef enum UmicomKernelFileOperation {
    UMICOM_FILE_OPEN = 1, UMICOM_FILE_CLOSE, UMICOM_FILE_READ, UMICOM_FILE_WRITE,
    UMICOM_FILE_SEEK, UMICOM_FILE_RESIZE, UMICOM_FILE_QUERY, UMICOM_FILE_DUPLICATE,
    UMICOM_FILE_CREATE, UMICOM_FILE_REMOVE, UMICOM_FILE_READ_DIRECTORY,
    UMICOM_FILE_REWIND_DIRECTORY
} UmicomKernelFileOperation;

typedef struct UmicomKernelFileRequest {
    UmicomU64 operation;
    UmicomU64 descriptor;
    UmicomU64 address;
    UmicomU64 bytes;
    UmicomU64 argument;
    UmicomU64 options;
} UmicomKernelFileRequest;
typedef struct UmicomKernelFileResult {
    UmicomU64 status;
    UmicomU64 value; /* Descriptor, transferred prefix, or wire-record byte count. */
} UmicomKernelFileResult;
typedef struct UmicomKernelFileInfo {
    UmicomU64 identity;
    UmicomU64 kind;
    UmicomU64 bytes;
    UmicomU64 maximumBytes;
} UmicomKernelFileInfo;
typedef struct UmicomKernelFileEntry {
    char name[UMICOM_VFS_NAME_BYTES];
    UmicomKernelFileInfo info;
} UmicomKernelFileEntry;
_Static_assert(sizeof(UmicomKernelFileRequest) == 48U, "File requests contain six words");
_Static_assert(sizeof(UmicomKernelFileResult) == 16U, "File results contain two words");
_Static_assert(sizeof(UmicomKernelFileInfo) == 32U, "File metadata contains four words");
_Static_assert(sizeof(UmicomKernelFileEntry) == 96U, "Directory wire records have no padding");

/* ECALL: a7=FILE; a0=request address; a1=result address; a2=48; a3=16.
 * Return a0=status. Other integer registers are preserved. If result memory is
 * invalid it is not written; a0 still reports BAD_USER_BUFFER. On a completed
 * backend request result.status equals a0, including a partial-I/O error.
 *
 * OPEN/CREATE/REMOVE: address=path, bytes=explicit length excluding terminator.
 * Embedded NUL is invalid. OPEN argument=rights, options=append (0 or 1).
 * CREATE/REMOVE argument=FILE or DIRECTORY, options=0, descriptor=0.
 * READ/WRITE: descriptor, address=buffer, bytes=0..4096, other words zero.
 * SEEK/RESIZE: descriptor, argument=absolute offset/length, other words zero.
 * DUPLICATE: descriptor, argument=reduced rights, other words zero.
 * QUERY/READ_DIRECTORY: descriptor, address=output, bytes=exact wire size.
 * CLOSE/REWIND_DIRECTORY: descriptor only; all remaining words zero.
 *
 * Accepted operations return to the machine dispatcher before invoking VFS.
 * Completion resumes after this ECALL, not at the program's entry point. No
 * user buffer is treated as a physical address and no Kernel pointer is returned.
 */
#endif /* UMICOM_KERNEL_FILE_ABI_H */
