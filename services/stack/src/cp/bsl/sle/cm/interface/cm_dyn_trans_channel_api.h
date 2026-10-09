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
 * this file contains the CM for dynamic transport channel API definitions
 *
 ***************************************************************************/

#ifndef CM_DYN_TRANS_CHANNEL_API_H
#define CM_DYN_TRANS_CHANNEL_API_H

#include "cm_def.h"
#include "cm_trans_channel_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CM_DYN_TRANS_SLQI_MAX_NUM 8U
#define CM_DYN_TRANS_AID_MAX_NUM 1U

typedef enum {
    CM_TRANS_CHANNEL_SLQI_LOW = 0,   // SLE异步链路低速模式，默认
    CM_TRANS_CHANNEL_SLQI_HIGH,      // SLE异步链路高速模式
    CM_TRANS_CHANNEL_SLQI_MAX,
} CM_TransChannelSlqi_E;

typedef enum {
    CM_SLE_RADIO_FRAME_TYPE_1 = 0,  // 无线帧类型1
    CM_SLE_RADIO_FRAME_TYPE_2 = 1,  // 无线帧类型2
    CM_SLE_RADIO_FRAME_TYPE_3 = 2,  // 无线帧类型3
    CM_SLE_RADIO_FRAME_TYPE_4 = 3,  // 无线帧类型4
} CM_TransConnFrameType_E;

/**
 * @brief 传输通道建立响应
 */
typedef enum {
    CM_DYN_TRANS_CHAN_ESTABLISH_SUCCESS = 0,                  /* 成功 */
    CM_DYN_TRANS_CHAN_ESTABLISH_FAIL = 1,                     /* 激活失败, 大多数情况下使用 */
    CM_DYN_TRANS_CHAN_ESTABLISH_NOT_FOUND_DEVICE = 2,         /* 激活失败, 无法找到target Layer2 ID对应的星闪设备 */
    CM_DYN_TRANS_CHAN_ESTABLISH_NO_NECESSARY_MESSAGE = 3,     /* 激活失败, 缺少必要指示信息引起的失败 */
    CM_DYN_TRANS_CHAN_ESTABLISH_NOT_SUPPORT_PARAM = 4,        /* 激活失败, 代表TansportChannelEstablish.request参数不支持,
                                                                 注意: 如果参数不支持，提前入口拦截则不再回调反馈该状态 */
    CM_DYN_TRANS_CHAN_ESTABLISH_NOT_ESTABLISH_EXCLUSIVE = 5,  /* 激活失败, 无法建立独享的传输通道 */
                                                              /* 6~60预留 */
    CM_DYN_TRANS_CHAN_ESTABLISH_TIMEOUT = 60,                 /* 激活失败, 超时 */
    CM_DYN_TRANS_CHAN_ESTABLISH_RSP_MAX                       /* 61~255预留 */
} CM_DynTransChanEstablishRsp_E;

/**
 * @brief 传输通道释放响应
 */
typedef enum {
    CM_DYN_TRANS_CHAN_RELEASE_SUCCESS = 0,                   /* 成功 */
    CM_DYN_TRANS_CHAN_RELEASE_FAIL = 1,                      /* 释放失败 */
    CM_DYN_TRANS_CHAN_RELEASE_PARTIAL_SUCCESS = 2,           /* 释放部分成功,（仅用于TransportChannelRelease.request
                                                                 携带有srcTCIDList且只能释放部分传输通道的情况）*/
                                                             /* 7~60预留 */
    CM_DYN_TRANS_CHAN_RELEASE_TIMEOUT = 60,                  /* 释放失败, 超时 */
    CM_DYN_TRANS_CHAN_RELEASE_RSP_MAX                        /* 61~255预留 */
} CM_DynTransChanReleaseRsp_E;

/**
 * @brief 传输通道状态指示
 */
typedef enum {
    CM_DYN_TRANS_CHAN_STATUS_INDICATION_NORMAL = 0,                   /* 正常（一般情况下, 被动创建成功使用） */
    CM_DYN_TRANS_CHAN_STATUS_INDICATION_EXCEPTION = 1,                /* 异常 */
    CM_DYN_TRANS_CHAN_STATUS_INDICATION_DICONNECTED = 2,              /* 断开（一般情况下, 被动释放成功使用） */
    CM_DYN_TRANS_CHAN_STATUS_INDICATION_MAX                           /* 3~255预留 */
} CM_DynTransChanStatusIndicationRsp_E;

/**
 * @brief 星闪创建动态传输通道请求
 */
typedef struct {
    void *extension;        /* 预留结构体指针 */
} CM_DynTransChannelExtParamReq_S;

/**
 * @brief 星闪创建动态传输通道请求请求结构体
 */
