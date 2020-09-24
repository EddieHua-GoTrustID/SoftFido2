//
//  SoftFido2UserClient.cpp
//  SoftFIDO2
//
//  Created by Eddie Hua on 2020/9/2.
//  Copyright © 2020 GoTrustID. All rights reserved.
//

#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/OSData.h>
#include <DriverKit/IODispatchQueue.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include "UserKernelShared.h"
#include "com_gotrustid_SoftFIDO2_SoftFido2Driver.h"
#include "SoftFido2UserClient.h"
#include "SoftFido2Device.h"
#include "BufMemoryUtils.hpp"

#define LOG_PREFIX "[SoftFido2][UserClient] "

void debugArguments(IOUserClientMethodArguments* arguments);

struct SoftFido2UserClient_IVars {
    com_gotrustid_SoftFIDO2_SoftFido2Driver* provider;
    SoftFido2Device* fido2Device;
    //IODispatchQueue* commandQueue = nullptr;
    //
    uint64_t notifyArgs[8];
    OSAction* notifyFrameAction = nullptr;
    IOBufferMemoryDescriptor* notifyFrameMemoryDesc = nullptr;
//    IOMemoryDescriptor* ouputDescriptor = nullptr;  // structureOutputDescriptor
};


bool SoftFido2UserClient::init() {
    if (super::init()) {
        ivars = IONewZero(SoftFido2UserClient_IVars, 1);
        if (ivars != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "init success.");
            return true;
        }
    }
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "init failed!");
    return false;
}

void SoftFido2UserClient::free() {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "free");

    OSSafeReleaseNULL(ivars->notifyFrameMemoryDesc);
    OSSafeReleaseNULL(ivars->notifyFrameAction);
    OSSafeReleaseNULL(ivars->fido2Device);
    IOSafeDeleteNULL(ivars, SoftFido2UserClient_IVars, 1);
    super::free();
}

kern_return_t IMPL(SoftFido2UserClient, Start) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "Start");

    ivars->provider = OSDynamicCast(com_gotrustid_SoftFIDO2_SoftFido2Driver, provider);
    if (!ivars->provider) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "Cast to com_gotrustid_SoftFIDO2_SoftFido2Driver Failed!");
        return kIOReturnError;
    }
    if (super::Start(provider, SUPERDISPATCH) != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "Start Failed!");
        return kIOReturnError;
    }
    kern_return_t ret = kIOReturnSuccess;
    IOService* service = nullptr;
    ret = Create(this, "Fido2HidProperties", &service);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "IOService::Create failed: 0x%x", ret);
        return ret;
    }
    
    SoftFido2Device* device = OSDynamicCast(SoftFido2Device, service);
    ivars->fido2Device = device;
    if (ivars->fido2Device == nullptr) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "SoftFido2Device is null");
        service->release();
        return kIOReturnError;
    }
//    ret = IODispatchQueue::Create("commandGate", 0 /*options*/ , 0 /*priority*/, &(ivars->commandQueue));
//    if (ret != kIOReturnSuccess) {
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "IODispatchQueue::Create CommandGate Failed : %d", ret);
//        return ret;
//    }
//    device->release();
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "--- Start ---");
    return ret;
}

kern_return_t IMPL(SoftFido2UserClient, Stop) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "Stop");
    if (ivars->fido2Device != nullptr) {
        ivars->fido2Device->release();
    }
    return Stop(provider, SUPERDISPATCH);
}

/*
 DriverKit 沒有
 IOReturn SoftU2FUserClient::clientClose(void) {}
 */
// 結果列印不出東西 textBuffer = <private>
//kern_return_t IMPL(SoftFido2UserClient, dump) {
//    uint8_t* byteArray = reinterpret_cast<uint8_t*>(address);
//    char textBuffer[512], hexBuf[6];
//    strlcpy(textBuffer, "DATA: ", 256);
//    for (int i = 0; i < length; i++) {
//        snprintf(hexBuf, 6, "%02x ", byteArray[i]);
//        strlcat(textBuffer, hexBuf, 256);
//    }
//    strlcat(textBuffer, "\n", 256);
//    os_log(OS_LOG_DEFAULT, LOG_PREFIX "textBuffer = %s", textBuffer);
//    return kIOReturnSuccess;
//}

