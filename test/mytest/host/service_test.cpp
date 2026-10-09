#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <chrono>
#include <functional>
#include <thread>
#include <array>
#include <atomic>
#include <deque>
#include "nearlink_ipshare_status.cpp"
#include "iposl_codec.c"
#include "parameters.h"
#define private public
#include "nearlink_ipshare_service.cpp"
#include "nearlink_ipshare_channel.cpp"
#undef private
using namespace OHOS::Nearlink;
static IposlProfileCallbacks profileCallbacks;
static uint64_t profileGeneration;
static uint32_t profileCapacity;
static int starts, writes;
static bool serverDrained=true;
extern "C" int32_t IposlProfileInit(const IposlProfileCallbacks *c) { profileCallbacks=*c; return 0; }
extern "C" void IposlProfileDeinit() {}
extern "C" void IposlProfileStopClient() {}
extern "C" void IposlProfileStopServer() {}
extern "C" bool IposlProfileServerIsDrained() { return serverDrained; }
extern "C" int32_t IposlProfileReleaseServerPeerChecked(const uint8_t *,uint8_t) { return 0; }
extern "C" int32_t IposlProfileStartServerAny(uint8_t,uint32_t capacity,uint64_t g)
{ profileCapacity=capacity; profileGeneration=g; ++starts; return 0; }
extern "C" void IposlProfileReleaseServerPeer(const uint8_t *,uint8_t) {}
extern "C" int32_t IposlProfileStartServer(const uint8_t *,uint8_t,uint8_t,uint64_t g) { profileGeneration=g; ++starts; return 0; }
extern "C" int32_t IposlProfileStartTerminal(const uint8_t *,uint8_t,const uint8_t *,uint8_t,uint64_t g)
{ profileGeneration=g; ++starts; return 0; }
extern "C" int32_t IposlProfileProbePeer(const uint8_t *,uint8_t,uint8_t,uint64_t) { return 0; }
extern "C" int32_t IposlProfileSendIp(uint16_t,uint8_t,uint8_t,const uint8_t *,uint16_t,uint64_t) { return 0; }
extern "C" uint32_t QOSM_TransChannelCreate(const QOSM_TransChannelParams_S *) { return 0; }
extern "C" uint32_t QOSM_TransChannelDestroy(const QOSM_TransChannelReleaseParams_S *) { return 0; }
namespace OHOS::Nearlink {
NearlinkIpShareTun::~NearlinkIpShareTun() {}
int32_t NearlinkIpShareTun::Open(const PacketCallback &, const std::string &) { fd_=1; return 0; }
void NearlinkIpShareTun::Close() { fd_=-1; }
int32_t NearlinkIpShareTun::Write(const uint8_t *,uint16_t) { ++writes; return 0; }
bool NearlinkIpShareTun::IsOpen() const { return fd_>=0; }
bool NearlinkIpShareTun::ParseIpv6Evidence(const std::string &,uint32_t,uint8_t *,const std::string &) { return false; }
bool NearlinkIpShareTun::IsIpv6AddressUsable(const uint8_t *,const std::string &) { return true; }
}
struct Observer : INearlinkIpShareObserver {
    uint64_t generation=0, sequence=0; int events=0;
    void OnStatusChanged(const NearlinkIpShareStatus &s) override {
        assert(s.generation>=generation);
        assert(s.generation>generation || s.sequence>sequence);
        generation=s.generation; sequence=s.sequence; ++events;
    }
};
int main()
{
    auto &s=NearlinkIpShareService::GetInstance(); auto &c=NearlinkIpShareChannel::GetInstance();
    auto observer=std::make_shared<Observer>();
    assert(s.Initialize()==0 && s.RegisterObserver(observer)==0);
    const std::string address="02:01:02:03:04:05"; uint8_t peer[6]={2,1,2,3,4,5};
    assert(s.StartNearlinkTerminalWithMode(address,2)!=0);
    mockSecure=false; assert(s.StartNearlinkTerminalWithMode(address,3)!=0); mockSecure=true;
    assert(s.StartNearlinkTerminalWithMode(address,3)==0);
    assert(s.StartNearlinkTerminalWithMode(address,3)==0 && s.StartTerminal(address)!=0);
    DrainTasks(); assert(starts==1);
    uint64_t first=profileGeneration;
    bool supported=false; NearlinkIpShareCapabilities capabilities;
    assert(s.QueryNearlinkIpShareCapabilities(address,capabilities)==0 && !capabilities.peerCapabilityKnown);
    profileCallbacks.onPeerCapabilities(peer,3,first); DrainTasks();
    assert(s.QueryNearlinkIpShareCapabilities(address,capabilities)==0);
    assert(capabilities.peerCapabilityKnown && capabilities.peerModes==std::vector<int32_t>({1,3}));
    assert(profileCallbacks.prepareMode(peer,3,first)==0);
    profileCallbacks.onConfigured(peer,false,0,3,first); DrainTasks();
    profileCallbacks.onConfigured(peer,true,0,3,first); DrainTasks();
    QOSM_TransChannelRspParams_S rsp={}; rsp.mtu=1500; memcpy(rsp.addr.addr,peer,6);
    rsp.srcPort=rsp.dstPort=c.IP_SHARE_PORT; rsp.lcid=3; rsp.tcid=4; rsp.status=QOSM_TRANS_CHANNEL_ESTABLISHED;
    c.HandleChannelStatus(&rsp); DrainTasks();
    NearlinkIpShareStatus status; s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::CHANNEL_READY && status.selectedMode==NearlinkIpShareMode::DUAL_STACK);
    assert(s.IsPeerSupported(address,supported)==0 && supported);
    assert(s.QueryNearlinkIpShareCapabilities(address,capabilities)==0);
    assert(capabilities.identifierPresent && capabilities.discoveryState==1);
    assert(capabilities.peerCapabilityKnown && capabilities.peerModes==std::vector<int32_t>({1,3}));
    NearlinkIpShareStatus afterQuery; s.GetStatus(afterQuery);
    assert(afterQuery.generation==status.generation && afterQuery.sequence==status.sequence);
    assert(s.Stop()==0); DrainTasks(); s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::STOPPING && !registered[1] && !registered[2]);
    rsp.status=QOSM_TRANS_CHANNEL_RELEASED; c.HandleChannelStatus(&rsp); DrainTasks(); s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::IDLE);
    assert(s.StartNearlinkGatewayWithMode(address,3)==0); DrainTasks();
    uint64_t next=profileGeneration; assert(next>first);
    profileCallbacks.onPeerCapabilities(peer,3,first); DrainTasks();
    assert(!s.capabilities_.peerCapabilityKnown);
    profileCallbacks.onConfigured(peer,true,0,3,first); DrainTasks(); s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::IFACE_READY && !c.enabled_);
    assert(profileCallbacks.prepareMode(peer,3,first)!=0);
    assert(profileCallbacks.prepareMode(peer,3,next)==0);
    profileCallbacks.onConfigured(peer,false,0,3,next); profileCallbacks.onConfigured(peer,true,0,3,next); DrainTasks();
    SLE_Addr_S peerAddrForAdmission = {};
    memcpy(peerAddrForAdmission.addr, peer, 6);
    auto otherAddrForAdmission = peerAddrForAdmission;
    otherAddrForAdmission.addr[5] ^= 1;
    assert(!c.IsAcceptingPort(&otherAddrForAdmission, c.IP_SHARE_PORT));
    assert(c.IsAcceptingPort(&peerAddrForAdmission, c.IP_SHARE_PORT));
    assert(c.channelPending_ && !c.IsAcceptingPort(&peerAddrForAdmission, c.IP_SHARE_PORT));
    supported=false;
    assert(s.IsPeerSupported(address,supported)==0 && supported);
    assert(s.QueryNearlinkIpShareCapabilities(address,capabilities)==0 && capabilities.identifierPresent);
    assert(!capabilities.peerCapabilityKnown && capabilities.peerModes.empty());
    rsp.status=QOSM_TRANS_CHANNEL_ESTABLISHED; c.HandleChannelStatus(&rsp); DrainTasks();
    rsp.status=QOSM_TRANS_CHANNEL_RELEASED; c.HandleChannelStatus(&rsp); DrainTasks(); s.GetStatus(status);
    assert(status.generation>next && status.serviceReady && status.selectedMode==NearlinkIpShareMode::NONE);
    assert(c.tun_.IsOpen() && !c.enabled_ && !registered[1] && !registered[2]);
    assert(s.Stop()==0); DrainTasks();
    // Stop before queued start: no profile work or stale start survives.
    int previous=starts; assert(s.StartTerminal(address)==0); assert(s.Stop()==0); DrainTasks();
    assert(starts==previous); s.GetStatus(status); assert(status.state==NearlinkIpShareState::IDLE);
    assert(s.StartTerminal(address)==0); DrainTasks();
    assert(s.Stop()==0 && s.Stop()==0);
    auto firstStop=tasks.front(); tasks.pop_front(); firstStop();
    assert(s.StartTerminal(address)==0); DrainTasks(); s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::DISCOVERING); // queued old stop cannot kill new generation
    s.Stop(); DrainTasks(); s.Shutdown();
    assert(s.Initialize()==0);
    for (int oldSetting : {0, 2, 7, 33}) {
        OHOS::system::mockMaxTerminals=oldSetting;
        assert(s.GetSupportedMaxTerminals()==32); // retired system parameter does not limit APP input
    }
    OHOS::system::mockMaxTerminals=2;
    assert(s.StartGatewayAny(1,0)!=0 && s.StartGatewayAny(1,33)!=0);
    assert(s.StartGatewayAny(1,1)==0); DrainTasks();
    s.GetStatus(status); assert(status.role==NearlinkIpShareRole::GATEWAY &&
        status.state==NearlinkIpShareState::SERVING_NO_UPSTREAM && status.peerAddress.empty());
    uint64_t gatewayGen=status.generation;
    uint8_t second[6]={2,1,2,3,4,6};
    assert(profileCallbacks.prepareMode(peer,1,gatewayGen)==0);
    s.GetStatus(status);
    assert(status.peerLinks.size()==1 && !status.peerLinks[0].active && !status.peerLinks[0].releasing);
    assert(profileCallbacks.prepareMode(second,1,gatewayGen)!=0); // one atomic IP seat
    profileCallbacks.onConfigured(peer,false,0,1,gatewayGen);
    profileCallbacks.onConfigured(peer,true,0,1,gatewayGen); DrainTasks();
    auto firstChannel=s.gatewayPeers_[0]->channel;
    assert(firstChannel->tun_.IsOpen() && c.IsDrained());
    QOSM_TransChannelRspParams_S firstRsp={}; firstRsp.mtu=1500; memcpy(firstRsp.addr.addr,peer,6);
    firstRsp.srcPort=firstRsp.dstPort=c.IP_SHARE_PORT;
    firstRsp.lcid=13; firstRsp.tcid=4; firstRsp.status=QOSM_TRANS_CHANNEL_ESTABLISHED;
    assert(c.HandleChannelStatus(&firstRsp)); DrainTasks();
    assert(s.gatewayPeers_[0]->active && firstChannel->CanSend(13,4,1,s.gatewayPeers_[0]->epoch));
    assert(s.CanSend(13,4,1,s.gatewayPeers_[0]->epoch));
    assert(!s.CanSend(13,4,1,s.gatewayPeers_[0]->epoch+100));
    s.GetStatus(status);assert(status.peerLinks[0].active);
    s.GetStatus(status); assert(status.state==NearlinkIpShareState::CHANNEL_READY &&
        status.ifaceName=="sleip0" && status.peerAddress.empty());
    auto oldEpoch = s.gatewayPeers_[0]->epoch;
    firstRsp.status=QOSM_TRANS_CHANNEL_RELEASED; c.HandleChannelStatus(&firstRsp); DrainTasks();
    assert(s.gatewayPeers_[0] && s.gatewayPeers_[0]->releasing);
    assert(profileCallbacks.prepareMode(peer,1,gatewayGen)==IPOSL_ERR_PEER_DRAINING);
    assert(profileCallbacks.prepareMode(second,1,gatewayGen)!=0); // still occupied until L3 cleanup
    assert(s.CompleteGatewayPeerRelease(oldEpoch+1)!=0);
    assert(s.CompleteGatewayPeerRelease(oldEpoch)==0); DrainTasks();
    assert(s.gatewayPeers_[0]==nullptr && s.GetStatus(status)==0 && status.serviceReady &&
        status.state==NearlinkIpShareState::SERVING_NO_UPSTREAM && status.ifaceName.empty());
    assert(profileCallbacks.prepareMode(second,1,gatewayGen)==0); // released slot reusable
    assert(s.Stop()==0); DrainTasks(); s.GetStatus(status); assert(status.state==NearlinkIpShareState::IDLE);
    assert(s.StartGatewayAny(3,2)==0); DrainTasks(); s.GetStatus(status); gatewayGen=status.generation;
    assert(profileCallbacks.prepareMode(peer,3,gatewayGen)==0);
    assert(profileCallbacks.prepareMode(second,1,gatewayGen)==0);
    assert(s.gatewayPeers_[0]!=nullptr && s.gatewayPeers_[1]!=nullptr);
    profileCallbacks.onConfigured(peer,true,0,3,gatewayGen);
    profileCallbacks.onConfigured(second,true,0,1,gatewayGen); DrainTasks();
    assert(s.gatewayPeers_[0]->channel->tun_.IsOpen() && s.gatewayPeers_[1]->channel->tun_.IsOpen());
    s.OnPeerDisconnected(address); DrainTasks();
    assert(s.CompleteGatewayPeerRelease(s.gatewayPeers_[0]->epoch)==0); DrainTasks();
    assert(s.gatewayPeers_[0]==nullptr && s.gatewayPeers_[1]!=nullptr &&
        s.gatewayPeers_[1]->channel->tun_.IsOpen()); // A2 survives A1 departure
    assert(s.Stop()==0); DrainTasks(); s.GetStatus(status); assert(status.state==NearlinkIpShareState::IDLE);
    assert(s.StartGatewayAny(1,1)==0); DrainTasks(); s.GetStatus(status); gatewayGen=status.generation;
    std::atomic<int> aResult{99}, bResult{99};
    std::thread aThread([&]() { aResult=profileCallbacks.prepareMode(peer,1,gatewayGen); });
    std::thread bThread([&]() { bResult=profileCallbacks.prepareMode(second,1,gatewayGen); });
    aThread.join(); bThread.join();
    assert((aResult==0)!=(bResult==0)); // near-simultaneous claims have exactly one winner
    assert(s.Stop()==0); DrainTasks(); s.GetStatus(status); assert(status.state==NearlinkIpShareState::IDLE);
    for (int capacity : {3, 5, 7, 32}) {
        assert(s.StartGatewayAny(1,capacity)==0); DrainTasks(); s.GetStatus(status); gatewayGen=status.generation;
        assert(profileCapacity==static_cast<uint32_t>(capacity) &&
            s.gatewayPeers_.size()==static_cast<size_t>(capacity));
        for (uint8_t n=0;n<capacity;++n) {
            uint8_t next[6]={2,1,2,3,4,static_cast<uint8_t>(10+n)};
            assert(profileCallbacks.prepareMode(next,1,gatewayGen)==0);
        }
        uint8_t overflow[6]={2,1,2,3,4,99};
        assert(profileCallbacks.prepareMode(overflow,1,gatewayGen)!=0);
        s.GetStatus(status);assert(status.peerLinks.size()==static_cast<size_t>(capacity));
        for(const auto &link:status.peerLinks)assert(!link.active);
        assert(s.Stop()==0); DrainTasks(); s.GetStatus(status);
        assert(status.state==NearlinkIpShareState::IDLE);
    }
    OHOS::system::mockMaxTerminals=2;
    assert(s.StartGatewayAny(1,2)==0);DrainTasks();s.GetStatus(status);gatewayGen=status.generation;
    assert(profileCallbacks.prepareMode(peer,1,gatewayGen)==0);
    auto epoch=s.gatewayPeers_[0]->epoch;
    std::atomic<bool> sending{true};
    std::thread cpSender([&]() {while(sending) (void)s.CanSend(1,2,1,epoch);});
    s.OnPeerDisconnected(address);DrainTasks();
    assert(s.CompleteGatewayPeerRelease(epoch)==0);DrainTasks();
    serverDrained=false;assert(s.Stop()==0);DrainTasks();s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::STOPPING);
    serverDrained=true;assert(s.Stop()==0);DrainTasks();s.GetStatus(status);
    assert(status.state==NearlinkIpShareState::IDLE);
    sending=false;cpSender.join();
    s.Shutdown();
}