typedef struct {
    uint8_t version;                            /* 原语版本号，当前标准版本设置为0 */
    uint16_t localIndex;                        /* 本地索引 */
    SLE_Addr_S addr;                            /* 星闪设备地址 */
    uint16_t srcPort;                           /* 本端端口号 */
    uint16_t dstPort;                           /* 对端端口号 */
    CM_TransModeConfig_S transModeConfig;       /* 传输模式配置参数 */
    uint8_t slqi;                               /* 星闪QoS参数索引, 参考CM_TransChannelSlqi_E定义 */
    CM_DynTransChannelExtParamReq_S *extension; /* 初始化动态传输通道参数，协议目前未定位具体结构, 可选 */
    uint8_t expectedTransportMode;              /* 期望的传输模式，参考CM_AccessTransportMode_E定义 */
    bool exclusive;                             /* 指示希望建立独享的逻辑信道，TRUE / FALSE, 默认值为FALSE */
    CM_TransConnFrameType_E frameType;
} CM_DynTransChannelEstablishParamReq_S;

/**
 * @brief 星闪释放动态传输通道请求请求结构体
 */
typedef struct {
    uint8_t version;                             /* 原语版本号，当前标准版本设置为0 */
    uint16_t localIndex;                         /* 本地索引 */
    SLE_Addr_S addr;                             /* 星闪设备地址 */
    uint8_t srcTcid;                             /* 如果出现，用于指示期望释放对应srcTcid的传输通道 */
    uint8_t dstTcid;                             /* 如果出现，用于指示期望释放对应dstTcid的传输通道 */
    CM_DynTransChannelExtParamReq_S *extension;  /* 初始化动态传输通道参数，协议目前未定位具体结构, 可选 */
} CM_DynTransChannelReleaseParamReq_S;

/**
 * @brief 动态传输通道创建和释放响应扩展参数结构体
 */
typedef struct {
    void *extension;        /* 预留结构体指针 */
} CM_DynTransChannelParamRspExt_S;

/**
 * @brief 星闪QoS参数结构体
 */
typedef struct {
    uint8_t slqiNum;                         /* 星闪QoS参数索引数目 */
    uint8_t slqi[CM_DYN_TRANS_SLQI_MAX_NUM]; /* 星闪QoS参数索引 */
} CM_DynTransChannelSlqiList_S;

/**
 * @brief 星闪业务标识结构体
 */
typedef struct {
    uint8_t aidNum;                         /* 星闪业务标识数目 */
    uint16_t aid[CM_DYN_TRANS_AID_MAX_NUM]; /* 星闪业务标识 */
} CM_DynTransChannelAidList_S;

/**
 * @brief 动态传输通道创建响应结构体
 */
typedef struct {
    uint8_t version;                            /* 原语版本号，当前标准版本设置为0 */
    uint16_t localIndex;                        /* 本地索引 */
    uint8_t  result;                            /* 传输通道创建响应结果, 参考CM_DynTransChanEstablishRsp_E定义 */
    uint16_t srcPort;                           /* 本端端口号 */
    uint16_t dstPort;                           /* 对端端口号 */
    uint8_t  transMode;                         /* 传输模式 */
    uint8_t  srcTcid;                           /* 本端传输通道ID */
    uint8_t  dstTcid;                           /* 对端传输通道ID */
    CM_DynTransChannelSlqiList_S slqiList;      /* 支持的SLQI列表 */
    CM_DynTransChannelAidList_S aidList;        /* 支持的AID列表 */
    uint16_t lcid;                              /* 逻辑链路handle */
    SLE_Addr_S addr;                            /* 星闪设备地址 */
    uint16_t mtu;                               /* 传输通道使用的MTU值 */
    CM_TransConnFrameType_E frameType;
    CM_DynTransChannelParamRspExt_S *extension; /* 扩展参数，协议目前未定位具体结构, 可选 */
} CM_DynTransChanEstablishParamRsp_S;

/**
 * @brief 动态传输通道释放响应结构体
 */
typedef struct {
    uint8_t version;                            /* 原语版本号，当前标准版本设置为0 */
    uint16_t localIndex;                        /* 本地索引 */
    uint8_t  result;                            /* 传输通道创建响应结果, 参考CM_DynTransChanReleaseRsp_E定义 */
    uint16_t srcPort;                           /* 本端端口号 */
    uint16_t dstPort;                           /* 对端端口号 */
    uint8_t  srcTcid;                           /* 本端传输通道ID */
    uint8_t  dstTcid;                           /* 对端传输通道ID */
    uint16_t lcid;                              /* 逻辑链路handle */
    CM_DynTransChannelSlqiList_S slqiList;      /* 支持的SLQI列表 */
    SLE_Addr_S addr;                            /* 星闪设备地址 */
    CM_TransConnFrameType_E frameType;
    CM_DynTransChannelParamRspExt_S *extension; /* 扩展参数，协议目前未定位具体结构, 可选 */
} CM_DynTransChanReleaseParamRsp_S;

