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
#include "iposl_codec.h"

#include <string.h>

const uint8_t g_iposlTerminalCapability[IPOSL_TERMINAL_CAPABILITY_LEN] = {
    0x01, 0x00, 0x07, 0x01, 0x05, 0xDC, 0x02, 0x01, 0x03, 0x03
};
const uint8_t g_iposlEmptyState[IPOSL_EMPTY_STATE_LEN] = {0x01, 0x00, 0x00};
const uint8_t g_iposlGatewayCapability[IPOSL_GATEWAY_CAPABILITY_LEN] = {
    0x01, 0x00, 0x09, 0x01, 0x03, 0x02, 0x05, 0xDC, 0x03, 0x01, 0x04, 0x03
};
const uint8_t g_iposlGatewayServing[IPOSL_GATEWAY_CAPABILITY_LEN] = {
    0x01, 0x00, 0x09, 0x01, 0x02, 0x02, 0x05, 0xDC, 0x03, 0x01, 0x04, 0x03
};

int32_t IposlCodecEncodeConfigRequest(const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t *out, size_t outLen)
{
    return IposlCodecEncodeConfigMode(layer2, IPOSL_IP_TYPE_IPV4, out, outLen);
}

int32_t IposlCodecEncodeConfigMode(const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t mode,
    uint8_t *out, size_t outLen)
{
    if ((mode != IPOSL_IP_TYPE_IPV4 && mode != IPOSL_IP_TYPE_DUAL_STACK) ||
        layer2 == NULL || out == NULL || outLen < IPOSL_CONFIG_REQUEST_LEN) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    out[0] = IPOSL_OPCODE_CONFIGURE;
    (void)memcpy(out + 1, layer2, IPOSL_LAYER2_ID_LEN);
    out[7] = (uint8_t)(IPOSL_MTU >> 8);
    out[8] = (uint8_t)(IPOSL_MTU & 0xFFu);
    out[9] = 0x01;
    out[10] = mode;
    return (int32_t)IPOSL_CONFIG_REQUEST_LEN;
}

int32_t IposlCodecEncodeOpenRequest(const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t *out, size_t outLen)
{
    if (layer2 == NULL || out == NULL || outLen < IPOSL_OPEN_REQUEST_LEN) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    out[0] = IPOSL_OPCODE_ENABLE;
    (void)memcpy(out + 1, layer2, IPOSL_LAYER2_ID_LEN);
    return (int32_t)IPOSL_OPEN_REQUEST_LEN;
}

int32_t IposlCodecEncodeResponse(uint8_t opcode, const uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t result,
    uint8_t *out, size_t outLen)
{
    if (layer2 == NULL || out == NULL || outLen < IPOSL_RESPONSE_LEN ||
        (opcode != IPOSL_OPCODE_CONFIGURE && opcode != IPOSL_OPCODE_ENABLE)) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    out[0] = opcode;
    (void)memcpy(out + 1, layer2, IPOSL_LAYER2_ID_LEN);
    out[7] = result;
    return (int32_t)IPOSL_RESPONSE_LEN;
}

