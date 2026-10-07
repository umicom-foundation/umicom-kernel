/*-----------------------------------------------------------------------------
 * Umicom Kernel normal system entry and independent recovery
 * File: kernel/normal_boot.c
 *
 * Normal startup does not run the cumulative test sequence. It protects boot
 * memory, runs an explicit native startup plan and then calls the existing
 * interactive shell. The separate recovery image bypasses memory allocation
 * and all services. Neither path claims disk persistence or authenticated login.
 *
 * The packaged jobs verify native launch/environment contracts, not fictional
 * disk or networking services. The controller's completed records remain
 * visible after the actual task images have been collected.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
/* Inventory is observational; independent recovery still skips this path. */
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/startup.h"
#include "umicom/kernel/boot_services.h"
#include "umicom/kernel/console_shell.h"
#include "umicom/kernel/console_input.h"
#include "umicom/kernel/console.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/linker.h"
#include "umicom/kernel/riscv64/trap.h"
#include "console_internal.h"

extern const UmicomU8 UmicomLaunchExecutableStart[];
extern const UmicomU8 UmicomLaunchExecutableEnd[];
static UmicomKernelBootServices umicomSystemServices;
static UmicomBoolean umicomSystemSelected;
static UmicomBoolean umicomSystemMemoryReady;
static UmicomBoolean umicomStartupLineStart = UMICOM_TRUE;
static const char *umicomStartupOutputOwner;

