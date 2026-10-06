/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: include/umicom/kernel/console_shell.h
 *
 * PURPOSE:
 *   Provide a bounded development console over the existing RAM filesystem,
 *   process supervisor and checked process file services.
 *
 * EDUCATIONAL OVERVIEW:
 *   This is a trusted machine-mode administration front end, not a user shell,
 *   login service or security boundary. It owns one RAM-only filesystem and one
 *   foreground process. The command parser never follows user-space pointers;
 *   loaded programs still enter through the established checked user boundary.
 *
 *   Stable, initially zero-filled storage is required. Closing releases owned
 *   resources in dependency order; it does not reset or reuse live generations.
 *   Calls and output callbacks are serial and may not re-enter this owner.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_CONSOLE_SHELL_H
#define UMICOM_KERNEL_CONSOLE_SHELL_H
#include "umicom/kernel/ramfs.h"
#include "umicom/kernel/process_supervisor.h"
#include "umicom/kernel/user_files.h"

#define UMICOM_SHELL_LINE_BYTES 512U
#define UMICOM_SHELL_ARGUMENT_LIMIT 4U
/* Structured launch adds a command token plus up to sixteen argv elements. */
#define UMICOM_SHELL_TOKEN_LIMIT 17U
#define UMICOM_SHELL_IMAGE_BYTES 131072U
#define UMICOM_SHELL_QUANTUM_TICKS 50000U
#define UMICOM_SHELL_SLICE_LIMIT 256U

typedef enum UmicomKernelConsoleLineEvent {
    UMICOM_CONSOLE_LINE_NONE,
    UMICOM_CONSOLE_LINE_READY,
    UMICOM_CONSOLE_LINE_CANCEL,
    UMICOM_CONSOLE_LINE_REJECTED
} UmicomKernelConsoleLineEvent;
typedef struct UmicomKernelConsoleLine {
    char bytes[UMICOM_SHELL_LINE_BYTES];
    UmicomSize length;
    UmicomBoolean discard; /* A damaged prefix cannot become a different command. */
    UmicomBoolean afterCr; /* One CR+LF pair submits once, not twice. */
} UmicomKernelConsoleLine;
typedef struct UmicomKernelShellCommand {
    char bytes[UMICOM_SHELL_LINE_BYTES]; /* Token copies, not borrowed input. */
    UmicomSize offsets[UMICOM_SHELL_ARGUMENT_LIMIT];
    UmicomSize count;
} UmicomKernelShellCommand;
typedef enum UmicomKernelShellStatus {
    UMICOM_SHELL_OK,
    UMICOM_SHELL_INVALID_ARGUMENT,
    UMICOM_SHELL_BAD_STATE,
    UMICOM_SHELL_SYNTAX,
    UMICOM_SHELL_UNKNOWN_COMMAND,
    UMICOM_SHELL_IO_ERROR,
    UMICOM_SHELL_PROCESS_ERROR,
    UMICOM_SHELL_BUSY,
    UMICOM_SHELL_UNSAFE,
    UMICOM_SHELL_CLEANUP_FAILED,
    UMICOM_SHELL_RANGE
} UmicomKernelShellStatus;

/* The callback writes exactly bytes characters, which need not end in NUL.
 * Only printable text and console-generated CR/LF/editing controls are emitted.
 * The callback and context must outlive the shell, and must not re-enter it. */
