/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/boot_handoff/write_tests.c
 *
 * PURPOSE:
 *   Check independent boot wire bytes, semantic refusals and producer output ownership.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_handoff.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); return 1; } } while(0)

/* Literal version-1 bytes are independent of the writer. A reader/writer pair
 * agreeing on the same wrong offset must not satisfy this golden-byte case. */
static const unsigned char golden[144]={
    [0]='U',[1]='M',[2]='I',[3]='B',[4]='O',[5]='O',[6]='T',
    [8]=1,[10]=64,[12]=144,[16]=1,[24]=7,
    [33]=0x10,[35]=0x80,[41]=0x10,[48]=1,[50]=1,
    [67]=0x80,[74]=0x10,[83]=0x80,[89]=0x20
};

static void Record(UmicomKernelBootHandoff *record)
{
    memset(record,0,sizeof *record);
    record->architecture=UMICOM_BOOT_RISCV64;
    record->bootstrapCpu=7;
    record->kernel.address=0x80001000ULL;
    record->kernel.bytes=4096;
    record->ramCount=1;
    record->ram[0].address=0x80000000ULL;
    record->ram[0].bytes=0x100000;
    record->reservedCount=1;
    record->reserved[0].address=0x80000000ULL;
    record->reserved[0].bytes=8192;
}

static int Refused(const UmicomKernelBootHandoff *record,UmicomSize capacity,
    UmicomBootHandoffStatus expected)
{
    unsigned char output[UMICOM_BOOT_HANDOFF_MAX_BYTES],before[sizeof output];
    memset(output,0xa5,sizeof output);
    memcpy(before,output,sizeof before);
    UmicomSize written=123;
    CHECK(UmicomKernelBootHandoffWrite(record,output,capacity,&written)==expected);
    CHECK(written==123&&!memcmp(output,before,sizeof output));
    return 0;
}

