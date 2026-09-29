#pragma once
#include "nlstk_ssap_app_client.h"
enum { ITEM_TYPE_VENDOR_PROPERTY=1, ITEM_TYPE_VENDOR_PRIMARY_SERVICE=2, ITEM_TYPE_VENDOR_METHOD=3,
    SSAP_PERMISSION_AUTHENTICATION_NEED=1, SSAP_PERMISSION_ENCRYPTION_NEED=2, SSAP_PERMISSION_AUTHORIZATION_NEED=4,
    SSAP_OPERATE_INDICATION_READ=1, SSAP_OPERATE_INDICATION_NOTIFY=2 };
struct Permission { uint32_t permissionValue; };
struct Operation { uint32_t operationValue; };
struct NLSTK_SsapServicePropertyParam_S {
    int type; NLSTK_SsapUuid_S uuid; Permission permission; Operation operation; NLSTK_VariableData_S val;
};
struct NLSTK_SsapServiceMethodParam_S { int type; NLSTK_SsapUuid_S uuid; Permission permission; };
struct Statement { NLSTK_SsapUuid_S uuid; int serviceType; };
struct NLSTK_ServiceParam_S {
    Statement serviceStatement; NLSTK_SsapServicePropertyParam_S *property; uint16_t servicePropertyNum;
    NLSTK_SsapServiceMethodParam_S *method; uint16_t serviceMethodNum;
};
struct NLSTK_SsapServerReadPropertyInfo_S { SLE_Addr_S addr; NLSTK_SsapUuid_S uuid; uint16_t handle; };
struct NLSTK_SsapServerCallMethodRequestInfo_S { SLE_Addr_S addr; uint16_t handle; NLSTK_VariableData_S param; };
struct NLSTK_SsapAppServerCb_S {
    void (*onCallMethod)(int32_t,uint16_t,NLSTK_SsapServerCallMethodRequestInfo_S *,bool,bool);
    void (*onReadPropertyAuthorizeRequest)(int32_t,uint16_t,NLSTK_SsapServerReadPropertyInfo_S *);
};
inline int sendResult = 0;
inline uint8_t wireResult = 0;
inline int NLSTK_SsapServerUpdatePropertyValue(int,int,NLSTK_VariableData_S *) { return 0; }
inline int NLSTK_SsapServerAuthorizeResult(int,int,bool) { return 0; }
inline int NLSTK_SsapServerSendMethodCallRes(int,int,NLSTK_VariableData_S *v) { wireResult=v->data[7]; return sendResult; }
inline int NLSTK_SsapServerAddService(int,NLSTK_ServiceParam_S *) { return 0; }
inline int NLSTK_SsapServerRegApp(NLSTK_SsapAppServerCb_S *,int *id) { *id=7; return 0; }
inline int NLSTK_SsapServerClearServices(int) { return 0; }
inline void NLSTK_SsapServerDeregisterApplication(int) {}
