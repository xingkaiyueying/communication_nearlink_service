
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>
#include "iposl_profile.h"
#include "iposl_codec.c"
static atomic_uint_fast64_t g_sendGeneration=42;
static uint64_t allowedGeneration=42;
static bool SecureSend(uint16_t l,uint8_t t,uint8_t pi,uint64_t g) { return l==1 && t==2 && (pi==1 || pi==2) && g==allowedGeneration; }
static const IposlProfileCallbacks *IposlGetCallbacks(void) { static IposlProfileCallbacks cb={.canSend=SecureSend}; return &cb; }
#define DTAP_PI_IPV4 1
#define NLSTK_LOG_ERROR(...) ((void)0)
typedef struct { unsigned char data[1500]; } SDF_Buff_S;
typedef struct { int pi; uint16_t lcid; uint8_t tcid; SDF_Buff_S *buff; } DTAP_Data_S;
static int live, mode, inCp, sends, failSend;
static void *SDF_MemAlloc(size_t n) { live++; return malloc(n); }
static void SDF_MemFree(void *p) { live--; free(p); }
static SDF_Buff_S *SDF_BuffNewWithReserve(int n) { (void)n; return SDF_MemAlloc(sizeof(SDF_Buff_S)); }
static uint8_t *SDF_BuffAppend(SDF_Buff_S *p,int n) { (void)n; return p->data; }
static void SDF_BuffFree(SDF_Buff_S *p) { SDF_MemFree(p); }
static int DTAP_DataSend(DTAP_Data_S *p) { assert(inCp); assert(p->buff->data[0]==0x60 && p->pi==2); sends++; if(failSend) return 7; SDF_BuffFree(p->buff); return 0; }
static void (*pending)(void*),(*release)(void*); static void *saved;
static uint32_t CP_PostTaskBlocked(void (*cb)(void*),void *a,void (*f)(void*),int timeout) {
 assert(timeout==500); if(mode==1) { f(a); return 1; }
 if(mode==2) { pending=cb; saved=a; release=f; return 2; }
 inCp=1; cb(a); inCp=0; f(a); return 0;
}

#include "profile_send.inc"
int main(void) {
 uint8_t bytes[1500]={0x60}; bytes[4]=5; bytes[5]=180; bytes[6]=17;
 assert(IposlProfileSendIp(1,2,2,bytes,1500,42)==0); assert(live==0 && sends==1);
 mode=1; assert(IposlProfileSendIp(1,2,2,bytes,1500,42)!=0); assert(live==0);
 mode=2; assert(IposlProfileSendIp(1,2,2,bytes,1500,42)!=0); bytes[0]=99;
 inCp=1; pending(saved); inCp=0; release(saved); assert(live==0 && sends==2);
 bytes[0]=0x60; assert(IposlProfileSendIp(1,2,2,bytes,1500,42)!=0);
 allowedGeneration=43; inCp=1; pending(saved); inCp=0; release(saved); assert(live==0 && sends==2);
 allowedGeneration=42; mode=0; failSend=1;
 assert(IposlProfileSendIp(1,2,2,bytes,1500,42)==7); assert(live==0);
 assert(IposlProfileSendIp(1,2,1,bytes,1500,42)!=0);
 assert(IposlProfileSendIp(1,2,2,bytes,1501,42)!=0); assert(live==0);
}
