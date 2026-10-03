/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/user_memory/user_memory_tests.c
 *
 * PURPOSE:
 *   Exercise the real ownership/copy/dispatcher C code on a native host under
 *   sanitizers. These tests do not emulate the RISC-V privileged instructions.
 *
 * EDUCATIONAL OVERVIEW:
 *   An aligned array stands in for physical RAM. Its pointer is deliberately
 *   used as a physical address by the unchanged freestanding page walker.
 *   Each virtual leaf is created through the real Kernel allocator and mapper;
 *   no mock supplies a successful translation or an invented permission result.
 *
 * AUTHOR AND ORGANISATION:
 *   Sammy Hegab
 *   Umicom Foundation
 *
 * LICENCE:
 *   MIT
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/riscv64/user_execution.h"

/* Page alignment makes every table/data address a valid simulated physical frame. */
alignas(4096) static UmicomU8 umicomTestRam[4096U * 32U];
static UmicomKernelVirtualAddressSpace umicomTestSpace;
static UmicomKernelUserPage umicomTestPages[5];
static UmicomKernelUserMemory umicomTestMemory;
static UmicomAddress umicomTestFirst;
static UmicomAddress umicomTestSecond;
static UmicomAddress umicomTestOther;
static UmicomAddress umicomTestCode;
#define UMICOM_TEST_DATA ((UmicomAddress)0x10000000U)
#define UMICOM_TEST_READONLY ((UmicomAddress)0x20000000U)
#define UMICOM_TEST_SUPERVISOR ((UmicomAddress)0x30000000U)
#define UMICOM_TEST_CODE ((UmicomAddress)0x40000000U)

