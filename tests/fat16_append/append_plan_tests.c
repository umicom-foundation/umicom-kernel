/* Bounded append planner qualification against independent media bytes.
 * Reuse the unchanged read-only fixture and storage-guard helpers; the local
 * adapter selects Append only inside this new translation unit. Existing
 * regression sources and their normal builds retain their original meaning.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_file_append.h"
#define main UmicomAppendOriginalPlannerEntry
#define UmicomKernelFat16PlanFileUpdate(v,p,o,i,b,t,w,f,d,m) \
    UmicomKernelFat16PlanFileAppend(v,p,i,b,t,w,f,d,m)
#include "../fat16_file_commit/file_plan_tests.c"
#undef UmicomKernelFat16PlanFileUpdate
#undef main

static void AppendPut32(UmicomU8 *target, UmicomU32 value)
{
    for (UmicomSize i = 0U; i < 4U; ++i) target[i] = (UmicomU8)(value >> (8U * i));
}
static void AppendPlanVerify(UmicomU64 directory, UmicomSize entry)
{
    CHECK(dataPlan.entry.bytes == requestOffset && dataPlan.offset == requestOffset && dataPlan.bytes == requestBytes);
    CHECK(dataPlan.count == 1U && dataPlan.sectors[0].sector == UMICOM_DISK_FIXTURE_DATA + 4U);
    CHECK(dataPlan.sectors[0].offset == requestOffset % 512U && dataPlan.sectors[0].bytes == requestBytes);
    CHECK(!dataPlan.sectors[0].inputOffset);
    UmicomU8 expected[512]; memcpy(expected, Sector(UMICOM_DISK_FIXTURE_DATA + 4U), sizeof(expected));
    memcpy(expected + requestOffset % 512U, inputBefore, requestBytes);
    CHECK(!memcmp(expected, dataPlan.sectors[0].data, sizeof(expected)));
    CHECK(filePlan.directorySector == directory && filePlan.entryOffset == entry);
    CHECK(!memcmp(filePlan.original, Sector(directory), 512U));
    memcpy(expected, Sector(directory), sizeof(expected)); expected[entry + 11U] |= 0x20U;
    Put16(expected + entry + 22U, 0x9afdU); Put16(expected + entry + 24U, 0x5d47U);
    AppendPut32(expected + entry + 28U, (UmicomU32)(requestOffset + requestBytes));
    CHECK(!memcmp(filePlan.data, expected, sizeof(expected)));
    CHECK(filePlan.encodedTime.writeTime == 0x9afdU && filePlan.encodedTime.writeDate == 0x5d47U);
    CHECK(filePlan.encodedTime.storedSecond == 58U);
    CHECK(!memcmp(media, mediumBefore, sizeof(media))); Scrubbed();
}
static void AppendPlanSuccess(const char *name)
{
    UmicomU8 *const root = Sector(UMICOM_DISK_FIXTURE_ROOT);
    UmicomU64 directory = UMICOM_DISK_FIXTURE_ROOT; UmicomSize entry = 96U;
    if (!strcmp(name,"archive_clear")) root[107U] = 0U;
    else if (!strcmp(name,"hidden_system")) root[107U] = 6U;
    else if (!strcmp(name,"single_byte")) requestBytes = 1U;
    else if (!strcmp(name,"exact_capacity")) requestBytes = 236U;
    else if (!strcmp(name,"last_byte")) { requestOffset = 1535U; requestBytes = 1U; AppendPut32(root + 124U, 1535U); }
    else if (!strcmp(name,"casefold")) memcpy(path,"/frag.bin",10U);
    else if (!strcmp(name,"last_root_record")) {
        UmicomU8 saved[32]; memcpy(saved,root+96U,sizeof(saved)); root[96U]=0xe5U;
        for(UmicomSize i=5U;i<511U;++i) root[i*32U]=0xe5U;
        memcpy(root+511U*32U,saved,sizeof(saved)); directory+=31U;entry=480U;
    } else if (!strcmp(name,"nested_fragmented")) {
        UmicomU8 saved[32]; memcpy(saved,root+96U,sizeof(saved)); root[96U]=0xe5U;
        FatLink(3U,30U);FatLink(30U,11U);FatLink(11U,0xffffU);
        for(UmicomSize i=3U;i<16U;++i) Cluster(3U)[i*32U]=0xe5U;
        memset(Cluster(30U),0,512U);memset(Cluster(11U),0,512U);
        for(UmicomSize i=0U;i<16U;++i) {Cluster(30U)[i*32U]=0xe5U;Cluster(11U)[i*32U]=0xe5U;}
        memcpy(Cluster(11U)+480U,saved,sizeof(saved));memcpy(path,"/DOCS/FRAG.BIN",15U);
        directory=UMICOM_DISK_FIXTURE_DATA+9U;entry=480U;
    } else CHECK(!strcmp(name,"ordinary")||!strcmp(name,"input_snapshot")||!strcmp(name,"callback_reentry")||!strcmp(name,"workspace_reuse"));
    memcpy(mediumBefore,media,sizeof(media));Start();
    snapshotInputs=!strcmp(name,"input_snapshot")?UMICOM_TRUE:UMICOM_FALSE;
    reenter=!strcmp(name,"callback_reentry")?UMICOM_TRUE:UMICOM_FALSE;
    CHECK(Plan()==UMICOM_DISK_OK);AppendPlanVerify(directory,entry);
    if(reenter) CHECK(reentries==1U);
    if(!strcmp(name,"workspace_reuse")) {
        memset(&dataPlan,0xa5,sizeof(dataPlan));memset(&filePlan,0x5a,sizeof(filePlan));reads=0U;
        CHECK(Plan()==UMICOM_DISK_OK);AppendPlanVerify(directory,entry);
    }
}
static void AppendPlanRange(const char *name)
{
    UmicomU8 *const root=Sector(UMICOM_DISK_FIXTURE_ROOT);
    UmicomKernelDiskStatus expected=UMICOM_DISK_RANGE;
    if(!strcmp(name,"one_over_capacity")) requestBytes=237U;
    else if(!strcmp(name,"full_cluster")) AppendPut32(root+124U,1536U);
    else if(!strcmp(name,"empty_file")) memcpy(path,"/EMPTY.TXT",11U);
    else if(!strcmp(name,"size_overflow")) AppendPut32(root+124U,0xffffffffU);
    else if(!strcmp(name,"zero_bytes")) {requestBytes=0U;expected=UMICOM_DISK_INVALID_ARGUMENT;}
    else if(!strcmp(name,"oversized")) {requestBytes=4097U;expected=UMICOM_DISK_INVALID_ARGUMENT;}
    else if(!strcmp(name,"directory")) {memcpy(path,"/DOCS",6U);expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"missing")) {memcpy(path,"/MISSING.BIN",13U);expected=UMICOM_DISK_NOT_FOUND;}
    else if(!strcmp(name,"read_only")) {root[107U]=1U;expected=UMICOM_DISK_READ_ONLY;}
    else CHECK(0);
    memcpy(mediumBefore,media,sizeof(media));Start();
    CHECK(Plan()==expected);Filled(&dataPlan,sizeof(dataPlan),0xa5U);Filled(&filePlan,sizeof(filePlan),0x5aU);
    CHECK(!memcmp(media,mediumBefore,sizeof(media)));
    if(expected==UMICOM_DISK_INVALID_ARGUMENT) {
        CHECK(!reads);Filled(&workspace,sizeof(workspace),0U);Filled(&fileWorkspace,sizeof(fileWorkspace),0U);
    } else Scrubbed();
}
int main(int argc,char **argv)
{
    CHECK(argc==3);Setup(argv[2]);requestOffset=1300U;requestBytes=197U;
    const char *const name=argv[1];
    if(!strncmp(name,"success.",8U)) AppendPlanSuccess(name+8U);
    else if(!strncmp(name,"range.",6U)) AppendPlanRange(name+6U);
    else if(!strncmp(name,"guard.",6U)) Guard(name+6U);
    else if(!strcmp(name,"all_read_failures")) ReadFailures();
    else if(!strcmp(name,"path_result_boundary")) PathBoundary();
    else if(!strncmp(name,"refusal.",8U)) Refusal(name+8U);
    else CHECK(0);
    printf("fat16-append.plan.%s: ok\n",name);return 0;
}