/**
 * @brief 动态传输通道状态指示结构体
 */
typedef struct {
    uint8_t version;                            /* 原语版本号，当前标准版本设置为0 */
    uint16_t localIndex;                        /* 本地索引 */
    uint8_t  result;                            /* 传输通道创建响应结果, 参考CM_DynTransChanStatusIndicationRsp_E定义 */
    uint16_t srcPort;                           /* 本端端口号 */
    uint16_t dstPort;                           /* 对端端口号 */
    bool added;                                 /* 传输通道 1: add(被动创建), 0: release(被动释放) */
    uint8_t  transMode;                         /* 传输模式 */
    uint8_t  srcTcid;                           /* 本端传输通道ID */
    uint8_t  dstTcid;                           /* 对端传输通道ID */
    CM_DynTransChannelSlqiList_S slqiList;      /* 支持的SLQI列表 */
    CM_DynTransChannelAidList_S aidList;        /* 支持的AID列表 */
    uint16_t lcid;                              /* 逻辑链路handle */
    SLE_Addr_S addr;                            /* 星闪设备地址 */
    uint16_t mtu;                               /* 传输通道使用的MTU值 */
    CM_DynTransChannelParamRspExt_S *extension; /* 扩展参数，协议目前未定位具体结构, 可选 */
} CM_DynTransChanStatusIndicationRsp_S;

typedef struct {
    uint16_t srcPort;
    uint16_t dstPort;
} CM_DynTransChanEstablishedCheckParam_S;

// Internal peer-aware hook; the legacy callback parameter layout stays unchanged.
typedef bool (*CM_DynTransPeerCheck)(uint16_t lcid, uint16_t srcPort, uint16_t dstPort);
void CM_DynTransChannStateMgrRegPeerCheck(CM_DynTransPeerCheck callback);

/**
 * @brief  动态传输通道主动创建响应回调函数
 */
typedef void (*CM_DynTransChannEstablishRspCbk)(const CM_DynTransChanEstablishParamRsp_S *param);

/**
 * @brief  动态传输通道主动释放响应回调函数
 */
typedef void (*CM_DynTransChannReleaseRspCbk)(const CM_DynTransChanReleaseParamRsp_S *param);

/**
 * @brief  动态传输通道(被动创建/释放)状态指示回调函数
 */
typedef void (*CM_DynTransChannStatusIndicationCbk)(const CM_DynTransChanStatusIndicationRsp_S *param);

/**
 * @brief  动态传输通道被动创建合法性检查回调函数
 */
typedef bool (*CM_DynTransChannEstablishedCheckCbk)(const CM_DynTransChanEstablishedCheckParam_S *param);

/**
 * @brief  动态传输通道模块回调函数
 */
typedef struct {
    CM_DynTransChannEstablishRspCbk establishRspCbk;         /* 必填 */
    CM_DynTransChannReleaseRspCbk releaseRspCbk;             /* 必填 */
    CM_DynTransChannStatusIndicationCbk statusIndicationCbk; /* 必填 */
    CM_DynTransChannEstablishedCheckCbk establishedCheckCbk; /* 可空 */
} CM_DynTransChannelCbks_S;

/**
 * @brief  CM动态传输通道注册相关回调
 * @param [in] < cbks > 模块注册回调指针, 参见CM_DynTransChannelCbks_S定义
 * @return SUCCESS: 成功, OTHER: 失败
 */
uint32_t CM_RegDynTransChannelCbks(const CM_DynTransChannelCbks_S *cbks);

/**
 * @brief  CM动态传输通道取消注册相关回调
 * @param [in] < void > 无
 * @return void
 */
void CM_UnRegDynTransChannelCbks(void);

/**
 * @brief 创建动态传输通道请求
 * @param  [in] < param > 请求结构参数, 参见CM_DynTransChannelEstablishParamReq_S定义
 * @return SUCCESS: 成功, OTHER: 失败
 */
uint32_t CM_DynTransChannelEstablishReq(const CM_DynTransChannelEstablishParamReq_S *param);

/**
 * @brief 释放动态传输通道请求
 * @param  [in] < param > 请求结构体参数, 参见CM_DynTransChannelReleaseParamReq_S定义
 * @return SUCCESS: 成功, OTHER: 失败
 */
uint32_t CM_DynTransChannelReleaseReq(const CM_DynTransChannelReleaseParamReq_S *param);

#ifdef __cplusplus
}
#endif

#endif