int32_t IposlCodecDecodeRequest(const uint8_t *data, size_t len, uint8_t *opcode,
    uint8_t layer2[IPOSL_LAYER2_ID_LEN])
{
    if (data == NULL || opcode == NULL || layer2 == NULL || len < 1) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    // Preserve a structurally complete request identity for a method-level rejection.
    // Semantic errors must not silently become an unencodable opcode 0 response.
    if ((data[0] == IPOSL_OPCODE_CONFIGURE || data[0] == IPOSL_OPCODE_ENABLE) &&
        len >= 1 + IPOSL_LAYER2_ID_LEN) {
        *opcode = data[0];
        (void)memcpy(layer2, data + 1, IPOSL_LAYER2_ID_LEN);
    }
    if (data[0] == IPOSL_OPCODE_CONFIGURE) {
        if (len != IPOSL_CONFIG_REQUEST_LEN || data[7] != 0x05 || data[8] != 0xDC ||
            data[9] != 0x01 || (data[10] != IPOSL_IP_TYPE_IPV4 && data[10] != IPOSL_IP_TYPE_DUAL_STACK)) {
            return IPOSL_ERR_INVALID_PARAM;
        }
    } else if (data[0] == IPOSL_OPCODE_ENABLE) {
        if (len != IPOSL_OPEN_REQUEST_LEN) {
            return IPOSL_ERR_INVALID_PARAM;
        }
    } else {
        return IPOSL_ERR_INVALID_PARAM;
    }
    *opcode = data[0];
    (void)memcpy(layer2, data + 1, IPOSL_LAYER2_ID_LEN);
    return IPOSL_SUCCESS;
}

int32_t IposlCodecDecodeResponse(const uint8_t *data, size_t len, uint8_t expectedOpcode,
    uint8_t layer2[IPOSL_LAYER2_ID_LEN], uint8_t *result)
{
    if (data == NULL || layer2 == NULL || result == NULL || len != IPOSL_RESPONSE_LEN ||
        (expectedOpcode != IPOSL_OPCODE_CONFIGURE && expectedOpcode != IPOSL_OPCODE_ENABLE) ||
        data[0] != expectedOpcode) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    if (data[7] != 0xFF && ((expectedOpcode == IPOSL_OPCODE_CONFIGURE && data[7] > 0x07) ||
        (expectedOpcode == IPOSL_OPCODE_ENABLE && data[7] > 0x03))) {
        return IPOSL_ERR_INVALID_PARAM;
    }
    (void)memcpy(layer2, data + 1, IPOSL_LAYER2_ID_LEN);
    *result = data[7];
    return IPOSL_SUCCESS;
}

bool IposlCodecGatewayModes(const uint8_t *data, size_t length, uint8_t *modes)
{
    if (data == NULL || modes == NULL || length < 3 ||
        (((size_t)data[1] << 8) | data[2]) != length - 3) return false;
    uint8_t seen = 0, nat = 0, communication = 0, ip = 0;
    uint16_t mtu = 0;
    for (size_t offset = 3; offset < length;) {
        uint8_t type = data[offset++];
        if (type < 1 || type > 4 || (seen & (1u << type)) != 0) return false;
        size_t size = type == 2 ? 2 : 1;
        if (length - offset < size) return false;
        seen |= 1u << type;
        if (type == 1) nat = data[offset];
        if (type == 2) mtu = ((uint16_t)data[offset] << 8) | data[offset + 1];
        if (type == 3) communication = data[offset];
        if (type == 4) ip = data[offset];
        offset += size;
    }
    if (seen != 0x1e || (nat & 2) == 0 || mtu < IPOSL_MTU || (communication & 1) == 0) return false;
    *modes = (ip & 1) == 0 ? 0 : ((ip & 3) == 3 ? 3 : 1);
    return true;
}

uint8_t IposlCodecSelectMode(uint8_t requested, uint8_t peerModes)
{
    if ((requested != 1 && requested != 3) || (peerModes & 1) == 0) return 0;
    return requested == 3 && (peerModes & 3) == 3 ? 3 : 1;
}

bool IposlCodecMayFallback(uint8_t opcode, uint8_t mode, uint8_t result, bool alreadyRetried, bool secure)
{
    /* Only a decoded explicit CONFIGURE rejection; never an SSAP/security/transport error. */
    return opcode == 1 && mode == 3 && result != 0 && (result <= 7 || result == 0xff) &&
        !alreadyRetried && secure;
}

void IposlCodecS1LinkLocal(const uint8_t layer2[6], uint8_t address[16])
{
    memset(address, 0, 16);
    address[0] = 0xfe; address[1] = 0x80;
    address[8] = layer2[0] ^ 2; address[9] = layer2[1]; address[10] = layer2[2];
    address[11] = 0xff; address[12] = 0xfe;
    memcpy(address + 13, layer2 + 3, 3);
}

