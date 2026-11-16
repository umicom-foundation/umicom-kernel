/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/tcp_sequence/sequence_tests.c
 *
 * PURPOSE:
 *   Check sequence boundaries, zero windows and independently enumerated modulo intervals against production arithmetic.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/tcp_sequence.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static int compare(void) {
    const UmicomU32 bases[]={0U,1U,0x7fffffffU,0x80000000U,0xfffffffeU,0xffffffffU};
    for(unsigned i=0;i<6U;++i){
        const UmicomU32 b=bases[i];
        CHECK(UmicomKernelTcpSequenceCompare(b,b)==UMICOM_TCP_SEQUENCE_EQUAL);
        CHECK(UmicomKernelTcpSequenceCompare(b+1U,b)==UMICOM_TCP_SEQUENCE_AFTER);
        CHECK(UmicomKernelTcpSequenceCompare(b-1U,b)==UMICOM_TCP_SEQUENCE_BEFORE);
        CHECK(UmicomKernelTcpSequenceCompare(b+0x7fffffffU,b)==UMICOM_TCP_SEQUENCE_AFTER);
        CHECK(UmicomKernelTcpSequenceCompare(b+0x80000000U,b)==UMICOM_TCP_SEQUENCE_AMBIGUOUS);
        CHECK(UmicomKernelTcpSequenceCompare(b+0x80000001U,b)==UMICOM_TCP_SEQUENCE_BEFORE);
    }
    return 0;
}
static int advance(void) {
    UmicomTcpSequenceValue r=UmicomKernelTcpSequenceAdvance(0xffffffffU,1U);
    CHECK(r.valid==UMICOM_TRUE&&r.value==0U);
    r=UmicomKernelTcpSequenceAdvance(5U,0U);CHECK(r.valid==UMICOM_TRUE&&r.value==5U);
    r=UmicomKernelTcpSequenceAdvance(0xffffffffU,0x7fffffffU);CHECK(r.valid==UMICOM_TRUE&&r.value==0x7ffffffeU);
    r=UmicomKernelTcpSequenceAdvance(7U,0x80000000U);CHECK(r.valid==UMICOM_FALSE&&r.value==0U);
    r=UmicomKernelTcpSequenceAdvance(7U,0xffffffffU);CHECK(r.valid==UMICOM_FALSE&&r.value==0U);
    return 0;
}
static int length(void) {
    for(unsigned flags=0;flags<256U;++flags){
        unsigned controls=(flags%2U)+((flags/2U)%2U);
        UmicomTcpSequenceValue r=UmicomKernelTcpSequenceLength(13U,(UmicomU8)flags);
        CHECK(r.valid==UMICOM_TRUE&&r.value==13U+controls);
        r=UmicomKernelTcpSequenceLength(0x7fffffffU-controls,(UmicomU8)flags);
        CHECK(r.valid==UMICOM_TRUE&&r.value==0x7fffffffU);
        r=UmicomKernelTcpSequenceLength(0x80000000U-controls,(UmicomU8)flags);
        CHECK(r.valid==UMICOM_FALSE&&r.value==0U);
    }
    CHECK(UmicomKernelTcpSequenceLength(0xffffffffU,3U).valid==UMICOM_FALSE);
    return 0;
}
static int ack(void) {
    CHECK(UmicomKernelTcpNewAcknowledgment(100U,110U,101U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpNewAcknowledgment(100U,110U,110U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpNewAcknowledgment(100U,110U,100U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpNewAcknowledgment(100U,110U,111U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpNewAcknowledgment(100U,110U,99U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpNewAcknowledgment(5U,5U,5U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpNewAcknowledgment(0xfffffffeU,2U,0U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpNewAcknowledgment(0U,0x80000000U,1U)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpNewAcknowledgment(100U,99U,100U)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpNewAcknowledgment(0U,0x7fffffffU,0x7fffffffU)==UMICOM_TCP_SEQUENCE_ACCEPT);
    return 0;
}
static int window(void) {
    CHECK(UmicomKernelTcpReceiveSequence(100U,0U,100U,0U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,0U,99U,0U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,0U,100U,1U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,100U,0U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,109U,0U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,110U,0U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,99U,2U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,109U,2U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,99U,1U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveSequence(100U,10U,99U,12U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveSequence(0xfffffffeU,4U,0U,1U)==UMICOM_TCP_SEQUENCE_ACCEPT);
    CHECK(UmicomKernelTcpReceiveSequence(0xfffffffeU,4U,2U,1U)==UMICOM_TCP_SEQUENCE_REJECT);
    CHECK(UmicomKernelTcpReceiveSequence(0U,0x80000000U,0U,0U)==UMICOM_TCP_SEQUENCE_INVALID);
    CHECK(UmicomKernelTcpReceiveSequence(0U,0U,0U,0x80000000U)==UMICOM_TCP_SEQUENCE_INVALID);
    return 0;
}
/* The oracle enumerates each occupied sequence value with a wide modulo,
 * independently of production's subtraction-based membership predicate. */
static UmicomU32 wrap(UmicomU64 n){return (UmicomU32)(n%0x100000000ULL);}
static int contains(UmicomU32 start,unsigned count,UmicomU32 value){
    for(unsigned i=0;i<count;++i)if(wrap((UmicomU64)start+i)==value)return 1;
    return 0;
}
static int model(void) {
    const UmicomU32 bases[]={0U,0xfffffff8U,0x7ffffff8U,12345U};
    for(unsigned b=0;b<4U;++b)for(unsigned w=0;w<=12U;++w)
    for(unsigned n=0;n<=12U;++n)for(unsigned o=0;o<=32U;++o){
        UmicomU32 s=wrap((UmicomU64)bases[b]+0x100000000ULL-16U+o);
        int expected=w==0U ? n==0U&&s==bases[b] :
            contains(bases[b],w,s)||(n!=0U&&contains(bases[b],w,wrap((UmicomU64)s+n-1U)));
        CHECK(UmicomKernelTcpReceiveSequence(bases[b],w,s,n)==
            (expected?UMICOM_TCP_SEQUENCE_ACCEPT:UMICOM_TCP_SEQUENCE_REJECT));
        int accepted=0;
        for(unsigned i=1;i<=w;++i)if(wrap((UmicomU64)bases[b]+i)==s)accepted=1;
        CHECK(UmicomKernelTcpNewAcknowledgment(bases[b],wrap((UmicomU64)bases[b]+w),s)==
            (accepted?UMICOM_TCP_SEQUENCE_ACCEPT:UMICOM_TCP_SEQUENCE_REJECT));
    }
    return 0;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"compare"))return compare();
    if(!strcmp(argv[1],"advance"))return advance();
    if(!strcmp(argv[1],"length"))return length();
    if(!strcmp(argv[1],"ack"))return ack();
    if(!strcmp(argv[1],"window"))return window();
    if(!strcmp(argv[1],"model"))return model();
    return 2;
}
