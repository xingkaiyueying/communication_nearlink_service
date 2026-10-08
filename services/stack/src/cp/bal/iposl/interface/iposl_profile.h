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
#ifndef IPOSL_PROFILE_H
#define IPOSL_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IPOSL_IDENTIFIER_SERVICE_UUID "8f6f1d00-7b0c-4a73-9d4e-6e6561726c01"
#define IPOSL_CONFIG_SERVICE_UUID "5b285005-4555-4fa5-b957-c29c0d8a60c3"
#define IPOSL_TERMINAL_CAPABILITY_UUID "fa361b52-cb0a-49ed-8427-1a675accd9db"
#define IPOSL_TERMINAL_STATE_UUID "e4720ab0-e62a-42d2-ad96-98627ceeaedb"
#define IPOSL_GATEWAY_CAPABILITY_UUID "2e68c566-e314-466d-9629-402f16cab219"
#define IPOSL_GATEWAY_STATE_UUID "43117fb2-909e-4b70-b6f9-1052d729c89b"
#define IPOSL_NODE_CONFIG_METHOD_UUID "7aa3120e-f0d2-4560-b711-a5b618b7a32b"

#define IPOSL_IP_TYPE_IPV4 0x01u
#define IPOSL_IP_TYPE_DUAL_STACK 0x03u
#define IPOSL_OPCODE_CONFIGURE 0x01u
#define IPOSL_OPCODE_ENABLE 0x02u
#define IPOSL_MTU 1500u
#define IPOSL_LAYER2_ID_LEN 6u

enum {
    IPOSL_SUCCESS = 0,
    IPOSL_ERR_INVALID_PARAM = -1,
    IPOSL_ERR_INVALID_STATE = -2,
    IPOSL_ERR_SSAP = -3,
    IPOSL_ERR_NOT_SUPPORTED = -4,
    /* Private Demo response 0xfe: previous peer session is still draining. */
    IPOSL_ERR_PEER_DRAINING = -6,
};

typedef struct IposlProfileCallbacks {
    void (*onPeerSupported)(const uint8_t peer[IPOSL_LAYER2_ID_LEN], bool supported, int32_t error, uint8_t peerModes, bool known, uint64_t generation);
    void (*onConfigured)(const uint8_t peer[IPOSL_LAYER2_ID_LEN], bool opened, int32_t error, uint8_t mode, uint64_t generation);
    /* Synchronous resource admission on CP, before a successful wire response. mode=0 rolls back. */
    int32_t (*prepareMode)(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t mode, uint64_t generation);
    bool (*isSecure)(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint64_t generation);
    bool (*canSend)(uint16_t lcid, uint8_t tcid, uint8_t pi, uint64_t generation);
    /* Terminal-only result of reading the gateway's complete mode bitmap. */
    void (*onPeerCapabilities)(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t peerModes, uint64_t generation);
    bool (*isSecureAddress)(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, uint64_t generation);
} IposlProfileCallbacks;

int32_t IposlProfileInit(const IposlProfileCallbacks *callbacks);
void IposlProfileDeinit(void);
int32_t IposlProfileStartServerAny(uint8_t mode, uint32_t capacity, uint64_t generation);
void IposlProfileReleaseServerPeer(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType);
int32_t IposlProfileStartServer(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, uint8_t mode, uint64_t generation);
void IposlProfileStopServer(void);
int32_t IposlProfileProbePeer(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, uint8_t mode, uint64_t generation);
int32_t IposlProfileStartTerminal(const uint8_t gateway[IPOSL_LAYER2_ID_LEN], uint8_t addressType,
    const uint8_t localLayer2[IPOSL_LAYER2_ID_LEN], uint8_t mode, uint64_t generation);
void IposlProfileStopClient(void);
/* Called from the TUN thread; copies data and serializes DTAP access on CP. */
int32_t IposlProfileSendIpv4(uint16_t lcid, uint8_t tcid, const uint8_t *data, uint16_t length);
int32_t IposlProfileSendIp(uint16_t lcid, uint8_t tcid, uint8_t pi, const uint8_t *data,
    uint16_t length, uint64_t generation);
void IposlCodecS1LinkLocal(const uint8_t layer2[6], uint8_t address[16]);
bool IposlCodecValidatePacket(uint8_t pi, const uint8_t *data, size_t length);
int32_t IposlCodecEncodeConfigMode(const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t mode,
    uint8_t *out, size_t outLen);
bool IposlCodecGatewayModes(const uint8_t *data, size_t length, uint8_t *modes);
uint8_t IposlCodecSelectMode(uint8_t requested, uint8_t peerModes);
bool IposlCodecMayFallback(uint8_t opcode, uint8_t mode, uint8_t result, bool alreadyRetried, bool secure);
uint16_t IposlProfileIdentityServiceMemberCount(void);
uint8_t IposlProfileDataProtocolIndicator(void);

int32_t IposlCodecEncodeConfigRequest(const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t *out, size_t outLen);
int32_t IposlCodecEncodeOpenRequest(const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t *out, size_t outLen);
int32_t IposlCodecEncodeResponse(uint8_t opcode, const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t result,
    uint8_t *out, size_t outLen);
int32_t IposlCodecDecodeRequest(const uint8_t *data, size_t len, uint8_t *opcode,
    uint8_t layer2[IPOSL_LAYER2_ID_LEN]);
int32_t IposlCodecDecodeResponse(const uint8_t *data, size_t len, uint8_t expectedOpcode,
    uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t *result);
bool IposlCodecVerifyGoldenVectors(void);

#ifdef __cplusplus
}
#endif
#endif  // IPOSL_PROFILE_H
