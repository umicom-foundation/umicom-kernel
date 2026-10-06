/* Umicom Kernel actual-ELF / RAMFS / loader integration test.
 * This host program checks real separately linked RV64 ELF bytes, real VFS
 * reads and the real loader's private mappings. It executes no RISC-V code.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "umicom/kernel/ramfs.h"
#include "umicom/kernel/process.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"loader bridge:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==UMICOM_VFS_OK)
alignas(4096) static UmicomU8 umicomArena[256U * 4096U];
static UmicomKernelRamfs umicomFs;
static UmicomKernelVfs umicomVfs;
static UmicomKernelVfsClient umicomClient;
static UmicomKernelProcess umicomProcess;
static UmicomU8 umicomBytes[UMICOM_RAMFS_FILE_BYTES];
static UmicomU8 umicomReadback[UMICOM_RAMFS_FILE_BYTES];
UmicomBoolean UmicomKernelObjectCacheAccessAllowed(void) { return UMICOM_TRUE; }
UmicomKernelMemoryStatus UmicomVfsActualAllocate(UmicomAddress *outFrame);
UmicomKernelMemoryStatus UmicomVfsActualFree(UmicomAddress frame);
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryAllocateFrame(UmicomAddress *outFrame)
{
    return UmicomVfsActualAllocate(outFrame);
}
UmicomKernelMemoryStatus UmicomKernelPhysicalMemoryFreeFrame(UmicomAddress frame)
{
    return UmicomVfsActualFree(frame);
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *const input=fopen(argv[1],"rb"); CHECK(input!=NULL);
    const size_t bytes=fread(umicomBytes,1U,sizeof(umicomBytes),input);
    CHECK(bytes>64U && bytes<sizeof(umicomBytes) && !ferror(input)); CHECK(fgetc(input)==EOF); CHECK(fclose(input)==0);
    UmicomKernelExecutablePlan plan={0};
    CHECK(UmicomKernelExecutableInspect(umicomBytes,(UmicomSize)bytes,&plan)==UMICOM_EXECUTABLE_OK);
    CHECK(UmicomKernelPhysicalMemoryInitialize((UmicomAddress)umicomArena,sizeof(umicomArena))==UMICOM_KERNEL_MEMORY_OK);
    OK(UmicomKernelRamfsInitialize(&umicomFs)); OK(UmicomKernelVfsMount(&umicomVfs,UmicomKernelRamfsOperations(),&umicomFs));
    OK(UmicomKernelVfsClientOpen(&umicomClient,&umicomVfs,101U,UMICOM_VFS_RIGHT_ALL));
    OK(UmicomKernelVfsCreate(&umicomClient,"/bin",UMICOM_VFS_DIRECTORY));
    OK(UmicomKernelVfsCreate(&umicomClient,"/bin/umicom-diagnostic.elf",UMICOM_VFS_FILE));
    UmicomKernelFileDescriptor fd=0U;
    UmicomSize transferred=0U;
    OK(UmicomKernelVfsOpen(&umicomClient,"/bin/umicom-diagnostic.elf",UMICOM_VFS_RIGHT_READ|UMICOM_VFS_RIGHT_WRITE,UMICOM_FALSE,&fd));
    OK(UmicomKernelVfsWrite(&umicomClient,fd,umicomBytes,(UmicomSize)bytes,&transferred));CHECK(transferred==bytes);
    const UmicomU8 badMagic[4]={0U,0U,0U,0U};
    OK(UmicomKernelVfsSeek(&umicomClient,fd,0U));OK(UmicomKernelVfsWrite(&umicomClient,fd,badMagic,4U,&transferred));
    OK(UmicomKernelVfsSeek(&umicomClient,fd,0U));OK(UmicomKernelVfsRead(&umicomClient,fd,umicomReadback,(UmicomSize)bytes,&transferred));
    UmicomKernelExecutableStatus inspected=UMICOM_EXECUTABLE_OK;
    UmicomKernelPhysicalMemorySnapshot before={0},after={0};
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&before)==UMICOM_KERNEL_MEMORY_OK);
    CHECK(UmicomKernelProcessCreate(&umicomProcess,umicomReadback,(UmicomSize)bytes,991U,&inspected)==UMICOM_PROCESS_EXECUTABLE_REFUSED);
    CHECK(inspected==UMICOM_EXECUTABLE_BAD_MAGIC);
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&after)==UMICOM_KERNEL_MEMORY_OK && after.allocatedFrames==before.allocatedFrames);
    /* Repair the file itself, not merely the temporary read buffer. */
    OK(UmicomKernelVfsSeek(&umicomClient,fd,0U));OK(UmicomKernelVfsWrite(&umicomClient,fd,umicomBytes,4U,&transferred));
    OK(UmicomKernelVfsSeek(&umicomClient,fd,0U));OK(UmicomKernelVfsRead(&umicomClient,fd,umicomReadback,(UmicomSize)bytes,&transferred));
    CHECK(transferred==bytes && !memcmp(umicomReadback,umicomBytes,bytes));
    CHECK(UmicomKernelProcessCreate(&umicomProcess,umicomReadback,(UmicomSize)bytes,991U,&inspected)==UMICOM_PROCESS_OK);
    CHECK(umicomProcess.state==UMICOM_PROCESS_READY && umicomProcess.entry==plan.entry);
    OK(UmicomKernelVfsClientClose(&umicomClient,&transferred));OK(UmicomKernelVfsUnmount(&umicomVfs));OK(UmicomKernelRamfsClose(&umicomFs));
    memset(umicomReadback,0xee,sizeof(umicomReadback));
    for(UmicomSize s=0U;s<plan.segmentCount;++s) {
        const UmicomKernelExecutableSegment *const segment=&plan.segments[s];
        CHECK(segment->memoryBytes<=sizeof(umicomReadback));
        /* Preserve the existing checked-copy limit. Larger observations use
         * bounded chunks rather than widening a production security contract. */
        for(UmicomSize offset=0U;offset<segment->memoryBytes;) {
            const UmicomSize remaining=segment->memoryBytes-offset;
            const UmicomSize part=remaining<UMICOM_USER_COPY_LIMIT?remaining:UMICOM_USER_COPY_LIMIT;
            CHECK(UmicomKernelUserMemoryRead(&umicomProcess.report.memory,segment->virtualBase+offset,
                umicomReadback+offset,part)==UMICOM_USER_RESULT_OK);
            offset+=part;
        }
        CHECK(!memcmp(umicomReadback,umicomBytes+segment->fileOffset,(size_t)segment->fileBytes));
        for(UmicomSize i=segment->fileBytes;i<segment->memoryBytes;++i)CHECK(umicomReadback[i]==0U);
    }
    CHECK(UmicomKernelProcessDestroy(&umicomProcess)==UMICOM_PROCESS_OK);
    CHECK(UmicomKernelPhysicalMemorySnapshotRead(&after)==UMICOM_KERNEL_MEMORY_OK && after.allocatedFrames==0U);
    CHECK(UmicomKernelPhysicalMemoryValidate()==UMICOM_KERNEL_MEMORY_OK);
    puts("PASS actual ELF stored in RAMFS, rejected/repaired, loaded privately and checked after filesystem destruction; no guest instructions executed");
    return 0;
}
