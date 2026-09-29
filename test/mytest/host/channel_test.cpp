#include <cassert>
#include <array>
#include <cstring>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include "iposl_codec.c"
#include "sleip_probe_packets.h"
#include "ipv6_test_packets.h"
#define private public
#include "nearlink_ipshare_channel.cpp"
#undef private
static int creates, destroys, tunWrites, sent;
static uint8_t sentPi;
static bool secure = true;
extern "C" uint32_t QOSM_TransChannelCreate(const QOSM_TransChannelParams_S *) { ++creates; return 0; }
extern "C" uint32_t QOSM_TransChannelDestroy(const QOSM_TransChannelReleaseParams_S *) { ++destroys; return 0; }
extern "C" int32_t IposlProfileSendIp(uint16_t lcid, uint8_t tcid, uint8_t pi, const uint8_t *, uint16_t, uint64_t gen)
{
    if (!OHOS::Nearlink::NearlinkIpShareService::CanSend(lcid, tcid, pi, gen)) return -1;
    ++sent; sentPi = pi; return 0;
}
namespace OHOS::Nearlink {
bool NearlinkIpShareService::CanSend(uint16_t l, uint8_t t, uint8_t p, uint64_t g)
{ return secure && NearlinkIpShareChannel::GetInstance().CanSend(l,t,p,g); }
NearlinkIpShareTun::~NearlinkIpShareTun() {}
int32_t NearlinkIpShareTun::Open(const PacketCallback &) { fd_ = 1; return 0; }
void NearlinkIpShareTun::Close() { fd_ = -1; }
int32_t NearlinkIpShareTun::Write(const uint8_t *, uint16_t) { ++tunWrites; return fd_ >= 0 ? 0 : -1; }
bool NearlinkIpShareTun::IsOpen() const { return fd_ >= 0; }
bool NearlinkIpShareTun::ParseIpv6Evidence(const std::string &text,uint32_t index,uint8_t *out)
{
    if (text != "fe80::42" || index != 7) return false;
    auto address = Lla(0x42); std::copy(address.begin(), address.end(), out); return true;
}
bool NearlinkIpShareTun::IsIpv6AddressUsable(const uint8_t *) { return true; } // kernel boundary
}
int main()
{
    using namespace OHOS::Nearlink;
    auto &c = NearlinkIpShareChannel::GetInstance();
    uint8_t peer[6] = {2,1,2,3,4,5}, local[6] = {2,6,7,8,9,10};
    assert(c.Initialize([](bool,int32_t,uint64_t){}) == 0);
    assert(!registered[1] && !registered[2]);
    assert(c.SetPeer(peer,0,true,peer,local,10) == 0 && c.CreateTun() == 0);
    assert(!c.IsAcceptingPort(c.IP_SHARE_PORT));
    failPi = 2; assert(c.PrepareMode(3) != 0 && !registered[1] && !registered[2]);
    failPi = 0; assert(c.PrepareMode(3) == 0 && registered[1] && registered[2]);
    assert(c.EnableMode(1) != 0 && c.EnableMode(3) == 0 && c.Open(peer,0) == 0);
    QOSM_TransChannelRspParams_S rsp = {};
    memcpy(rsp.addr.addr,peer,6); rsp.srcPort = rsp.dstPort = c.IP_SHARE_PORT;
    rsp.lcid = 3; rsp.tcid = 4; rsp.status = QOSM_TRANS_CHANNEL_ESTABLISHED;
    c.HandleChannelStatus(&rsp);
    assert(creates == 1 && c.channelEstablished_);
    assert(c.SetPeer(peer,0,true,peer,local,11) != 0);
    NearlinkIpShareAddressEvidence evidence;
    evidence.address = "fe80::42"; evidence.ifindex = 7; evidence.prefixLength = 64;
    evidence.generation = 10; evidence.sequence = 1; evidence.flags = 0x40;
    evidence.preferredLifetime = 20; evidence.validLifetime = 40;
    assert(c.UpdateValidatedAddress(evidence) == 0);
    assert(c.UpdateValidatedAddress(evidence) != 0); // repeated sequence
    evidence.sequence = 2; evidence.generation = 9; assert(c.UpdateValidatedAddress(evidence) != 0);
    evidence.generation = 10; evidence.ifindex = 8; assert(c.UpdateValidatedAddress(evidence) != 0);
    evidence.ifindex = 7; evidence.flags = 0; assert(c.UpdateValidatedAddress(evidence) == 0);
    evidence.sequence = 3; evidence.preferredLifetime = evidence.validLifetime = 0;
    assert(c.UpdateValidatedAddress(evidence) == 0 && c.ipv6_.Mappings().empty());

    // The RA fixture configures the gateway address in the kernel, outside the DHCP evidence callback.
    // A verified local NA must therefore seed that owner before policy validation and defeat peer DAD.
    auto ra = Ra(); assert(c.AuthorizePacket(ra.data(), ra.size(), 10, false));
    auto occupiedDad = Dad(Global(1)); assert(c.AuthorizePacket(occupiedDad.data(), occupiedDad.size(), 10, true));
    auto ownerNa = Na(Global(1), Group(1)); assert(c.AuthorizePacket(ownerNa.data(), ownerNa.size(), 10, false));
    assert(std::count_if(c.ipv6_.Mappings().begin(), c.ipv6_.Mappings().end(),
        [](const auto &mapping) { return mapping.conflict; }) == 1);

    // The S1 fixed endpoints require the same DAD/first-source authorization as every S2 address.
    uint8_t peerLla[16], localLla[16];
    IposlCodecS1LinkLocal(peer, peerLla); IposlCodecS1LinkLocal(local, localLla);
    Address peerAddr{}, localAddr{};
    std::copy(peerLla, peerLla + 16, peerAddr.begin());
    std::copy(localLla, localLla + 16, localAddr.begin());
    auto pd = Dad(peerAddr), ld = Dad(localAddr);
    assert(c.AuthorizePacket(pd.data(), pd.size(), 10, true));
    assert(c.AuthorizePacket(ld.data(), ld.size(), 10, false));
    auto first = Echo(peerAddr, localAddr);
    assert(c.AuthorizePacket(first.data(), first.size(), 10, true));
    first = Echo(localAddr, peerAddr);
    assert(c.AuthorizePacket(first.data(), first.size(), 10, false));
    uint8_t packet[1500]; SDF_Buff_S buffer = {}; DTAP_Data_Info_S info = {2,3,4};
    ProbeData(packet,2,false,peer,local,1); memcpy(buffer.data,packet,1500); buffer.size = 1500;
    assert(registered[2](&info,&buffer) == 0 && tunWrites == 1);
    info.pi = 1; assert(registered[1](&info,&buffer) != 0); info.pi = 3;
    assert(c.Receive(&info,&buffer) != 0); info.pi = 2;
    info.tcid = 9; assert(c.Receive(&info,&buffer) != 0); info.tcid = 4;
    buffer.data[8] ^= 1; assert(c.Receive(&info,&buffer) != 0); buffer.data[8] ^= 1;
    buffer.size = 1499; assert(c.Receive(&info,&buffer) != 0); buffer.size = 1500;
    secure = false; assert(c.Receive(&info,&buffer) != 0); secure = true;
    assert(!c.AuthorizePacket(packet,1500,9,true));
    ProbeData(packet,2,true,local,peer,1); assert(c.Send(packet,1500) == 0 && sentPi == 2);
    uint8_t dhcp[300];
    ProbeDhcp(dhcp,5,peer); assert(!c.AuthorizePacket(dhcp,300,10,false)); // unsolicited ACK
    for (uint8_t m : {1,2,3,5}) {
        ProbeDhcp(dhcp,m,peer);
        assert(c.AuthorizePacket(dhcp,300,10,m == 1 || m == 3));
    }
    for (uint8_t round = 0; round < 8; ++round) for (uint8_t pi : {1,2}) {
        ProbeData(packet,pi,true,local,peer,round); assert(c.Send(packet,1500) == 0 && sentPi == pi);
        ProbeData(buffer.data,pi,false,peer,local,round); info.pi = pi;
        assert(c.Receive(&info,&buffer) == 0);
    }
    c.Close(); assert(!registered[1] && !registered[2] && !c.IsDrained());
    assert(c.Receive(&info,&buffer) != 0 && c.Send(packet,1500) != 0 && !c.CanSend(3,4,2,10));
    rsp.status = QOSM_TRANS_CHANNEL_RELEASED; c.HandleChannelStatus(&rsp); assert(c.IsDrained());
    assert(c.SetPeer(peer,0,true,peer,local,11) == 0 && c.CreateTun() == 0 && c.PrepareMode(1) == 0);
    assert(registered[1] && !registered[2]); assert(c.EnableMode(1) == 0 && c.Open(peer,0) == 0);
    c.Close(); assert(!c.IsDrained()); // cancelled pending create must drain before another start
    rsp.status = QOSM_TRANS_CHANNEL_ESTABLISHED; c.HandleChannelStatus(&rsp);
    assert(destroys == 2 && !c.channelEstablished_);
    rsp.status = QOSM_TRANS_CHANNEL_RELEASED; c.HandleChannelStatus(&rsp); assert(c.IsDrained());
    assert(c.SetPeer(peer,0,true,peer,local,12) == 0 && c.PrepareMode(3) == 0);
    assert(c.PrepareMode(0) == 0 && !registered[1] && !registered[2]);
    assert(c.PrepareMode(1) == 0 && !registered[2]); // explicit fallback resource rollback
    c.Close(); c.Deinitialize();
}
