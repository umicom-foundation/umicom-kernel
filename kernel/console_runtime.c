/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console_runtime.c
 *
 * PURPOSE:
 *   Keep the explicit interactive image alive at a serial prompt while the
 *   ordinary diagnostic image retains its existing automatic finish path.
 *
 * EDUCATIONAL NOTE:
 *   UART input is polled with interrupts disabled. WFI would be wrong here:
 *   no UART interrupt is enabled to wake a sleeping hart. This is deliberately
 *   a development console, not an energy-efficient terminal or full init system.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#ifdef UMICOM_KERNEL_SERVICE_CONSOLE
/* Optional live-service domain; the original boot-job report is unchanged. */
#include "umicom/kernel/service_console.h"
#endif
#include "umicom/kernel/console_shell.h"
#ifdef UMICOM_KERNEL_TERMINAL
#include "umicom/kernel/console_terminal.h"
#endif
#include "umicom/kernel/console_input.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#ifdef UMICOM_KERNEL_STARTUP_SERVICES
#include "umicom/kernel/startup.h"
#endif

extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
extern const UmicomU8 UmicomFileExecutableStart[];
extern const UmicomU8 UmicomFileExecutableEnd[];
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
extern const UmicomU8 UmicomLaunchExecutableStart[];
extern const UmicomU8 UmicomLaunchExecutableEnd[];
#endif
static UmicomKernelConsoleShell umicomInteractiveShell;
#ifdef UMICOM_KERNEL_TERMINAL
extern const UmicomU8 UmicomStreamExecutableStart[];
extern const UmicomU8 UmicomStreamExecutableEnd[];
static UmicomKernelConsoleTerminal umicomInteractiveTerminal;
#endif

static void UmicomInteractiveOutput(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    /* Forward exact byte counts. A file containing NUL never reaches this path
     * as an unchecked C string; the command engine already escaped controls. */
    for (UmicomSize i = 0U; i < bytes; ++i) UmicomPlatformConsoleWriteByte((UmicomU8)text[i]);
}
void UmicomKernelConsoleShellRun(void)
{
    const UmicomKernelShellImage images[] = {
#ifdef UMICOM_KERNEL_PROGRAM_LAUNCH
        {"/bin/umicom-launch-client.elf", UmicomLaunchExecutableStart,
            (UmicomSize)(UmicomLaunchExecutableEnd - UmicomLaunchExecutableStart)},
#endif
#ifdef UMICOM_KERNEL_TERMINAL
        {"/bin/umicom-stream-client.elf", UmicomStreamExecutableStart,
            (UmicomSize)(UmicomStreamExecutableEnd - UmicomStreamExecutableStart)},
#endif
        {"/bin/umicom-diagnostic.elf", UmicomEmbeddedExecutableStart,
            (UmicomSize)(UmicomEmbeddedExecutableEnd - UmicomEmbeddedExecutableStart)},
        {"/bin/umicom-file-client.elf", UmicomFileExecutableStart,
            (UmicomSize)(UmicomFileExecutableEnd - UmicomFileExecutableStart)}
    };
    if (UmicomKernelConsoleShellInitialize(&umicomInteractiveShell, UmicomInteractiveOutput,
            (void *)0, images, sizeof(images) / sizeof(images[0])) != UMICOM_SHELL_OK) {
        (void)UmicomKernelConsoleShellClose(&umicomInteractiveShell);
#ifdef UMICOM_KERNEL_STARTUP_SERVICES
        /* Recovery cannot allocate or restart a partially initialised owner. */
        UmicomKernelNormalBootRecover("console-initialisation");
#endif
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x94U);
        UmicomPlatformHalt();
    }
#ifdef UMICOM_KERNEL_TERMINAL
    if (UmicomKernelConsoleTerminalAttach(&umicomInteractiveTerminal, &umicomInteractiveShell) != UMICOM_SHELL_OK) {
        (void)UmicomKernelConsoleShellClose(&umicomInteractiveShell);
#ifdef UMICOM_KERNEL_STARTUP_SERVICES
        /* Recovery cannot allocate or restart a partially initialised owner. */
        UmicomKernelNormalBootRecover("console-initialisation");
#endif
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
        UmicomPlatformFinishFailure(0x96U);
        UmicomPlatformHalt();
    }
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_CONSOLE_READY");
    UmicomKernelConsoleWriteLine("Umicom Kernel development console. Type help.");
    /* Preserve the original volatile-only description for review. The trusted
     * explicit FAT16 utility now has its own write/flush persistence path. */
#if 0
    UmicomKernelConsoleWriteLine("RAM-only data; no login, disk persistence or host filesystem access.");
#endif
    UmicomKernelConsoleWriteLine("RAMFS is volatile. Explicit FAT16 data updates use fatwrite and fatflush.");
    UmicomKernelConsoleWriteLine("Ordered FAT16 updates use fatcommitopen, fatstage and fatcommit; unfinished stages remain dirty.");
    UmicomKernelConsoleWriteLine("Timestamped FAT16 file updates use fatfileopen, fatfilestage and fatfilecommit; supply the calendar time explicitly.");
    UmicomKernelConsoleShellPrompt(&umicomInteractiveShell);
    while (!umicomInteractiveShell.exitRequested) {
#ifdef UMICOM_KERNEL_SERVICE_CONSOLE
        /* One bounded background quantum precedes input draining. Foreground
         * execution uses its original path after the machine state is restored. */
        if (UmicomKernelServiceConsolePoll(&umicomInteractiveShell) != UMICOM_SHELL_OK) {
            UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
            UmicomPlatformFinishFailure(0xb6U);
            UmicomPlatformHalt();
        }
#endif
        /* Bound input draining so a stream of characters cannot starve the
         * foreground program. Its original timer bounds each admitted quantum. */
        for (UmicomSize i = 0U; i < 16U && !umicomInteractiveShell.exitRequested; ++i) {
            UmicomU8 byte = 0U;
            const UmicomKernelConsoleInputStatus input = UmicomPlatformConsoleTryReadByte(&byte);
            if (input == UMICOM_CONSOLE_INPUT_IDLE) break;
            if (input == UMICOM_CONSOLE_INPUT_BYTE) (void)UmicomKernelConsoleShellFeed(&umicomInteractiveShell, byte);
            else UmicomKernelConsoleShellInputLost(&umicomInteractiveShell);
        }
        if (umicomInteractiveShell.foreground != 0U) {
            const UmicomKernelShellStatus step = UmicomKernelConsoleShellStep(&umicomInteractiveShell);
            if (step == UMICOM_SHELL_UNSAFE || umicomInteractiveShell.state == UMICOM_VFS_POISONED ||
                (step != UMICOM_SHELL_OK && umicomInteractiveShell.foreground != 0U)) {
                /* A prompt after an unverified Kernel return would imply that
                 * file operations were safe. Retain ownership and stop instead. */
                UmicomKernelConsoleWriteLine("console: foreground lifecycle failure; stopping without forced reclamation");
                UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
                UmicomPlatformFinishFailure(0x95U);
                UmicomPlatformHalt();
            }
        }
    }
    /* poweroff already completed orderly task/file cleanup. Returning uses the
     * original UMICOM_KERNEL_END and finisher code rather than duplicating it. */
}