static UmicomSize UmicomStartupLength(const char *text)
{
    UmicomSize bytes = 0U;
    while (text[bytes]) ++bytes;
    return bytes;
}
static void UmicomStartupUart(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    for (UmicomSize i = 0U; i < bytes; ++i) UmicomPlatformConsoleWriteByte((UmicomU8)text[i]);
}
static void UmicomStartupNumber(void *context, UmicomKernelStartupOutput output, UmicomU64 value)
{
    char number[20]; UmicomSize used = 0U;
    do { number[used++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (used) output(context, &number[--used], 1U);
}
void UmicomKernelNormalBootReport(void *context, UmicomKernelStartupOutput output)
{
    if (!output) return;
    if (!umicomSystemSelected) {
        const char *message = "Normal startup was not selected for this image.\r\n";
        output(context, message, UmicomStartupLength(message)); return;
    }
    const char *memory = umicomSystemMemoryReady ? "boot memory: reserved\r\n" : "boot memory: unavailable\r\n";
    output(context, memory, UmicomStartupLength(memory));
    if (!umicomSystemServices.initialised) {
        const char *message = "Startup services were not admitted.\r\n";
        output(context, message, UmicomStartupLength(message)); return;
    }
    for (UmicomSize i = 0U; i < umicomSystemServices.count; ++i) {
        UmicomKernelBootServiceInfo info;
        UmicomConsoleClear(&info, sizeof(info));
        if (UmicomKernelBootServicesQuery(&umicomSystemServices, i, &info) != UMICOM_BOOT_SERVICE_OK) return;
        output(context, info.name, UmicomStartupLength(info.name)); output(context, ": ", 2U);
        const char *state = UmicomKernelBootServiceStateName(info.state);
        output(context, state, UmicomStartupLength(state)); output(context, " attempts=", 10U);
        UmicomStartupNumber(context, output, info.attempts); output(context, " identity=", 10U);
        UmicomStartupNumber(context, output, info.identity); output(context, " reason=", 8U);
        UmicomStartupNumber(context, output, (UmicomU64)info.reason); output(context, "\r\n", 2U);
    }
}
static void UmicomStartupRecoveryReport(void *context)
{
    UmicomKernelNormalBootReport(context, UmicomStartupUart);
}
static void UmicomStartupServiceOutput(void *context, const char *name, const UmicomKernelStreamPacket *packet)
{
    (void)context;
    if (umicomStartupOutputOwner != name && !umicomStartupLineStart) {
        UmicomKernelConsoleWriteLine("");
        umicomStartupLineStart = UMICOM_TRUE;
    }
    umicomStartupOutputOwner = name; /* Names belong to the stable closed-report owner. */
    static const char hex[] = "0123456789abcdef";
    for (UmicomSize i = 0U; i < packet->bytes; ++i) {
        if (umicomStartupLineStart) {
            /* Stream records need not coincide with lines. Prefix a line once,
             * not every small write made by the application's formatter. */
            UmicomKernelConsoleWrite("["); UmicomKernelConsoleWrite(name); UmicomKernelConsoleWrite("] ");
            if (packet->selector == UMICOM_STREAM_ERROR) UmicomKernelConsoleWrite("stderr: ");
            umicomStartupLineStart = UMICOM_FALSE;
        }
        const UmicomU8 byte = packet->data[i];
        if (byte == 10U) {
            UmicomKernelConsoleWriteLine("");
            umicomStartupLineStart = UMICOM_TRUE;
        } else if (byte >= 32U && byte <= 126U) UmicomPlatformConsoleWriteByte(byte);
        else {
            /* A boot job cannot issue terminal escape commands through stdout. */
            const char escaped[] = {'\\', 'x', hex[byte >> 4U], hex[byte & 15U]};
            UmicomStartupUart((void *)0, escaped, sizeof(escaped));
        }
    }
}

static _Noreturn void UmicomStartupPoweroff(void)
{
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_END");
    UmicomPlatformFinishSuccess(); UmicomPlatformHalt();
    for (;;) {} /* The existing platform declarations predate non-return annotations. */
}
static _Noreturn void UmicomStartupRecovery(const char *reason, UmicomBoolean expectedSelection)
{
    UmicomKernelConsoleWrite("recovery.reason="); UmicomKernelConsoleWriteLine(reason);
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_RECOVERY_READY");
#ifdef UMICOM_KERNEL_STARTUP_AUTOMATED
    /* The caller supplies true only at the exact dedicated test selection.
     * An unrelated boot error cannot pass merely by reaching some recovery UI. */
    if (expectedSelection) {
        UmicomKernelConsoleWriteLine("UMICOM_KERNEL_RECOVERY_SELECTION_READY");
        UmicomStartupPoweroff();
    }
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL");
    UmicomPlatformFinishFailure(0xb1U); UmicomPlatformHalt();
#else
    (void)expectedSelection;
#endif
    UmicomKernelConsoleWriteLine("Independent recovery. Type help. No filesystem or process admission.");
    UmicomKernelConsoleLine line;
    UmicomConsoleClear(&line, sizeof(line));
    UmicomKernelConsoleWrite("recovery> ");
    for (;;) {
        UmicomU8 byte = 0U;
        const UmicomKernelConsoleInputStatus input = UmicomPlatformConsoleTryReadByte(&byte);
        if (input == UMICOM_CONSOLE_INPUT_IDLE) continue;
        if (input != UMICOM_CONSOLE_INPUT_BYTE) { line.discard = UMICOM_TRUE; continue; }
        const UmicomSize oldLength = line.length;
        const UmicomKernelConsoleLineEvent event = UmicomKernelConsoleLineFeed(&line, byte);
        if (event == UMICOM_CONSOLE_LINE_NONE) {
            if (!line.discard && line.length > oldLength) UmicomPlatformConsoleWriteByte((UmicomU8)line.bytes[oldLength]);
            else if (!line.discard && line.length < oldLength)
                for (UmicomSize i = line.length; i < oldLength; ++i) UmicomKernelConsoleWrite("\b \b");
            continue;
        }
        UmicomKernelConsoleWriteLine("");
        if (event == UMICOM_CONSOLE_LINE_READY) {
            if (UmicomKernelRecoveryCommand(line.bytes, line.length, reason, UmicomStartupUart,
                    UmicomStartupRecoveryReport, (void *)0) == UMICOM_RECOVERY_POWEROFF)
                UmicomStartupPoweroff();
        } else UmicomKernelConsoleWriteLine("Command discarded.");
        UmicomKernelConsoleLineConsume(&line);
        UmicomKernelConsoleWrite("recovery> ");
    }
}
void UmicomKernelNormalBootRecover(const char *reason)
{
    /* The established diagnostic console keeps its original failure behaviour.
     * Normal startup alone has the independent, non-mutating recovery fallback. */
    if (umicomSystemSelected) UmicomStartupRecovery(reason, UMICOM_FALSE);
}
_Noreturn void UmicomKernelNormalBoot(UmicomU64 hart, UmicomAddress deviceTree)
{
    umicomSystemSelected = UMICOM_TRUE;
    UmicomKernelConsoleInitialize();
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_BEGIN");
    UmicomKernelConsoleWriteLine("name=Umicom Kernel");
    UmicomRiscvTrapInstall();
    UmicomPlatformTimerDisable(0U); /* Establish a parked timer; do not execute timer tests. */
#ifdef UMICOM_KERNEL_FORCED_RECOVERY
    UmicomKernelConsoleWriteLine("boot.mode=recovery");
    UmicomKernelPhysicalMemorySnapshot untouched;
    if (UmicomKernelPhysicalMemorySnapshotRead(&untouched) != UMICOM_KERNEL_MEMORY_NOT_INITIALISED)
        UmicomStartupRecovery("forced-recovery-owner-already-present", UMICOM_FALSE);
    UmicomStartupRecovery("forced-recovery", UMICOM_TRUE);
#endif
    UmicomKernelConsoleWriteLine("boot.mode=normal");
    if (UmicomKernelBootMemoryInitialize(hart, deviceTree,
            UMICOM_KERNEL_LINKER_ADDRESS(__kernel_start), UMICOM_KERNEL_LINKER_ADDRESS(__kernel_end))
        != UMICOM_BOOT_MEMORY_OK) UmicomStartupRecovery("boot-memory-input", UMICOM_FALSE);
    umicomSystemMemoryReady = UMICOM_TRUE;
    UmicomKernelPhysicalMemorySnapshot beforeServices;
    if (UmicomKernelPhysicalMemorySnapshotRead(&beforeServices) != UMICOM_KERNEL_MEMORY_OK)
        UmicomStartupRecovery("boot-memory-accounting", UMICOM_FALSE);
    UmicomKernelConsoleWriteLine("boot.memory=reserved");

    /* Copy the checked description without replacing the existing RAM/device
     * policy. Unsupported inventory is visible through the hardware command. */
    UmicomKernelHardwareCapture(deviceTree);

    /* Compiled startup definitions are trusted immutable inputs. These jobs
     * exercise the real structured launch client, with no file grants and EOF
     * stdin. They are bootstrap contract checks, not long-running daemons. */
    static const UmicomKernelLaunchString runtimeArguments[] = {{"/boot/launch-check",18U},{"runtime-contract",16U}};
    static const UmicomKernelLaunchString sessionArguments[] = {{"/boot/launch-check",18U},{"session-environment",19U}};
    static const UmicomKernelLaunchString environment[] = {{"LANG=C",6U},{"UMICOM_BOOT=normal",18U}};
    const UmicomSize imageBytes = (UmicomSize)(UmicomLaunchExecutableEnd - UmicomLaunchExecutableStart);
    UmicomKernelBootServiceSpec specs[2];
    UmicomConsoleClear(specs, sizeof(specs));
    for (UmicomSize i=0U; i<2U; ++i) {
        specs[i].name = i ? "session-environment" : "runtime-contract";
        specs[i].image = UmicomLaunchExecutableStart; specs[i].imageBytes = imageBytes;
        specs[i].launch.arguments = i ? sessionArguments : runtimeArguments;
        specs[i].launch.argumentCount = 2U; specs[i].launch.environment = environment;
        specs[i].launch.environmentCount = 2U; specs[i].dependencies = i ? 1U : 0U;
        specs[i].attempts = i ? 1U : 2U; specs[i].retryTicks = 100000U;
        specs[i].timeoutTicks = 10000000U; specs[i].sliceLimit = 128U; specs[i].required = UMICOM_TRUE;
    }
#ifdef UMICOM_KERNEL_STARTUP_FAILURE
    /* Only the independent recovery-selection test substitutes malformed ELF
     * input. It exercises the existing loader and the complete retry policy. */
    static const UmicomU8 malformed[] = {0U,1U,2U,3U};
    specs[0].image = malformed; specs[0].imageBytes = sizeof(malformed);
#endif
    if (UmicomKernelBootServicesInitialize(&umicomSystemServices, specs, 2U, UmicomStartupServiceOutput, (void *)0)
        != UMICOM_BOOT_SERVICE_OK) UmicomStartupRecovery("startup-plan", UMICOM_FALSE);
    while (umicomSystemServices.phase == UMICOM_BOOT_STARTING) {
        const UmicomKernelBootServiceStatus status = UmicomKernelBootServicesStep(&umicomSystemServices, 50000U);
        if (status != UMICOM_BOOT_SERVICE_OK) UmicomStartupRecovery("startup-controller", UMICOM_FALSE);
    }
    const UmicomBoolean ready = umicomSystemServices.phase == UMICOM_BOOT_READY ? UMICOM_TRUE : UMICOM_FALSE;
    if (UmicomKernelBootServicesClose(&umicomSystemServices) != UMICOM_BOOT_SERVICE_OK)
        UmicomStartupRecovery("startup-cleanup", UMICOM_FALSE);
    /* Startup acceptance includes actual release accounting. A completed
     * service report cannot hide frames retained by a supposedly closed job. */
    UmicomKernelPhysicalMemorySnapshot afterServices;
    if (UmicomKernelPhysicalMemorySnapshotRead(&afterServices) != UMICOM_KERNEL_MEMORY_OK ||
        afterServices.allocatedFrames != beforeServices.allocatedFrames ||
        afterServices.freeFrames != beforeServices.freeFrames ||
        afterServices.reservedFrames != beforeServices.reservedFrames ||
        UmicomKernelPhysicalMemoryValidate() != UMICOM_KERNEL_MEMORY_OK)
        UmicomStartupRecovery("startup-frame-accounting", UMICOM_FALSE);
    UmicomKernelNormalBootReport((void *)0, UmicomStartupUart);
    if (!ready) {
        UmicomBoolean expected = UMICOM_FALSE;
#ifdef UMICOM_KERNEL_STARTUP_FAILURE
        expected = umicomSystemServices.records[0].info.attempts == 2U &&
            umicomSystemServices.records[0].info.reason == UMICOM_SERVICE_LOAD_ERROR &&
            umicomSystemServices.records[1].info.attempts == 0U ? UMICOM_TRUE : UMICOM_FALSE;
#endif
        UmicomStartupRecovery("required-startup-service", expected);
    }
#ifdef UMICOM_KERNEL_STARTUP_FAILURE
    /* A supposedly malformed required service must not reach normal readiness. */
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_FAIL"); UmicomPlatformFinishFailure(0xb2U); UmicomPlatformHalt();
#endif
    UmicomKernelConsoleWriteLine("UMICOM_KERNEL_NORMAL_STARTUP_READY");
#ifdef UMICOM_KERNEL_STARTUP_AUTOMATED
    UmicomStartupPoweroff(); /* The smoke image tests startup, never human typing. */
#endif
    /* Reuse the complete existing shell/terminal lifecycle. It creates RAMFS
     * once for this session and returns only after its orderly poweroff. */
    UmicomKernelConsoleShellRun();
    UmicomStartupPoweroff();
}
