/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/boot_handoff_write.c
 *
 * PURPOSE:
 *   Encode canonical boot metadata through the existing reader before publishing bytes.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/boot_handoff.h"
#include "umicom/kernel/address.h"
#include "boot_memory.h"

static UmicomBoolean Overlap(UmicomAddress a,UmicomAddress aEnd,
    UmicomAddress b,UmicomAddress bEnd)
{
    return a<bEnd&&b<aEnd?UMICOM_TRUE:UMICOM_FALSE;
}

static void Put16(UmicomU8 *out,UmicomU16 value)
{
    out[0]=(UmicomU8)value;
    out[1]=(UmicomU8)(value>>8U);
}

static void Put32(UmicomU8 *out,UmicomU32 value)
{
    for(unsigned i=0;i<4U;++i)out[i]=(UmicomU8)(value>>(8U*i));
}

static void Put64(UmicomU8 *out,UmicomU64 value)
{
    for(unsigned i=0;i<8U;++i)out[i]=(UmicomU8)(value>>(8U*i));
}

static void PutSpan(UmicomU8 *out,const UmicomBootSpan *span)
{
    Put64(out,span->address);
    Put64(out+8U,span->bytes);
}

UmicomBootHandoffStatus UmicomKernelBootHandoffWrite(
    const UmicomKernelBootHandoff *record,void *destination,
    UmicomSize capacity,UmicomSize *written)
{
    if(!record||!destination||!written)return UMICOM_BOOT_HANDOFF_ARGUMENT;
    if(capacity>UMICOM_BOOT_HANDOFF_MAX_BYTES)return UMICOM_BOOT_HANDOFF_LIMIT;
    if(!capacity)return UMICOM_BOOT_HANDOFF_LENGTH;
    const UmicomAddress input=(UmicomAddress)record;
    const UmicomAddress output=(UmicomAddress)destination;
    const UmicomAddress count=(UmicomAddress)written;
    UmicomAddress inputEnd=0U,outputEnd=0U,countEnd=0U;
    if(input%_Alignof(UmicomKernelBootHandoff)||count%_Alignof(UmicomSize)||
        !UmicomKernelAddressAddChecked(input,sizeof *record,&inputEnd)||
        !UmicomKernelAddressAddChecked(output,capacity,&outputEnd)||
        !UmicomKernelAddressAddChecked(count,sizeof *written,&countEnd)||
        Overlap(input,inputEnd,output,outputEnd)||
        Overlap(input,inputEnd,count,countEnd)||
        Overlap(output,outputEnd,count,countEnd))
        return UMICOM_BOOT_HANDOFF_ARGUMENT;

    /* Do not narrow caller-controlled counts or enum values before admission:
     * a large pixel tag could otherwise truncate to a supported 16-bit value. */
    if(!record->ramCount||record->ramCount>UMICOM_BOOT_HANDOFF_RAM_LIMIT||
        record->reservedCount>UMICOM_BOOT_HANDOFF_RESERVED_LIMIT)
        return UMICOM_BOOT_HANDOFF_LIMIT;
    if((record->architecture!=UMICOM_BOOT_RISCV64&&
        record->architecture!=UMICOM_BOOT_X86_64&&record->architecture!=UMICOM_BOOT_AARCH64)||
        (record->tableKind!=UMICOM_BOOT_TABLE_NONE&&record->tableKind!=UMICOM_BOOT_TABLE_FDT&&
         record->tableKind!=UMICOM_BOOT_TABLE_ACPI_RSDP)||
        (record->framebuffer.format!=UMICOM_BOOT_PIXELS_NONE&&
         record->framebuffer.format!=UMICOM_BOOT_PIXELS_RGBX32&&
         record->framebuffer.format!=UMICOM_BOOT_PIXELS_BGRX32))
        return UMICOM_BOOT_HANDOFF_UNSUPPORTED;
    UmicomSize commandBytes=0U;
    while(commandBytes<UMICOM_BOOT_HANDOFF_COMMAND_BYTES&&record->commandLine[commandBytes])
        ++commandBytes;
    if(commandBytes==UMICOM_BOOT_HANDOFF_COMMAND_BYTES)return UMICOM_BOOT_HANDOFF_TEXT;
    const UmicomSize length=64U+16U*(record->ramCount+record->reservedCount)+48U+commandBytes;
    if(capacity<length)return UMICOM_BOOT_HANDOFF_LENGTH;

    /* Stage byte fields instead of copying a C struct's padding or native byte
     * order. Existing volatile boot-memory helpers prevent hidden libc calls.
     * The maximum record is bounded; this does not allocate or reserve RAM. */
    UmicomU8 encoded[UMICOM_BOOT_HANDOFF_MAX_BYTES];
    UmicomBootMemoryClear(encoded,length);
    static const UmicomU8 magic[8]={'U','M','I','B','O','O','T',0};
    UmicomBootMemoryCopy(encoded,magic,sizeof magic);
    Put16(encoded+8U,1U);
    Put16(encoded+10U,UMICOM_BOOT_HANDOFF_HEADER_BYTES);
    Put32(encoded+12U,(UmicomU32)length);
    Put32(encoded+16U,(UmicomU32)record->architecture);
    Put64(encoded+24U,record->bootstrapCpu);
    PutSpan(encoded+32U,&record->kernel);
    Put16(encoded+48U,(UmicomU16)record->ramCount);
    Put16(encoded+50U,(UmicomU16)record->reservedCount);
    Put32(encoded+52U,(UmicomU32)record->tableKind);
    Put16(encoded+56U,(UmicomU16)commandBytes);
    Put16(encoded+58U,(UmicomU16)record->framebuffer.format);
    UmicomSize offset=64U;
    for(UmicomSize i=0U;i<record->ramCount;++i,offset+=16U)
        PutSpan(encoded+offset,&record->ram[i]);
    for(UmicomSize i=0U;i<record->reservedCount;++i,offset+=16U)
        PutSpan(encoded+offset,&record->reserved[i]);
    PutSpan(encoded+offset,&record->table);
    offset+=16U;
    PutSpan(encoded+offset,&record->framebuffer.storage);
    Put32(encoded+offset+16U,record->framebuffer.width);
    Put32(encoded+offset+20U,record->framebuffer.height);
    Put32(encoded+offset+24U,record->framebuffer.strideBytes);
    offset+=32U;
    for(UmicomSize i=0U;i<commandBytes;++i)encoded[offset+i]=(UmicomU8)record->commandLine[i];

    /* The reader remains the single authority for semantic admission. It checks
     * every span and optional descriptor before either caller output is touched. */
    UmicomKernelBootHandoff checked;
    UmicomBootHandoffStatus status=UmicomKernelBootHandoffRead(encoded,length,&checked);
    if(status!=UMICOM_BOOT_HANDOFF_OK)return status;
    UmicomBootMemoryCopy(destination,encoded,length);
    *written=length;
    return UMICOM_BOOT_HANDOFF_OK;
}