static void UmicomTestRequire(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAILED: %s\n", message);
        exit(EXIT_FAILURE);
    }
}
static UmicomAddress UmicomTestAllocate(void)
{
    UmicomAddress frame = 0U;
    UmicomTestRequire(UmicomKernelPhysicalMemoryAllocateFrame(&frame) == UMICOM_KERNEL_MEMORY_OK,
        "frame allocation");
    return frame;
}
static void UmicomTestMap(UmicomSize slot, UmicomAddress va, UmicomAddress pa,
    UmicomKernelVirtualMemoryPermissions permissions)
{
    UmicomTestRequire(UmicomKernelVirtualMemoryMapPage(&umicomTestSpace, va, pa, permissions) ==
        UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "map fixture");
    umicomTestPages[slot].virtualBase = va;
    umicomTestPages[slot].physicalBase = pa;
    umicomTestPages[slot].permissions = permissions;
}
static void UmicomTestPrepare(void)
{
    UmicomTestRequire(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomTestRam,
        sizeof(umicomTestRam)) == UMICOM_KERNEL_MEMORY_OK, "initialise fixture RAM");
    UmicomTestRequire(UmicomKernelVirtualAddressSpaceCreate(&umicomTestSpace) ==
        UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "create fixture root");
    umicomTestFirst = UmicomTestAllocate();
    umicomTestOther = UmicomTestAllocate(); /* Separate the two backing pages. */
    umicomTestSecond = UmicomTestAllocate();
    umicomTestCode = UmicomTestAllocate();
    const UmicomKernelVirtualMemoryPermissions rw = UMICOM_KERNEL_VIRTUAL_MEMORY_USER |
        UMICOM_KERNEL_VIRTUAL_MEMORY_READ | UMICOM_KERNEL_VIRTUAL_MEMORY_WRITE;
    UmicomTestMap(0U, UMICOM_TEST_DATA, umicomTestFirst, rw);
    UmicomTestMap(1U, UMICOM_TEST_DATA + 4096U, umicomTestSecond, rw);
    UmicomTestMap(2U, UMICOM_TEST_READONLY, umicomTestOther,
        UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    UmicomTestMap(3U, UMICOM_TEST_SUPERVISOR, umicomTestOther, UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    UmicomTestMap(4U, UMICOM_TEST_CODE, umicomTestCode,
        UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ |
        UMICOM_KERNEL_VIRTUAL_MEMORY_EXECUTE);
    umicomTestMemory.space = &umicomTestSpace;
    umicomTestMemory.pages = umicomTestPages;
    umicomTestMemory.pageCount = 5U;
    for (UmicomSize i = 0U; i < 8U; ++i) {
        ((UmicomU8 *)umicomTestFirst)[4088U + i] = (UmicomU8)(0x40U + i);
        ((UmicomU8 *)umicomTestSecond)[i] = (UmicomU8)(0x48U + i);
    }
}
static void UmicomTestTeardown(void)
{
    UmicomTestRequire(UmicomKernelVirtualAddressSpaceDestroy(&umicomTestSpace) ==
        UMICOM_KERNEL_VIRTUAL_MEMORY_OK, "destroy fixture tables");
    const UmicomAddress frames[] = {umicomTestFirst, umicomTestSecond, umicomTestOther, umicomTestCode};
    for (UmicomSize i = 0U; i < sizeof(frames) / sizeof(frames[0]); ++i) {
        UmicomTestRequire(UmicomKernelPhysicalMemoryFreeFrame(frames[i]) == UMICOM_KERNEL_MEMORY_OK,
            "free caller data frame");
    }
    UmicomKernelPhysicalMemorySnapshot snapshot;
    UmicomTestRequire(UmicomKernelPhysicalMemorySnapshotRead(&snapshot) == UMICOM_KERNEL_MEMORY_OK &&
        snapshot.allocatedFrames == 0U && snapshot.freeFrames == snapshot.totalFrames &&
        UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK, "no fixture frame leaks");
}

int main(int argc, char **argv)
{
    UmicomTestRequire(argc == 2, "select one named native case");
    UmicomTestPrepare();
    const char *const name = argv[1];
    UmicomU8 bytes[64];
    memset(bytes, 0xcc, sizeof(bytes));
    const UmicomAddress cross = UMICOM_TEST_DATA + 4088U;
    UmicomU64 status = 0U;
    if (strcmp(name, "cross-page-read") == 0) {
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, cross, bytes, 16U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_OK, "cross-page read");
        for (UmicomSize i = 0U; i < 16U; ++i) {
            UmicomTestRequire(bytes[i] == (UmicomU8)(0x40U + i), "offset preserved across noncontiguous backing");
        }
    } else if (strcmp(name, "cross-page-write") == 0) {
        status = UmicomKernelUserMemoryWrite(&umicomTestMemory, cross, bytes, 16U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_OK, "cross-page write");
        for (UmicomSize i = 0U; i < 8U; ++i) {
            UmicomTestRequire(((UmicomU8 *)umicomTestFirst)[4088U + i] == 0xccU &&
                ((UmicomU8 *)umicomTestSecond)[i] == 0xccU, "both backing pages written");
        }
    } else if (strcmp(name, "read-only-write") == 0) {
        status = UmicomKernelUserMemoryWrite(&umicomTestMemory, UMICOM_TEST_READONLY, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_DENIED && *(UmicomU8 *)umicomTestOther == 0U,
            "read-only destination unchanged");
    } else if (strcmp(name, "supervisor-read") == 0) {
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, UMICOM_TEST_SUPERVISOR, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_DENIED && bytes[0] == 0xccU,
            "present supervisor page is not a user buffer");
    } else if (strcmp(name, "unknown-page") == 0) {
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, 0U, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_BAD_ADDRESS && bytes[0] == 0xccU,
            "missing page refused without copying");
    } else if (strcmp(name, "range-wrap") == 0) {
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, ~(UmicomAddress)0U - 3U, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_BAD_ADDRESS, "wrapping span");
    } else if (strcmp(name, "noncanonical") == 0) {
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, 0x4000000000ULL, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_BAD_ADDRESS, "noncanonical Sv39 span");
    } else if (strcmp(name, "partial-destination") == 0) {
        status = UmicomKernelUserMemoryWrite(&umicomTestMemory, UMICOM_TEST_DATA + 8184U, bytes, 16U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_BAD_ADDRESS, "second destination page is absent");
        for (UmicomSize i = 4088U; i < 4096U; ++i) {
            UmicomTestRequire(((UmicomU8 *)umicomTestSecond)[i] == 0U, "first page not partially changed");
        }
    } else if (strcmp(name, "oversized") == 0) {
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, UMICOM_TEST_DATA, bytes, 65U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_TOO_LARGE, "bounded byte count");
    } else if (strcmp(name, "zero-length") == 0) {
        UmicomTestRequire(UmicomKernelUserMemoryRead(0, 0U, 0, 0U) == UMICOM_USER_RESULT_OK &&
            UmicomKernelUserMemoryWrite(0, 0U, 0, 0U) == UMICOM_USER_RESULT_OK, "documented no-op");
    } else if (strcmp(name, "null-kernel-buffer") == 0) {
        UmicomTestRequire(UmicomKernelUserMemoryRead(&umicomTestMemory, UMICOM_TEST_DATA, 0, 8U) ==
            UMICOM_USER_RESULT_BAD_ADDRESS, "nonempty copy needs kernel output");
    } else if (strcmp(name, "wrong-backing-frame") == 0) {
        umicomTestPages[0].physicalBase = umicomTestOther;
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, UMICOM_TEST_DATA, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_DENIED, "PTE and Kernel owner must agree");
    } else if (strcmp(name, "widened-permission") == 0) {
        umicomTestPages[0].permissions = UMICOM_KERNEL_VIRTUAL_MEMORY_USER | UMICOM_KERNEL_VIRTUAL_MEMORY_READ;
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, UMICOM_TEST_DATA, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_DENIED, "PTE cannot widen its registered rights");
    } else if (strcmp(name, "unregistered-page") == 0) {
        umicomTestMemory.pageCount = 1U;
        status = UmicomKernelUserMemoryRead(&umicomTestMemory, UMICOM_TEST_DATA + 4096U, bytes, 8U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_DENIED, "mapping alone is not backing ownership");
    } else if (strcmp(name, "invalid-access") == 0) {
        status = UmicomKernelUserMemoryCheck(&umicomTestMemory, UMICOM_TEST_DATA, 8U, 0U);
        UmicomTestRequire(status == UMICOM_USER_RESULT_DENIED, "empty access mask refused");
    } else {
        /* Direct dispatcher tests use real page tables but supply synthetic trap
         * frames. They test policy, not hardware delivery of the exception. */
        UmicomKernelUserSession session;
        UmicomRiscvTrapFrame frame;
        memset(&session, 0, sizeof(session));
        memset(&frame, 0, sizeof(frame));
        session.memory = umicomTestMemory;
        session.identity = 202U;
        frame.mepc = UMICOM_TEST_CODE;
        frame.mcause = 8U;
        frame.x17_a7 = UMICOM_USER_CALL_IDENTITY;
        if (strcmp(name, "unknown-call") == 0) {
            frame.x17_a7 = 99U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 1U &&
                frame.x10_a0 == UMICOM_USER_RESULT_UNKNOWN_CALL && frame.mepc == UMICOM_TEST_CODE + 4U,
                "unknown request resumes with a refusal");
        } else if (strcmp(name, "assigned-identity") == 0) {
            frame.x10_a0 = 9999U;
            frame.x11_a1 = 0x1234U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 1U &&
                frame.x10_a0 == 202U && frame.x11_a1 == 0x1234U, "identity comes from Kernel; other registers survive");
        } else if (strcmp(name, "checked-copy") == 0) {
            frame.x17_a7 = UMICOM_USER_CALL_COPY;
            frame.x10_a0 = UMICOM_TEST_DATA + 128U;
            frame.x11_a1 = cross;
            frame.x12_a2 = 16U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 1U &&
                frame.x10_a0 == 0U && session.copiedBytes == 16U, "copy service resumes");
            for (UmicomSize i = 0U; i < 16U; ++i) {
                UmicomTestRequire(((UmicomU8 *)umicomTestFirst)[128U + i] == (UmicomU8)(0x40U + i),
                    "dispatcher copy produced real bytes");
            }
        } else if (strcmp(name, "denied-copy") == 0) {
            frame.x17_a7 = UMICOM_USER_CALL_COPY;
            frame.x10_a0 = UMICOM_TEST_READONLY;
            frame.x11_a1 = cross;
            frame.x12_a2 = 8U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 1U &&
                frame.x10_a0 == UMICOM_USER_RESULT_DENIED && session.rejectedCalls == 1U &&
                *(UmicomU8 *)umicomTestOther == 0U, "copy refusal has no destination side effect");
        } else if (strcmp(name, "exit-call") == 0) {
            frame.x17_a7 = UMICOM_USER_CALL_EXIT;
            frame.x10_a0 = 42U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 0U &&
                session.stopReason == UMICOM_USER_STOP_EXIT && session.exitValue == 42U, "exit never resumes user PC");
        } else if (strcmp(name, "call-budget") == 0) {
            session.callCount = UMICOM_USER_CALL_LIMIT;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 0U &&
                session.stopReason == UMICOM_USER_STOP_CALL_BUDGET, "service budget is bounded");
        } else if (strcmp(name, "deadline-event") == 0) {
            frame.mcause = UMICOM_RISCV_MCAUSE_INTERRUPT_BIT | 7U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 0U &&
                session.stopReason == UMICOM_USER_STOP_DEADLINE && session.callCount == 0U, "timer is not an ECALL");
        } else if (strcmp(name, "wrong-privilege") == 0) {
            frame.mstatus = 0x1800U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 0U &&
                session.stopReason == UMICOM_USER_STOP_MONITOR_ERROR, "machine fault is not a user test success");
        } else if (strcmp(name, "next-pc-refusal") == 0) {
            frame.mepc = UMICOM_TEST_CODE + 4092U;
            UmicomTestRequire(UmicomKernelUserTrapDispatch(&session, &frame) == 0U &&
                session.stopReason == UMICOM_USER_STOP_MONITOR_ERROR, "return PC cannot leave executable user pages");
        } else {
            UmicomTestRequire(0, "unknown native case name");
        }
    }
    UmicomTestTeardown();
    printf("PASS: %s\n", name);
    return EXIT_SUCCESS;
}
