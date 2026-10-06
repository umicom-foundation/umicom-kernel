/*-----------------------------------------------------------------------------
 * Umicom Kernel structured-launch acceptance
 * File: kernel/program_launch_validation.c
 *
 * These cases use the actual command engine, loader, private user stacks and
 * standard streams. Only the host-side native tests substitute instruction
 * execution. Guest readiness is emitted after collection and frame accounting.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/program_launch.h"
#include "umicom/kernel/console_terminal.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "console_internal.h"

extern const UmicomU8 UmicomLaunchExecutableStart[], UmicomLaunchExecutableEnd[];
extern const UmicomU8 UmicomEmbeddedExecutableStart[], UmicomEmbeddedExecutableEnd[];
static UmicomKernelConsoleShell umicomLaunchShell;
static UmicomKernelConsoleTerminal umicomLaunchTerminal;
static char umicomLaunchTranscript[8192];
static UmicomSize umicomLaunchTranscriptBytes;
static UmicomU64 umicomLaunchChecks;
static void UmicomLaunchRequire(UmicomBoolean condition, const char *reason)
{
    ++umicomLaunchChecks;
    if (condition) return;
    UmicomKernelConsoleWrite("program-launch.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x98U);
    UmicomPlatformHalt();
}
static void UmicomLaunchCapture(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    UmicomLaunchRequire(bytes <= sizeof(umicomLaunchTranscript) - umicomLaunchTranscriptBytes,
        "bounded launch transcript");
    for (UmicomSize i = 0U; i < bytes; ++i) umicomLaunchTranscript[umicomLaunchTranscriptBytes++] = text[i];
}
static UmicomBoolean UmicomLaunchContains(const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    for (UmicomSize i = 0U; bytes <= umicomLaunchTranscriptBytes && i <= umicomLaunchTranscriptBytes - bytes; ++i) {
        UmicomBoolean same = UMICOM_TRUE;
        for (UmicomSize j = 0U; j < bytes; ++j)
            if (umicomLaunchTranscript[i+j] != text[j]) same = UMICOM_FALSE;
        if (same) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static void UmicomLaunchCommand(const char *command)
{
    UmicomSize length = 0U;
    while (command[length]) ++length;
    umicomLaunchTranscriptBytes = 0U;
    UmicomConsoleClear(umicomLaunchTranscript, sizeof(umicomLaunchTranscript));
    UmicomLaunchRequire(UmicomKernelConsoleShellExecute(&umicomLaunchShell, command, length)
        == UMICOM_SHELL_OK, "admit foreground launch");
}
static void UmicomLaunchFinish(void)
{
    for (UmicomSize i = 0U; i < UMICOM_SHELL_SLICE_LIMIT + 16U && umicomLaunchShell.foreground; ++i)
        UmicomLaunchRequire(UmicomKernelConsoleShellStep(&umicomLaunchShell) == UMICOM_SHELL_OK,
            "execute and collect foreground without another entry path");
    UmicomLaunchRequire(!umicomLaunchShell.foreground && umicomLaunchShell.hasCompletion &&
        umicomLaunchShell.lastCompletion.state == UMICOM_USER_TASK_EXITED, "program exited and was collected");
}
static UmicomBoolean UmicomLaunchMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval ? UMICOM_TRUE : UMICOM_FALSE;
}
void UmicomKernelProgramLaunchValidateExecution(void)
{
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomConsoleClear(&before, sizeof(before));
    UmicomConsoleClear(&after, sizeof(after));
    UmicomLaunchRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK,
        "capture allocation baseline");
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    const UmicomU64 timer = UmicomPlatformTimerCompareRead(0U);
    UmicomKernelConsoleWriteLine("program-launch-test=begin");
    const UmicomKernelShellImage images[] = {
        {"/bin/umicom-launch-client.elf", UmicomLaunchExecutableStart,
            (UmicomSize)(UmicomLaunchExecutableEnd-UmicomLaunchExecutableStart)},
        {"/bin/umicom-diagnostic.elf", UmicomEmbeddedExecutableStart,
            (UmicomSize)(UmicomEmbeddedExecutableEnd-UmicomEmbeddedExecutableStart)}
    };
    UmicomLaunchRequire(UmicomKernelConsoleShellInitialize(&umicomLaunchShell, UmicomLaunchCapture,
        (void *)0, images, 2U) == UMICOM_SHELL_OK, "seed actual independently linked executables");
    UmicomLaunchRequire(UmicomKernelConsoleTerminalAttach(&umicomLaunchTerminal, &umicomLaunchShell)
        == UMICOM_SHELL_OK, "attach existing foreground terminal");

    UmicomKernelConsoleWriteLine("program-launch.case=quoted-and-empty-arguments");
    UmicomLaunchCommand("exec /bin/umicom-launch-client.elf transfer \"Account one\" \"\" 250 approved");
    const UmicomKernelSupervisedProcessHandle first = umicomLaunchShell.foreground;
    UmicomLaunchRequire(UmicomKernelProcessSupervisorSetArgument(&umicomLaunchShell.supervisor,
        UMICOM_SUPERVISION_GUARDIAN, first, 99U) != UMICOM_SUPERVISION_OK,
        "numeric setter cannot corrupt a committed argc");
    UmicomLaunchFinish();
    UmicomLaunchRequire(umicomLaunchShell.lastCompletion.exitValue == 0U &&
        UmicomLaunchContains("argc=6") && UmicomLaunchContains("argv[2]=<Account one>") &&
        UmicomLaunchContains("argv[3]=<>") && UmicomLaunchContains("env[0]=<LANG=C>") &&
        UmicomLaunchContains("env[1]=<UMICOM_CONSOLE=serial>") && UmicomLaunchContains("launch-context=valid"),
        "real user code observed copied arguments, terminators and explicit environment");
    UmicomKernelConsoleWriteLine("program-launch.argv-envp=verified-in-user-mode");

    UmicomKernelConsoleWriteLine("program-launch.case=maximum-argument-count");
    UmicomLaunchCommand("exec /bin/umicom-launch-client.elf a b c d e f g h i j k l m n o");
    UmicomLaunchFinish();
    UmicomLaunchRequire(umicomLaunchShell.lastCompletion.exitValue == 0U &&
        UmicomLaunchContains("argc=16") && UmicomLaunchContains("argv[15]=<o>"), "all admitted argv entries arrived");

    UmicomKernelConsoleWriteLine("program-launch.case=syntax-refusal-without-execution");
    const UmicomU64 dispatches = umicomLaunchShell.supervisor.scheduler.dispatches;
    static const char badQuote[] = "exec /bin/umicom-launch-client.elf \"unfinished";
    UmicomLaunchRequire(UmicomKernelConsoleShellExecute(&umicomLaunchShell, badQuote, sizeof(badQuote)-1U)
        == UMICOM_SHELL_SYNTAX && !umicomLaunchShell.foreground &&
        umicomLaunchShell.supervisor.scheduler.dispatches == dispatches, "late syntax error created no task");

    UmicomKernelConsoleWriteLine("program-launch.case=legacy-numeric-entry");
    UmicomLaunchCommand("run /bin/umicom-diagnostic.elf 7");
    UmicomLaunchFinish();
    UmicomLaunchRequire(umicomLaunchShell.lastCompletion.exitValue == umicomLaunchShell.lastCompletion.identity + 40U,
        "existing numeric calling convention remains intact");
    UmicomKernelConsoleWriteLine("program-launch.numeric-entry=preserved");

    UmicomKernelConsoleWriteLine("program-launch.case=fresh-image-after-collection");
    UmicomLaunchCommand("execrw /bin/umicom-launch-client.elf replacement");
    UmicomLaunchRequire(umicomLaunchShell.foreground != first, "new lifetime has a distinct task token");
    UmicomLaunchFinish();
    UmicomLaunchRequire(umicomLaunchShell.lastCompletion.exitValue == 0U &&
        UmicomLaunchContains("argc=2") && UmicomLaunchContains("argv[1]=<replacement>") &&
        !UmicomLaunchContains("Account one"), "no earlier argv bytes are inherited");
    UmicomLaunchRequire(UmicomKernelConsoleShellClose(&umicomLaunchShell) == UMICOM_SHELL_OK,
        "collect tasks, drain output and close file owners");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomLaunchRequire(UmicomLaunchMachineEqual(&machineBefore, &machineAfter) &&
        UmicomPlatformTimerCompareRead(0U) == timer, "machine controls restored");
    UmicomLaunchRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        after.allocatedFrames == before.allocatedFrames && after.reservedFrames == before.reservedFrames &&
        after.freeFrames == before.freeFrames, "no launch or filesystem frames leaked");
    UmicomKernelConsoleWriteLine("program-launch.machine-state=restored");
    UmicomKernelConsoleWriteLine("program-launch.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("program-launch.completed-cases=5");
    UmicomKernelConsoleWriteLine("program-launch-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_PROGRAM_LAUNCH_READY");
}