bool IposlCodecValidatePacket(uint8_t pi, const uint8_t *data, size_t length)
{
    if (data == NULL || length > IPOSL_MTU) return false;
    if (pi == 1) {
        if (length < 20 || data[0] >> 4 != 4 || (data[0] & 15) < 5) return false;
        return (size_t)(data[0] & 15) * 4 <= length && (((size_t)data[2] << 8) | data[3]) == length;
    }
    if (pi != 2 || length < 40 || data[0] >> 4 != 6 ||
        (((size_t)data[4] << 8) | data[5]) != length - 40) return false;
    size_t offset = 40, bytes = 0;
    unsigned count = 0;
    uint8_t next = data[6];
    while (next == 0 || next == 43 || next == 60 || next == 51 || next == 44) {
        if (++count > 8 || length - offset < 2) return false;
        if (next == 44) {
            /* Structural check only; the channel requires an existing formal address mapping. */
            return length - offset > 8 && bytes + 8 <= 256 && data[offset + 1] == 0 &&
                (data[offset] == 6 || data[offset] == 17) && (data[offset + 3] & 6) == 0 &&
                (!(data[offset + 3] & 1) || (length - offset - 8) % 8 == 0);
        }
        size_t size = next == 51 ? ((size_t)data[offset + 1] + 2) * 4 :
            ((size_t)data[offset + 1] + 1) * 8;
        if (size > length - offset || bytes + size > 256) return false;
        next = data[offset];
        offset += size;
        bytes += size;
    }
    return true;
}

bool IposlCodecVerifyGoldenVectors(void)
{
    static const uint8_t layer2[IPOSL_LAYER2_ID_LEN] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    static const uint8_t configRequest[IPOSL_CONFIG_REQUEST_LEN] = {
        0x01, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x05, 0xDC, 0x01, 0x01
    };
    static const uint8_t openRequest[IPOSL_OPEN_REQUEST_LEN] = {
        0x02, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55
    };
    static const uint8_t configResponse[IPOSL_RESPONSE_LEN] = {
        0x01, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x00
    };
    uint8_t out[IPOSL_CONFIG_REQUEST_LEN] = {0};
    uint8_t decodedLayer2[IPOSL_LAYER2_ID_LEN] = {0};
    uint8_t opcode = 0;
    uint8_t result = 0xFF;
    if (IposlCodecEncodeConfigRequest(layer2, out, sizeof(out)) != (int32_t)sizeof(configRequest) ||
        memcmp(out, configRequest, sizeof(configRequest)) != 0 ||
        IposlCodecDecodeRequest(out, sizeof(configRequest), &opcode, decodedLayer2) != IPOSL_SUCCESS ||
        opcode != IPOSL_OPCODE_CONFIGURE || memcmp(decodedLayer2, layer2, sizeof(layer2)) != 0) {
        return false;
    }
    if (IposlCodecEncodeOpenRequest(layer2, out, sizeof(out)) != (int32_t)sizeof(openRequest) ||
        memcmp(out, openRequest, sizeof(openRequest)) != 0 ||
        IposlCodecDecodeRequest(out, sizeof(openRequest), &opcode, decodedLayer2) != IPOSL_SUCCESS ||
        opcode != IPOSL_OPCODE_ENABLE) {
        return false;
    }
    if (IposlCodecDecodeResponse(configResponse, sizeof(configResponse), IPOSL_OPCODE_CONFIGURE,
        decodedLayer2, &result) != IPOSL_SUCCESS || result != 0) {
        return false;
    }
    out[0] = 0x7F;
    return IposlCodecDecodeRequest(out, 1, &opcode, decodedLayer2) == IPOSL_ERR_INVALID_PARAM;
}
