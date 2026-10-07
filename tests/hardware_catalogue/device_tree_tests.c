/*-----------------------------------------------------------------------------
 * Umicom Kernel device-tree and hardware-catalogue regression tests
 * File: tests/hardware_catalogue/device_tree_tests.c
 *
 * Fixtures are constructed as byte protocols so every malformed offset and
 * truncated span is deliberate. No dtc executable, third-party parser, device
 * register or RISC-V emulator is needed for these host tests. Their boot-model
 * coverage is not a claim that the real firmware or guest has executed here.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/hardware_catalogue.h"
#include "umicom/kernel/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define OK(x) CHECK((x) == UMICOM_TREE_OK)
#define BAD(x) CHECK((x) != UMICOM_TREE_OK)

typedef struct Fixture {
    UmicomU8 structure[100000], strings[20000], blob[UMICOM_TREE_MAX_BYTES + 128U];
    UmicomKernelTreeReservation reserves[UMICOM_TREE_RESERVATION_LIMIT + 2U];
    UmicomSize structureBytes, stringsBytes, bytes, reserveCount;
} Fixture;
static Fixture fixture;
static UmicomKernelDeviceTreeReader reader;
static UmicomKernelHardwareCatalogue catalogue;
static UmicomAddress hostRamBase;
static UmicomSize hostRamBytes;
static char output[65536];
static UmicomSize outputBytes;

/* The actual boot-span inspector uses this model before dereferencing a DTB. */
void UmicomPlatformPhysicalMemoryDescribe(UmicomPlatformPhysicalMemoryInfo *out)
{
    out->base = hostRamBase; out->bytes = hostRamBytes;
}
static void Write(void *context, const char *text, UmicomSize bytes)
{
    (void)context;
    CHECK(bytes < sizeof(output) - outputBytes);
    memcpy(output + outputBytes, text, (size_t)bytes); outputBytes += bytes; output[outputBytes] = 0;
}
static void Put32(UmicomU8 *p, UmicomU32 value)
{
    p[0] = (UmicomU8)(value >> 24U); p[1] = (UmicomU8)(value >> 16U);
    p[2] = (UmicomU8)(value >> 8U); p[3] = (UmicomU8)value;
}
static UmicomU32 Get32(const UmicomU8 *p)
{
    return ((UmicomU32)p[0] << 24U) | ((UmicomU32)p[1] << 16U) | ((UmicomU32)p[2] << 8U) | p[3];
}
static void Put64(UmicomU8 *p, UmicomU64 value)
{
    Put32(p, (UmicomU32)(value >> 32U)); Put32(p + 4U, (UmicomU32)value);
}
static void Reset(void)
{
    memset(&fixture, 0, sizeof(fixture)); memset(&reader, 0, sizeof(reader));
    memset(&catalogue, 0, sizeof(catalogue)); memset(output, 0, sizeof(output)); outputBytes = 0U;
}
static void Token(UmicomU32 token)
{
    CHECK(fixture.structureBytes + 4U <= sizeof(fixture.structure));
    Put32(fixture.structure + fixture.structureBytes, token); fixture.structureBytes += 4U;
}
static void Begin(const char *name)
{
    Token(1U); const UmicomSize n = strlen(name) + 1U;
    CHECK(n < sizeof(fixture.structure) - fixture.structureBytes);
    memcpy(fixture.structure + fixture.structureBytes, name, (size_t)n); fixture.structureBytes += n;
    while (fixture.structureBytes & 3U) fixture.structure[fixture.structureBytes++] = 0U;
}
static void End(void) { Token(2U); }
static void Prop(const char *name, const void *value, UmicomSize bytes)
{
    const UmicomSize n = strlen(name) + 1U;
    CHECK(n < sizeof(fixture.strings) - fixture.stringsBytes);
    CHECK(bytes + 16U < sizeof(fixture.structure) - fixture.structureBytes);
    Token(3U); Token((UmicomU32)bytes); Token((UmicomU32)fixture.stringsBytes);
    memcpy(fixture.strings + fixture.stringsBytes, name, (size_t)n); fixture.stringsBytes += n;
    if (bytes) memcpy(fixture.structure + fixture.structureBytes, value, (size_t)bytes);
    fixture.structureBytes += bytes;
    while (fixture.structureBytes & 3U) fixture.structure[fixture.structureBytes++] = 0U;
}
static void Text(const char *name, const char *value) { Prop(name, value, strlen(value) + 1U); }
static void Cell(const char *name, UmicomU32 value)
{
    UmicomU8 bytes[4]; Put32(bytes, value); Prop(name, bytes, sizeof(bytes));
}
static void Reg(UmicomU64 address, UmicomU64 bytes)
{
    UmicomU8 value[16]; Put64(value, address); Put64(value + 8U, bytes); Prop("reg", value, sizeof(value));
}
static void Root(void)
{
    Begin(""); Cell("#address-cells", 2U); Cell("#size-cells", 2U);
}
static void Device(const char *name, const char *compatible, UmicomU64 address, UmicomU64 bytes)
{
    Begin(name); if (compatible) Text("compatible", compatible); Reg(address, bytes); End();
}
static void Finish(void)
{
    Token(9U);
    const UmicomSize start = 40U + (fixture.reserveCount + 1U) * 16U;
    const UmicomSize strings = start + fixture.structureBytes;
    fixture.bytes = strings + fixture.stringsBytes;
    CHECK(fixture.bytes <= sizeof(fixture.blob));
    Put32(fixture.blob, 0xd00dfeedU); Put32(fixture.blob + 4U, (UmicomU32)fixture.bytes);
    Put32(fixture.blob + 8U, (UmicomU32)start); Put32(fixture.blob + 12U, (UmicomU32)strings);
    Put32(fixture.blob + 16U, 40U); Put32(fixture.blob + 20U, 17U); Put32(fixture.blob + 24U, 16U);
    Put32(fixture.blob + 32U, (UmicomU32)fixture.stringsBytes);
    Put32(fixture.blob + 36U, (UmicomU32)fixture.structureBytes);
    for (UmicomSize i = 0U; i < fixture.reserveCount; ++i) {
        Put64(fixture.blob + 40U + 16U*i, fixture.reserves[i].address);
        Put64(fixture.blob + 48U + 16U*i, fixture.reserves[i].bytes);
    }
    memcpy(fixture.blob + start, fixture.structure, (size_t)fixture.structureBytes);
    memcpy(fixture.blob + strings, fixture.strings, (size_t)fixture.stringsBytes);
}
static void Golden(void)
{
    Reset(); Root(); Text("model", "Umicom synthetic virt fixture"); Text("compatible", "riscv-virtio");
    Begin("chosen"); Text("stdout-path", "/soc/serial@10000000:115200n8"); End();
    Begin("cpus"); Cell("#address-cells", 1U); Cell("#size-cells", 0U); Cell("timebase-frequency", 10000000U);
    Begin("cpu@0"); Text("device_type", "cpu"); Cell("reg", 0U); Text("status", "okay");
    Begin("interrupt-controller"); Text("compatible", "riscv,cpu-intc"); Cell("phandle", 1U);
    Cell("#interrupt-cells", 1U); Prop("interrupt-controller", 0, 0U); End(); End(); End();
    Begin("memory@80000000"); Text("device_type", "memory"); Reg(0x80000000ULL, 128U*1024U*1024U); End();
    Begin("soc"); Cell("#address-cells", 2U); Cell("#size-cells", 2U);
    Text("compatible", "simple-bus"); Prop("ranges", 0, 0U);
    Begin("interrupt-controller@c000000"); Text("compatible", "sifive,plic-1.0.0");
    Cell("phandle", 2U); Cell("#interrupt-cells", 1U); Prop("interrupt-controller", 0, 0U);
    Reg(0xc000000ULL, 0x400000U); End();
    Device("clint@2000000", "riscv,clint0", 0x2000000ULL, 0x10000U);
    Begin("serial@10000000"); Text("compatible", "ns16550a"); Reg(0x10000000ULL, 0x100U);
    Cell("interrupt-parent", 2U); Cell("interrupts", 10U); End();
    for (unsigned i = 0U; i < 8U; ++i) {
        char name[64]; snprintf(name, sizeof(name), "virtio_mmio@%x", 0x10001000U + i*0x1000U);
        Device(name, "virtio,mmio", (UmicomU64)0x10001000U + (UmicomU64)i*0x1000U, 0x1000U);
    }
    End();
    Begin("reserved-memory"); Cell("#address-cells", 2U); Cell("#size-cells", 2U); Prop("ranges",0,0U);
    Device("reserved@81000000", 0, 0x81000000ULL, 0x1000U); End();
    End(); fixture.reserveCount = 1U;
    fixture.reserves[0] = (UmicomKernelTreeReservation){0x82000000ULL, 0x1000U}; Finish();
}
static void Parse(void) { OK(UmicomKernelDeviceTreeOpen(fixture.blob, fixture.bytes, &reader)); }
static void Build(void) { Parse(); OK(UmicomKernelHardwareCatalogueBuild(&reader, &catalogue)); }
static UmicomKernelHardwareDevice *FindKind(UmicomKernelHardwareKind kind)
{
    for (UmicomU32 i = 0U; i < catalogue.devices; ++i) if (catalogue.entries[i].kind == kind) return &catalogue.entries[i];
    CHECK(0); return 0;
}
static UmicomU8 *Value(const char *path, const char *name)
{
    Parse(); UmicomU32 index = 0U; UmicomKernelTreeSpan span = {0};
    OK(UmicomKernelDeviceTreeFind(&reader, path, &index)); OK(UmicomKernelDeviceTreeProperty(&reader,index,name,&span));
    return fixture.blob + (span.data - fixture.blob);
}
static void RejectParse(void)
{
    BAD(UmicomKernelDeviceTreeOpen(fixture.blob, fixture.bytes, &reader));
    CHECK(!reader.opened && !reader.self && !reader.blob && !reader.nodeCount);
}
static void RejectBuild(void)
{
    Parse(); BAD(UmicomKernelHardwareCatalogueBuild(&reader, &catalogue));
    CHECK(!catalogue.ready && !catalogue.devices);
}
static void TestGolden(void)
{
    Golden(); Build(); CHECK(catalogue.ready && catalogue.timebaseFrequency == 10000000U);
    CHECK(catalogue.firmwareReservations == 1U && catalogue.reservations[0].address == 0x82000000ULL);
    CHECK(FindKind(UMICOM_HARDWARE_MEMORY)->registers[0].physicalAddress == 0x80000000ULL);
    CHECK(FindKind(UMICOM_HARDWARE_CPU)->registers[0].busAddress == 0U);
    CHECK(FindKind(UMICOM_HARDWARE_UART)->interruptParent == 2U);
    CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress == 0x10000000ULL);
    unsigned slots = 0U; for (unsigned i=0U;i<catalogue.devices;++i) if (catalogue.entries[i].kind==UMICOM_HARDWARE_VIRTIO_MMIO) ++slots;
    CHECK(slots == 8U);
}
static void TestRootOnly(void) { Reset(); Begin(""); End(); Finish(); Build(); CHECK(catalogue.devices == 0U); }
static void TestArguments(void)
{
    Golden(); BAD(UmicomKernelDeviceTreeOpen(0,fixture.bytes,&reader));
    BAD(UmicomKernelDeviceTreeOpen(fixture.blob,fixture.bytes,0));
    BAD(UmicomKernelDeviceTreeOpen(&reader,sizeof(reader),&reader));
    BAD(UmicomKernelHardwareCatalogueBuild(0,&catalogue));
    BAD(UmicomKernelDeviceTreeOpen((void *)(UmicomUIntPtr)(~(UmicomUIntPtr)0U-3U),10U,&reader));
}
static void TestTruncations(void)
{
    Golden();
    for (UmicomSize n=0U; n<fixture.bytes; ++n) {
        UmicomU8 *copy=malloc((size_t)(n?n:1U)); CHECK(copy); if(n)memcpy(copy,fixture.blob,(size_t)n);
        BAD(UmicomKernelDeviceTreeOpen(copy,n,&reader)); CHECK(!reader.opened); free(copy);
    }
}
static void TestUnaligned(void)
{
    Golden(); UmicomU8 *p=malloc((size_t)fixture.bytes+1U);CHECK(p);memcpy(p+1U,fixture.blob,(size_t)fixture.bytes);
    OK(UmicomKernelDeviceTreeOpen(p+1U,fixture.bytes,&reader)); OK(UmicomKernelHardwareCatalogueBuild(&reader,&catalogue));free(p);
    CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress==0x10000000ULL);
}
static void TestMagic(void) { Golden(); fixture.blob[0]^=1U;RejectParse(); }
static void TestTotal(void) { Golden();Put32(fixture.blob+4U,39U);RejectParse(); }
static void TestOversize(void)
{ Golden();Put32(fixture.blob+4U,UMICOM_TREE_MAX_BYTES+1U);fixture.bytes=UMICOM_TREE_MAX_BYTES+1U;RejectParse(); }
static void TestOldFormat(void) { Golden();Put32(fixture.blob+20U,16U);RejectParse(); }
static void TestFuture(void) { Golden();Put32(fixture.blob+20U,20U);Put32(fixture.blob+24U,17U);Build(); }
static void TestIncompatible(void) { Golden();Put32(fixture.blob+20U,20U);Put32(fixture.blob+24U,18U);RejectParse(); }
static void TestBlockOverlap(void) { Golden();Put32(fixture.blob+12U,Get32(fixture.blob+8U));RejectParse(); }
static void TestStructureOffset(void) { Golden();Put32(fixture.blob+8U,41U);RejectParse(); }
static void TestReservationOffset(void) { Golden();Put32(fixture.blob+16U,41U);RejectParse(); }
static void TestBlockExtent(void) { Golden();Put32(fixture.blob+36U,0xfffffffcU);RejectParse(); }
static void TestMissingReserveEnd(void)
{ Golden();Put64(fixture.blob+56U,0x84000000ULL);Put64(fixture.blob+64U,4096U);RejectParse(); }
static void TestReserveWrap(void) { Golden();Put64(fixture.blob+40U,~(UmicomU64)0U-3U);RejectParse(); }
static void TestReserveZero(void) { Golden();Put64(fixture.blob+48U,0U);RejectParse(); }
static void TestReserveOverlap(void)
{ Reset();Root();End();fixture.reserveCount=2U;fixture.reserves[0]=(UmicomKernelTreeReservation){4096U,4096U};fixture.reserves[1]=(UmicomKernelTreeReservation){4100U,64U};Finish();RejectParse(); }
static void TestReserveCapacity(void)
{
    for (unsigned n=32U;n<=33U;++n) { Reset();Root();End();fixture.reserveCount=n;
        for(unsigned i=0U;i<n;++i)fixture.reserves[i]=(UmicomKernelTreeReservation){((UmicomU64)i+1U)*4096U,4096U};
        Finish();if(n==32U)Parse();else RejectParse(); }
}
static void TestBadToken(void) { Golden();Put32(fixture.blob+Get32(fixture.blob+8U),7U);RejectParse(); }
static void TestUnderflow(void) { Reset();Token(2U);Finish();RejectParse(); }
static void TestMissingRoot(void) { Reset();Token(4U);Token(4U);Finish();RejectParse(); }
static void TestSecondRoot(void) { Reset();Begin("");End();Begin("");End();Finish();RejectParse(); }
static void TestUnclosedRoot(void) { Reset();Root();Finish();RejectParse(); }
static void TestMissingEnd(void) { Golden();Put32(fixture.blob+Get32(fixture.blob+8U)+Get32(fixture.blob+36U)-4U,4U);RejectParse(); }
static void TestAfterChild(void) { Reset();Root();Begin("child");End();Cell("late",1U);End();Finish();RejectParse(); }
static void TestBadNodeName(void) { Reset();Root();Begin("wrong/name");End();End();Finish();RejectParse(); }
static void TestNodePadding(void)
{ Reset();Root();Begin("a");fixture.structure[fixture.structureBytes-1U]=1U;End();End();Finish();RejectParse(); }
static void TestPropertyPadding(void)
{ Reset();Root();const UmicomU8 b=1U;Prop("x",&b,1U);fixture.structure[fixture.structureBytes-1U]=1U;End();Finish();RejectParse(); }
static void TestNameOffset(void)
{ Golden();Parse();const UmicomKernelTreeProperty p=reader.properties[0];Put32(fixture.blob+p.valueOffset-4U,0xffffffffU);RejectParse(); }
static void TestValueExtent(void)
{ Golden();Parse();const UmicomKernelTreeProperty p=reader.properties[0];Put32(fixture.blob+p.valueOffset-8U,0xffffffffU);RejectParse(); }
static void TestDuplicateProperty(void) { Reset();Root();Cell("#address-cells",2U);End();Finish();RejectParse(); }
static void TestDuplicateChild(void) { Reset();Root();Begin("child");End();Begin("child");End();End();Finish();RejectParse(); }
static void TestDepth(void)
{ for(unsigned n=32U;n<=33U;++n){Reset();Root();for(unsigned i=1U;i<n;++i)Begin("child");for(unsigned i=0U;i<n;++i)End();Finish();if(n==32U)Parse();else RejectParse();} }
static void TestNodeCapacity(void)
{ for(unsigned n=128U;n<=129U;++n){Reset();Root();for(unsigned i=1U;i<n;++i){char s[32];snprintf(s,sizeof(s),"node%u",i);Begin(s);End();}End();Finish();if(n==128U)Parse();else RejectParse();} }
static void TestPropertyCapacity(void)
{ for(unsigned n=512U;n<=513U;++n){Reset();Begin("");for(unsigned i=0U;i<n;++i){char s[32];snprintf(s,sizeof(s),"prop%u",i);Cell(s,i);}End();Finish();if(n==512U)Parse();else RejectParse();} }
static void TestPaths(void)
{
    Golden();Parse();UmicomU32 node=99U;OK(UmicomKernelDeviceTreeFind(&reader,"/soc/serial@10000000",&node));
    char path[256];memset(path,0xa5,sizeof(path));OK(UmicomKernelDeviceTreePath(&reader,node,path,sizeof(path)));
    CHECK(!strcmp(path,"/soc/serial@10000000"));path[0]='Q';BAD(UmicomKernelDeviceTreePath(&reader,node,path,4U));CHECK(path[0]=='Q');
    CHECK(UmicomKernelDeviceTreeFind(&reader,"/soc/serial",&node)==UMICOM_TREE_NOT_FOUND);
    BAD(UmicomKernelDeviceTreeFind(&reader,"soc/serial@10000000",&node));
    Reset();Root();char longName[96];memset(longName,'a',95);longName[95]=0;
    for (unsigned i = 0U; i < 3U; ++i) { Begin(longName); }
    for (unsigned i = 0U; i < 4U; ++i) { End(); }
    Finish(); Parse();
    BAD(UmicomKernelDeviceTreePath(&reader,3U,path,sizeof(path)));
}
static void TestPropertySuffix(void)
{ Reset();Begin("");Cell("prefix,field",3U);End();Finish();Parse();Put32(fixture.blob+reader.properties[0].valueOffset-4U,7U);Parse();UmicomKernelTreeSpan span={0};OK(UmicomKernelDeviceTreeProperty(&reader,0U,"field",&span)); }
static void TestCopies(void)
{
    Golden();UmicomU8 *copy=malloc((size_t)fixture.bytes);CHECK(copy);memcpy(copy,fixture.blob,(size_t)fixture.bytes);Build();
    CHECK(!memcmp(copy,fixture.blob,(size_t)fixture.bytes));free(copy);memset(fixture.blob,0,sizeof(fixture.blob));
    CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress==0x10000000ULL);
    CHECK(!strcmp(catalogue.model,"Umicom synthetic virt fixture"));
}
static void TestStrings(void)
{
    const UmicomU8 good[]={'a',0,'n','s','1','6','5','5','0','a',0};UmicomBoolean found=UMICOM_FALSE;
    OK(UmicomKernelTreeStringListContains((UmicomKernelTreeSpan){good,sizeof(good)},"ns16550a",&found));CHECK(found);
    const UmicomU8 bad[]={'a',0,'x'};found=UMICOM_FALSE;
    BAD(UmicomKernelTreeStringListContains((UmicomKernelTreeSpan){bad,sizeof(bad)},"a",&found));CHECK(!found);
    char out[16]="unchanged";BAD(UmicomKernelTreeString((UmicomKernelTreeSpan){good,sizeof(good)},out,sizeof(out)));CHECK(!strcmp(out,"unchanged"));
    UmicomU32 value=99U;BAD(UmicomKernelTreeU32((UmicomKernelTreeSpan){good,3U},&value));CHECK(value==99U);
}
static void TestCompatibleList(void)
{ Reset();Root();Begin("uart@1000");const char list[]="vendor,uart\0ns16550a";Prop("compatible",list,sizeof(list));Reg(4096U,256U);End();End();Finish();Build();CHECK(!strcmp(FindKind(UMICOM_HARDWARE_UART)->compatible,"vendor,uart")); }
static void TestMalformedCompatible(void) { Golden();UmicomU8 *p=Value("/soc/serial@10000000","compatible");p[8]='X';RejectBuild(); }
static void TestDisabledAncestor(void)
{ Reset();Root();Begin("bus");Cell("#address-cells",2U);Cell("#size-cells",2U);Text("status","disabled");Prop("ranges",0,0U);Device("uart@1000","ns16550a",4096U,256U);End();End();Finish();Build();CHECK(!FindKind(UMICOM_HARDWARE_UART)->enabled); }
static void BusFixture(unsigned mode)
{
    Reset();Root();Begin("bus");Cell("#address-cells",1U);Cell("#size-cells",1U);
    UmicomU8 mapping[32];memset(mapping,0,sizeof(mapping));Put32(mapping,0x1000U);Put64(mapping+4U,0x90000000ULL);Put32(mapping+12U,0x1000U);
    if(mode==0U)Prop("ranges",0,0U);
    else if(mode>=2U){if(mode==3U){memcpy(mapping+16U,mapping,16U);Put32(mapping+16U,0x1800U);}Prop("ranges",mapping,mode==3U?32U:16U);}
    Begin("uart@1100");Text("compatible","ns16550a");UmicomU8 reg[8];Put32(reg,0x1100U);Put32(reg+4U,0x100U);Prop("reg",reg,8U);End();End();End();Finish();
}
static void TestEmptyRanges(void) { BusFixture(0U);Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress==0x1100U); }
static void TestAbsentRanges(void) { BusFixture(1U);Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].translation==UMICOM_TREE_UNTRANSLATED); }
static void TestTranslatedRanges(void) { BusFixture(2U);Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress==0x90000100ULL); }
static void TestOverlappingRanges(void) { BusFixture(3U);RejectBuild(); }
static void TestCrossWindow(void) { BusFixture(2U);Put32(Value("/bus/uart@1100","reg")+4U,0x1000U);Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].translation==UMICOM_TREE_UNTRANSLATED); }
static void TestRangeWrap(void) { BusFixture(2U);Put64(Value("/bus","ranges")+4U,~(UmicomU64)0U-10U);RejectBuild(); }
static void TestRangeStride(void) { BusFixture(2U);UmicomU8 *p=Value("/bus","ranges");Put32(p-8U,15U);RejectBuild(); }
static void TestUnsupportedCells(void) { BusFixture(0U);Put32(Value("/bus","#address-cells"),3U);Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registerStatus==UMICOM_TREE_UNSUPPORTED_CELLS); }
static void TestCellDefaults(void)
{ Reset();Root();Begin("bus");Prop("ranges",0,0U);Begin("uart@1000");Text("compatible","ns16550a");UmicomU8 reg[12];Put64(reg,4096U);Put32(reg+8U,256U);Prop("reg",reg,12U);End();End();End();Finish();Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress==4096U); }
static void TestBadReg(void) { Golden();UmicomU8 *p=Value("/soc/serial@10000000","reg");Put32(p-8U,15U);RejectBuild(); }
static void TestRegWrap(void) { Golden();Put64(Value("/soc/serial@10000000","reg"),~(UmicomU64)0U-100U);RejectBuild(); }
static void TestBadPhandle(void) { Golden();Put32(Value("/soc/interrupt-controller@c000000","phandle"),0U);RejectBuild(); }
static void TestDuplicatePhandle(void) { Golden();Put32(Value("/soc/interrupt-controller@c000000","phandle"),1U);RejectBuild(); }
static void TestDanglingInterruptParent(void) { Golden();Put32(Value("/soc/serial@10000000","interrupt-parent"),999U);RejectBuild(); }
static void TestMalformedClock(void) { Golden();UmicomU8 *p=Value("/cpus","timebase-frequency");Put32(p-8U,3U);p[3]=0U;RejectBuild(); }
static void TestMissingClock(void) { Reset();Root();End();Finish();Build();CHECK(!catalogue.timebaseFrequency); }
static void TestCatalogueCapacity(void)
{ for(unsigned n=64U;n<=65U;++n){Reset();Root();for(unsigned i=0U;i<n;++i){char name[32];snprintf(name,sizeof(name),"device@%x",4096U+i*4096U);Device(name,"vendor,test",4096U+(UmicomU64)i*4096U,256U);}End();Finish();Parse();if(n==64U)OK(UmicomKernelHardwareCatalogueBuild(&reader,&catalogue));else BAD(UmicomKernelHardwareCatalogueBuild(&reader,&catalogue));} }
static void TestMultiMemory(void)
{ Reset();Root();Begin("memory@80000000");Text("device_type","memory");UmicomU8 value[32];Put64(value,0x80000000ULL);Put64(value+8U,0x100000U);Put64(value+16U,0x90000000ULL);Put64(value+24U,0x200000U);Prop("reg",value,32U);End();End();Finish();Build();CHECK(FindKind(UMICOM_HARDWARE_MEMORY)->registerCount==2U); }
static void TestRegisterCapacity(void)
{ Reset();Root();Begin("device@1000");UmicomU8 v[80];for(unsigned i=0U;i<5U;++i){Put64(v+16U*i,4096U+i*4096U);Put64(v+16U*i+8U,256U);}Prop("reg",v,sizeof(v));End();End();Finish();RejectBuild(); }
static void TestNestedRanges(void)
{
    Reset();Root();Begin("outer");Cell("#address-cells",1U);Cell("#size-cells",1U);
    UmicomU8 a[16];Put32(a,0x1000U);Put64(a+4U,0x90000000ULL);Put32(a+12U,0x10000U);Prop("ranges",a,16U);
    Begin("inner");Cell("#address-cells",1U);Cell("#size-cells",1U);
    UmicomU8 b[12];Put32(b,0U);Put32(b+4U,0x2000U);Put32(b+8U,0x1000U);Prop("ranges",b,12U);
    Begin("uart@100");Text("compatible","ns16550a");UmicomU8 c[8];Put32(c,0x100U);Put32(c+4U,0x80U);Prop("reg",c,8U);
    End();End();End();End();Finish();Build();CHECK(FindKind(UMICOM_HARDWARE_UART)->registers[0].physicalAddress==0x90001100ULL);
}
static void TestMutations(void)
{
    Golden();const UmicomSize bytes=fixture.bytes;UmicomU8 *copy=malloc((size_t)bytes);CHECK(copy);UmicomU32 seed=0x9e3779b9U;
    for(unsigned i=0U;i<8000U;++i){memcpy(copy,fixture.blob,(size_t)bytes);seed=seed*1664525U+1013904223U;
        const UmicomSize offset=seed%bytes;copy[offset]^=(UmicomU8)(1U<<(seed%8U));
        UmicomKernelTreeStatus s=UmicomKernelDeviceTreeOpen(copy,bytes,&reader);
        if(s==UMICOM_TREE_OK){s=UmicomKernelHardwareCatalogueBuild(&reader,&catalogue);CHECK(s==UMICOM_TREE_OK?catalogue.ready:!catalogue.ready);}
        else CHECK(!reader.opened);
    }free(copy);
}
static void TestCaptureAndReport(void)
{
    Golden();hostRamBase=(UmicomAddress)fixture.blob;hostRamBytes=fixture.bytes;
    UmicomKernelHardwareCapture(hostRamBase);OK(UmicomKernelHardwareCaptureStatus());
    const UmicomKernelHardwareCatalogue *c=UmicomKernelHardwareCatalogueRead();CHECK(c&&c->ready);
    memset(fixture.blob,0,sizeof(fixture.blob));UmicomKernelHardwareCapture(0U);OK(UmicomKernelHardwareCaptureStatus());
    UmicomKernelHardwareReport(0,Write);CHECK(strstr(output,"kind=virtio-mmio-slot"));CHECK(strstr(output,"not confirmed disks"));CHECK(strstr(output,"physical=0x10000000"));
    CHECK(strstr(output,"hart-id=0"));CHECK(strstr(output,"irq-route=not-decoded"));CHECK(!strstr(output,"\x1b"));
}
static void TestCaptureBounds(void)
{
    Golden();hostRamBase=(UmicomAddress)fixture.blob;hostRamBytes=fixture.bytes;
    UmicomKernelHardwareCapture(hostRamBase-1U);CHECK(UmicomKernelHardwareCaptureStatus()==UMICOM_TREE_OUTSIDE_BUFFER);
    CHECK(!UmicomKernelHardwareCatalogueRead());UmicomKernelHardwareReport(0,Write);CHECK(strstr(output,"No complete hardware inventory"));
}
static void TestPhandleMismatch(void)
{ Reset();Root();Begin("controller");Cell("phandle",1U);Cell("linux,phandle",2U);End();End();Finish();RejectBuild(); }
static void TestChosenAlias(void)
{ Reset();Root();Begin("chosen");Text("stdout-path","serial0:115200n8");End();End();Finish();Build();CHECK(!strcmp(catalogue.stdoutPath,"serial0:115200n8")); }
static void TestReadFailureOutputs(void)
{
    Golden();Parse();UmicomKernelTreeSpan s={(const UmicomU8 *)1,123U};
    CHECK(UmicomKernelDeviceTreeProperty(&reader,0U,"absent",&s)==UMICOM_TREE_NOT_FOUND);CHECK(s.bytes==123U);
    UmicomU32 n=123U;CHECK(UmicomKernelDeviceTreeFind(&reader,"/absent",&n)==UMICOM_TREE_NOT_FOUND);CHECK(n==123U);
    UmicomKernelDeviceTreeReader copy=reader;BAD(UmicomKernelDeviceTreeFind(&copy,"/",&n));
}
static void TestNodeNameCapacity(void)
{ for(unsigned n=95U;n<=96U;++n){Reset();Root();char name[97];memset(name,'a',n);name[n]=0;Begin(name);End();End();Finish();if(n==95U)Parse();else RejectParse();} }
static void TestTrailingEnd(void)
{ Reset();Root();End();Token(9U);Finish();RejectParse(); }
static void TestReserveHeaderOverlap(void) { Golden();Put32(fixture.blob+16U,32U);RejectParse(); }
static void TestZeroSizedAddressable(void)
{ Reset();Root();Device("device@1000","vendor,test",4096U,0U);End();Finish();Build();CHECK(catalogue.entries[0].registers[0].translation==UMICOM_TREE_UNSUPPORTED_CELLS); }