kern_return_t IMPL(SoftFido2UserClient, frameReceived) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "frameReceived Report = %p", report);
    // <<< GetLength >>>
    uint64_t length = 0;
    kern_return_t ret = report->GetLength(&length);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->GetLength Failed!");
        return ret;
    }
    // (結果)都是 64bytes
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->GetLength = %llu", length);
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "sizeof(U2FHID_FRAME) = %lu", sizeof(U2FHID_FRAME));
    
    // 注意：直接存取 segments[0].address，會導致Driver停止
    if (ivars->notifyFrameMemoryDesc != nullptr) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "notifyFrameMemoryDesc Release");
        OSSafeReleaseNULL(ivars->notifyFrameMemoryDesc);
    }
    // 使用 IOBufferMemoryDescriptor : A memory buffer allocated in the caller's address space.
    //    kIOMemoryDirectionOut : A buffer to which the current process writes.
    // 將 Physical Address MAPING 過去 : 無法驗證內容?
    uint64_t outAddress = 0;
    uint64_t outLength = 0;
    IOBufferMemoryDescriptor* frameMemoryDesc = nullptr;
    IOBufferMemoryDescriptor::Create(kIOMemoryDirectionOut, 64, 0, &frameMemoryDesc);
    ivars->notifyFrameMemoryDesc = frameMemoryDesc;
    frameMemoryDesc->Map(0, 0, length, 0, &outAddress, &outLength);
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "notifyFrameMemoryDesc address = %llu", outAddress);
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "notifyFrameMemoryDesc length = %llu", outLength);
    ivars->notifyArgs[0] = outAddress;
    // [失敗] report map到 output buffer 的address
    IOMemoryMap* map = nullptr;
    ret = report->CreateMapping(kIOMemoryMapFixedAddress, outAddress, outLength, length, 0, &map);
    if (ret == kIOReturnSuccess) {
        ivars->notifyArgs[0] = map->GetAddress();
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->CreateMapping address = %llu", map->GetAddress());
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->CreateMapping length = %llu", map->GetLength());
    } else {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->CreateMapping failed = %d", ret);
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->CreateMapping system err = %x", err_get_system(ret));
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->CreateMapping    sub err = %x", err_get_sub(ret));
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->CreateMapping   code err = %x", err_get_code(ret));
    }
    // TRY: 故意寫一些假資料進去(好像沒影響
//      uint8_t* byteArray = (uint8_t*) outAddress;
//      byteArray[0] = 0x78;
//      byteArray[1] = 0x56;
//      byteArray[2] = 0x34;
//      byteArray[3] = 0x12;
    // 無法dump(outAddress, outLength);
    // << 這裡 Map 會失敗 >>
//    uint64_t address1;
//    uint64_t len1;
//    ret = report->Map(0, 0, 0, 0, &address1, &len1);
//    if (ret == kIOReturnSuccess) {
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "address1 = %llu", address1);
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "len1 = %llu", len1);
//    } else {
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->Map failed = %d", ret);
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->Map system err = %x", err_get_system(ret));
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->Map    sub err = %x", err_get_sub(ret));
//        os_log(OS_LOG_DEFAULT, LOG_PREFIX "report->Map   code err = %x", err_get_code(ret));
//    }
    //----------------------------
    //IOUserClientAsyncArgumentsArray args;
//    uint32_t asyncDataCount = 8;//;//(uint32_t) (segments[0].length / 64);
    uint32_t asyncDataCount = 1;
    //AsyncCompletion(OSAction *action, IOReturn status, IOUserClientAsyncArgumentsArray, uint32_t asyncDataCount)
    // TODO: 傳遞的內容是 asyncDataCount個 8bytes => U2FHID_FRAME size 64bytes
    //      asyncDataCount 是 64/8
    //AsyncCompletion(ivars->notifyFrameAction, kIOReturnSuccess, args, asyncDataCount);
    //os_log(OS_LOG_DEFAULT, LOG_PREFIX "After AsyncCompletion args = %llu, count = %u", (uint64_t) args, asyncDataCount);
    // source 內部會 check:   asyncDataCount 要小於 Array Size
    // if (asyncDataCount > (sizeof(msg->content.__asyncData) / sizeof(msg->content.__asyncData[0]))) return;
    AsyncCompletion(ivars->notifyFrameAction, kIOReturnSuccess, ivars->notifyArgs, asyncDataCount);
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "After AsyncCompletion args = %llu, count = %u", (uint64_t) ivars->notifyArgs, asyncDataCount);
    return kIOReturnSuccess;
    // 舊的參考
    //    numArgs = sizeof(U2FHID_FRAME) / sizeof(io_user_reference_t) = 64/8
    //io_user_reference_t *args = (io_user_reference_t *)reportMap->getAddress();
    //sendAsyncResult64(*_notifyRef, kIOReturnSuccess, args, sizeof(U2FHID_FRAME) / sizeof(io_user_reference_t));
}

