#include <cassert>
#include <cstring>
#include "iposl_codec.c"
#include "sleip_probe_packets.h"
int main()
{
    uint8_t p[1600] = {}, local[6] = {2,1,2,3,4,5}, peer[6] = {2,6,7,8,9,10};
    ProbeData(p,2,false,local,peer,1);
    assert(IposlCodecValidatePacket(2,p,1500));
    for (size_t n=0; n<1500; ++n) assert(!IposlCodecValidatePacket(2,p,n));
    assert(!IposlCodecValidatePacket(2,p,1501) && !IposlCodecValidatePacket(3,p,1500));
    p[6]=44; assert(!IposlCodecValidatePacket(2,p,1500)); // unconfirmed fragment
    p[6]=0; p[40]=17; p[41]=255; assert(!IposlCodecValidatePacket(2,p,1500));
    p[41]=0; assert(IposlCodecValidatePacket(2,p,1500));
    for (int i=0;i<9;++i) { p[40+i*8]=0; p[41+i*8]=0; }
    p[104]=17; assert(!IposlCodecValidatePacket(2,p,1500)); // ninth extension
    p[96]=17; assert(IposlCodecValidatePacket(2,p,1500)); // eight short extensions
    p[40]=17; p[41]=32; assert(!IposlCodecValidatePacket(2,p,1500)); // 264 bytes
    p[41]=31; assert(IposlCodecValidatePacket(2,p,1500));
    ProbeData(p,1,false,local,peer,1); assert(IposlCodecValidatePacket(1,p,1500));
    p[0]=0x44; assert(!IposlCodecValidatePacket(1,p,1500));
    uint8_t request[11], layer[6], opcode=0;
    assert(IposlCodecEncodeConfigMode(local,3,request,11)==11);
    for(size_t n=0;n<11;++n) assert(IposlCodecDecodeRequest(request,n,&opcode,layer)!=0);
    request[10]=2; assert(IposlCodecDecodeRequest(request,11,&opcode,layer)!=0);
    assert(!IposlCodecMayFallback(2,3,255,false,true));
    assert(!IposlCodecMayFallback(1,3,8,false,true));
    assert(!IposlCodecMayFallback(1,1,255,false,true));
}
