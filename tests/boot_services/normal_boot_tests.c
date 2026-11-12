/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/boot_services/normal_boot_tests.c
 *
 * PURPOSE:
 *   Exercise normal-boot decisions and startup output boundaries with explicit host observations.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


/* This translation unit includes the real coordinator. Physical allocation,
 * linked image addresses, the controller and machine operations are synthetic.
 * No user instructions, UART, guest memory or physical device are executed.
 * Each CTest case owns a fresh process; setjmp only observes a non-returning
 * platform finish, never a value in a returned automatic stack frame. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/linker.h"
#include "umicom/kernel/physical_memory.h"
static UmicomAddress TestLinkAddress(const void *symbol);
#undef UMICOM_KERNEL_LINKER_ADDRESS
#define UMICOM_KERNEL_LINKER_ADDRESS(symbol) TestLinkAddress(symbol)
#ifndef UMICOM_COORDINATOR_INTERACTIVE
#define UMICOM_KERNEL_STARTUP_AUTOMATED 1
#endif
#include "../../kernel/normal_boot.c"

const UmicomU8 UmicomLaunchExecutableStart[64] = {0U};
const UmicomU8 UmicomLaunchExecutableEnd[1] = {0U};
UmicomU8 __kernel_start[1];
UmicomU8 __kernel_end[1];
static jmp_buf finishPoint;
static char captured[16384];
static size_t capturedBytes;
static const char *caseName;
static unsigned checks, memoryCalls, snapshotCalls, inventoryCalls, initCalls;
static unsigned stepCalls, closeCalls, shellCalls, trapCalls, timerCalls, failureCode;
static int finishKind;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAILED: %s at line %d\n", #condition, __LINE__); exit(EXIT_FAILURE); \
} } while (0)
static int Is(const char *name) { return strcmp(caseName, name) == 0; }
static void Append(const char *bytes, size_t count)
{
    CHECK(count < sizeof(captured) - capturedBytes);
    memcpy(captured + capturedBytes, bytes, count);
    capturedBytes += count;
    captured[capturedBytes] = '\0';
}
static void Observe(void *context, const char *text, UmicomSize bytes)
{
    CHECK(context == NULL);
    Append(text, (size_t)bytes);
}
void UmicomKernelConsoleInitialize(void) {}
void UmicomKernelConsoleWrite(const char *text) { Append(text, strlen(text)); }
void UmicomKernelConsoleWriteLine(const char *text)
{
    UmicomKernelConsoleWrite(text); Append("\r\n", 2U);
}
void UmicomPlatformConsoleWriteByte(UmicomU8 value)
{
    const char byte = (char)value; Append(&byte, 1U);
}
void UmicomRiscvTrapInstall(void) { ++trapCalls; }
void UmicomPlatformTimerDisable(UmicomU64 hart) { CHECK(hart == 0U); ++timerCalls; }
void UmicomPlatformFinishSuccess(void) { finishKind = 1; longjmp(finishPoint, 1); }
void UmicomPlatformFinishFailure(UmicomU32 code)
{
    finishKind = 2; failureCode = (unsigned)code; longjmp(finishPoint, 1);
}
void UmicomPlatformHalt(void) { CHECK(0); }
void UmicomKernelConsoleShellRun(void) { ++shellCalls; }
/* Automated recovery must finish before polling. The interactive target only
 * exercises successful startup and the existing shell-return ownership path. */
