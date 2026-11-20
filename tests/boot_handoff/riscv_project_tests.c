/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/boot_handoff/riscv_project_tests.c
 *
 * PURPOSE:
 *   Verify translation of the native page plan into a canonical boot report.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/riscv64/boot_handoff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void Refuse(const UmicomRiscvBootHandoffInput *input, const UmicomKernelBootReservationPlan *plan,
    UmicomBootHandoffStatus expected)
{
    UmicomKernelBootHandoff out;
    unsigned char previous[sizeof out];
    memset(&out, 0xa5, sizeof out);
    memcpy(previous, &out, sizeof previous);
    CHECK(UmicomRiscvBootHandoffProject(input, plan, &out) == expected);
    CHECK(memcmp(previous, &out, sizeof previous) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *mode = argv[1];
    UmicomRiscvBootHandoffInput input = {7U, {0x80000000U, 0x1000000U},
        {0x80200000U, 0x9000U}, {0x80f0f000U, 0x80U}};
    UmicomKernelDeviceTreeLayout tree = {.totalBytes = 0x80U, .reservationCount = 2U,
        .reservations = {{0x80300123U, 0x1000U}, {0x805001U, 0x40U}}};
    UmicomKernelBootReservationPlan plan;
    CHECK(UmicomKernelBootReservationsPlan((UmicomAddress)input.ram.address, input.ram.bytes,
        (UmicomAddress)(input.kernel.address + input.kernel.bytes), (UmicomAddress)input.deviceTree.address,
        &tree, &plan));
    CHECK(plan.count == 3U);
    CHECK(plan.ranges[0].base == 0x80000000U && plan.ranges[0].bytes == 0x209000U);
    CHECK(plan.ranges[1].base == 0x80300000U && plan.ranges[1].bytes == 0x2000U);
    CHECK(plan.ranges[2].base == 0x80f0f000U && plan.ranges[2].bytes == 0x1000U);

    if (strcmp(mode, "valid") == 0) {
        UmicomKernelBootHandoff out;
        CHECK(UmicomRiscvBootHandoffProject(&input, &plan, &out) == UMICOM_BOOT_HANDOFF_OK);
        CHECK(out.architecture == UMICOM_BOOT_RISCV64 && out.bootstrapCpu == 7U);
        CHECK(out.ramCount == 1U && out.ram[0].address == 0x80000000U && out.ram[0].bytes == 0x1000000U);
        CHECK(out.kernel.address == 0x80200000U && out.kernel.bytes == 0x9000U);
        CHECK(out.tableKind == UMICOM_BOOT_TABLE_FDT && out.table.address == 0x80f0f000U && out.table.bytes == 0x80U);
        CHECK(out.framebuffer.format == UMICOM_BOOT_PIXELS_NONE && out.commandLine[0] == '\0');
        CHECK(out.reservedCount == plan.count);
        for (UmicomSize i = 0U; i < plan.count; ++i) {
            CHECK(out.reserved[i].address == plan.ranges[i].base && out.reserved[i].bytes == plan.ranges[i].bytes);
        }
        memset(&input, 0, sizeof input);
        memset(&plan, 0, sizeof plan);
        CHECK(out.kernel.address == 0x80200000U && out.reserved[1].bytes == 0x2000U);
    } else if (strcmp(mode, "bounds") == 0) {
        plan.count = UMICOM_KERNEL_BOOT_RESERVATION_LIMIT + 1U;
        Refuse(&input, &plan, UMICOM_BOOT_HANDOFF_LIMIT);
    } else if (strcmp(mode, "kernel") == 0) {
        plan.ranges[0].bytes = 0x200000U;
        Refuse(&input, &plan, UMICOM_BOOT_HANDOFF_MEMORY);
    } else if (strcmp(mode, "device-tree") == 0) {
        --plan.count;
        Refuse(&input, &plan, UMICOM_BOOT_HANDOFF_RANGE);
    } else if (strcmp(mode, "tree-size") == 0) {
        input.deviceTree.bytes = 39U;
        Refuse(&input, &plan, UMICOM_BOOT_HANDOFF_RANGE);
    } else if (strcmp(mode, "ram") == 0) {
        ++input.ram.bytes;
        Refuse(&input, &plan, UMICOM_BOOT_HANDOFF_MEMORY);
    } else if (strcmp(mode, "arguments") == 0) {
        Refuse(NULL, &plan, UMICOM_BOOT_HANDOFF_ARGUMENT);
        Refuse(&input, NULL, UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(UmicomRiscvBootHandoffProject(&input, &plan, NULL) == UMICOM_BOOT_HANDOFF_ARGUMENT);
    } else {
        CHECK(0);
    }
    return 0;
}
