#include <cassert>
#include "nearlink_ipshare_status.cpp"
#include "nearlink_service_ipc_interface_code.h"
using namespace OHOS;
using namespace OHOS::Nearlink;
static_assert(NL_IPSHARE_IS_PEER_SUPPORTED == 0 && NL_IPSHARE_START_GATEWAY == 1 &&
    NL_IPSHARE_START_TERMINAL == 2 && NL_IPSHARE_STOP == 3 && NL_IPSHARE_GET_STATUS == 4 &&
    NL_IPSHARE_REGISTER_OBSERVER == 5 && NL_IPSHARE_UNREGISTER_OBSERVER == 6 &&
    NL_IPSHARE_QUERY_CAPABILITIES == 7 && NL_IPSHARE_START_GATEWAY_WITH_MODE == 8 &&
    NL_IPSHARE_START_TERMINAL_WITH_MODE == 9);
int main()
{
    NearlinkIpShareStatus value;
    value.role = NearlinkIpShareRole::TERMINAL; value.state = NearlinkIpShareState::CHANNEL_READY;
    value.peerAddress = "02:11:22:33:44:55"; value.ifaceName = "sleip0"; value.contextId = "local-context";
    value.generation = 0x123456789abcdefULL; value.sequence = 17;
    value.requestedMode = value.selectedMode = NearlinkIpShareMode::DUAL_STACK;
    Parcel good; assert(value.Marshalling(good));
    for (size_t n = 0; n < good.bytes.size(); ++n) {
        Parcel truncated; truncated.bytes.assign(good.bytes.begin(),good.bytes.begin()+n);
        NearlinkIpShareStatus target; target.contextId = "sentinel";
        assert(!target.ReadFromParcel(truncated) && target.contextId == "sentinel");
    }
    NearlinkIpShareStatus copy; assert(copy.ReadFromParcel(good));
    assert(copy.generation == value.generation && copy.sequence == 17 && copy.selectedMode == value.selectedMode);
    value.peerAddress.assign(18,'x'); Parcel bad; assert(!value.Marshalling(bad));
    value.peerAddress.clear(); value.requestedMode = NearlinkIpShareMode::IPV4;
    assert(!value.Marshalling(bad));
    NearlinkIpShareCapabilities cap; cap.identifierPresent = true; cap.discoveryState = 1;
    cap.peerModes = {1,3}; cap.peerCapabilityKnown = true;
    Parcel caps; assert(cap.Marshalling(caps));
    for (size_t n = 0; n < caps.bytes.size(); ++n) {
        Parcel truncated; truncated.bytes.assign(caps.bytes.begin(),caps.bytes.begin()+n);
        NearlinkIpShareCapabilities target;
        assert(!target.ReadFromParcel(truncated) && !target.peerCapabilityKnown);
    }
    for (int count : {-1,3,0x7fffffff}) {
        Parcel malformed; malformed.WriteBool(true); malformed.WriteInt32(1); malformed.WriteInt32(count);
        NearlinkIpShareCapabilities target; assert(!target.ReadFromParcel(malformed));
    }
    Parcel oversized; oversized.bytes.resize(65537);
    NearlinkIpShareStatus target; assert(!target.ReadFromParcel(oversized));
    cap.peerModes = {1,1}; Parcel duplicate; assert(!cap.Marshalling(duplicate));
    NearlinkIpShareAddressEvidence evidence;
    evidence.generation = 3; evidence.sequence = 1; evidence.address = "fd77::2";
    evidence.ifindex = 7; evidence.prefixLength = 64; evidence.preferredLifetime = 10; evidence.validLifetime = 30;
    Parcel addressParcel; assert(evidence.Write(addressParcel));
    for (size_t n = 0; n < addressParcel.bytes.size(); ++n) {
        Parcel truncated; truncated.bytes.assign(addressParcel.bytes.begin(), addressParcel.bytes.begin() + n);
        NearlinkIpShareAddressEvidence result; result.address = "unchanged";
        assert(!result.Read(truncated) && result.address == "unchanged");
    }
    NearlinkIpShareAddressEvidence decoded; assert(decoded.Read(addressParcel) && decoded.validLifetime == 30);
    evidence.preferredLifetime = 31; Parcel invalid; assert(!evidence.Write(invalid));
    invalid.bytes = addressParcel.bytes;
    uint32_t badPreferred = 31;
    memcpy(invalid.bytes.data() + invalid.bytes.size() - 8, &badPreferred, sizeof(badPreferred));
    assert(!decoded.Read(invalid));
}
