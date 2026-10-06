/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/console_shell_validation.c
 *
 * PURPOSE:
 *   Drive the real command editor, filesystem and foreground runner without
 *   requiring terminal input in an automated guest. User programs are actual
 *   independently linked ELF images, not calls into Kernel test functions.
 *
 * AUTHOR AND ORGANISATION: Sammy Hegab, Umicom Foundation
 * LICENCE: MIT
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/console_shell.h"
#include "console_internal.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
extern const UmicomU8 UmicomEmbeddedExecutableStart[];
extern const UmicomU8 UmicomEmbeddedExecutableEnd[];
extern const UmicomU8 UmicomFileExecutableStart[];
extern const UmicomU8 UmicomFileExecutableEnd[];
static UmicomKernelConsoleShell umicomConsoleValidation;
static UmicomU64 umicomConsoleChecks;
static UmicomU64 umicomConsoleOutputBytes;
static void UmicomConsoleExpect(UmicomBoolean condition, const char *why)
{
    ++umicomConsoleChecks;
    if (condition) return;
    UmicomKernelConsoleWrite("console-shell.failure=");
    UmicomKernelConsoleWriteLine(why);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x96U);
    UmicomPlatformHalt();
}
static void UmicomConsoleObserve(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    for (UmicomSize i = 0U; i < bytes; ++i) {
        /* The editor may emit backspace/CR/LF. Arbitrary file controls must not
         * become terminal sequences in this automated transcript either. */
        const UmicomU8 byte = (UmicomU8)text[i];
        UmicomConsoleExpect(byte == 8U || byte == 10U || byte == 13U ||
            (byte >= 32U && byte <= 126U), "output is safe terminal text");
    }
    umicomConsoleOutputBytes += bytes;
}
static UmicomSize UmicomConsoleLength(const char *text)
{
    UmicomSize n = 0U; while (text[n] != '\0') ++n; return n;
}
static void UmicomConsoleCommand(const char *text)
{
    UmicomConsoleExpect(UmicomKernelConsoleShellExecute(&umicomConsoleValidation, text,
        UmicomConsoleLength(text)) == UMICOM_SHELL_OK, text);
}
static void UmicomConsoleRunToPrompt(void)
{
    for (UmicomSize i = 0U; umicomConsoleValidation.foreground != 0U && i < UMICOM_SHELL_SLICE_LIMIT + 8U; ++i)
        UmicomConsoleExpect(UmicomKernelConsoleShellStep(&umicomConsoleValidation) == UMICOM_SHELL_OK,
            "one existing supervisor quantum returns safely");
    UmicomConsoleExpect(umicomConsoleValidation.foreground == 0U, "bounded run returns the prompt");
}
void UmicomKernelConsoleShellValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("console-shell-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomConsoleClear(&before, sizeof(before));
    UmicomConsoleClear(&after, sizeof(after));
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomConsoleClear(&machineBefore, sizeof(machineBefore));
    UmicomConsoleClear(&machineAfter, sizeof(machineAfter));
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    UmicomConsoleExpect(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "snapshot before mounting the console filesystem");
    const UmicomKernelShellImage images[] = {
        {"/bin/umicom-diagnostic.elf", UmicomEmbeddedExecutableStart,
            (UmicomSize)(UmicomEmbeddedExecutableEnd - UmicomEmbeddedExecutableStart)},
        {"/bin/umicom-file-client.elf", UmicomFileExecutableStart,
            (UmicomSize)(UmicomFileExecutableEnd - UmicomFileExecutableStart)}
    };
    UmicomConsoleExpect(UmicomKernelConsoleShellInitialize(&umicomConsoleValidation,
        UmicomConsoleObserve, (void *)0, images, sizeof(images) / sizeof(images[0])) == UMICOM_SHELL_OK,
        "mount and seed one private shell filesystem");

    UmicomKernelConsoleWriteLine("console-shell.case=editing-and-file-commands");
    const char edited[] = "create /notes/draftx\b\r\n";
    /* Backspace removes the final x before Enter. The CR+LF pair must create
     * exactly one file rather than submit a second, empty command. */
    for (UmicomSize i = 0U; i < sizeof(edited)-1U; ++i)
        UmicomConsoleExpect(UmicomKernelConsoleShellFeed(&umicomConsoleValidation, (UmicomU8)edited[i]) == UMICOM_SHELL_OK,
            "feed editable serial-style input");
    UmicomConsoleCommand("stat /notes/draft");
    UmicomConsoleCommand("create /notes/plan");
    UmicomConsoleCommand("write /notes/plan \"Transfer reviewed\"");
    UmicomConsoleCommand("append /notes/plan \" and approved\"");
    UmicomConsoleCommand("ls /notes");
    UmicomConsoleCommand("stat /notes/plan");
    UmicomConsoleCommand("cat /notes/plan");
    UmicomKernelFileDescriptor fd = 0U;
    char result[64];
    UmicomConsoleClear(result, sizeof(result));
    UmicomSize count = 0U;
    UmicomConsoleExpect(UmicomKernelVfsOpen(&umicomConsoleValidation.client, "/notes/plan",
        UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &fd) == UMICOM_VFS_OK, "open command-created file");
    UmicomConsoleExpect(UmicomKernelVfsRead(&umicomConsoleValidation.client, fd, result,
        sizeof(result), &count) == UMICOM_VFS_OK, "read command-created contents");
    static const char expected[] = "Transfer reviewed and approved";
    UmicomConsoleExpect(count == sizeof(expected)-1U, "write and append lengths agree");
    for (UmicomSize i = 0U; i < count; ++i) UmicomConsoleExpect(result[i] == expected[i], "file bytes survive command parsing");
    UmicomConsoleExpect(UmicomKernelVfsClose(&umicomConsoleValidation.client, fd) == UMICOM_VFS_OK, "close independent checker");
    UmicomKernelConsoleWriteLine("console-shell.file-roundtrip=pass");

    UmicomKernelConsoleWriteLine("console-shell.case=rejected-input-has-no-side-effect");
    const char bad[] = "create /notes/rejected\x1b[A\r\n";
    for (UmicomSize i = 0U; i < sizeof(bad)-1U; ++i)
        (void)UmicomKernelConsoleShellFeed(&umicomConsoleValidation, (UmicomU8)bad[i]);
    UmicomConsoleExpect(UmicomKernelVfsOpen(&umicomConsoleValidation.client, "/notes/rejected",
        UMICOM_VFS_RIGHT_READ, UMICOM_FALSE, &fd) == UMICOM_VFS_NOT_FOUND, "escape sequence cannot execute a prefix");
    UmicomConsoleExpect(UmicomKernelConsoleShellExecute(&umicomConsoleValidation, "create /notes/\"bad", sizeof("create /notes/\"bad")-1U)
        != UMICOM_SHELL_OK, "unmatched quote refuses entire command");
    UmicomConsoleCommand("create /notes/not-an-elf");
    UmicomConsoleCommand("write /notes/not-an-elf \"not executable\"");
    UmicomConsoleExpect(UmicomKernelConsoleShellExecute(&umicomConsoleValidation,
        "run /notes/not-an-elf", sizeof("run /notes/not-an-elf")-1U) == UMICOM_SHELL_PROCESS_ERROR,
        "invalid file cannot become a runnable program");
    UmicomConsoleExpect(umicomConsoleValidation.foreground == 0U, "failed load publishes no foreground task");

    UmicomKernelConsoleWriteLine("console-shell.case=execute-file-and-recover-from-fault");
    UmicomConsoleCommand("run /bin/umicom-diagnostic.elf 7");
    for (UmicomSize i = 0U; i < sizeof(umicomConsoleValidation.image); ++i)
        UmicomConsoleExpect(umicomConsoleValidation.image[i] == 0U, "loaded image does not borrow staging bytes");
    UmicomConsoleRunToPrompt();
    UmicomConsoleExpect(umicomConsoleValidation.lastCompletion.state == UMICOM_USER_TASK_EXITED &&
        umicomConsoleValidation.lastCompletion.exitValue == umicomConsoleValidation.lastCompletion.identity + 40U,
        "independent diagnostic returns its expected value");
    UmicomConsoleCommand("run /bin/umicom-diagnostic.elf 2");
    UmicomConsoleRunToPrompt();
    UmicomConsoleExpect(umicomConsoleValidation.lastCompletion.state == UMICOM_USER_TASK_FAULTED &&
        umicomConsoleValidation.lastCompletion.trapCause == 15U, "readonly fault does not lose the shell");
    UmicomKernelConsoleWriteLine("console-shell.file-execution-and-fault-recovery=pass");

    UmicomKernelConsoleWriteLine("console-shell.case=explicit-file-authority");
    UmicomConsoleCommand("runrw /bin/umicom-file-client.elf 0");
    UmicomConsoleRunToPrompt();
    UmicomConsoleExpect(umicomConsoleValidation.lastCompletion.state == UMICOM_USER_TASK_EXITED &&
        umicomConsoleValidation.lastCompletion.exitValue == 0x6000U + umicomConsoleValidation.lastCompletion.identity,
        "writer uses real checked file services and terminal descriptor cleanup");
    UmicomConsoleCommand("run /bin/umicom-file-client.elf 1");
    UmicomConsoleRunToPrompt();
    UmicomConsoleExpect(umicomConsoleValidation.lastCompletion.state == UMICOM_USER_TASK_EXITED &&
        umicomConsoleValidation.lastCompletion.exitValue == 0x6100U + umicomConsoleValidation.lastCompletion.identity,
        "read-only client cannot escalate to shared-filesystem writing");
    UmicomKernelConsoleWriteLine("console-shell.process-file-services=pass");

    UmicomKernelConsoleWriteLine("console-shell.case=foreground-cancellation");
    UmicomConsoleCommand("run /bin/umicom-diagnostic.elf 3");
    UmicomConsoleExpect(UmicomKernelConsoleShellStep(&umicomConsoleValidation) == UMICOM_SHELL_OK,
        "non-yielding program is timer-bounded");
    UmicomConsoleExpect(umicomConsoleValidation.foreground != 0U, "busy program retains a continuation");
    UmicomConsoleExpect(UmicomKernelConsoleShellFeed(&umicomConsoleValidation, 3U) == UMICOM_SHELL_OK,
        "Ctrl-C cancels through original supervisor");
    UmicomConsoleExpect(umicomConsoleValidation.foreground == 0U &&
        umicomConsoleValidation.lastCompletion.state == UMICOM_USER_TASK_CANCELLED,
        "cancelled image is collected before the prompt returns");
    UmicomKernelConsoleWriteLine("console-shell.foreground-cancel=pass");

    UmicomKernelConsoleWriteLine("console-shell.case=orderly-ram-only-shutdown");
    UmicomConsoleCommand("poweroff");
    UmicomConsoleExpect(umicomConsoleValidation.exitRequested && umicomConsoleValidation.state == UMICOM_VFS_CLOSED,
        "shutdown closes file clients before unmounting storage");
    UmicomConsoleExpect(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        after.allocatedFrames == before.allocatedFrames && after.freeFrames == before.freeFrames &&
        after.reservedFrames == before.reservedFrames && UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK,
        "all temporary frames returned to their owner");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomConsoleExpect(machineBefore.mstatus == machineAfter.mstatus && machineBefore.mie == machineAfter.mie &&
        machineBefore.mtvec == machineAfter.mtvec && machineBefore.mscratch == machineAfter.mscratch &&
        machineBefore.medeleg == machineAfter.medeleg && machineBefore.mideleg == machineAfter.mideleg &&
        machineBefore.satp == machineAfter.satp && machineBefore.pmpcfg0 == machineAfter.pmpcfg0 &&
        machineBefore.pmpaddr0 == machineAfter.pmpaddr0 && machineBefore.mepc == machineAfter.mepc &&
        machineBefore.mcause == machineAfter.mcause && machineBefore.mtval == machineAfter.mtval,
        "shell preserves borrowed machine controls");
    UmicomConsoleExpect(umicomConsoleOutputBytes != 0U, "commands produce an observable transcript");
    UmicomKernelConsoleWriteLine("console-shell.machine-state=restored");
    UmicomKernelConsoleWriteLine("console-shell.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("console-shell.completed-cases=6");
    UmicomKernelConsoleWriteLine("console-shell-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_CONSOLE_SHELL_READY");
}
