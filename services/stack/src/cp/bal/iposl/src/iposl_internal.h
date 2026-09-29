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
#ifndef IPOSL_INTERNAL_H
#define IPOSL_INTERNAL_H

#include "iposl_profile.h"

const IposlProfileCallbacks *IposlGetCallbacks(void);
int32_t IposlServerInitialize(void);
void IposlServerDeinit(void);
int32_t IposlServerStart(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, uint8_t mode, uint64_t generation);
void IposlServerStop(void);
int32_t IposlClientStart(const uint8_t peer[IPOSL_LAYER2_ID_LEN], uint8_t addressType, bool terminal,
    const uint8_t localLayer2[IPOSL_LAYER2_ID_LEN], uint8_t mode, uint64_t generation);
void IposlClientStop(void);

#endif  // IPOSL_INTERNAL_H
