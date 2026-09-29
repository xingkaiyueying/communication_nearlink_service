#include "ipv6_test_packets.h"
int main()
{
    Policy policy;
    auto accept = [&](std::vector<uint8_t> p, bool terminal, uint64_t now = 1) {
        return policy.Authorize(p.data(), p.size(), terminal, terminal ? terminalId : gatewayId, now);
    };
    assert(!accept(Echo(Global(2), Global(1)), true)); // no arbitrary first-source learning
    auto ra = Ra(); assert(accept(ra, false)); assert(!accept(ra, true));
    for (size_t size = 40; size < ra.size(); ++size) {
        auto truncated = ra; truncated.resize(size); assert(!accept(truncated, false));
    }
    auto bad = ra; bad[7] = 254; assert(!accept(bad, false));
    bad = ra; bad[42] ^= 1; assert(!accept(bad, false));
    bad = ra; bad[95] ^= 1; Checksum(bad); assert(!accept(bad, false));
    bad = ra; bad[89] = 0; Checksum(bad); assert(!accept(bad, false));
    auto dad = Dad(Global(2)); assert(accept(dad, true));
    assert(policy.Mappings().back().confirmed == false);
    bad = dad; bad.resize(72); bad[5] = 32; bad[64] = 1; bad[65] = 1;
    std::copy(terminalId, terminalId + 6, bad.begin() + 66); Checksum(bad);
    assert(!accept(bad, true)); // DAD must not carry SLLAO
    assert(accept(Echo(Global(2), Global(1)), true));
    assert(policy.Mappings().back().confirmed);
    auto fragment = Echo(Global(2), Global(1));
    fragment.resize(64, 0); fragment[5] = 24; fragment[6] = 44;
    std::fill(fragment.begin() + 40, fragment.end(), 0);
    fragment[40] = 17; fragment[43] = 1;
    assert(accept(fragment, true)); // formal TCP/UDP mapping delegates reassembly to kernel
    fragment[23] = 99; assert(!accept(fragment, true)); // fragment cannot create/promote mapping
    fragment[23] = 2; fragment[40] = 58; fragment[48] = 135;
    assert(!accept(fragment, true)); // ND fragments cannot reach the control path
    auto echoFragment = Echo(Global(2), Global(1));
    echoFragment.insert(echoFragment.begin() + 40, 8, 0);
    echoFragment[5] = echoFragment.size() - 40; echoFragment[6] = 44; echoFragment[40] = 58;
    assert(accept(echoFragment, true)); // checksummed atomic Echo fragment
    auto remote = Global(90); remote[0] = 0x20; remote[1] = 1;
    for (uint8_t type : {1, 2, 3, 4}) {
        auto error = Packet(type, remote, Global(2), 56); error[7] = 64; Checksum(error);
        assert(accept(error, false));
        error[39] = 99; Checksum(error); assert(!accept(error, false));
    }
    auto opaque = Echo(remote, Global(2)); opaque[6] = 253;
    assert(accept(opaque, false)); // confirmed data is independent of the upper protocol
    opaque = Echo(Global(2), remote); opaque[6] = 253; assert(accept(opaque, true));
    for (uint8_t i = 3; i < 10; ++i) {
        assert(accept(Dad(Global(i)), true)); assert(accept(Echo(Global(i), Global(1)), true));
    }
    assert(accept(Dad(Global(10)), true)); assert(!accept(Echo(Global(10), Global(1)), true));
    assert(accept(Echo(Global(2), Global(1)), true)); // full mapping table preserves old traffic
    policy.Reset(); assert(accept(Ra(), false)); assert(accept(Dad(Global(2)), true));
    assert(accept(Dad(Global(2)), false)); assert(!accept(Echo(Global(2), Global(1)), true)); // simultaneous DAD
    policy.Reset(); assert(accept(Ra(), false));
    assert(policy.ApplyLocal(Global(1), false, 0, 30, 90, 1)); // gateway kernel owns ::1
    assert(accept(Dad(Global(1)), true));
    assert(accept(Na(Global(1), Group(1)), false)); // authenticated owner defeats A's DAD candidate
    assert(!accept(Echo(Global(1), Global(1)), true));
    policy.Reset(); assert(accept(Ra(), false)); assert(accept(Dad(Global(1)), true));
    assert(accept(Na(Global(1), Group(1)), false)); // peer owner may be learned from NA during local DAD
    assert(!accept(Echo(Global(1), Global(1)), true));
    policy.Reset(); assert(accept(Ra(), false)); assert(accept(Dad(Global(2)), true));
    policy.Expire(62); assert(!accept(Echo(Global(2), Global(1)), true, 62)); // silence cannot confirm
    policy.Reset(); assert(accept(Ra(), false)); assert(accept(Dad(Global(2)), true));
    assert(accept(Echo(Global(2), Global(1)), true));
    policy.Expire(32); assert(policy.Mappings().back().preferredUntil == 31);
    assert(accept(Echo(Global(2), Global(1)), true, 32)); // deprecated still valid
    policy.Expire(92); assert(!accept(Echo(Global(2), Global(1)), true, 92));
    policy.Reset(); assert(accept(Ra(), false));
    assert(policy.ApplyLocal(Global(2), true, 0x40, 20, 40, 1));
    assert(!policy.Mappings().back().confirmed);
    assert(policy.ApplyLocal(Global(2), true, 0, 20, 40, 2));
    assert(policy.Mappings().back().confirmed && policy.Mappings().back().validUntil == 42);
    assert(policy.LocalUsable(Global(2), true, 2));
    assert(!policy.LocalUsable(Global(2), true, 3)); // cache expires without extending the lease
    assert(policy.ObserveKernelLocal(Global(2), true, 3));
    assert(policy.LocalUsable(Global(2), true, 3));
    assert(policy.ApplyLocal(Global(2), true, 0, 0, 0, 3));
    assert(!accept(Echo(Global(2), Global(1)), true, 3));
    assert(!policy.LocalUsable(Global(2), true, 3));
    auto rs = Packet(133, Lla(2), Group(2), 8); Checksum(rs);
    assert(Policy::AddLayer2Option(rs, terminalId) && rs.size() == 56);
    assert(accept(rs, true));
    auto dadOption = Dad(Lla(3)); auto original = dadOption;
    assert(Policy::AddLayer2Option(dadOption, terminalId) && dadOption == original);
    std::cout << "IPv6 control, malformed packets, DAD conflict, first-source, capacity, lifetimes PASS\n";
}