/* IOUserClientMethodDispatch
 IOUserClientMethodFunction function;
 uint32_t                   checkCompletionExists;
 uint32_t                   checkScalarInputCount;
 uint32_t                   checkStructureInputSize;
 uint32_t                   checkScalarOutputCount;
 uint32_t                   checkStructureOutputSize;
 */
//const IOUserClientMethodDispatch sMethods[kNumberOfMethods] = {
//    {(IOUserClientMethodFunction)&SoftFido2UserClient::sSendFrame, 0, 0, sizeof(U2FHID_FRAME), 0, 0},
//    {(IOUserClientMethodFunction)&SoftFido2UserClient::sNotifyFrame, 0, 0, 0, 0, sizeof(U2FHID_FRAME)},
//};
//SoftFido2UserClient *target, uint64_t* reference, IOUserClientMethodArguments *arguments
kern_return_t sSendFrame(SoftFido2UserClient* target, IOUserClientMethodArguments *arguments) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "sSendFrame");
    size_t frameLength = 0;
    U2FHID_FRAME* frame = nullptr;

    if (arguments->structureInput != nullptr) {
        OSData* input = arguments->structureInput;
        frameLength = input->getLength();
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "  SendFrame input length = %lu", frameLength);
        frame = (U2FHID_FRAME*) input->getBytesNoCopy();
    } else {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "  SendFrame arguments->structureInput is null");
    }
    // ---- Debug Stop ----
    if (frameLength == HID_RPT_SIZE) {
        return target->sendFrame(frame, frameLength);
    }
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "  SendFrame kIOReturnBadArgument");
    return kIOReturnBadArgument;
}


// 心得:
//      - ExternalMethod的 arguments->structureInput 有值，但透過 super::ExternalMethod 傳入 static sSendFrame就變null
//      - 而 reference 有值，只是不知道是什麼? 是宣告的 U2FHID_FRAME ?
kern_return_t SoftFido2UserClient::ExternalMethod(uint64_t selector,
                                                  IOUserClientMethodArguments* arguments,
                                                  const IOUserClientMethodDispatch* dispatch,
                                                  OSObject* target,
                                                  void* reference) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "------------------<ExternalMethod>------------------");
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod selector = %llu", selector);
    
    // ExternalMethod target is null.
    // ExternalMethod dispatch is null.
    // ExternalMethod reference is null.
    if (reference != nullptr) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod reference ✅");
    } else {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod reference = null");
    }
    
    debugArguments(arguments);
    // 區分不同的 selector，對應到不同的行為
    // 交由 IODispatchQueue 來處理
    switch (selector) {
        case kSoftFidoUserClientSendFrame: {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod SendFrame");
            //【嘗試方法】此處直接取用 arguments->structureInput，是可以取到內容。
            //  直接呼叫 sendFrame好像更方便
            return sSendFrame(this, arguments);
            //【傳統方法】透過此法呼叫 sSendFrame, reference的資料是傳入 U2FHID_FRAME，不需要自己轉換。
            //  應該是在宣告時有指定 checkStructureInputSize
            // sSendFrame, 0, sizeof(U2FHID_FRAME), 0, 0
            //dispatch = &sMethods[kSoftFidoUserClientSendFrame];
            //target = this;
            //return super::ExternalMethod(selector, arguments, dispatch, target, reference);
        }
        // 測試直接回傳
//        case kSoftFidoUserClientNotifyFrame + 1: {
//            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod NotifyFrame (Sync)");
//            U2FHID_FRAME frame;
//            frame.type = 0xFF;
//            frame.cid = 123;
//            arguments->structureOutput = OSData::withBytes(&frame, sizeof(frame));
//            return kIOReturnSuccess;
//        }
        case kSoftFidoUserClientNotifyFrame: {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod NotifyFrame (Async)");
            //dispatch = &sMethods[kSoftFidoUserClientNotifyFrame];
            //target = this;
            ivars->notifyFrameAction = arguments->completion;
            ivars->notifyFrameAction->retain();
            //
//            if (arguments->structureOutputDescriptor != nullptr) {
//                ivars->ouputDescriptor = arguments->structureOutputDescriptor;
//                ivars->ouputDescriptor->retain();
//            }
            return kIOReturnSuccess;
            //return super::ExternalMethod(selector, arguments, dispatch, target, reference);   // 用這個會失敗!? 可能是 定義的 與 傳入的不符合
        }
        default:
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod No support selector : %llu", selector);
            break;
    }
    
    return kIOReturnBadArgument;
}