static int Case(const char *name)
{
    UmicomKernelBootHandoff record;
    Record(&record);
    unsigned char output[UMICOM_BOOT_HANDOFF_MAX_BYTES+1U];
    memset(output,0xa5,sizeof output);
    UmicomSize written=0;
    if(!strcmp(name,"golden")) {
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(written==sizeof golden&&!memcmp(output,golden,sizeof golden));
        for(UmicomSize i=written;i<sizeof output;++i)CHECK(output[i]==0xa5);
    } else if(!strcmp(name,"tail")) {
        strcpy(record.commandLine,"quiet");
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(written==149&&!memcmp(output+144,"quiet",5));
        CHECK(output[56]==5&&output[57]==0);
        for(UmicomSize i=written;i<sizeof output;++i)CHECK(output[i]==0xa5);
    } else if(!strcmp(name,"architectures")) {
        for(unsigned i=1;i<=3;++i) {
            record.architecture=(UmicomBootArchitecture)i;
            CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
            CHECK(output[16]==i&&output[17]==0&&output[18]==0&&output[19]==0);
        }
    } else if(!strcmp(name,"roundtrip")) {
        record.tableKind=UMICOM_BOOT_TABLE_FDT;
        record.table.address=0x80000100ULL;record.table.bytes=40;
        record.framebuffer.format=UMICOM_BOOT_PIXELS_BGRX32;
        record.framebuffer.storage.address=0x10000000ULL;
        record.framebuffer.storage.bytes=1024;
        record.framebuffer.width=16;record.framebuffer.height=4;record.framebuffer.strideBytes=64;
        strcpy(record.commandLine,"console=serial");
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
        UmicomKernelBootHandoff decoded;
        CHECK(UmicomKernelBootHandoffRead(output,written,&decoded)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(decoded.tableKind==UMICOM_BOOT_TABLE_FDT&&decoded.table.bytes==40);
        CHECK(decoded.framebuffer.storage.address==0x10000000ULL);
        CHECK(decoded.framebuffer.width==16&&decoded.framebuffer.height==4&&decoded.framebuffer.strideBytes==64);
        CHECK(decoded.framebuffer.format==UMICOM_BOOT_PIXELS_BGRX32);
        CHECK(!strcmp(decoded.commandLine,"console=serial"));
    } else if(!strcmp(name,"maximum")) {
        record.ramCount=32;record.reservedCount=64;
        for(UmicomSize i=1;i<32;++i) {
            record.ram[i].address=0x80000000ULL+i*0x200000ULL;
            record.ram[i].bytes=0x100000;
        }
        for(UmicomSize i=1;i<64;++i) {
            record.reserved[i].address=0x80008000ULL+i*4096;
            record.reserved[i].bytes=4096;
        }
        memset(record.commandLine,'x',255);record.commandLine[255]=0;
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(written==1903);
        UmicomKernelBootHandoff decoded;
        CHECK(UmicomKernelBootHandoffRead(output,written,&decoded)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(decoded.ramCount==32&&decoded.reservedCount==64&&strlen(decoded.commandLine)==255);
    } else if(!strcmp(name,"unused")) {
        memset(record.ram+1,0xa5,sizeof record.ram-sizeof record.ram[0]);
        memset(record.reserved+1,0xa5,sizeof record.reserved-sizeof record.reserved[0]);
        memset(record.commandLine+1,'x',sizeof record.commandLine-1);
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(written==sizeof golden&&!memcmp(output,golden,sizeof golden));
    } else if(!strcmp(name,"unaligned")) {
        CHECK(UmicomKernelBootHandoffWrite(&record,output+1,4096,&written)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(output[0]==0xa5&&written==sizeof golden&&!memcmp(output+1,golden,sizeof golden));
    } else if(!strcmp(name,"capacity")) {
        for(UmicomSize n=1;n<sizeof golden;++n)CHECK(!Refused(&record,n,UMICOM_BOOT_HANDOFF_LENGTH));
        CHECK(UmicomKernelBootHandoffWrite(&record,output,sizeof golden,&written)==UMICOM_BOOT_HANDOFF_OK);
        CHECK(written==sizeof golden);
    } else if(!strcmp(name,"capacity_limit"))CHECK(!Refused(&record,4097,UMICOM_BOOT_HANDOFF_LIMIT));
    else if(!strcmp(name,"zero_capacity"))CHECK(!Refused(&record,0,UMICOM_BOOT_HANDOFF_LENGTH));
    else if(!strcmp(name,"ram_count")) {
        record.ramCount=0;CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_LIMIT));
        record.ramCount=65537;CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_LIMIT));
    } else if(!strcmp(name,"reserved_count")) {
        record.reservedCount=65537;CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_LIMIT));
    } else if(!strcmp(name,"text_termination")) {
        memset(record.commandLine,'x',sizeof record.commandLine);
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_TEXT));
    } else if(!strcmp(name,"text_control")) {
        record.commandLine[0]='\n';
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_TEXT));
        record.commandLine[0]=(char)0x80;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_TEXT));
    } else if(!strcmp(name,"pixel_tag")) {
        record.framebuffer.format=(UmicomBootPixelFormat)65537;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_UNSUPPORTED));
    } else if(!strcmp(name,"table_tag")) {
        record.tableKind=(UmicomBootFirmwareTable)4;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_UNSUPPORTED));
    } else if(!strcmp(name,"architecture_tag")) {
        record.architecture=(UmicomBootArchitecture)0;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_UNSUPPORTED));
    } else if(!strcmp(name,"kernel_range")) {
        record.kernel.address=~(UmicomU64)0;record.kernel.bytes=1;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_RANGE));
    } else if(!strcmp(name,"ram_overlap")) {
        record.ramCount=2;record.ram[1].address=0x80001000ULL;record.ram[1].bytes=4096;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_MEMORY));
    } else if(!strcmp(name,"reservation")) {
        record.reserved[0].bytes=4096;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_MEMORY));
    } else if(!strcmp(name,"firmware")) {
        record.tableKind=UMICOM_BOOT_TABLE_FDT;
        record.table.address=0x80008000ULL;record.table.bytes=40;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_RANGE));
    } else if(!strcmp(name,"framebuffer")) {
        record.framebuffer.format=UMICOM_BOOT_PIXELS_RGBX32;
        record.framebuffer.storage.address=0x10000000ULL;record.framebuffer.storage.bytes=256;
        record.framebuffer.width=16;record.framebuffer.height=4;record.framebuffer.strideBytes=63;
        CHECK(!Refused(&record,4096,UMICOM_BOOT_HANDOFF_FRAMEBUFFER));
    } else if(!strcmp(name,"overlap_record")||!strcmp(name,"counter_in_record")) {
        unsigned char before[sizeof record];memcpy(before,&record,sizeof record);
        if(!strcmp(name,"overlap_record"))
            CHECK(UmicomKernelBootHandoffWrite(&record,&record,sizeof record,&written)==UMICOM_BOOT_HANDOFF_ARGUMENT);
        else CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,&record.ramCount)==UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(!memcmp(before,&record,sizeof record));
        CHECK(written==0);
        for(size_t i=0;i<sizeof output;++i)CHECK(output[i]==0xa5);
    } else if(!strcmp(name,"overlap_counter")) {
        union {UmicomSize count;unsigned char bytes[4096];} shared;
        memset(&shared,0xa5,sizeof shared);
        unsigned char before[sizeof shared];memcpy(before,&shared,sizeof shared);
        CHECK(UmicomKernelBootHandoffWrite(&record,shared.bytes,sizeof shared.bytes,&shared.count)==UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(!memcmp(before,&shared,sizeof shared));
    } else if(!strcmp(name,"input_alignment")) {
        _Alignas(UmicomKernelBootHandoff) unsigned char storage[sizeof record+1U];
        memset(storage,0xa5,sizeof storage);
        CHECK(UmicomKernelBootHandoffWrite((const UmicomKernelBootHandoff *)(storage+1),output,4096,&written)==UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(written==0);
        for(size_t i=0;i<sizeof output;++i)CHECK(output[i]==0xa5);
    } else if(!strcmp(name,"counter_alignment")) {
        _Alignas(UmicomSize) unsigned char storage[sizeof(UmicomSize)+1U];
        memset(storage,0xa5,sizeof storage);
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,(UmicomSize *)(storage+1))==UMICOM_BOOT_HANDOFF_ARGUMENT);
        for(size_t i=0;i<sizeof storage;++i)CHECK(storage[i]==0xa5);
        for(size_t i=0;i<sizeof output;++i)CHECK(output[i]==0xa5);
    } else if(!strcmp(name,"arguments")) {
        CHECK(!Refused(NULL,4096,UMICOM_BOOT_HANDOFF_ARGUMENT));
        CHECK(UmicomKernelBootHandoffWrite(&record,NULL,4096,&written)==UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(UmicomKernelBootHandoffWrite(&record,output,4096,NULL)==UMICOM_BOOT_HANDOFF_ARGUMENT);
        CHECK(written==0);
    } else return 2;
    return 0;
}

int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    return Case(argv[1]);
}