UmicomKernelConsoleInputStatus UmicomPlatformConsoleTryReadByte(UmicomU8 *byte)
{
    (void)byte; CHECK(0); return UMICOM_CONSOLE_INPUT_ERROR;
}
static UmicomAddress TestLinkAddress(const void *symbol)
{
    if (symbol == (const void *)__kernel_start) return 0x80200000U;
    if (symbol == (const void *)__kernel_end) return 0x80400000U;
    if (symbol == (const void *)UmicomLaunchExecutableStart) return 0x80300000U;
    CHECK(symbol == (const void *)UmicomLaunchExecutableEnd);
    if (Is("empty-image")) return 0x80300000U;
    if (Is("reversed-image")) return 0x802fffffU;
    return 0x80300040U;
}
UmicomKernelBootMemoryStatus UmicomKernelBootMemoryInitialize(UmicomU64 hart,
    UmicomAddress tree, UmicomAddress start, UmicomAddress end)
{
    ++memoryCalls;
    CHECK(hart == 0U && tree == 0x88000000U);
    CHECK(start == 0x80200000U && end == 0x80400000U);
    return Is("memory-input") ? UMICOM_BOOT_MEMORY_BAD_INPUT : UMICOM_BOOT_MEMORY_OK;
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemorySnapshotRead(UmicomKernelPhysicalMemorySnapshot *out)
{
    ++snapshotCalls;
#ifdef UMICOM_KERNEL_FORCED_RECOVERY
    (void)out;
    return Is("forced-owner-present") ? UMICOM_KERNEL_MEMORY_OK : UMICOM_KERNEL_MEMORY_NOT_INITIALISED;
#else
    if ((snapshotCalls == 1U && Is("memory-snapshot")) ||
        (snapshotCalls == 2U && Is("accounting-read")))
        return UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE;
    memset(out, 0, sizeof(*out));
    out->allocatedFrames = 0U; out->freeFrames = 8U; out->reservedFrames = 2U;
    if (snapshotCalls == 2U) {
        if (Is("allocated-changed")) out->allocatedFrames = 1U;
        if (Is("free-changed")) out->freeFrames = 7U;
        if (Is("reserved-changed")) out->reservedFrames = 3U;
    }
    return UMICOM_KERNEL_MEMORY_OK;
#endif
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryValidate(void)
{
    return Is("accounting-invalid") ? UMICOM_KERNEL_MEMORY_INVARIANT_FAILURE : UMICOM_KERNEL_MEMORY_OK;
}
void UmicomKernelHardwareCapture(UmicomAddress tree)
{
    CHECK(tree == 0x88000000U); ++inventoryCalls;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesInitialize(UmicomKernelBootServices *owner,
    const UmicomKernelBootServiceSpec *specs, UmicomSize count,
    UmicomKernelBootServiceOutput output, void *context)
{
    ++initCalls;
    CHECK(owner == &umicomSystemServices && !owner->initialised);
    CHECK(count == 2U && output != NULL && context == NULL);
    CHECK(specs[0].dependencies == 0U && specs[1].dependencies == 1U);
    CHECK(specs[0].attempts == 2U && specs[1].attempts == 1U);
    for (UmicomSize i = 0U; i < count; ++i) {
        CHECK(specs[i].required == UMICOM_TRUE);
        CHECK(specs[i].launch.argumentCount == 2U && specs[i].launch.environmentCount == 2U);
        CHECK(specs[i].launch.arguments[0].bytes == 18U);
        CHECK(!memcmp(specs[i].launch.arguments[0].data, "/boot/launch-check", 18U));
        CHECK(specs[i].launch.environment[1].bytes == 18U);
        CHECK(!memcmp(specs[i].launch.environment[1].data, "UMICOM_BOOT=normal", 18U));
        CHECK(specs[i].sliceLimit == 128U && specs[i].timeoutTicks == 10000000U);
    }
#ifdef UMICOM_KERNEL_STARTUP_FAILURE
    CHECK(specs[0].imageBytes == 4U);
#else
    CHECK(specs[0].image == UmicomLaunchExecutableStart && specs[0].imageBytes == 64U);
#endif
    CHECK(specs[1].image == UmicomLaunchExecutableStart && specs[1].imageBytes == 64U);
    if (Is("startup-plan")) return UMICOM_BOOT_SERVICE_INVALID_PLAN;
    owner->initialised = UMICOM_TRUE; owner->self = owner; owner->count = count;
    owner->phase = UMICOM_BOOT_STARTING; owner->output = output; owner->outputContext = context;
    for (UmicomSize i = 0U; i < count; ++i) {
        strcpy(owner->records[i].info.name, specs[i].name);
        owner->records[i].info.state = UMICOM_SERVICE_SUCCEEDED;
        owner->records[i].info.attempts = 1U;
        owner->records[i].info.identity = i + 1U;
    }
    return UMICOM_BOOT_SERVICE_OK;
}
static void Packet(const char *name, UmicomU64 selector, const char *bytes, size_t count)
{
    UmicomKernelStreamPacket packet;
    CHECK(count <= sizeof(packet.data));
    memset(&packet, 0, sizeof(packet));
    packet.selector = selector; packet.bytes = (UmicomSize)count;
    memcpy(packet.data, bytes, count);
    UmicomStartupServiceOutput(NULL, name, &packet);
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesStep(UmicomKernelBootServices *owner, UmicomU64 quantum)
{
    CHECK(owner == &umicomSystemServices && quantum == 50000U);
    CHECK(++stepCalls <= 2U);
    if (stepCalls == 1U && (Is("report-boundary") || Is("recovery-boundary")))
        Packet(owner->records[0].info.name, UMICOM_STREAM_OUTPUT, "partial", 7U);
    if (Is("startup-step") || Is("recovery-boundary")) return UMICOM_BOOT_SERVICE_UNSAFE;
    if (stepCalls == 1U) return UMICOM_BOOT_SERVICE_OK;
#ifdef UMICOM_KERNEL_STARTUP_FAILURE
    owner->phase = UMICOM_BOOT_RECOVERY;
    owner->records[0].info.state = UMICOM_SERVICE_FAILED;
    owner->records[0].info.reason = Is("wrong-reason") ? UMICOM_SERVICE_SETUP_ERROR : UMICOM_SERVICE_LOAD_ERROR;
    owner->records[0].info.attempts = Is("wrong-attempts") ? 1U : 2U;
    owner->records[1].info.attempts = Is("dependent-started") ? 1U : 0U;
#else
    owner->phase = Is("required-failure") ? UMICOM_BOOT_RECOVERY : UMICOM_BOOT_READY;
#endif
    return UMICOM_BOOT_SERVICE_OK;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesClose(UmicomKernelBootServices *owner)
{
    CHECK(owner == &umicomSystemServices); ++closeCalls;
    if (Is("startup-cleanup")) return UMICOM_BOOT_SERVICE_CLEANUP_FAILED;
    owner->closed = UMICOM_TRUE;
    return UMICOM_BOOT_SERVICE_OK;
}
UmicomKernelBootServiceStatus UmicomKernelBootServicesQuery(UmicomKernelBootServices *owner,
    UmicomSize index, UmicomKernelBootServiceInfo *out)
{
    CHECK(owner == &umicomSystemServices && index < owner->count);
    memcpy(out, &owner->records[index].info, sizeof(*out));
    return UMICOM_BOOT_SERVICE_OK;
}
const char *UmicomKernelBootServiceStateName(UmicomKernelBootServiceState state)
{
    return state == UMICOM_SERVICE_SUCCEEDED ? "succeeded" : "failed";
}
static void OutputCase(void)
{
    static const char first[] = "first", second[] = "second";
    if (Is("stream-fragments")) {
        Packet(first, UMICOM_STREAM_OUTPUT, "one", 3U);
        Packet(first, UMICOM_STREAM_OUTPUT, " two\n", 5U);
        CHECK(!strcmp(captured, "[first] one two\r\n"));
    } else if (Is("stream-switch")) {
        Packet(first, UMICOM_STREAM_OUTPUT, "out", 3U);
        Packet(first, UMICOM_STREAM_ERROR, "err", 3U);
        Packet(first, UMICOM_STREAM_OUTPUT, "next\n", 5U);
        CHECK(!strcmp(captured, "[first] out\r\n[first] stderr: err\r\n[first] next\r\n"));
    } else if (Is("stream-owner")) {
        Packet(first, UMICOM_STREAM_OUTPUT, "one", 3U);
        Packet(second, UMICOM_STREAM_OUTPUT, "two\n", 4U);
        CHECK(!strcmp(captured, "[first] one\r\n[second] two\r\n"));
    } else if (Is("stream-empty")) {
        Packet(first, UMICOM_STREAM_OUTPUT, "one", 3U);
        Packet(second, UMICOM_STREAM_ERROR, "", 0U);
        Packet(first, UMICOM_STREAM_OUTPUT, " two\n", 5U);
        CHECK(!strcmp(captured, "[first] one two\r\n"));
    } else if (Is("stream-controls")) {
        static const char bytes[] = {0, 27, '\r', '\t', (char)255, '\n'};
        Packet(first, UMICOM_STREAM_ERROR, bytes, sizeof(bytes));
        CHECK(!strcmp(captured, "[first] stderr: \\x00\\x1b\\x0d\\x09\\xff\r\n"));
    } else CHECK(0);
    UmicomStartupOutputFinish();
    CHECK(umicomStartupLineStart && umicomStartupOutputOwner == NULL);
}
static void ReportCase(void)
{
    if (Is("report-null")) {
        UmicomKernelNormalBootReport(NULL, NULL); CHECK(capturedBytes == 0U); return;
    }
    if (Is("report-not-selected")) {
        UmicomKernelNormalBootReport(NULL, Observe);
        CHECK(strstr(captured, "Normal startup was not selected") != NULL); return;
    }
    umicomSystemSelected = UMICOM_TRUE;
    UmicomKernelNormalBootReport(NULL, Observe);
    CHECK(strstr(captured, "boot memory: unavailable\r\n") != NULL);
    CHECK(strstr(captured, "Startup services were not admitted.\r\n") != NULL);
}
static void BootCase(void)
{
    if (setjmp(finishPoint) == 0) UmicomKernelNormalBoot(0U, 0x88000000U);
    CHECK(trapCalls == 1U && timerCalls == 1U);
    CHECK(strstr(captured, "UMICOM_KERNEL_BEGIN\r\n") != NULL);
#ifdef UMICOM_KERNEL_FORCED_RECOVERY
    CHECK(memoryCalls == 0U && inventoryCalls == 0U && initCalls == 0U);
    CHECK(snapshotCalls == 1U && shellCalls == 0U);
    if (Is("forced-owner-present")) {
        CHECK(finishKind == 2 && failureCode == 0xb1U);
        CHECK(strstr(captured, "forced-recovery-owner-already-present") != NULL);
        CHECK(strstr(captured, "RECOVERY_SELECTION_READY") == NULL);
    } else {
        CHECK(Is("forced-ready") && finishKind == 1);
        CHECK(strstr(captured, "RECOVERY_SELECTION_READY") != NULL);
    }
#elif defined(UMICOM_KERNEL_STARTUP_FAILURE)
    CHECK(memoryCalls == 1U && initCalls == 1U && stepCalls == 2U && closeCalls == 1U);
    CHECK(snapshotCalls == 2U && shellCalls == 0U);
    if (Is("expected-failure")) {
        CHECK(finishKind == 1 && strstr(captured, "RECOVERY_SELECTION_READY") != NULL);
    } else {
        CHECK(Is("wrong-attempts") || Is("wrong-reason") || Is("dependent-started"));
        CHECK(finishKind == 2 && strstr(captured, "RECOVERY_SELECTION_READY") == NULL);
    }
    CHECK(strstr(captured, "NORMAL_STARTUP_READY") == NULL);
#else
    const char *reason = NULL;
    if (Is("memory-input")) reason = "boot-memory-input";
    else if (Is("memory-snapshot")) reason = "boot-memory-accounting";
    else if (Is("empty-image") || Is("reversed-image")) reason = "startup-image-range";
    else if (Is("startup-plan")) reason = "startup-plan";
    else if (Is("startup-step") || Is("recovery-boundary")) reason = "startup-controller";
    else if (Is("startup-cleanup")) reason = "startup-cleanup";
    else if (Is("accounting-read") || Is("allocated-changed") || Is("free-changed") ||
        Is("reserved-changed") || Is("accounting-invalid")) reason = "startup-frame-accounting";
    else if (Is("required-failure")) reason = "required-startup-service";
    else CHECK(Is("ready") || Is("report-boundary"));
    CHECK(memoryCalls == 1U);
    if (reason) {
        CHECK(finishKind == 2 && failureCode == 0xb1U);
        CHECK(strstr(captured, reason) != NULL && shellCalls == 0U);
        CHECK(strstr(captured, "NORMAL_STARTUP_READY") == NULL);
        CHECK(strstr(captured, "RECOVERY_SELECTION_READY") == NULL);
    } else {
        CHECK(finishKind == 1 && closeCalls == 1U && snapshotCalls == 2U);
        CHECK(strstr(captured, "NORMAL_STARTUP_READY") != NULL);
#ifdef UMICOM_COORDINATOR_INTERACTIVE
        CHECK(shellCalls == 1U);
#else
        CHECK(shellCalls == 0U);
#endif
    }
    if (Is("memory-input") || Is("memory-snapshot") || Is("empty-image") || Is("reversed-image"))
        CHECK(initCalls == 0U && stepCalls == 0U && closeCalls == 0U);
    if (Is("startup-plan")) CHECK(initCalls == 1U && stepCalls == 0U && closeCalls == 0U);
    if (Is("startup-step") || Is("recovery-boundary")) CHECK(stepCalls == 1U && closeCalls == 0U);
    if (Is("report-boundary")) CHECK(strstr(captured, "partial\r\nboot memory: reserved\r\n") != NULL);
    if (Is("recovery-boundary")) CHECK(strstr(captured, "partial\r\nrecovery.reason=startup-controller\r\n") != NULL);
#endif
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    caseName = argv[1];
    if (!strncmp(caseName, "stream-", 7U)) OutputCase();
    else if (Is("report-null") || Is("report-not-selected") || Is("report-unadmitted")) ReportCase();
    else BootCase();
    printf("coordinator observations checked: %u\n", checks);
    return 0;
}
