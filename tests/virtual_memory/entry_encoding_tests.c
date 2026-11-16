/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/virtual_memory/entry_encoding_tests.c
 *
 * PURPOSE:
 *   Verify stored Sv39 entry admission before software walks, mutation and destruction, using owned table storage.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/


#include "umicom/kernel/virtual_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define VA ((UmicomAddress)0x2000U)
#define PA ((UmicomAddress)0x3000U)
#define BIT(n) ((UmicomU64)1U << (n))
alignas(4096) static unsigned char ram[4096U * 16U];
static unsigned char beforeRam[sizeof ram];
static UmicomKernelVirtualAddressSpace space;
static UmicomKernelVirtualAddressSpace beforeSpace;
static UmicomKernelPhysicalMemorySnapshot beforeAccounting;
static UmicomU64 *entries[3];

static UmicomKernelPhysicalMemorySnapshot Accounting(void)
{
    UmicomKernelPhysicalMemorySnapshot result = {0};
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&result) == UMICOM_KERNEL_MEMORY_OK);
    return result;
}
static void Capture(void)
{
    memcpy(beforeRam,ram,sizeof ram);
    beforeSpace = space;
    beforeAccounting = Accounting();
}
static void Unchanged(void)
{
    UmicomKernelPhysicalMemorySnapshot now = Accounting();
    CHECK(!memcmp(beforeRam,ram,sizeof ram));
    CHECK(space.rootTablePhysicalAddress == beforeSpace.rootTablePhysicalAddress);
    CHECK(space.pageTableFrames == beforeSpace.pageTableFrames);
    CHECK(space.mappedPages == beforeSpace.mappedPages && space.initialised == beforeSpace.initialised);
    CHECK(now.ramBase == beforeAccounting.ramBase && now.ramBytes == beforeAccounting.ramBytes);
    CHECK(now.pageBytes == beforeAccounting.pageBytes && now.totalFrames == beforeAccounting.totalFrames);
    CHECK(now.reservedFrames == beforeAccounting.reservedFrames);
    CHECK(now.allocatedFrames == beforeAccounting.allocatedFrames && now.freeFrames == beforeAccounting.freeFrames);
    CHECK(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
}
static void Initialize(void)
{
    memset(ram,0xa5,sizeof ram);
    CHECK(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)ram,sizeof ram) == UMICOM_KERNEL_MEMORY_OK);
    CHECK(UmicomKernelVirtualAddressSpaceCreate(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(UmicomKernelVirtualMemoryMapPage(&space,VA,PA,UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    /* Resolve only valid owned tables before fault injection. Leaf bytes are
     * never read; PA is an encoding fixture, not a claim of usable host RAM. */
    entries[0] = (UmicomU64 *)space.rootTablePhysicalAddress;
    entries[1] = (UmicomU64 *)(UmicomAddress)((entries[0][0] >> 10U) << 12U);
    entries[2] = (UmicomU64 *)(UmicomAddress)((entries[1][0] >> 10U) << 12U);
    CHECK(space.pageTableFrames == 3U && space.mappedPages == 1U);
}
static void Refused(UmicomKernelVirtualMemoryStatus expected)
{
    UmicomAddress translated = 19U;
    UmicomKernelVirtualMemoryPermissions permissions = 37U;
    Capture();
    CHECK(UmicomKernelVirtualMemoryTranslate(&space,VA+17U,&translated,&permissions) == expected);
    CHECK(translated == 19U && permissions == 37U); Unchanged();
    CHECK(UmicomKernelVirtualMemoryValidate(&space) == expected); Unchanged();
    CHECK(UmicomKernelVirtualMemoryMapPage(&space,VA,PA,UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == expected); Unchanged();
    CHECK(UmicomKernelVirtualMemoryUnmapPage(&space,VA) == expected); Unchanged();
    /* Destruction first validates; it must not free part of a corrupt owner. */
    CHECK(UmicomKernelVirtualAddressSpaceDestroy(&space) == expected); Unchanged();
}
static void Accepted(UmicomKernelVirtualMemoryPermissions expected)
{
    UmicomAddress translated = 19U;
    UmicomKernelVirtualMemoryPermissions permissions = 37U;
    Capture();
    CHECK(UmicomKernelVirtualMemoryValidate(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK); Unchanged();
    CHECK(UmicomKernelVirtualMemoryTranslate(&space,VA+17U,&translated,&permissions) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(translated == PA+17U && permissions == expected); Unchanged();
    CHECK(UmicomKernelVirtualMemoryMapPage(&space,VA,PA,UMICOM_KERNEL_VIRTUAL_MEMORY_READ) == UMICOM_KERNEL_VIRTUAL_MEMORY_ALREADY_MAPPED);
    Unchanged();
}
int main(int argc,char **argv)
{
    CHECK(argc == 4);
    const char *mode = argv[1];
    unsigned level = 0U, value = 0U;
    CHECK(sscanf(argv[2],"%u",&level) == 1 && level < 3U);
    CHECK(sscanf(argv[3],"%u",&value) == 1 && value < 64U);
    Initialize();
    UmicomU64 *entry = &entries[level][level == 2U ? 2U : 0U];
    UmicomU64 original = *entry;
    if (!strcmp(mode,"reserved")) {
        CHECK(value >= 54U);
        *entry |= BIT(value);
        Refused(UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE);
    } else if (!strcmp(mode,"nonleaf")) {
        CHECK(level < 2U && (value == 2U || value == 4U || value == 5U || value == 6U || value == 7U));
        *entry |= BIT(value);
        Refused(UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE);
    } else if (!strcmp(mode,"rights")) {
        CHECK(level == 2U && value < 8U);
        *entry = (*entry & ~(UmicomU64)14U) | ((UmicomU64)value << 1U);
        if (value == 0U || value == 2U || value == 6U)
            Refused(UMICOM_KERNEL_VIRTUAL_MEMORY_CORRUPT_PAGE_TABLE);
        else
            Accepted((UmicomKernelVirtualMemoryPermissions)value);
    } else if (!strcmp(mode,"large")) {
        CHECK(level < 2U);
        *entry |= BIT(1U);
        Refused(UMICOM_KERNEL_VIRTUAL_MEMORY_UNSUPPORTED_LARGE_PAGE);
    } else if (!strcmp(mode,"software")) {
        CHECK(value == 8U || value == 9U);
        *entry |= BIT(value);
        Accepted(UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
    } else if (!strcmp(mode,"invalid-payload")) {
        /* Invalid entries are ignored even when their unused payload contains
         * reserved bits. This slot has never owned a child table or mapping. */
        CHECK(level == 0U);
        entry = &entries[0][1]; original = *entry;
        *entry = ~(UmicomU64)1U;
        Accepted(UMICOM_KERNEL_VIRTUAL_MEMORY_READ);
        UmicomAddress output = 19U;
        UmicomKernelVirtualMemoryPermissions permissions = 37U;
        CHECK(UmicomKernelVirtualMemoryTranslate(&space,(UmicomAddress)1U<<30U,&output,&permissions)
            == UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED);
        CHECK(output == 19U && permissions == 37U); Unchanged();
        CHECK(UmicomKernelVirtualMemoryUnmapPage(&space,(UmicomAddress)1U<<30U)
            == UMICOM_KERNEL_VIRTUAL_MEMORY_NOT_MAPPED); Unchanged();
    } else CHECK(0);
    *entry = original;
    CHECK(UmicomKernelVirtualMemoryValidate(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(UmicomKernelVirtualMemoryUnmapPage(&space,VA) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(space.pageTableFrames == 1U && space.mappedPages == 0U);
    CHECK(UmicomKernelVirtualAddressSpaceDestroy(&space) == UMICOM_KERNEL_VIRTUAL_MEMORY_OK);
    CHECK(Accounting().allocatedFrames == 0U);
    CHECK(UmicomKernelPhysicalMemoryValidate() == UMICOM_KERNEL_MEMORY_OK);
    return 0;
}
