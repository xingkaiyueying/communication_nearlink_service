/* Copyright (C) 2026 Huawei Device Co., Ltd. SPDX-License-Identifier: Apache-2.0 */
#ifndef SLEIP_PROBE_PACKETS_H
#define SLEIP_PROBE_PACKETS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "iposl_profile.h"

/* Test traffic only: never install these addresses or a route in the system. */
static void ProbePut16(uint8_t *p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static uint16_t ProbeChecksum(const uint8_t *p, size_t length, uint32_t sum)
{
    for (size_t i = 0; i < length; i += 2) sum += ((uint32_t)p[i] << 8) | (i + 1 < length ? p[i + 1] : 0);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
static void ProbeIpv4(uint8_t *p, size_t length, bool gateway)
{
    p[0] = 0x45; ProbePut16(p + 2, length); p[8] = 64; p[9] = 17;
    p[12] = 192; p[13] = 168; p[14] = 77; p[15] = gateway ? 1 : 2;
    p[16] = 192; p[17] = 168; p[18] = 77; p[19] = gateway ? 2 : 1;
    ProbePut16(p + 10, ProbeChecksum(p, 20, 0));
}
static void ProbeDhcp(uint8_t p[300], uint8_t message, const uint8_t terminal[6])
{
    memset(p, 0, 300);
    bool reply = message == 2 || message == 5;
    ProbeIpv4(p, 300, reply);
    memset(p + 10, 0, 2);
    if (!reply) memset(p + 12, 0, 4);
    memset(p + 16, 255, 4);
    ProbePut16(p + 10, ProbeChecksum(p, 20, 0));
    ProbePut16(p + 20, reply ? 67 : 68); ProbePut16(p + 22, reply ? 68 : 67);
    ProbePut16(p + 24, 280);
    p[28] = reply ? 2 : 1; p[29] = 1; p[30] = 6; p[35] = 42;
    if (reply) { p[44] = 192; p[45] = 168; p[46] = 77; p[47] = 2; }
    memcpy(p + 56, terminal, 6);
    p[264] = 99; p[265] = 130; p[266] = 83; p[267] = 99;
    size_t i = 268;
    p[i++] = 53; p[i++] = 1; p[i++] = message;
    p[i++] = 50; p[i++] = 4; p[i++] = 192; p[i++] = 168; p[i++] = 77; p[i++] = 2;
    p[i++] = 54; p[i++] = 4; p[i++] = 192; p[i++] = 168; p[i++] = 77; p[i++] = 1;
    p[i++] = 51; p[i++] = 4; p[i++] = 0; p[i++] = 0; p[i++] = 0; p[i++] = 120;
    p[i++] = 1; p[i++] = 4; p[i++] = 255; p[i++] = 255; p[i++] = 255; p[i++] = 0;
    p[i] = 255;
}
static void ProbeData(uint8_t p[1500], uint8_t pi, bool gateway, const uint8_t local[6], const uint8_t peer[6],
    uint8_t sequence)
{
    memset(p, 0, 1500);
    size_t header = pi == 1 ? 20 : 40;
    if (pi == 1) ProbeIpv4(p, 1500, gateway);
    else {
        p[0] = 0x60; ProbePut16(p + 4, 1460); p[6] = 17; p[7] = 64;
        IposlCodecS1LinkLocal(local, p + 8); IposlCodecS1LinkLocal(peer, p + 24);
    }
    ProbePut16(p + header, 30201); ProbePut16(p + header + 2, 30201);
    ProbePut16(p + header + 4, 1500 - header);
    for (size_t i = header + 8; i < 1500; ++i) p[i] = (uint8_t)(i + sequence);
    if (pi == 2) {
        uint32_t sum = 17 + 1460;
        for (size_t i = 8; i < 40; i += 2) sum += ((uint32_t)p[i] << 8) | p[i + 1];
        uint16_t checksum = ProbeChecksum(p + 40, 1460, sum);
        ProbePut16(p + 46, checksum == 0 ? 65535 : checksum);
    }
}
#endif
