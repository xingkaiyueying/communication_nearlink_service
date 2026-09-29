#pragma once
#include "sdf_addr.h"
typedef int NLSTK_Errcode_E;
enum { SSAP_APP_INVALID_ID=-1, NLSTK_ERRCODE_SUCCESS=0, SSAP_START_HANDLE=1, SSAP_END_HANDLE=65535,
       FIND_STRUCTURE_TYPE_PRIMARY_SERVICE=1, SSAP_CONNECT_STATE_CONNECTED=1, SSAP_CONNECT_STATE_DISCONNECTED=0 };
struct NLSTK_SsapUuid_S { uint8_t uuid[16]; };
struct NLSTK_VariableData_S { uint16_t len; uint8_t *data; };
struct NLSTK_SsapPrty_S { uint16_t handle; NLSTK_SsapUuid_S uuid; };
struct NLSTK_SsapServ_S { uint16_t propertyNum; NLSTK_SsapPrty_S *properties; uint16_t methodNum; NLSTK_SsapPrty_S *methods; };
struct NLSTK_SsapClientReadPropertyInfo_S { uint16_t handle; NLSTK_SsapUuid_S uuid; uint8_t errorCode; NLSTK_VariableData_S value; };
using NLSTK_SsapClientCallMethodResult_S=NLSTK_SsapClientReadPropertyInfo_S;
using NLSTK_SsapClientFreeFunc=void (*)(NLSTK_SsapServ_S *,uint16_t);
struct NLSTK_SsapAppClientCb_S {
    void (*onConnectionStateChanged)(int32_t,uint8_t,NLSTK_Errcode_E,int32_t);
    void (*onFindServiceByUuid)(int32_t,NLSTK_SsapUuid_S *,NLSTK_Errcode_E);
    void (*onGetServices)(int32_t,NLSTK_SsapUuid_S *,NLSTK_SsapServ_S *,uint16_t,NLSTK_SsapClientFreeFunc);
    void (*onCallMethod)(int32_t,NLSTK_SsapClientCallMethodResult_S *,NLSTK_Errcode_E);
    void (*onReadProperty)(int32_t,NLSTK_SsapClientReadPropertyInfo_S *,NLSTK_Errcode_E);
};
static NLSTK_SsapUuid_S discovered;
static int methodCalls=0, reads=0;
inline int NLSTK_SsapClientDisconnect(int) { return 0; }
inline void NLSTK_SsapClientDeregAppAsync(int) {}
inline int NLSTK_SsapClientCallMethod(int,uint16_t,NLSTK_VariableData_S *,bool) { ++methodCalls; return 0; }
inline int NLSTK_SsapClientReadProperty(int,uint16_t) { ++reads; return 0; }
inline int NLSTK_SsapClientDiscoverServicesByUuid(int,NLSTK_SsapUuid_S *u,int,int,int) { discovered=*u; return 0; }
inline int NLSTK_SsapClientGetServicesByUuidAsyn(int,NLSTK_SsapUuid_S *) { return 0; }
inline int NLSTK_SsapClientRegApp(int *id,NLSTK_SsapAppClientCb_S *,SLE_Addr_S *) { *id=7; return 0; }
inline int NLSTK_SsapClientConnect(int) { return 0; }
