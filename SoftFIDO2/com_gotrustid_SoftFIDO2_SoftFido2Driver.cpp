//
//  SoftFIDO2.cpp
//  SoftFIDO2
//
//  Created by Eddie Hua on 2020/9/1.
//  Copyright © 2020 GoTrustID. All rights reserved.
//

#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserServer.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IODispatchQueue.h>
#include "com_gotrustid_SoftFIDO2_SoftFido2Driver.h"

#define LOG_PREFIX "[SoftFido2][Driver] "

struct com_gotrustid_SoftFIDO2_SoftFido2Driver_IVars {
    //IODispatchQueue* workQueue; // SoftU2f 用IOWorkLoop *_workLoop，DriverKit 沒有
};

bool com_gotrustid_SoftFIDO2_SoftFido2Driver::init() {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "init");
    if (super::init()) {
        ivars = IONewZero(com_gotrustid_SoftFIDO2_SoftFido2Driver_IVars, 1);
        if (ivars != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "init success.");
            return true;
        }
    }
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "init failed.");
    return false;
}

void com_gotrustid_SoftFIDO2_SoftFido2Driver::free() {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "free");
    IOSafeDeleteNULL(ivars, com_gotrustid_SoftFIDO2_SoftFido2Driver_IVars, 1);
    super::free();
}


kern_return_t IMPL(com_gotrustid_SoftFIDO2_SoftFido2Driver, Start) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "Start");
    kern_return_t ret = kIOReturnSuccess;
    ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "Start Failed!");
        Stop(provider, SUPERDISPATCH);
        return ret;
    }

    RegisterService();
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "Start OK");
    return ret;
}

kern_return_t IMPL(com_gotrustid_SoftFIDO2_SoftFido2Driver, Stop) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "Stop");
    return Stop(provider, SUPERDISPATCH);
}
// App呼叫 IOServiceOpen 來 Start 新的 Service，系統就會呼叫這個 function
kern_return_t IMPL(com_gotrustid_SoftFIDO2_SoftFido2Driver, NewUserClient) {
    kern_return_t ret = kIOReturnSuccess;
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "NewUserClient");
    IOService* client;
    ret = Create(this, "UserClientProperties", &client);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "IOService::Create failed: 0x%x", ret);
        return ret;
    }

    *userClient = OSDynamicCast(IOUserClient, client);
    if (!*userClient) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "OSDynamicCast failed");
        client->release();
        return kIOReturnError;
    }

    os_log(OS_LOG_DEFAULT, LOG_PREFIX "UserClient is created");
    return kIOReturnSuccess;
}