static void TestLegacyZeroHandle(void)
{ Reset();Root();Begin("controller");Cell("phandle",0U);Cell("linux,phandle",1U);End();End();Finish();RejectBuild(); }
static void TestCellWidthOverflow(void)
{ BusFixture(2U);UmicomU8 *p=Value("/bus","ranges");Put32(p,0xffffff00U);RejectBuild(); }
static void TestRegisterWidthOverflow(void)
{ BusFixture(0U);UmicomU8 *p=Value("/bus/uart@1100","reg");Put32(p,0xfffffff0U);RejectBuild(); }
static void TestDuplicateHart(void)
{ Reset();Root();Begin("cpus");Cell("#address-cells",1U);Cell("#size-cells",0U);
  Begin("cpu@0");Text("device_type","cpu");Cell("reg",0U);End();
  Begin("cpu@1");Text("device_type","cpu");Cell("reg",0U);End();End();End();Finish();RejectBuild(); }
static void TestOverlappingMemory(void)
{ Reset();Root();Begin("memory@80000000");Text("device_type","memory");Reg(0x80000000ULL,0x10000U);End();
  Begin("memory@80001000");Text("device_type","memory");Reg(0x80001000ULL,0x1000U);End();End();Finish();RejectBuild(); }
static void TestPropertyNameCapacity(void)
{ for(unsigned n=63U;n<=64U;++n){Reset();Root();char name[65];memset(name,'a',n);name[n]=0;Cell(name,1U);End();Finish();if(n==63U)Parse();else RejectParse();} }
static void TestCatalogueOverlap(void)
{
    Golden();Parse();
    BAD(UmicomKernelHardwareCatalogueBuild(&reader,(UmicomKernelHardwareCatalogue *)(void *)&reader));
    CHECK(reader.opened);BAD(UmicomKernelHardwareCatalogueBuild(&reader,(UmicomKernelHardwareCatalogue *)(void *)fixture.blob));
    CHECK(Get32(fixture.blob)==0xd00dfeedU);
}
static void TestRangeCapacity(void)
{
    Reset();Root();Begin("bus");Cell("#address-cells",1U);Cell("#size-cells",1U);
    UmicomU8 mappings[33U*16U];for(unsigned i=0U;i<33U;++i){Put32(mappings+16U*i,i*4096U);Put64(mappings+16U*i+4U,0x90000000ULL+i*4096U);Put32(mappings+16U*i+12U,4096U);}
    Prop("ranges",mappings,sizeof(mappings));Begin("device@0");UmicomU8 reg[8];Put32(reg,0U);Put32(reg+4U,256U);Prop("reg",reg,8U);End();End();End();Finish();RejectBuild();
}

