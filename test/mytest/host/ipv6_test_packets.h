#pragma once
#include <cassert>
#include <iostream>
#include "nearlink_ipshare_ipv6.h"
using Policy = OHOS::Nearlink::NearlinkIpShareIpv6;
using Address = Policy::Address;
static uint8_t terminalId[6] = {2, 1, 2, 3, 4, 5};
static uint8_t gatewayId[6] = {2, 6, 7, 8, 9, 10};
static Address Lla(uint8_t last) { Address a{}; a[0] = 0xfe; a[1] = 0x80; a[15] = last; return a; }
static Address Global(uint8_t last) { Address a{}; a[0] = 0xfd; a[1] = 0x77; a[15] = last; return a; }
static Address Group(uint8_t last) { Address a{}; a[0] = 0xff; a[1] = 2; a[15] = last; return a; }
static void Put32(uint8_t *p, uint32_t n) { p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n; }
static void Checksum(std::vector<uint8_t> &p)
{
    p[42] = p[43] = 0;
    uint32_t sum = p.size() - 40 + 58;
    for (size_t i = 8; i < p.size(); i += 2) sum += (uint16_t(p[i]) << 8) | (i + 1 < p.size() ? p[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    p[42] = (~sum) >> 8; p[43] = ~sum;
}
static std::vector<uint8_t> Packet(uint8_t type, Address source, Address dest, size_t payload)
{
    std::vector<uint8_t> p(40 + payload); p[0] = 0x60; p[4] = payload >> 8; p[5] = payload;
    p[6] = 58; p[7] = 255;
    std::copy(source.begin(), source.end(), p.begin() + 8);
    std::copy(dest.begin(), dest.end(), p.begin() + 24); p[40] = type;
    return p;
}
static std::vector<uint8_t> Dad(Address target)
{
    Address d = Group(0); d[11] = 1; d[12] = 0xff;
    std::copy(target.begin() + 13, target.end(), d.begin() + 13);
    auto p = Packet(135, {}, d, 24); std::copy(target.begin(), target.end(), p.begin() + 48); Checksum(p); return p;
}
static std::vector<uint8_t> Na(Address owner, Address destination)
{
    auto p = Packet(136, owner, destination, 24);
    p[44] = 0x20;
    std::copy(owner.begin(), owner.end(), p.begin() + 48);
    Checksum(p); return p;
}
static std::vector<uint8_t> Ra(uint32_t preferred = 30, uint32_t valid = 90)
{
    auto p = Packet(134, Lla(1), Group(1), 56);
    p[56] = 3; p[57] = 4; p[58] = 64; p[59] = 0xc0; Put32(&p[60], valid); Put32(&p[64], preferred);
    auto prefix = Global(0); std::copy(prefix.begin(), prefix.end(), p.begin() + 72);
    p[88] = 1; p[89] = 1; std::copy(gatewayId, gatewayId + 6, p.begin() + 90);
    Checksum(p); return p;
}
static std::vector<uint8_t> Echo(Address source, Address destination)
{
    auto p = Packet(128, source, destination, 16); Checksum(p); return p;
}
