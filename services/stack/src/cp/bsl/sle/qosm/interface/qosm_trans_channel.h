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

/****************************************************************************
 *
 * this file contains related apis of transport channel establish/release.
 *
 ***************************************************************************/

#ifndef QOSM_TRANS_CHANNEL_H
#define QOSM_TRANS_CHANNEL_H

#include <stdint.h>
#include <stdbool.h>

#include "sdf_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum : uint8_t {
    QOSM_TRANS_CHANNEL_ESTABLISHED,     // 建立完成
    QOSM_TRANS_CHANNEL_ESTABLISH_FAIL,  // 建立失败
    QOSM_TRANS_CHANNEL_RELEASED,        // 释放完成
    QOSM_TRANS_CHANNEL_RELEASE_FAIL,    // 释放失败
} QOSM_TransChannelStatus_E;

typedef enum : uint8_t {
    QOSM_TRANS_CHANNEL_SLQI_LOW,   // SLE异步链路低速模式，默认
    QOSM_TRANS_CHANNEL_SLQI_HIGH,  // SLE异步链路高速模式

    QOSM_TRANS_CHANNEL_SLQI_MAX,
} QOSM_TransChannelSlqi_E;

typedef enum : uint8_t {
    TRANSPORT_MODE_BASIC = 0,    // 基础模式
    TRANSPORT_MODE_TRANSPARENT,  // 透传模式
    TRANSPORT_MODE_STREAM,       // 流模式
    TRANSPORT_MODE_RELIABLE,     // 可靠模式

    TRANSPORT_MODE_MAX,  // 传输模式数目
} QOSM_TransMode_E;

typedef struct {
    uint8_t mode;  // 传输模式
} QOSM_TransChannelConf_S;

typedef enum {
    QOSM_SLE_RADIO_FRAME_TYPE_1 = 0,  // 无线帧类型1
    QOSM_SLE_RADIO_FRAME_TYPE_2 = 1,  // 无线帧类型2
    QOSM_SLE_RADIO_FRAME_TYPE_3 = 2,  // 无线帧类型3
    QOSM_SLE_RADIO_FRAME_TYPE_4 = 3,  // 无线帧类型4
} QOSM_TransConnFrameType_E;

typedef enum : uint8_t {
    SLE_MODE_ICB = 0,  // 同步数据链路
    SLE_MODE_ACB,  // 异步数据链路
    SLE_MODE_MAX,
} QOSM_SleMode_E;

typedef enum : uint8_t {
    ACCESS_TRANS_MODE_UNICAST = 0,        // 单播
    ACCESS_TRANS_MODE_DATA_MCST,      // 数据组播
    ACCESS_TRANS_MODE_FEEDBACK_MCST,  // 反馈组播
    ACCESS_TRANS_MODE_BIDI_MCST,      // 双向组播
    ACCESS_TRANS_MODE_SEND_BCST,      // 广播发送
    ACCESS_TRANS_MODE_RECV_BCST,      // 广播接收
} QOSM_AccessTransMode_E;

typedef struct {
    SLE_Addr_S addr;                 // 星闪设备地址
    uint8_t linkMode;                // 逻辑链路模式，SLE见QOSM_SleMode_E
    uint8_t accessTransMode;         // 星闪接入层数据传输方式，当前仅支持单播,见QOSM_AccessTransMode_E
    uint16_t srcPort;                // 业务源端口号
    uint16_t dstPort;                // 业务目的端口号
    QOSM_TransChannelSlqi_E slqi;    // 星闪QoS参数索引
    QOSM_TransChannelConf_S tcConf;  // 传输通道配置
    QOSM_TransConnFrameType_E frameType;// 连接帧类型
} QOSM_TransChannelParams_S;

typedef struct {
    SLE_Addr_S addr;                   // 星闪设备地址
    uint8_t transMode;                 // 传输模式
    uint8_t tcid;                      // 传输通道标识
    uint16_t lcid;                     // 逻辑链路标识
    uint16_t srcPort;                  // 源端口号
    uint16_t dstPort;                  // 目的端口号
    uint16_t mtu;                      // 传输通道使用的mtu值
    QOSM_TransChannelSlqi_E slqi;      // 星闪QoS参数索引
    QOSM_TransChannelStatus_E status;  // 传输通道状态
    QOSM_TransConnFrameType_E frameType;// 连接帧类型
} QOSM_TransChannelRspParams_S;

typedef struct {
    SLE_Addr_S addr;  // 星闪设备地址
    uint8_t tcid;     // 传输通道标识
} QOSM_TransChannelReleaseParams_S;

/**
 * @brief 定义传输通道状态回调函数指针
 * @param respParams 传输通道响应参数指针
 * @return 无
 */
typedef void (*QOSM_TransChannelStatusCbk)(const QOSM_TransChannelRspParams_S *respParams);

/**
 * @brief 定义传输通道被动创建响应检查信息是否合法回调函数指针, 调用方需实现同步返回检查结果
 * @param srcPort 业务源端口号
 * @return bool 合法返回true，否则返回false
 */
typedef bool (*QOSM_TransChannelEstablishedCheckCbk)(const SLE_Addr_S *addr, uint16_t srcPort);

typedef struct {
    QOSM_TransChannelStatusCbk statusCbk;
    QOSM_TransChannelEstablishedCheckCbk establishedCheck;
} QOSM_TransChannelCbks_S;

/**
 * @brief 注册传输通道回调函数
 * @param cbks 传输通道回调函数结构体指针
 * @return 返回QOSM_SUCCESS表示成功，其他值表示失败
 */
uint32_t QOSM_TransChannelCbksRegister(const QOSM_TransChannelCbks_S *cbks);

/**
 * @brief 取消注册传输通道回调函数
 * @return 返回QOSM_SUCCESS表示成功，其他值表示失败
 */
uint32_t QOSM_TransChannelCbksUnregister(void);

/**
 * @brief 创建传输通道
 * @param params 传输通道参数结构体指针
 * @return 返回QOSM_SUCCESS表示成功，其他值表示失败
 */
uint32_t QOSM_TransChannelCreate(const QOSM_TransChannelParams_S *params);

/**
 * @brief 释放传输通道
 * @param params 释放传输通道参数结构体指针
 * @return 返回QOSM_SUCCESS表示成功，其他值表示失败
 */
uint32_t QOSM_TransChannelDestroy(const QOSM_TransChannelReleaseParams_S *params);

/**
 * @brief 初始化QOS管理传输通道模块
 * @return 返回0表示初始化成功，否则返回错误代码
 */
uint32_t QOSM_TransChannelInit(void);

/**
 * @brief 反初始化QOS管理传输通道模块
 */
void QOSM_TransChannelDeInit(void);

#ifdef __cplusplus
}
#endif
#endif  // QOSM_TRANSPOR_CHANNEL_H