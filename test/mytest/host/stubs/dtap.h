#pragma once
#include "sdf_buff.h"
constexpr int DTAP_PI_IPV4 = 1, DTAP_PI_IPV6 = 2;
struct DTAP_Data_Info_S { uint8_t pi; uint16_t lcid; uint8_t tcid; };
struct DTAP_Data_S { uint8_t pi; uint16_t lcid; uint8_t tcid; SDF_Buff_S *buff; };
using Receive = int (*)(DTAP_Data_Info_S *, SDF_Buff_S *);
inline Receive registered[3] = {};
inline int failPi = 0;
inline int DTAP_RegisterProtoRecvCbk(int pi, Receive cb) { if (pi == failPi) return -1; registered[pi] = cb; return 0; }
inline int DTAP_UnregisterProtoRecvCbk(int pi) { registered[pi] = nullptr; return 0; }
