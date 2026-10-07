/* Read-only rename preparation qualification. The existing synthetic fixture
 * helpers are included unchanged; all calls and expected rename sectors below
 * are independent of the production rename implementation.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT. */
#include "umicom/kernel/fat16_rename.h"
#define main UmicomRenameOriginalPlannerEntry
#include "../fat16_file_commit/file_plan_tests.c"
#undef main
static UmicomKernelFat16RenameWorkspace renameWorkspace, renameWorkspaceBefore;
static UmicomKernelFat16RenamePlan renamePlan, renameBefore;
static char renameName[13];
static UmicomBoolean renameSnapshot, renameReenter;
static UmicomSize renameReentries;
static UmicomKernelDiskStatus RenamePlanCall(void)
{
    return UmicomKernelFat16PlanRename(&volume,path,renameName,&workspace,&renameWorkspace,&renamePlan);
}
static UmicomBoolean RenameRead(void *context,UmicomU64 lba,UmicomU8 *output)
{
    const UmicomBoolean ok=Read(context,lba,output);
    if(reads==1U && renameSnapshot) {memset(path,'x',sizeof(path));memset(renameName,'y',sizeof(renameName));}
    if(reads==1U && renameReenter) {
        CHECK(RenamePlanCall()==UMICOM_DISK_BUSY);CHECK(UmicomKernelFat16Close(&volume)==UMICOM_DISK_BUSY);++renameReentries;
    }
    return ok;
}
static void RenameStart(void)
{
    Start();volume.reader.read=RenameRead;memset(&renameWorkspace,0,sizeof(renameWorkspace));
    memset(&renamePlan,0xa5,sizeof(renamePlan));renameSnapshot=UMICOM_FALSE;renameReenter=UMICOM_FALSE;renameReentries=0U;
}
static void RenameScrubbed(void)
{
    CHECK(workspace.self==&workspace && renameWorkspace.self==&renameWorkspace);
    Filled((const UmicomU8 *)&workspace+sizeof(workspace.self),sizeof(workspace)-sizeof(workspace.self),0U);
    Filled((const UmicomU8 *)&renameWorkspace+sizeof(renameWorkspace.self),sizeof(renameWorkspace)-sizeof(renameWorkspace.self),0U);
    CHECK(!volume.busy && !volume.fatCached);
}
static void RenameVerify(UmicomU64 directory,UmicomSize at,const char *canonical)
{
    CHECK(renamePlan.directorySector==directory && renamePlan.entryOffset==at);
    CHECK(!strcmp(renamePlan.updatedName,canonical));CHECK(!memcmp(renamePlan.original,Sector(directory),512U));
    UmicomU8 expected[512];memcpy(expected,Sector(directory),sizeof(expected));memset(expected+at,' ',11U);
    UmicomSize field=0U,pos=0U;
    for(UmicomSize i=0U;canonical[i];++i) {
        if(canonical[i]=='.') {field=8U;pos=0U;} else expected[at+field+pos++]=(UmicomU8)canonical[i];
    }
    expected[at+12U]&=(UmicomU8)~0x18U;
    CHECK(!memcmp(renamePlan.data,expected,sizeof(expected)));
    CHECK(!memcmp(media,mediumBefore,sizeof(media)));RenameScrubbed();
}
static void RenameSuccess(const char *name)
{
    UmicomU8 *const root=Sector(UMICOM_DISK_FIXTURE_ROOT);UmicomU64 directory=UMICOM_DISK_FIXTURE_ROOT;UmicomSize at=96U;
    const char *canonical="SAVED.BIN";
    if(!strcmp(name,"empty")) {memcpy(path,"/EMPTY.TXT",11U);at=128U;}
    else if(!strcmp(name,"archive_clear")) root[107U]=0U;
    else if(!strcmp(name,"hidden_system")) root[107U]=0x26U;
    else if(!strcmp(name,"case_flags")) root[108U]=0x18U;
    else if(!strcmp(name,"mixed_case")) memcpy(renameName,"sAvEd.BiN",10U);
    else if(!strcmp(name,"no_extension")) {memcpy(renameName,"NOEXT",6U);canonical="NOEXT";}
    else if(!strcmp(name,"maximum_alias")) {memcpy(renameName,"AbCdEfGh.XyZ",13U);canonical="ABCDEFGH.XYZ";}
    else if(!strcmp(name,"short_extension")) {memcpy(renameName,"N.X",4U);canonical="N.X";}
    else if(!strcmp(name,"different_parent")) {memcpy(renameName,"GUIDE.TXT",10U);canonical="GUIDE.TXT";}
    else if(!strcmp(name,"deleted_collision")) {
        memset(root+160U,0,32U);memcpy(root+160U,"SAVED   BIN",11U);root[160U]=0xe5U;
    } else if(!strcmp(name,"root_last_record")) {
        UmicomU8 saved[32];memcpy(saved,root+96U,32U);root[96U]=0xe5U;
        for(UmicomSize i=5U;i<511U;++i) root[i*32U]=0xe5U;
        memcpy(root+511U*32U,saved,32U);directory+=31U;at=480U;
    } else if(!strcmp(name,"nested_fragmented")) {
        UmicomU8 saved[32];memcpy(saved,root+96U,32U);root[96U]=0xe5U;
        FatLink(3U,30U);FatLink(30U,11U);FatLink(11U,0xffffU);
        for(UmicomSize i=3U;i<16U;++i) Cluster(3U)[i*32U]=0xe5U;
        memset(Cluster(30U),0,512U);memset(Cluster(11U),0,512U);
        for(UmicomSize i=0U;i<16U;++i) {Cluster(30U)[i*32U]=0xe5U;Cluster(11U)[i*32U]=0xe5U;}
        memcpy(Cluster(11U)+480U,saved,32U);memcpy(path,"/DOCS/FRAG.BIN",15U);
        directory=UMICOM_DISK_FIXTURE_DATA+9U;at=480U;
    } else CHECK(!strcmp(name,"ordinary")||!strcmp(name,"snapshot")||!strcmp(name,"reentry")||!strcmp(name,"reuse"));
    memcpy(mediumBefore,media,sizeof(media));RenameStart();
    renameSnapshot=!strcmp(name,"snapshot")?UMICOM_TRUE:UMICOM_FALSE;
    renameReenter=!strcmp(name,"reentry")?UMICOM_TRUE:UMICOM_FALSE;
    CHECK(RenamePlanCall()==UMICOM_DISK_OK);RenameVerify(directory,at,canonical);
    CHECK(renamePlan.originalEntry.bytes==(!strcmp(name,"empty")?0U:1300U));
    CHECK(renamePlan.originalEntry.firstCluster==(!strcmp(name,"empty")?0U:4U));
    if(renameReenter) CHECK(renameReentries==1U);
    if(!strcmp(name,"reuse")) {CHECK(RenamePlanCall()==UMICOM_DISK_OK);RenameVerify(directory,at,canonical);}
}
static void RenameRefusal(const char *name)
{
    UmicomU8 *const root=Sector(UMICOM_DISK_FIXTURE_ROOT);UmicomKernelDiskStatus expected=UMICOM_DISK_EXISTS;
    if(!strcmp(name,"same_alias")) memcpy(renameName,"FRAG.BIN",9U);
    else if(!strcmp(name,"case_only")) memcpy(renameName,"frag.bin",9U);
    else if(!strcmp(name,"file_collision")) memcpy(renameName,"README.TXT",11U);
    else if(!strcmp(name,"directory_collision")) memcpy(renameName,"DOCS",5U);
    else if(!strcmp(name,"root")) {memcpy(path,"/",2U);expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"directory")) {memcpy(path,"/DOCS",6U);expected=UMICOM_DISK_IS_DIRECTORY;}
    else if(!strcmp(name,"missing")) {memcpy(path,"/MISSING.BIN",13U);expected=UMICOM_DISK_NOT_FOUND;}
    else if(!strcmp(name,"readonly")) {root[107U]|=1U;expected=UMICOM_DISK_READ_ONLY;}
    else if(!strcmp(name,"allocated_empty")) {memcpy(path,"/EMPTY.TXT",11U);Put16(root+154U,20U);FatLink(20U,0xffffU);expected=UMICOM_DISK_CORRUPT;}
    else if(!strcmp(name,"orphan")) {FatLink(20U,0xffffU);expected=UMICOM_DISK_CORRUPT;}
    else CHECK(0);
    memcpy(mediumBefore,media,sizeof(media));RenameStart();CHECK(RenamePlanCall()==expected);
    Filled(&renamePlan,sizeof(renamePlan),0xa5U);CHECK(!memcmp(media,mediumBefore,sizeof(media)));RenameScrubbed();
    if(!strcmp(name,"file_collision")) {
        /* EMPTY follows the source record; collision checks must not stop
         * after seeing the source or only consider preceding siblings. */
        memcpy(renameName,"EMPTY.TXT",10U);CHECK(RenamePlanCall()==UMICOM_DISK_EXISTS);
        Filled(&renamePlan,sizeof(renamePlan),0xa5U);RenameScrubbed();
    }
}
static void RenameGrammar(const char *name)
{
    const char *replacement=NULL;UmicomKernelDiskStatus expected=UMICOM_DISK_INVALID_ARGUMENT;
    if(!strcmp(name,"empty")) replacement="";
    else if(!strcmp(name,"dot")) replacement=".";
    else if(!strcmp(name,"parent")) replacement="..";
    else if(!strcmp(name,"leading_dot")) replacement=".BIN";
    else if(!strcmp(name,"trailing_dot")) replacement="SAVED.";
    else if(!strcmp(name,"multiple_dots")) replacement="A.B.C";
    else if(!strcmp(name,"base_long")) replacement="ABCDEFGHI.X";
    else if(!strcmp(name,"extension_long")) replacement="A.ABCD";
    else if(!strcmp(name,"space")) replacement="A B.X";
    else if(!strcmp(name,"slash")) replacement="DOCS/A.X";
    else if(!strcmp(name,"backslash")) replacement="DOCS\\A.X";
    else if(!strcmp(name,"colon")) replacement="A:B.X";
    else if(!strcmp(name,"control")) replacement="A\tB.X";
    else if(!strcmp(name,"high_byte")) replacement="A\x80";
    else if(!strcmp(name,"unterminated")) memset(renameName,'A',sizeof(renameName));
    else if(!strcmp(name,"relative_source")) memcpy(path,"FRAG.BIN",9U);
    else if(!strcmp(name,"parent_source")) memcpy(path,"/DOCS/../FRAG.BIN",18U);
    else if(!strcmp(name,"unterminated_source")) {memset(path,'A',sizeof(path));expected=UMICOM_DISK_LIMIT;}
    else CHECK(0);
    if(replacement) {memset(renameName,0,sizeof(renameName));memcpy(renameName,replacement,strlen(replacement)+1U);}
    RenameStart();CHECK(RenamePlanCall()==expected);CHECK(!reads);
    Filled(&renamePlan,sizeof(renamePlan),0xa5U);
    if(!strcmp(name,"relative_source")||!strcmp(name,"parent_source")) RenameScrubbed();
    else {Filled(&workspace,sizeof(workspace),0U);Filled(&renameWorkspace,sizeof(renameWorkspace),0U);}
}
static void RenameAlphabet(void)
{
    static const char allowed[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-$~!#%&()@^{}'";
    for(unsigned byte=1U;byte<=255U;++byte) {
        RenameStart();memcpy(renameName,"AXB.C",6U);renameName[1]=(char)byte;
        const UmicomBoolean valid=strchr(allowed,(int)byte)?UMICOM_TRUE:UMICOM_FALSE;
        CHECK(RenamePlanCall()==(valid?UMICOM_DISK_OK:UMICOM_DISK_INVALID_ARGUMENT));
        if(!valid) {CHECK(!reads);Filled(&renamePlan,sizeof(renamePlan),0xa5U);}
    }
}
static void RenameFailures(void)
{
    memcpy(mediumBefore,media,sizeof(media));RenameStart();CHECK(RenamePlanCall()==UMICOM_DISK_OK);
    const UmicomSize count=reads;
    for(UmicomSize i=1U;i<=count;++i) {
        RenameStart();failAt=i;CHECK(RenamePlanCall()==UMICOM_DISK_IO_ERROR);CHECK(reads==i);
        Filled(&renamePlan,sizeof(renamePlan),0xa5U);CHECK(!memcmp(media,mediumBefore,sizeof(media)));RenameScrubbed();
    }
    printf("rename planner read-failure positions=%llu\n",(unsigned long long)count);
}
static void RenameGuard(const char *name)
{
    RenameStart();UmicomKernelFat16 *owner=&volume;
    UmicomKernelFat16UpdateWorkspace *base=&workspace;UmicomKernelFat16RenameWorkspace *scratch=&renameWorkspace;
    UmicomKernelFat16RenamePlan *out=&renamePlan;const char *source=path,*replacement=renameName;
    UmicomKernelDiskStatus expected=UMICOM_DISK_INVALID_ARGUMENT;
    static UmicomKernelFat16RenameWorkspace copied;
    if(!strcmp(name,"output_volume")) out=(UmicomKernelFat16RenamePlan *)(void *)&volume;
    else if(!strcmp(name,"output_base_workspace")) out=(UmicomKernelFat16RenamePlan *)(void *)&workspace;
    else if(!strcmp(name,"output_workspace")) out=&renameWorkspace.stage;
    else if(!strcmp(name,"workspace_volume")) scratch=(UmicomKernelFat16RenameWorkspace *)(void *)&volume;
    else if(!strcmp(name,"workspaces_overlap")) scratch=(UmicomKernelFat16RenameWorkspace *)(void *)&workspace;
    else if(!strcmp(name,"path_volume")) source=(const char *)&volume;
    else if(!strcmp(name,"path_base_workspace")) source=(const char *)&workspace;
    else if(!strcmp(name,"path_workspace")) source=renameWorkspace.path;
    else if(!strcmp(name,"path_output")) source=(const char *)&renamePlan;
    else if(!strcmp(name,"name_volume")) replacement=(const char *)&volume;
    else if(!strcmp(name,"name_base_workspace")) replacement=(const char *)&workspace;
    else if(!strcmp(name,"name_workspace")) replacement=renameWorkspace.newName;
    else if(!strcmp(name,"name_output")) replacement=renamePlan.updatedName;
    else if(!strcmp(name,"strings_overlap")) replacement=path+1U;
    else if(!strcmp(name,"volume_alignment")) owner=(UmicomKernelFat16 *)((UmicomU8 *)&volume+1U);
    else if(!strcmp(name,"workspace_alignment")) scratch=(UmicomKernelFat16RenameWorkspace *)((UmicomU8 *)&renameWorkspace+1U);
    else if(!strcmp(name,"output_alignment")) out=(UmicomKernelFat16RenamePlan *)((UmicomU8 *)&renamePlan+1U);
    else if(!strcmp(name,"output_overflow")) out=(UmicomKernelFat16RenamePlan *)(~(UmicomAddress)0U-7U);
    else if(!strcmp(name,"workspace_overflow")) scratch=(UmicomKernelFat16RenameWorkspace *)(~(UmicomAddress)0U-7U);
    else if(!strcmp(name,"null_volume")) owner=NULL;
    else if(!strcmp(name,"null_workspace")) scratch=NULL;
    else if(!strcmp(name,"null_base_workspace")) base=NULL;
    else if(!strcmp(name,"null_output")) out=NULL;
    else if(!strcmp(name,"null_path")) source=NULL;
    else if(!strcmp(name,"null_name")) replacement=NULL;
    else if(!strcmp(name,"busy_volume")) {volume.busy=UMICOM_TRUE;expected=UMICOM_DISK_BUSY;}
    else if(!strcmp(name,"busy_base_workspace")) {workspace.busy=UMICOM_TRUE;expected=UMICOM_DISK_BUSY;}
    else if(!strcmp(name,"busy_workspace")) {renameWorkspace.busy=UMICOM_TRUE;expected=UMICOM_DISK_BUSY;}
    else if(!strcmp(name,"dirty_workspace")) {renameWorkspace.newName[0]='x';expected=UMICOM_DISK_BAD_STATE;}
    else if(!strcmp(name,"copied_workspace")) {memset(&copied,0,sizeof(copied));copied.self=&copied;renameWorkspace=copied;expected=UMICOM_DISK_BAD_STATE;}
    else if(!strcmp(name,"copied_volume")) {volumeCopy=volume;owner=&volumeCopy;expected=UMICOM_DISK_BAD_STATE;}
    else CHECK(0);
    volumeBefore=volume;workspaceBefore=workspace;renameWorkspaceBefore=renameWorkspace;renameBefore=renamePlan;
    CHECK(UmicomKernelFat16PlanRename(owner,source,replacement,base,scratch,out)==expected);
    CHECK(!reads && !memcmp(&volume,&volumeBefore,sizeof(volume)) && !memcmp(&workspace,&workspaceBefore,sizeof(workspace)));
    CHECK(!memcmp(&renameWorkspace,&renameWorkspaceBefore,sizeof(renameWorkspace)) && !memcmp(&renamePlan,&renameBefore,sizeof(renamePlan)));
}
static void RenameBoundary(const char *name)
{
    static struct {char prefix[8];UmicomKernelFat16RenamePlan plan;} arena,before;
    RenameStart();memset(&arena,'A',sizeof(arena));before=arena;
    CHECK((const void *)&arena.plan==(const void *)(arena.prefix+8U));
    const char *source=path,*replacement=renameName;
    if(!strcmp(name,"path_output")) source=arena.prefix;
    else {CHECK(!strcmp(name,"name_output"));replacement=arena.prefix;}
    CHECK(UmicomKernelFat16PlanRename(&volume,source,replacement,&workspace,&renameWorkspace,&arena.plan)==UMICOM_DISK_INVALID_ARGUMENT);
    CHECK(!reads && !memcmp(&arena,&before,sizeof(arena)));Filled(&workspace,sizeof(workspace),0U);Filled(&renameWorkspace,sizeof(renameWorkspace),0U);
}
int main(int argc,char **argv)
{
    CHECK(argc==3);Setup(argv[2]);memcpy(renameName,"SAVED.BIN",10U);const char *const name=argv[1];
    if(!strncmp(name,"success.",8U)) RenameSuccess(name+8U);
    else if(!strncmp(name,"refusal.",8U)) RenameRefusal(name+8U);
    else if(!strncmp(name,"grammar.",8U)) RenameGrammar(name+8U);
    else if(!strncmp(name,"guard.",6U)) RenameGuard(name+6U);
    else if(!strncmp(name,"boundary.",9U)) RenameBoundary(name+9U);
    else if(!strcmp(name,"alphabet")) RenameAlphabet();
    else if(!strcmp(name,"all_read_failures")) RenameFailures();
    else CHECK(0);
    printf("fat16-rename.plan.%s: ok\n",name);return 0;
}
