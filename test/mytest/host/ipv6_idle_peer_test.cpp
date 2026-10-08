#include "ipv6_test_packets.h"

int main()
{
    // Real policy for two separate /64 links; no sleep or platform network mocks.
    for (uint8_t slot = 0; slot < 2; ++slot) {
        Policy policy;
        Address address = Global(2), remote = Global(90), prefix = Global(0);
        prefix[7] = address[7] = slot;
        remote[0] = 0x20;
        auto advertise = [&](uint64_t now, uint32_t valid = 300) {
            auto ra = Ra(valid ? 180 : 0, valid);
            std::copy(prefix.begin(), prefix.end(), ra.begin() + 72);
            Checksum(ra);
            return policy.Authorize(ra.data(), ra.size(), false, gatewayId, now);
        };
        auto send = [&](std::vector<uint8_t> packet, bool terminal, uint64_t now) {
            return policy.Authorize(packet.data(), packet.size(), terminal,
                                    terminal ? terminalId : gatewayId, now);
        };
        auto mapping = [&]() -> const Policy::Mapping * {
            for (const auto &m : policy.Mappings()) {
                if (m.address == address && m.terminal) return &m;
            }
            return nullptr;
        };
        assert(advertise(1));
        assert(send(Dad(address), true, 2));
        for (uint64_t now : {62, 182, 302, 422}) assert(advertise(now));
        policy.Expire(482);
        assert(mapping()); // old production forgets this address at the first RA after 60 seconds
        assert(!mapping()->confirmed); // time and RA never finish DAD
        assert(!send(Echo(remote, address), false, 482));
        auto corrupt = Echo(address, remote);
        corrupt[42] ^= 1;
        assert(!send(corrupt, true, 482));
        assert(mapping() && !mapping()->confirmed);
        Address arbitrary = address;
        arbitrary[15] = 99;
        assert(!send(Echo(arbitrary, remote), true, 482));
        auto fragment = Echo(address, remote);
        fragment.insert(fragment.begin() + 40, 8, 0);
        fragment[5] = fragment.size() - 40;
        fragment[6] = 44;
        fragment[40] = 58;
        assert(!send(fragment, true, 482)); // tentative source cannot be promoted by a fragment
        assert(send(Echo(address, remote), true, 482));
        assert(mapping()->confirmed);
        assert(send(Echo(remote, address), false, 482));
        policy.Reset();
        assert(!send(Echo(address, remote), true, 483));

        assert(advertise(500));
        assert(send(Dad(address), true, 501));
        assert(advertise(562, 0));
        assert(!send(Echo(address, remote), true, 563)); // unconfirmed withdrawn address is revoked
        policy.Reset();
        assert(advertise(600));
        assert(send(Dad(address), true, 601));
        assert(!send(Echo(address, remote), true, 901)); // no renewal: valid lifetime ends
        policy.Reset();
        assert(advertise(1000));
        assert(send(Dad(address), true, 1001));
        assert(send(Dad(address), false, 1001));
        assert(advertise(1062));
        assert(!send(Echo(address, remote), true, 1063)); // conflicts survive idle time
        policy.Reset();
        assert(advertise(1100));
        assert(policy.ApplyLocal(address, true, 0x40, 180, 300, 1101));
        assert(!send(Echo(address, remote), true, 1162)); // local tentative evidence is not wire DAD
        assert(send(Dad(Lla(2)), true, 1163));
        policy.Expire(1224);
        auto solicit = Packet(135, Lla(2), Lla(1), 24);
        auto target = Lla(1);
        std::copy(target.begin(), target.end(), solicit.begin() + 48);
        Checksum(solicit);
        assert(Policy::AddLayer2Option(solicit, terminalId));
        assert(send(solicit, true, 1224)); // late NUD still sees the peer's DAD evidence
        assert(!send(Echo(Lla(2), remote), true, 1224)); // link-local cannot escape onto the Internet
        assert(send(Echo(Lla(2), Lla(1)), true, 1224)); // valid first local data, not elapsed time
        policy.Reset();
        assert(!send(solicit, true, 1225));
        for (uint8_t i = 1; i <= 16; ++i) assert(send(Dad(Lla(i)), true, 1300));
        policy.Expire(1400);
        assert(!send(Dad(Lla(17)), true, 1400)); // retained tentative evidence remains capacity-bounded
        policy.Reset();
        assert(send(Dad(Lla(17)), true, 1401));
    }
    std::cout << "two_idle_peers=PASS (late echo, RA renewal, checksum/fragment/conflict/expiry/reset)\n";
}
