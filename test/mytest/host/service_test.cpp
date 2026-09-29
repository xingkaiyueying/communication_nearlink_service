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
#define private public
#include "nearlink_ipshare_service.cpp"
#include "nearlink_ipshare_channel.cpp"
#undef private
using namespace OHOS::Nearlink;
static IposlProfileCallbacks profileCallbacks;
static uint64_t profileGeneration;
static int starts, writes;
extern "C" int32_t IposlProfileInit(const IposlProfileCallbacks *c) { profileCallbacks=*c; return 0; }
extern "C" void IposlProfileDeinit() {}
extern "C" void IposlProfileStopClient() {}
extern "C" void IposlProfileStopServer() {}
extern "C" int32_t IposlProfileStartServer(const uint8_t *,uint8_t,uint8_t,uint64_t g) { profileGeneration=g; ++starts; return 0; }
extern "C" int32_t IposlProfileStartTerminal(const uint8_t *,uint8_t,const uint8_t *,uint8_t,uint64_t g)
{ profileGeneration=g; ++starts; return 0; }
extern "C" int32_t IposlProfileProbePeer(const uint8_t *,uint8_t,uint8_t,uint64_t) { return 0; }
extern "C" int32_t IposlProfileSendIp(uint16_t,uint8_t,uint8_t,const uint8_t *,uint16_t,uint64_t) { return 0; }
extern "C" uint32_t QOSM_TransChannelCreate(const QOSM_TransChannelParams_S *) { return 0; }
extern "C" uint32_t QOSM_TransChannelDestroy(const QOSM_TransChannelReleaseParams_S *) { return 0; }
namespace OHOS::Nearlink {
NearlinkIpShareTun::~NearlinkIpShareTun() {}
int32_t NearlinkIpShareTun::Open(const PacketCallback &) { fd_=1; return 0; }
void NearlinkIpShareTun::Close() { fd_=-1; }
int32_t NearlinkIpShareTun::Write(const uint8_t *,uint16_t) { ++writes; return 0; }
bool NearlinkIpShareTun::IsOpen() const { return fd_>=0; }
bool NearlinkIpShareTun::ParseIpv6Evidence(const std::string &,uint32_t,uint8_t *) { return false; }
bool NearlinkIpShareTun::IsIpv6AddressUsable(const uint8_t *) { return true; }
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
    QOSM_TransChannelRspParams_S rsp={}; memcpy(rsp.addr.addr,peer,6);
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
    assert(c.IsAcceptingPort(c.IP_SHARE_PORT));
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
}