//virtual kern_return_t sendFrame(U2FHID_FRAME *frame, size_t frameSize);
kern_return_t IMPL(SoftFido2UserClient, sendFrame) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "sendFrame refCount = %lu", frameSize);
    // 把資料做成 report
    IOMemoryDescriptor* report = nullptr;
    auto ret = BufMemoryUtils::createWithBytes(frame, frameSize, &report);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "sendFrame > create report failed");
        return ret;
    }
    uint64_t reportLength;
    ret = report->GetLength(&reportLength);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "sendFrame > get report length failed");
        return ret;
    }
    ret = ivars->fido2Device->handleReport(mach_absolute_time(), report, static_cast<uint32_t>(reportLength), kIOHIDReportTypeInput, 0);
    if (ret != kIOReturnSuccess) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "sendFrame > fido2Device->handleReport failed");
    }
    return ret;
}

/*
 CopyClientMemoryForType(\
     uint64_t type,\
     uint64_t * options,\
     IOMemoryDescriptor ** memory,\
     OSDispatchMethod supermethod = NULL);\
 */
kern_return_t IMPL(SoftFido2UserClient, CopyClientMemoryForType) {
    os_log(OS_LOG_DEFAULT, LOG_PREFIX "CopyClientMemoryForType = %llu", type);
    kern_return_t ret;
    if (type == 0) {
        IOBufferMemoryDescriptor* buffer = nullptr;
        ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut, 128 /* capacity */, 8 /* alignment */, &buffer);
        if (ret != kIOReturnSuccess) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "CopyClientMemoryForType > IOBufferMemoryDescriptor::Create failed: 0x%x", ret);
        } else {
            *memory = buffer; // returned with refcount 1
        }
    } else {
        ret = super::CopyClientMemoryForType(type, options, memory);
    }
    return ret;
}


#pragma mark - Debug

void debugArguments(IOUserClientMethodArguments* arguments) {
    if (arguments != nullptr) {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->version = %llu", arguments->version);
        
        if (arguments->completion != nullptr) { // Async 才會有
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->completion ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->completion = null");
        }
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->scalarInputCount = %u", arguments->scalarInputCount);
        if (arguments->scalarInput != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->scalarInput ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->scalarInput = null");
        }
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->scalarOutputCount = %u", arguments->scalarOutputCount);
        if (arguments->scalarOutput != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->scalarOutput ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->scalarOutput = null");
        }
        if (arguments->structureInput != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureInput ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureInput = null");
        }
        if (arguments->structureInputDescriptor != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureInputDescriptor ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureInputDescriptor = null");
        }
        
        if (arguments->structureOutput != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureOutput ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureOutput = null");
        }
        
        if (arguments->structureOutputDescriptor != nullptr) {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureOutputDescriptor ✅");
        } else {
            os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureOutputDescriptor = null");
        }
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments->structureOutputMaximumSize = %llu", arguments->structureOutputMaximumSize);
    } else {
        os_log(OS_LOG_DEFAULT, LOG_PREFIX "ExternalMethod arguments is null!");
    }
}