typedef void (*UmicomKernelShellOutput)(void *context, const char *text, UmicomSize bytes);
typedef struct UmicomKernelShellImage {
    const char *path;             /* Canonical absolute Kernel-owned pathname. */
    const UmicomU8 *bytes;        /* Immutable executable input, copied into RAMFS. */
    UmicomSize count;
} UmicomKernelShellImage;
typedef struct UmicomKernelConsoleShell {
    const struct UmicomKernelConsoleShell *self;
    UmicomKernelVfsLifetime state;
    UmicomBoolean busy;
    UmicomBoolean exitRequested;
    UmicomKernelShellOutput output;
    void *outputContext;
    /* Optional terminal attachment leaves the original command-only path available. */
    struct UmicomKernelConsoleTerminal *terminal;
    /* Borrowed only during synchronous admission; cleared before a quantum. */
    const UmicomKernelProgramLaunchSpec *launchSpec;
    UmicomKernelConsoleLine line;
    UmicomKernelRamfs storage;
    UmicomKernelVfs vfs;
    UmicomKernelVfsClient client;
    UmicomKernelProcessSupervisor supervisor;
    UmicomKernelUserFiles files;
    UmicomKernelFileDescriptor descriptor; /* A failed close remains retryable. */
    UmicomKernelSupervisedProcessHandle foreground;
    UmicomKernelProcessCompletion lastCompletion;
    UmicomBoolean hasCompletion;
    UmicomBoolean inputDuringRun; /* Discard a partial line typed into a busy foreground. */
    UmicomKernelVfsStatus lastFileStatus;
    UmicomKernelSupervisionStatus lastProcessStatus;
    UmicomU64 commands;
    UmicomU8 io[256U];
    UmicomU8 image[UMICOM_SHELL_IMAGE_BYTES]; /* Bounded snapshot; scrubbed after Spawn. */
} UmicomKernelConsoleShell;

/* Input is ASCII. Backspace/DEL edit, Ctrl-U clears and Ctrl-C cancels.
 * Unsupported controls, NUL, ESC and overflow discard through the next newline.
 * A rejected line is never executed as a truncated command. On READY, the
 * caller consumes the line before clearing it; afterCr survives that clear. */
UmicomKernelConsoleLineEvent UmicomKernelConsoleLineFeed(UmicomKernelConsoleLine *line, UmicomU8 byte);
void UmicomKernelConsoleLineConsume(UmicomKernelConsoleLine *line);
/* Parse a complete length-delimited line. Quotes must surround a whole token;
 * no escapes, interpolation, pipelines or multi-command syntax are evaluated. */
UmicomKernelShellStatus UmicomKernelShellParse(const char *line, UmicomSize bytes, UmicomKernelShellCommand *out);
UmicomBoolean UmicomKernelShellUnsigned(const char *text, UmicomU64 *out);

/* Initialise once, mount RAMFS, attach file services and install supplied seed
 * images under /bin. On partial setup failure, Close still owns rollback.
 * The regular startup also creates /notes, /records, /shared and a short README. */
UmicomKernelShellStatus UmicomKernelConsoleShellInitialize(UmicomKernelConsoleShell *shell,
    UmicomKernelShellOutput output, void *context, const UmicomKernelShellImage *images, UmicomSize imageCount);
UmicomKernelShellStatus UmicomKernelConsoleShellExecute(UmicomKernelConsoleShell *shell,
    const char *line, UmicomSize bytes);
/* Polling owns no user execution stack: each Step admits at most one existing
 * timer-bounded quantum. Feed can process Ctrl-C between those quanta. Ordinary
 * input while a foreground runs is discarded, not queued as a later command. */
UmicomKernelShellStatus UmicomKernelConsoleShellStep(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelConsoleShellFeed(UmicomKernelConsoleShell *shell, UmicomU8 byte);
void UmicomKernelConsoleShellInputLost(UmicomKernelConsoleShell *shell);
void UmicomKernelConsoleShellPrompt(UmicomKernelConsoleShell *shell);
UmicomKernelShellStatus UmicomKernelConsoleShellClose(UmicomKernelConsoleShell *shell);
void UmicomKernelConsoleShellValidateExecution(void);
void UmicomKernelConsoleShellRun(void);
/* Shared tokenizer. text has LINE_BYTES storage; offsets has capacity entries.
 * Inputs/outputs are trusted non-overlapping Kernel buffers. Complete failure
 * leaves all outputs unchanged. Legacy Parse retains its four-token ceiling. */
UmicomKernelShellStatus UmicomKernelShellTokenize(const char *line, UmicomSize bytes,
    char *text, UmicomSize *offsets, UmicomSize capacity, UmicomSize *outCount);
#endif /* UMICOM_KERNEL_CONSOLE_SHELL_H */
