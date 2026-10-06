/*-----------------------------------------------------------------------------
 * Umicom Kernel standard-stream guest acceptance
 * File: kernel/standard_streams_validation.c
 *
 * The ELF below is separately linked and loaded through the established owner.
 * Its READ/WRITE instructions really enter the user trap path in a QEMU run.
 * Native tests of queue policy must not be presented as this hardware evidence.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/user_streams.h"
#include "umicom/kernel/console_terminal.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/riscv64/supervisor.h"
#include "console_internal.h"

extern const UmicomU8 UmicomStreamExecutableStart[];
extern const UmicomU8 UmicomStreamExecutableEnd[];
static UmicomKernelUserScheduler umicomStreamScheduler;
static UmicomKernelUserStreams umicomStreamDomain;
static UmicomKernelConsoleShell umicomStreamShell;
static UmicomKernelConsoleTerminal umicomStreamTerminal;
static UmicomU64 umicomStreamChecks;
static UmicomU8 umicomStreamTranscript[8192];
static UmicomSize umicomStreamTranscriptBytes;
static UmicomSize umicomStreamPackets;
static UmicomSize umicomStreamErrors;
static void UmicomStreamsRequire(UmicomBoolean ok, const char *reason)
{
    ++umicomStreamChecks;
    if (ok) return;
    UmicomKernelConsoleWrite("standard-streams.failure=");
    UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0x97U);
    UmicomPlatformHalt();
}
static void UmicomStreamsResetCapture(void)
{
    UmicomConsoleClear(umicomStreamTranscript, sizeof(umicomStreamTranscript));
    umicomStreamTranscriptBytes = 0U;
    umicomStreamPackets = 0U;
    umicomStreamErrors = 0U;
}
static void UmicomStreamsCapture(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    UmicomStreamsRequire(bytes <= sizeof(umicomStreamTranscript) - umicomStreamTranscriptBytes,
        "bounded diagnostic transcript");
    for (UmicomSize i = 0U; i < bytes; ++i) umicomStreamTranscript[umicomStreamTranscriptBytes++] = (UmicomU8)text[i];
}
static UmicomBoolean UmicomStreamsContains(const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    for (UmicomSize i = 0U; bytes <= umicomStreamTranscriptBytes && i <= umicomStreamTranscriptBytes - bytes; ++i) {
        UmicomBoolean equal = UMICOM_TRUE;
        for (UmicomSize j = 0U; j < bytes; ++j)
            if (umicomStreamTranscript[i+j] != (UmicomU8)text[j]) equal = UMICOM_FALSE;
        if (equal) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static UmicomKernelUserTaskHandle UmicomStreamsSpawn(UmicomU64 mode, UmicomU64 budget, UmicomBoolean grant)
{
    UmicomKernelUserTaskHandle task = 0U;
    UmicomStreamsRequire(UmicomKernelUserTaskCreate(&umicomStreamScheduler, UmicomStreamExecutableStart,
        (UmicomSize)(UmicomStreamExecutableEnd-UmicomStreamExecutableStart), mode, budget, &task)
        == UMICOM_USER_SCHEDULE_OK, "load independent stream client");
    if (grant) UmicomStreamsRequire(UmicomKernelUserStreamsGrant(&umicomStreamDomain, task)
        == UMICOM_STREAM_OK, "grant this task's three directions");
    return task;
}
static UmicomKernelUserTaskInfo UmicomStreamsInfo(UmicomKernelUserTaskHandle task)
{
    UmicomKernelUserTaskInfo info;
    UmicomConsoleClear(&info, sizeof(info));
    UmicomStreamsRequire(UmicomKernelUserTaskQuery(&umicomStreamScheduler, task, &info)
        == UMICOM_USER_SCHEDULE_OK, "query retained process state");
    return info;
}
static UmicomKernelStreamInfo UmicomStreamsQueue(UmicomKernelUserTaskHandle task)
{
    UmicomKernelStreamInfo info = {0};
    UmicomStreamsRequire(UmicomKernelUserStreamsQuery(&umicomStreamDomain, task, &info)
        == UMICOM_STREAM_OK, "query Kernel-owned stream state");
    return info;
}
static void UmicomStreamsRun(void)
{
    UmicomKernelUserTaskHandle selected = 0U;
    const UmicomKernelUserScheduleStatus status = UmicomKernelUserSchedulerRunOne(&umicomStreamScheduler, 100000U, &selected);
    UmicomStreamsRequire(status == UMICOM_USER_SCHEDULE_OK || status == UMICOM_USER_SCHEDULE_IDLE,
        "run or honestly report an input/output wait");
}
static void UmicomStreamsDrain(UmicomKernelUserTaskHandle task, UmicomBoolean ordered)
{
    for (UmicomSize i = 0U; i < UMICOM_STREAM_OUTPUT_RECORDS; ++i) {
        UmicomKernelStreamPacket packet;
        UmicomConsoleClear(&packet, sizeof(packet));
        const UmicomKernelStreamStatus status = UmicomKernelUserStreamsDrain(&umicomStreamDomain, task, &packet);
        if (status == UMICOM_STREAM_WOULD_BLOCK) break;
        UmicomStreamsRequire(status == UMICOM_STREAM_OK, "drain accepted output even after exit");
        if (ordered) UmicomStreamsRequire(packet.selector == (umicomStreamPackets % 2U ? UMICOM_STREAM_ERROR : UMICOM_STREAM_OUTPUT),
            "stdout/stderr order across backpressure");
        ++umicomStreamPackets;
        if (packet.selector == UMICOM_STREAM_ERROR) ++umicomStreamErrors;
        UmicomStreamsCapture((void *)0, (const char *)packet.data, packet.bytes);
    }
}
static void UmicomStreamsWaitFor(UmicomKernelUserTaskHandle task, UmicomKernelStreamWait wait)
{
    for (UmicomSize i = 0U; i < 100U && UmicomStreamsQueue(task).wait != wait; ++i) UmicomStreamsRun();
    UmicomStreamsRequire(UmicomStreamsQueue(task).wait == wait, "program reached a real stream wait");
    const UmicomKernelUserTaskInfo before = UmicomStreamsInfo(task);
    for (UmicomSize i = 0U; i < 4U; ++i) {
        UmicomKernelUserTaskHandle selected = 0xeeeeU;
        UmicomStreamsRequire(UmicomKernelUserSchedulerRunOne(&umicomStreamScheduler, 100000U, &selected)
            == UMICOM_USER_SCHEDULE_IDLE && selected == 0xeeeeU, "waiting task spends no user dispatch");
    }
    const UmicomKernelUserTaskInfo after = UmicomStreamsInfo(task);
    UmicomStreamsRequire(before.slices == after.slices && before.systemCalls == after.systemCalls &&
        before.resumePc == after.resumePc, "waiting preserves budgets and continuation");
}
static void UmicomStreamsFinish(UmicomKernelUserTaskHandle task, UmicomBoolean ordered)
{
    for (UmicomSize i = 0U; i < 100U; ++i) {
        UmicomStreamsDrain(task, ordered);
        const UmicomKernelUserTaskInfo info = UmicomStreamsInfo(task);
        if (info.state != UMICOM_USER_TASK_READY && info.state != UMICOM_USER_TASK_PAUSED) return;
        UmicomStreamsRun();
    }
    UmicomStreamsRequire(UMICOM_FALSE, "bounded completion after input/readiness");
}
static void UmicomStreamsReap(UmicomKernelUserTaskHandle task)
{
    UmicomStreamsRequire(UmicomKernelUserTaskReap(&umicomStreamScheduler, task)
        == UMICOM_USER_SCHEDULE_OK, "reap after accepted output is drained");
}
static UmicomBoolean UmicomStreamsMachineEqual(const UmicomRiscvSupervisorMachineState *a,
    const UmicomRiscvSupervisorMachineState *b)
{
    return a->mstatus == b->mstatus && a->mie == b->mie && a->mtvec == b->mtvec &&
        a->mscratch == b->mscratch && a->medeleg == b->medeleg && a->mideleg == b->mideleg &&
        a->satp == b->satp && a->pmpcfg0 == b->pmpcfg0 && a->pmpaddr0 == b->pmpaddr0 &&
        a->mepc == b->mepc && a->mcause == b->mcause && a->mtval == b->mtval ? UMICOM_TRUE : UMICOM_FALSE;
}
void UmicomKernelStandardStreamsValidateExecution(void)
{
    UmicomKernelConsoleWriteLine("standard-streams-test=begin");
    UmicomKernelPhysicalMemorySnapshot before, after;
    UmicomConsoleClear(&before, sizeof(before));
    UmicomConsoleClear(&after, sizeof(after));
    UmicomRiscvSupervisorMachineState machineBefore, machineAfter;
    UmicomRiscvSupervisorMachineStateRead(&machineBefore);
    const UmicomU64 compare = UmicomPlatformTimerCompareRead(0U);
    UmicomStreamsRequire(UmicomKernelPhysicalMemorySnapshotRead(&before) == UMICOM_KERNEL_MEMORY_OK, "snapshot frame baseline");
    UmicomStreamsRequire(UmicomKernelUserSchedulerInitialize(&umicomStreamScheduler) == UMICOM_USER_SCHEDULE_OK,
        "initialise scheduler once");
    UmicomStreamsRequire(UmicomKernelUserStreamsAttach(&umicomStreamDomain, &umicomStreamScheduler) == UMICOM_STREAM_OK,
        "attach streams before admission");

    UmicomKernelConsoleWriteLine("standard-streams.case=waiting-input-and-cross-page-echo");
    UmicomStreamsResetCapture();
    UmicomKernelUserTaskHandle task = UmicomStreamsSpawn(0U, 128U, UMICOM_TRUE);
    UmicomStreamsWaitFor(task, UMICOM_STREAM_WAIT_INPUT);
    static const UmicomU8 line[] = "Transfer approved across pages.\n";
    UmicomStreamsRequire(UmicomKernelUserStreamsInput(&umicomStreamDomain, task, line, sizeof(line)-1U)
        == UMICOM_STREAM_OK, "provide one complete line");
    UmicomStreamsFinish(task, UMICOM_FALSE);
    UmicomStreamsRequire(UmicomStreamsInfo(task).state == UMICOM_USER_TASK_EXITED && UmicomStreamsInfo(task).exitValue == 0U &&
        UmicomStreamsContains("Transfer approved across pages.") && umicomStreamErrors == 1U, "resume, copy and distinguish stderr");
    UmicomStreamsReap(task);
    UmicomKernelConsoleWriteLine("standard-streams.input-resume-without-spin=pass");

    UmicomKernelConsoleWriteLine("standard-streams.case=output-backpressure-and-order");
    UmicomStreamsResetCapture();
    task = UmicomStreamsSpawn(1U, 128U, UMICOM_TRUE);
    UmicomStreamsWaitFor(task, UMICOM_STREAM_WAIT_OUTPUT);
    UmicomStreamsRequire(UmicomStreamsQueue(task).outputRecords == UMICOM_STREAM_OUTPUT_RECORDS, "full output queue retained");
    UmicomStreamsFinish(task, UMICOM_TRUE);
    UmicomStreamsRequire(umicomStreamPackets == 12U && umicomStreamErrors == 6U &&
        UmicomStreamsInfo(task).exitValue == 0U && UmicomStreamsInfo(task).state == UMICOM_USER_TASK_EXITED,
        "twelve ordered writes survive suspension");
    UmicomStreamsReap(task);
    UmicomKernelConsoleWriteLine("standard-streams.stdout-stderr-order=preserved");

    UmicomKernelConsoleWriteLine("standard-streams.case=eof-and-refusal-status");
    UmicomStreamsResetCapture();
    task = UmicomStreamsSpawn(0U, 128U, UMICOM_TRUE);
    UmicomStreamsWaitFor(task, UMICOM_STREAM_WAIT_INPUT);
    UmicomStreamsRequire(UmicomKernelUserStreamsEndInput(&umicomStreamDomain, task) == UMICOM_STREAM_OK, "close stdin only");
    UmicomStreamsFinish(task, UMICOM_FALSE);
    UmicomStreamsRequire(UmicomStreamsInfo(task).state == UMICOM_USER_TASK_EXITED &&
        UmicomStreamsInfo(task).exitValue == 0U && UmicomStreamsContains("End of input."), "EOF is not empty-live input");
    UmicomStreamsReap(task);
    task = UmicomStreamsSpawn(2U, 128U, UMICOM_TRUE);
    UmicomStreamsFinish(task, UMICOM_FALSE);
    UmicomStreamsRequire(UmicomStreamsInfo(task).state == UMICOM_USER_TASK_EXITED && UmicomStreamsInfo(task).exitValue == 0U,
        "bad directions, addresses and nonblocking empty input refused");
    UmicomStreamsReap(task);

    UmicomKernelConsoleWriteLine("standard-streams.case=terminal-output-survives-fault-and-budget");
    for (UmicomU64 mode = 3U; mode <= 5U; ++mode) {
        task = UmicomStreamsSpawn(mode, 8U, UMICOM_TRUE);
        for (UmicomSize i = 0U; i < 30U; ++i) {
            const UmicomKernelUserTaskInfo info = UmicomStreamsInfo(task);
            if (info.state != UMICOM_USER_TASK_READY && info.state != UMICOM_USER_TASK_PAUSED) break;
            UmicomStreamsRun();
        }
        const UmicomKernelUserTaskState expected = mode == 3U ? UMICOM_USER_TASK_FAULTED :
            mode == 4U ? UMICOM_USER_TASK_EXHAUSTED : UMICOM_USER_TASK_EXITED;
        UmicomStreamsRequire(UmicomStreamsInfo(task).state == expected, "real fault, timer budget or exit");
        UmicomStreamsRequire(UmicomKernelUserTaskReap(&umicomStreamScheduler, task)
            == UMICOM_USER_SCHEDULE_CLEANUP_FAILED, "collection refuses undrained output");
        UmicomStreamsResetCapture();
        UmicomStreamsDrain(task, UMICOM_FALSE);
        UmicomStreamsRequire(UmicomStreamsContains("Output accepted before stopping."), "output survives its producer");
        UmicomStreamsReap(task);
    }
    UmicomKernelConsoleWriteLine("standard-streams.terminal-output=retained-until-drained");

    UmicomKernelConsoleWriteLine("standard-streams.case=cancel-wait-and-ungranted-replacement");
    task = UmicomStreamsSpawn(0U, 128U, UMICOM_TRUE);
    UmicomStreamsWaitFor(task, UMICOM_STREAM_WAIT_INPUT);
    UmicomStreamsRequire(UmicomKernelUserTaskCancel(&umicomStreamScheduler, task) == UMICOM_USER_SCHEDULE_OK, "cancel a blocked input");
    UmicomStreamsDrain(task, UMICOM_FALSE);
    const UmicomKernelUserTaskHandle old = task;
    UmicomStreamsReap(task);
    task = UmicomStreamsSpawn(5U, 128U, UMICOM_FALSE);
    for (UmicomSize i = 0U; i < 30U && UmicomStreamsInfo(task).state != UMICOM_USER_TASK_EXITED; ++i) UmicomStreamsRun();
    UmicomStreamsRequire(UmicomStreamsInfo(task).state == UMICOM_USER_TASK_EXITED && UmicomStreamsInfo(task).exitValue == 0xeb14U,
        "replacement has no inherited stream authority");
    UmicomKernelStreamInfo stale = {0};
    UmicomStreamsRequire(UmicomKernelUserStreamsQuery(&umicomStreamDomain, old, &stale) == UMICOM_STREAM_INVALID_HANDLE,
        "old token does not select a replacement");
    UmicomStreamsReap(task);
    UmicomStreamsRequire(UmicomKernelUserSchedulerValidate(&umicomStreamScheduler) == UMICOM_USER_SCHEDULE_OK &&
        UmicomKernelUserStreamsClose(&umicomStreamDomain) == UMICOM_STREAM_OK, "close empty stream owner");

    UmicomKernelConsoleWriteLine("standard-streams.case=terminal-editor-and-prompt-return");
    UmicomStreamsResetCapture();
    const UmicomKernelShellImage image = {"/bin/umicom-stream-client.elf", UmicomStreamExecutableStart,
        (UmicomSize)(UmicomStreamExecutableEnd - UmicomStreamExecutableStart)};
    UmicomStreamsRequire(UmicomKernelConsoleShellInitialize(&umicomStreamShell, UmicomStreamsCapture, (void *)0,
        &image, 1U) == UMICOM_SHELL_OK, "initialise real command engine");
    UmicomStreamsRequire(UmicomKernelConsoleTerminalAttach(&umicomStreamTerminal, &umicomStreamShell)
        == UMICOM_SHELL_OK, "attach terminal without another command parser");
    static const char command[] = "run /bin/umicom-stream-client.elf 0";
    UmicomStreamsRequire(UmicomKernelConsoleShellExecute(&umicomStreamShell, command, sizeof(command)-1U)
        == UMICOM_SHELL_OK, "run named stream executable");
    for (UmicomSize i = 0U; i < 100U; ++i) {
        UmicomKernelStreamInfo info = {0};
        UmicomStreamsRequire(UmicomKernelConsoleShellStep(&umicomStreamShell) == UMICOM_SHELL_OK, "foreground stream quantum");
        UmicomStreamsRequire(umicomStreamShell.foreground != 0U, "stdin reader still alive");
        UmicomStreamsRequire(UmicomKernelUserStreamsQuery(&umicomStreamTerminal.streams, umicomStreamShell.foreground, &info)
            == UMICOM_STREAM_OK, "inspect foreground wait");
        if (info.wait == UMICOM_STREAM_WAIT_INPUT) break;
    }
    static const char entered[] = "Account verified\n";
    for (UmicomSize i = 0U; i < sizeof(entered)-1U; ++i)
        UmicomStreamsRequire(UmicomKernelConsoleShellFeed(&umicomStreamShell, (UmicomU8)entered[i]) == UMICOM_SHELL_OK,
            "route bytes to program editor, not shell commands");
    for (UmicomSize i = 0U; i < 100U && umicomStreamShell.foreground; ++i)
        UmicomStreamsRequire(UmicomKernelConsoleShellStep(&umicomStreamShell) == UMICOM_SHELL_OK, "return foreground to prompt");
    UmicomStreamsRequire(!umicomStreamShell.foreground && umicomStreamShell.hasCompletion &&
        umicomStreamShell.lastCompletion.exitValue == 0U && UmicomStreamsContains("received: Account verified") &&
        UmicomStreamsContains("[stderr] ") && UmicomStreamsContains("umicom> "), "interactive output and prompt integration");
    UmicomStreamsRequire(UmicomKernelConsoleShellClose(&umicomStreamShell) == UMICOM_SHELL_OK, "ordered terminal/filesystem shutdown");
    UmicomRiscvSupervisorMachineStateRead(&machineAfter);
    UmicomStreamsRequire(UmicomStreamsMachineEqual(&machineBefore, &machineAfter) &&
        UmicomPlatformTimerCompareRead(0U) == compare, "machine controls and timer restored");
    UmicomStreamsRequire(UmicomKernelPhysicalMemorySnapshotRead(&after) == UMICOM_KERNEL_MEMORY_OK &&
        after.allocatedFrames == before.allocatedFrames && after.freeFrames == before.freeFrames &&
        after.reservedFrames == before.reservedFrames, "frame accounting returned to baseline");
    UmicomKernelConsoleWriteLine("standard-streams.machine-state=restored");
    UmicomKernelConsoleWriteLine("standard-streams.frame-accounting=restored");
    UmicomKernelConsoleWriteLine("standard-streams.completed-cases=6");
    UmicomKernelConsoleWrite("standard-streams.completed-checks=");
    UmicomKernelConsoleWriteUnsigned(umicomStreamChecks);
    UmicomKernelConsoleWriteLine("");
    UmicomKernelConsoleWriteLine("standard-streams-test=pass");
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_STANDARD_STREAMS_READY");
}
