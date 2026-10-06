/*-----------------------------------------------------------------------------
 * Umicom Kernel startup-memory and independent recovery policy tests
 * File: tests/boot_services/startup_support_tests.c
 *
 * The real memory bootstrap, DTB inspector, allocator and recovery command
 * interpreter are compiled. RAM is an aligned host buffer; UART and privilege
 * transitions are not exercised. Each test is a new process so normal startup
 * really begins with an uninitialised physical-memory owner.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/startup.h"
#include "umicom/kernel/platform.h"
#include "umicom/kernel/physical_memory.h"
#include "umicom/kernel/console_shell.h"

#define UMICOM_STARTUP_TEST_RAM_BYTES 1048576U
alignas(4096) static UmicomU8 ram[UMICOM_STARTUP_TEST_RAM_BYTES];
static UmicomPlatformPhysicalMemoryInfo geometry;
static char outputText[4096];
static UmicomSize outputBytes;
static unsigned reports;
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"FAILED: %s\n",#c); exit(EXIT_FAILURE); } } while(0)
void UmicomPlatformPhysicalMemoryDescribe(UmicomPlatformPhysicalMemoryInfo *out) { *out=geometry; }
static void Output(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    CHECK(bytes < sizeof(outputText)-outputBytes);
    memcpy(outputText+outputBytes,text,(size_t)bytes); outputBytes+=bytes; outputText[outputBytes]=0;
}
static void Report(void *context) { (void)context; ++reports; }
static void MemoryCase(const char *name)
{
    geometry.base=(UmicomAddress)ram;geometry.bytes=sizeof(ram);
    UmicomAddress start=geometry.base+4096U,end=geometry.base+16384U,dtb=geometry.base+32768U;
    /* The existing inspector reads magic and total length. This is a deliberately
     * synthetic fixed header, not a real hardware-discovery catalogue. */
    UmicomU8 *header=(UmicomU8 *)dtb;
    header[0]=0xd0;header[1]=0x0d;header[2]=0xfe;header[3]=0xed;header[7]=40;
    UmicomU64 hart=0U;
    UmicomKernelBootMemoryStatus expected=UMICOM_BOOT_MEMORY_BAD_INPUT;
    if(!strcmp(name,"memory-valid")||!strcmp(name,"memory-reservation")||!strcmp(name,"memory-reinitialise"))expected=UMICOM_BOOT_MEMORY_OK;
    else if(!strcmp(name,"memory-hart"))hart=1U;
    else if(!strcmp(name,"memory-empty-ram"))geometry.bytes=0;
    else if(!strcmp(name,"memory-overflow-ram"))geometry.base=~(UmicomAddress)0U-100U;
    else if(!strcmp(name,"memory-alignment"))geometry.bytes--;
    else if(!strcmp(name,"memory-image-below"))start=geometry.base-1;
    else if(!strcmp(name,"memory-image-empty"))end=start;
    else if(!strcmp(name,"memory-image-outside"))end=geometry.base+sizeof(ram)+4096;
    else if(!strcmp(name,"memory-image-unaligned"))end++;
    else if(!strcmp(name,"memory-dtb-below"))dtb=geometry.base-1;
    else if(!strcmp(name,"memory-dtb-truncated"))dtb=geometry.base+sizeof(ram)-4;
    else if(!strcmp(name,"memory-dtb-magic"))header[0]=0;
    else if(!strcmp(name,"memory-dtb-size"))header[7]=8;
    else if(!strcmp(name,"memory-dtb-overlap"))end=dtb+4096U;
    else if(!strcmp(name,"memory-capacity"))geometry.bytes=(UMICOM_KERNEL_PHYSICAL_MAX_FRAMES+1)*4096U;
    else CHECK(0);
    CHECK(UmicomKernelBootMemoryInitialize(hart,dtb,start,end)==expected);
    UmicomKernelPhysicalMemorySnapshot snapshot;
    if(expected==UMICOM_BOOT_MEMORY_OK){
        CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot)==UMICOM_KERNEL_MEMORY_OK);
        CHECK(snapshot.allocatedFrames==0 && snapshot.reservedFrames==5 && snapshot.totalFrames==256);
        UmicomAddress allocated=0;
        CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&allocated)==UMICOM_KERNEL_MEMORY_OK && allocated>=end && allocated!=dtb);
        if(!strcmp(name,"memory-reinitialise")){
            CHECK(UmicomKernelBootMemoryInitialize(hart,dtb,start,end)==UMICOM_BOOT_MEMORY_ALREADY_INITIALISED);
            CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot)==UMICOM_KERNEL_MEMORY_OK && snapshot.allocatedFrames==1);
        }
        CHECK(UmicomKernelPhysicalMemoryFreeFrame(allocated)==UMICOM_KERNEL_MEMORY_OK);
        CHECK(UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK);
    }else CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot)==UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
}
static void RecoveryCase(const char *name)
{
    const char *line="help"; UmicomSize bytes=4;
    UmicomKernelRecoveryAction expected=UMICOM_RECOVERY_CONTINUE;
    if(!strcmp(name,"recovery-poweroff")){line="poweroff";bytes=8;expected=UMICOM_RECOVERY_POWEROFF;}
    else if(!strcmp(name,"recovery-status")){line="status";bytes=6;}
    else if(!strcmp(name,"recovery-help")){}
    else if(!strcmp(name,"recovery-extra-token")){line="poweroff now";bytes=12;}
    else if(!strcmp(name,"recovery-prefix")){line="poweroffX";bytes=9;}
    else if(!strcmp(name,"recovery-case")){line="POWEROFF";bytes=8;}
    else if(!strcmp(name,"recovery-launch-refused")){line="exec /bin/check";bytes=15;}
    else if(!strcmp(name,"recovery-file-refused")){line="rm /README";bytes=10;}
    else if(!strcmp(name,"recovery-empty")){line="";bytes=0;}
    else if(!strcmp(name,"recovery-null")){line=NULL;bytes=1;}
    else if(!strcmp(name,"recovery-quote")){line="\"poweroff";bytes=9;}
    else if(!strcmp(name,"recovery-control")){line="poweroff\033";bytes=9;}
    else if(!strcmp(name,"recovery-nul")){line="poweroff\0extra";bytes=14;}
    else CHECK(0);
    CHECK(UmicomKernelRecoveryCommand(line,bytes,"unit-test-reason",Output,Report,NULL)==expected);
    CHECK(reports==(!strcmp(name,"recovery-status")?1U:0U));
    if(reports)CHECK(strstr(outputText,"unit-test-reason")!=NULL);
    UmicomKernelPhysicalMemorySnapshot snapshot;
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&snapshot)==UMICOM_KERNEL_MEMORY_NOT_INITIALISED);
}
int main(int argc,char **argv)
{
    if(argc!=2)return EXIT_FAILURE;
    if(!strncmp(argv[1],"memory-",7))MemoryCase(argv[1]);else RecoveryCase(argv[1]);
    printf("PASS %s (%u checks)\n",argv[1],checks);return EXIT_SUCCESS;
}