/* Each named CTest is an independent process, including the once-only capture
 * tests. Loops inside a case are additional checks, not inflated CTest counts. */
static const struct { const char *name; void (*run)(void); } cases[] = {
    {"qemu_like_inventory",TestGolden},{"root_only",TestRootOnly},{"invalid_arguments",TestArguments},
    {"every_truncated_prefix",TestTruncations},{"unaligned_input",TestUnaligned},{"magic",TestMagic},
    {"total_size",TestTotal},{"blob_limit",TestOversize},{"old_format",TestOldFormat},
    {"future_compatible_format",TestFuture},{"incompatible_format",TestIncompatible},
    {"block_overlap",TestBlockOverlap},{"structure_alignment",TestStructureOffset},
    {"reservation_alignment",TestReservationOffset},{"block_extent",TestBlockExtent},
    {"reservation_terminator",TestMissingReserveEnd},{"reservation_overflow",TestReserveWrap},
    {"reservation_zero",TestReserveZero},{"reservation_overlap",TestReserveOverlap},
    {"reservation_capacity",TestReserveCapacity},{"unknown_token",TestBadToken},
    {"node_underflow",TestUnderflow},{"missing_root",TestMissingRoot},{"second_root",TestSecondRoot},
    {"unclosed_root",TestUnclosedRoot},{"missing_end",TestMissingEnd},{"property_after_child",TestAfterChild},
    {"node_characters",TestBadNodeName},{"node_padding",TestNodePadding},{"property_padding",TestPropertyPadding},
    {"name_offset",TestNameOffset},{"property_extent",TestValueExtent},{"duplicate_property",TestDuplicateProperty},
    {"duplicate_child",TestDuplicateChild},{"depth_capacity",TestDepth},{"node_capacity",TestNodeCapacity},
    {"property_capacity",TestPropertyCapacity},{"canonical_paths",TestPaths},{"string_suffix_reference",TestPropertySuffix},
    {"input_immutable_output_owned",TestCopies},{"typed_values",TestStrings},{"compatible_list",TestCompatibleList},
    {"malformed_compatible",TestMalformedCompatible},{"disabled_ancestor",TestDisabledAncestor},
    {"empty_ranges_identity",TestEmptyRanges},{"absent_ranges_untranslated",TestAbsentRanges},
    {"translated_ranges",TestTranslatedRanges},{"ambiguous_ranges",TestOverlappingRanges},
    {"cross_window_register",TestCrossWindow},{"range_overflow",TestRangeWrap},{"range_stride",TestRangeStride},
    {"unsupported_cells",TestUnsupportedCells},{"noninherited_cell_defaults",TestCellDefaults},
    {"register_stride",TestBadReg},{"register_overflow",TestRegWrap},{"zero_phandle",TestBadPhandle},
    {"duplicate_phandle",TestDuplicatePhandle},{"dangling_interrupt_reference",TestDanglingInterruptParent},
    {"malformed_timebase",TestMalformedClock},{"missing_timebase",TestMissingClock},
    {"catalogue_capacity",TestCatalogueCapacity},{"multiple_memory_spans",TestMultiMemory},
    {"register_capacity",TestRegisterCapacity},{"nested_bus_translation",TestNestedRanges},
    {"deterministic_mutations",TestMutations},{"capture_and_report",TestCaptureAndReport},
    {"capture_bounds",TestCaptureBounds},{"phandle_alias_mismatch",TestPhandleMismatch},
    {"chosen_alias_literal",TestChosenAlias},{"failure_outputs",TestReadFailureOutputs},
    {"node_name_capacity",TestNodeNameCapacity},{"trailing_end",TestTrailingEnd},
    {"reservation_header_overlap",TestReserveHeaderOverlap},{"zero_sized_resource",TestZeroSizedAddressable},
    {"legacy_zero_phandle",TestLegacyZeroHandle},{"range_cell_width_overflow",TestCellWidthOverflow},
    {"register_cell_width_overflow",TestRegisterWidthOverflow},{"duplicate_hart_id",TestDuplicateHart},
    {"overlapping_memory",TestOverlappingMemory},{"property_name_capacity",TestPropertyNameCapacity},
    {"catalogue_owner_overlap",TestCatalogueOverlap},{"range_capacity",TestRangeCapacity}
};
int main(int argc, char **argv)
{
    if(argc!=2){fprintf(stderr,"Supply one case name.\n");return 2;}
    for(size_t i=0U;i<sizeof(cases)/sizeof(cases[0]);++i)if(!strcmp(argv[1],cases[i].name)){
        cases[i].run();printf("PASS %s\n",cases[i].name);return 0;
    }
    fprintf(stderr,"Unknown case %s\n",argv[1]);return 2;
}
