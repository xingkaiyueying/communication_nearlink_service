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
#ifndef NEARLINK_IPSHARE_CLIENT_C_H
#define NEARLINK_IPSHARE_CLIENT_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NL_IPSHARE_ADDRESS_TEXT_LEN 18
#define NL_IPSHARE_IFACE_TEXT_LEN 16
#define NL_IPSHARE_IPV4_TEXT_LEN 16
#define NL_IPSHARE_ERROR_STAGE_LEN 32

typedef struct NlIpShareStatusC {
    int32_t role;
    int32_t state;
    char peerAddress[NL_IPSHARE_ADDRESS_TEXT_LEN];
    char ifaceName[NL_IPSHARE_IFACE_TEXT_LEN];
    char ipv4Address[NL_IPSHARE_IPV4_TEXT_LEN];
    int32_t hasUpstream;
    char errorStage[NL_IPSHARE_ERROR_STAGE_LEN];
    int32_t errorCode;
    char contextId[65];
    uint64_t generation;
    uint64_t sequence;
    int32_t requestedMode;
    int32_t selectedMode;
    int32_t serviceReady;
} NlIpShareStatusC;

typedef struct NlIpShareCapabilitiesC {
    int32_t identifierPresent;
    int32_t discoveryState;
    int32_t localModes[2];
    int32_t localModeCount;
    int32_t peerModes[2];
    int32_t peerModeCount;
    int32_t peerCapabilityKnown;
} NlIpShareCapabilitiesC;

typedef struct NlIpShareIpv6AddressC {
    uint64_t generation, sequence;
    char address[46];
    uint32_t ifindex, prefixLength, flags, preferredLifetime, validLifetime;
} NlIpShareIpv6AddressC;
int32_t NlIpShareUpdateValidatedAddress(const NlIpShareIpv6AddressC *address);

int32_t NlIpShareQueryCapabilities(const char *peerAddress, NlIpShareCapabilitiesC *capabilities);
int32_t NlIpShareStartGatewayWithMode(const char *peerAddress, int32_t mode);
int32_t NlIpShareStartTerminalWithMode(const char *peerAddress, int32_t mode);

int32_t NlIpShareIsPeerSupported(const char *peerAddress, int32_t *supported);
int32_t NlIpShareStartGateway(const char *peerAddress);
int32_t NlIpShareStartTerminal(const char *gatewayAddress);
int32_t NlIpShareStop(void);
int32_t NlIpShareGetStatus(NlIpShareStatusC *status);

#ifdef __cplusplus
}
#endif
#endif  // NEARLINK_IPSHARE_CLIENT_C_H
