#include <cassert>
#include "iposl_codec.c"
#include "iposl_server.c"
static int prepared, rolledBack, notified;
static bool secure = true, reserveFails;
static uint8_t reserved;
static int Prepare(const uint8_t *, uint8_t mode, uint64_t generation)
{ assert(generation == 55); if (!mode) ++rolledBack; else ++prepared; reserved=mode; return reserveFails ? -1 : 0; }
static bool Secure(const uint8_t *, uint64_t generation) { return secure && generation == 55; }
static bool SecureTyped(const uint8_t *, uint8_t type, uint64_t generation) { return secure && type == 0 && generation == 55; }
static void Configured(const uint8_t *,bool,int32_t,uint8_t mode,uint64_t)
{ assert(mode == reserved); ++notified; }
const IposlProfileCallbacks *IposlGetCallbacks()
{ static IposlProfileCallbacks cb = {nullptr,Configured,Prepare,Secure,nullptr,nullptr,SecureTyped}; return &cb; }
int main()
{
    uint8_t peer[6] = {2,1,2,3,4,5}, bytes[11];
    assert(IposlServerInitialize() == 0 && IposlServerStart(peer,0,3,55) == 0);
    NLSTK_SsapServerCallMethodRequestInfo_S request = {}; memcpy(request.addr.addr,peer,6);
    request.handle=4; request.param={11,bytes}; IposlCodecEncodeConfigMode(peer,3,bytes,11);
    reserveFails=true; OnCallMethod(7,1,&request,true,false); assert(wireResult==255 && !g_configured);
    reserveFails=false; sendResult=-1; OnCallMethod(7,2,&request,true,false);
    assert(!g_configured && rolledBack==1 && notified==0);
    sendResult=0; OnCallMethod(7,3,&request,true,false);
    assert(wireResult==0 && g_configured && g_selectedMode==3 && notified==1);
    int count=prepared; OnCallMethod(7,4,&request,true,false); assert(prepared==count); // no second reservation
    bytes[10]=1; OnCallMethod(7,5,&request,true,false); assert(wireResult==255 && g_selectedMode==3);
    request.param.len=7; IposlCodecEncodeOpenRequest(peer,bytes,11);
    secure=false; OnCallMethod(7,6,&request,true,false); assert(wireResult==255 && !g_enabled);
    secure=true; OnCallMethod(7,7,&request,true,false); assert(g_enabled && wireResult==0);
    count=notified; OnCallMethod(7,8,&request,true,false); assert(notified==count);
    IposlServerStop(); assert(!g_enabled && !g_configured);
    assert(IposlServerStart(peer,0,1,55)==0);
    request.param.len=11; IposlCodecEncodeConfigMode(peer,3,bytes,11);
    OnCallMethod(7,9,&request,true,false); assert(wireResult==255 && !g_configured);
    bytes[10]=1; request.addr.addr[5]++; OnCallMethod(7,10,&request,true,false); assert(wireResult==255);
    request.addr.addr[5]--; OnCallMethod(7,11,&request,true,false); assert(wireResult==0 && g_selectedMode==1);
    IposlServerStop();
    assert(IposlServerStartAny(3,1,55)==0);
    request.param.len=11; IposlCodecEncodeConfigMode(peer,3,bytes,11);
    OnCallMethod(7,12,&request,true,false); assert(wireResult==0 && g_peers[0].used);
    uint8_t second[6]={2,1,2,3,4,6};
    memcpy(request.addr.addr,second,6); IposlCodecEncodeConfigMode(second,3,bytes,11);
    OnCallMethod(7,13,&request,true,false); assert(wireResult==255);
    IposlServerReleasePeer(peer,0);
    OnCallMethod(7,14,&request,true,false); assert(wireResult==0 && g_peers[0].used);
    IposlServerStop();
    assert(IposlServerStartAny(1,7,55)==0);
    for (uint8_t n=0;n<7;++n) {
        uint8_t next[6]={2,1,2,3,4,static_cast<uint8_t>(10+n)};
        memcpy(request.addr.addr,next,6);
        IposlCodecEncodeConfigMode(next,1,bytes,11);
        OnCallMethod(7,static_cast<uint16_t>(20+n),&request,true,false);
        assert(wireResult==0 && g_peers[n].used);
    }
    uint8_t eighth[6]={2,1,2,3,4,99};
    memcpy(request.addr.addr,eighth,6); IposlCodecEncodeConfigMode(eighth,1,bytes,11);
    OnCallMethod(7,30,&request,true,false); assert(wireResult==255);
    IposlServerDeinit();
}
