/*
 * Copyright (C) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <poll.h>
#include <chrono>
#include "sleip_probe_packets.h"

#include "iposl_profile.h"
#include "nearlink_ipshare_client_c.h"

#include "accesstoken_kit.h"
#include "nativetoken_kit.h"
#include "token_setproc.h"

static int SetProbeToken(void)
{
    const char *permissions[] = { "ohos.permission.ACCESS_NEARLINK", "ohos.permission.CONNECTIVITY_INTERNAL" };
    NativeTokenInfoParams info = {};
    info.permsNum = sizeof(permissions) / sizeof(permissions[0]);
    info.perms = permissions;
    info.processName = "sleip_nearlink_stage1";
    info.aplStr = "system_core";
    uint64_t token = GetAccessTokenId(&info);
    if (token == 0) return -1;
    int ret = SetSelfTokenID(token);
    return ret == 0 ? OHOS::Security::AccessToken::AccessTokenKit::ReloadNativeTokenInfo() : ret;
}

#define WAIT_COUNT 300
#define WAIT_INTERVAL_US 200000

static const char *StateName(int32_t state)
{
    static const char *names[] = {
        "IDLE", "STARTING", "DISCOVERING", "CONFIGURING", "IFACE_READY", "CHANNEL_READY",
        "DHCP", "SERVING", "SERVING_NO_UPSTREAM", "ACTIVE", "STOPPING", "ERROR"
    };
    if (state < 0 || state >= (int32_t)(sizeof(names) / sizeof(names[0]))) {
        return "UNKNOWN";
    }
    return names[state];
}

static void MaskAddress(const char *address, char masked[NL_IPSHARE_ADDRESS_TEXT_LEN])
{
    if (address == NULL || strlen(address) != 17) {
        (void)snprintf(masked, NL_IPSHARE_ADDRESS_TEXT_LEN, "**:**:**:**:**:**");
        return;
    }
    (void)snprintf(masked, NL_IPSHARE_ADDRESS_TEXT_LEN, "**:**:**:%s", address + 9);
}

static int PrintStatusResult(int32_t commandResult, bool failOnErrorState)
{
    NlIpShareStatusC status = {0};
    int32_t statusResult = NlIpShareGetStatus(&status);
    int32_t code = commandResult != 0 ? commandResult : statusResult;
    bool passed = code == 0 && (!failOnErrorState || status.state != 11);
    char masked[NL_IPSHARE_ADDRESS_TEXT_LEN] = {0};
    MaskAddress(status.peerAddress, masked);
    printf("generation=%llu sequence=%llu requestedMode=%d selectedMode=%d serviceReady=%d\n",
        (unsigned long long)status.generation, (unsigned long long)status.sequence, status.requestedMode, status.selectedMode, status.serviceReady);
    printf("peer=%s iface=%s\n", masked, status.ifaceName[0] == '\0' ? "-" : status.ifaceName);
    printf("RESULT=%s state=%s code=%d\n", passed ? "PASS" : "FAIL", StateName(status.state),
        passed ? status.errorCode : (code != 0 ? code : status.errorCode));
    return passed ? 0 : 1;
}

static int WaitForState(int32_t wanted)
{
    NlIpShareStatusC status = {0};
    for (int i = 0; i < WAIT_COUNT; ++i) {
        int32_t ret = NlIpShareGetStatus(&status);
        if (ret != 0) {
            return PrintStatusResult(ret, true);
        }
        if (status.state == wanted) {
            return PrintStatusResult(0, true);
        }
        if (status.state == 11) {
            return PrintStatusResult(status.errorCode, true);
        }
        usleep(WAIT_INTERVAL_US);
    }
    return PrintStatusResult(-1, true);
}

static int RunVector(void)
{
    uint8_t layer2[6] = {2,1,2,3,4,5}, out[11], packet[1500], modes = 0;
    uint8_t cap[] = {1,0,9,1,3,2,5,220,3,1,4,3};
    bool p2 = IposlCodecEncodeConfigMode(layer2, 3, out, sizeof(out)) == 11 && out[10] == 3 &&
        IposlCodecGatewayModes(cap, sizeof(cap), &modes) && modes == 3 &&
        IposlCodecSelectMode(3, 1) == 1 && IposlCodecSelectMode(3, 3) == 3 &&
        IposlCodecSelectMode(3, 0) == 0 && IposlCodecMayFallback(1,3,255,false,true) &&
        !IposlCodecMayFallback(1,3,255,true,true) && !IposlCodecMayFallback(1,3,255,false,false);
    ProbeData(packet, 2, false, layer2, layer2, 1);
    p2 = p2 && IposlCodecValidatePacket(2,packet,1500) && !IposlCodecValidatePacket(1,packet,1500) &&
        !IposlCodecValidatePacket(3,packet,1500) && !IposlCodecValidatePacket(2,packet,1499);
    printf("p2_codec_policy_vectors=%s (local-only; no IPC/channel/device proof)\n", p2 ? "PASS" : "FAIL");
    bool passed = p2 && IposlCodecVerifyGoldenVectors() && IposlProfileIdentityServiceMemberCount() == 0 &&
        IposlProfileDataProtocolIndicator() == 0x01 && IPOSL_IP_TYPE_IPV4 == 0x01 &&
        strcmp(IPOSL_IDENTIFIER_SERVICE_UUID, "8f6f1d00-7b0c-4a73-9d4e-6e6561726c01") == 0;
    printf("identity_members=0 pi=0x%02x opcode_config=0x%02x opcode_enable=0x%02x\n",
        IPOSL_IP_TYPE_IPV4, IPOSL_OPCODE_CONFIGURE, IPOSL_OPCODE_ENABLE);
    printf("RESULT=%s state=VECTOR code=%d\n", passed ? "PASS" : "FAIL", passed ? 0 : -1);
    return passed ? 0 : 1;
}

static int RunSupport(const char *address)
{
    int32_t supported = 0;
    int32_t ret = NlIpShareIsPeerSupported(address, &supported);
    char masked[NL_IPSHARE_ADDRESS_TEXT_LEN] = {0};
    MaskAddress(address, masked);
    printf("peer=%s supported=%s\n", masked, supported ? "true" : "false");
    printf("RESULT=%s state=%s code=%d\n", ret == 0 && supported ? "PASS" : "FAIL",
        ret == 0 ? "IDLE" : "ERROR", ret == 0 && supported ? 0 : (ret != 0 ? ret : -4));
    return ret == 0 && supported ? 0 : 1;
}

static bool ParseLayer2(const char *text, uint8_t out[6])
{
    unsigned a[6]; char tail;
    if (strlen(text) != 17 || sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c", &a[0], &a[1], &a[2],
        &a[3], &a[4], &a[5], &tail) != 6) return false;
    for (size_t i = 0; i < 6; ++i) out[i] = (uint8_t)a[i];
    return true;
}

static bool SameContext(uint64_t generation)
{
    NlIpShareStatusC status = {};
    return NlIpShareGetStatus(&status) == 0 && status.generation == generation && status.state == 5;
}

static bool SendProbe(int fd, int index, const uint8_t *packet, size_t length, uint64_t generation)
{
    if (!SameContext(generation)) return false;
    struct sockaddr_ll address = {};
    address.sll_family = AF_PACKET; address.sll_ifindex = index;
    address.sll_protocol = htons(packet[0] >> 4 == 6 ? ETH_P_IPV6 : ETH_P_IP);
    return sendto(fd, packet, length, 0, (struct sockaddr *)&address, sizeof(address)) == (ssize_t)length;
}

static bool ReceiveProbe(int fd, const uint8_t *expected, size_t length, uint64_t generation)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    uint8_t packet[1600];
    while (std::chrono::steady_clock::now() < deadline) {
        if (!SameContext(generation)) return false;
        struct pollfd item = {fd, POLLIN, 0};
        int ret = poll(&item, 1, 500);
        if (ret < 0 && errno != EINTR) return false;
        if (ret <= 0 || !(item.revents & POLLIN)) continue;
        struct sockaddr_ll source = {}; socklen_t size = sizeof(source);
        ssize_t got = recvfrom(fd, packet, sizeof(packet), 0, (struct sockaddr *)&source, &size);
        if (source.sll_pkttype != PACKET_OUTGOING && got == (ssize_t)length &&
            memcmp(packet, expected, length) == 0) return true;
    }
    return false;
}

static int RunNegative(const char *localText, const char *peerText)
{
    uint8_t local[6], peer[6], statusPeer[6];
    NlIpShareStatusC status = {};
    if (!ParseLayer2(localText, local) || !ParseLayer2(peerText, peer) || NlIpShareGetStatus(&status) != 0 ||
        status.state != 5 || status.selectedMode != 3 || !ParseLayer2(status.peerAddress, statusPeer) ||
        memcmp(peer, statusPeer, 6) != 0) return 2;
    unsigned index = if_nametoindex(status.ifaceName);
    int fd = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (!index || fd < 0) { if (fd >= 0) close(fd); return 1; }
    const char *names[] = {"short-length", "bad-version", "unconfirmed-fragment", "wrong-source", "oversize"};
    unsigned injected = 0;
    for (unsigned i = 0; i < 5; ++i) {
        uint8_t packet[1501] = {}; size_t size = 1500;
        ProbeData(packet, 2, status.role == 1, local, peer, 99);
        if (i == 0) size = 1499;
        if (i == 1) packet[0] = 0x50;
        if (i == 2) packet[6] = 44;
        if (i == 3) packet[8] ^= 1;
        if (i == 4) { size = 1501; ProbePut16(packet + 4, 1461); }
        bool sent = SendProbe(fd, index, packet, size, status.generation);
        if (sent) ++injected;
        printf("negative=%s outcome=%s errno=%d\n", names[i], sent ? "INJECTED" : "LOCAL_REJECT", sent ? 0 : errno);
    }
    close(fd);
    printf("injected=%u RESULT=REVIEW_REJECTION_LOGS; this command does not prove peer-side rejection\n", injected);
    return injected != 0 ? 0 : 1;
}


static int RunBearer(const char *role, const char *localText, const char *peerText)
{
    bool gateway = strcmp(role, "gateway") == 0;
    uint8_t local[6], peer[6], statusPeer[6];
    NlIpShareStatusC status = {};
    if ((!gateway && strcmp(role, "terminal") != 0) || !ParseLayer2(localText, local) ||
        !ParseLayer2(peerText, peer) || NlIpShareGetStatus(&status) != 0 ||
        status.state != 5 || status.role != (gateway ? 1 : 2) ||
        !ParseLayer2(status.peerAddress, statusPeer) || memcmp(peer, statusPeer, 6) != 0) return 2;
    unsigned index = if_nametoindex(status.ifaceName);
    int fd = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (!index || fd < 0) { if (fd >= 0) close(fd); return 1; }
    struct sockaddr_ll address = {};
    address.sll_family = AF_PACKET; address.sll_ifindex = index; address.sll_protocol = htons(ETH_P_ALL);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) { close(fd); return 1; }
    printf("bearer_ready role=%s generation=%llu mode=%d; no addresses/routes installed\n",
        role, (unsigned long long)status.generation, status.selectedMode);
    fflush(stdout);
    bool ok = true;
    uint8_t dhcp[300];
    const uint8_t *terminal = gateway ? peer : local;
    const uint8_t messages[] = {1, 2, 3, 5};
    for (size_t i = 0; i < 4 && ok; ++i) {
        ProbeDhcp(dhcp, messages[i], terminal);
        bool send = gateway == (i % 2 == 1);
        ok = send ? SendProbe(fd, index, dhcp, sizeof(dhcp), status.generation) :
            ReceiveProbe(fd, dhcp, sizeof(dhcp), status.generation);
    }
    for (uint8_t round = 0; round < 8 && ok; ++round) {
        for (uint8_t pi = 1; pi <= 1 && ok; ++pi) {
            uint8_t tx[1500], rx[1500];
            ProbeData(tx, pi, gateway, local, peer, round);
            ProbeData(rx, pi, !gateway, peer, local, round);
            if (gateway) ok = ReceiveProbe(fd, rx, sizeof(rx), status.generation) &&
                SendProbe(fd, index, tx, sizeof(tx), status.generation);
            else ok = SendProbe(fd, index, tx, sizeof(tx), status.generation) &&
                ReceiveProbe(fd, rx, sizeof(rx), status.generation);
            printf("round=%u pi=%u complete_sdu=1500 bidirectional=%s\n", round, pi, ok ? "PASS" : "FAIL");
            fflush(stdout);
        }
    }
    close(fd);
    printf("RESULT=%s state=BEARER_IPV4_ONLY (S2 IPv6 requires kernel DAD/SLAAC and real-address traffic)\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}


static void Usage(const char *program)
{
    fprintf(stderr, "usage: %s [--no-token] vector|status|stop|support ADDRESS|capabilities ADDRESS|gateway-start ADDRESS [ipv4|dual]|terminal-start ADDRESS [ipv4|dual]|bearer gateway|terminal LOCAL_L2 PEER_L2|negative LOCAL_L2 PEER_L2\n",
        program);
}

int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "vector") == 0) {
        return RunVector();
    }
    if (argc > 1 && strcmp(argv[1], "--no-token") == 0) {
        --argc;
        ++argv;
    } else if (SetProbeToken() != 0) {
        printf("RESULT=FAIL state=AUTH code=-1\n");
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        return PrintStatusResult(0, true);
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        int32_t ret = NlIpShareStop();
        return ret == 0 ? WaitForState(0) : PrintStatusResult(ret, true);
    }
    if (argc == 3 && strcmp(argv[1], "support") == 0) {
        return RunSupport(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "capabilities") == 0) {
        NlIpShareCapabilitiesC caps = {};
        int ret = NlIpShareQueryCapabilities(argv[2], &caps);
        printf("identifier=%d known=%d peerModeCount=%d modes=%d,%d code=%d\n", caps.identifierPresent,
            caps.peerCapabilityKnown, caps.peerModeCount, caps.peerModes[0], caps.peerModes[1], ret);
        return ret == 0 ? 0 : 1;
    }
    if (argc == 4 && (strcmp(argv[1], "gateway-start") == 0 || strcmp(argv[1], "terminal-start") == 0)) {
        int mode = strcmp(argv[3], "dual") == 0 ? 3 : strcmp(argv[3], "ipv4") == 0 ? 1 : 0;
        if (!mode) return 2;
        int ret = strcmp(argv[1], "gateway-start") == 0 ? NlIpShareStartGatewayWithMode(argv[2], mode) :
            NlIpShareStartTerminalWithMode(argv[2], mode);
        return ret == 0 ? WaitForState(5) : PrintStatusResult(ret, true);
    }
    if (argc == 4 && strcmp(argv[1], "negative") == 0) return RunNegative(argv[2], argv[3]);
    if (argc == 5 && strcmp(argv[1], "bearer") == 0) {
        return RunBearer(argv[2], argv[3], argv[4]);
    }
    if (argc == 3 && strcmp(argv[1], "gateway-start") == 0) {
        int32_t ret = NlIpShareStartGateway(argv[2]);
        return ret == 0 ? WaitForState(5) : PrintStatusResult(ret, true);
    }
    if (argc == 3 && strcmp(argv[1], "terminal-start") == 0) {
        int32_t ret = NlIpShareStartTerminal(argv[2]);
        return ret == 0 ? WaitForState(5) : PrintStatusResult(ret, true);
    }
    Usage(argv[0]);
    return 2;
}
