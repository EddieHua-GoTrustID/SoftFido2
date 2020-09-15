//
//  FidoHID.m
//  MacLogon
//
//  Created by Eddie Hua on 2019/10/4.
//  Copyright © 2019 GoTrustID. All rights reserved.
//

#import "FidoHID.h"
#import "softu2f.h"

static FidoHID* gU2fHid = nil;

@interface FidoHID()
@property (nonatomic) softu2f_ctx* ctx;
@property (nonatomic) NSMutableDictionary<NSNumber*, HIDMessageHandler>* handlers;

@property (atomic) NSThread* runThread;
@end

@implementation FidoHID

- (void) dealloc {
    if (_ctx != NULL) {
        NSLog(@"");
        softu2f_deinit(_ctx);
        _ctx = NULL;
    }
}

- (instancetype) init {
    _ctx = softu2f_init(SOFTU2F_DEBUG);
    self = [super init];
    if (_ctx != NULL && self != nil) {
        _handlers = [NSMutableDictionary new];
        _runThread = nil;
    } else {
        self = nil;
        _ctx = nil;
        NSLog(@"softu2f_init failed!");
    }
    gU2fHid = self;
    return self;
}

- (void) handleType:(CTAPHID_CMD) type Handler:(HIDMessageHandler) handler {
    if (_ctx == NULL) {
        return;
    }
    NSLog(@"type = %d", type);
    _handlers[@(type)] = handler;
    // register handler
    softu2f_hid_msg_handler_register(_ctx, type, my_message_handler);
}

bool my_message_handler(softu2f_ctx *ctx, softu2f_hid_message *req) {
    bool ret = false;
    if (req != NULL && gU2fHid != nil) {
        //req->cmd
        NSLog(@"Received message (code %d) on channel %d.\n", req->cmd, req->cid);

        HIDMessageHandler handler = gU2fHid.handlers[@(req->cmd)];
        if (handler != nil) {
            ret = handler(req);
        }
    }
    return ret;
}

- (bool) isRuning {
    return (_runThread != nil);
}

- (bool) run {
    if (_runThread != nil) {
        NSLog(@"SKIP, U2FHID thread running...");
        return false;
    }
    NSLog(@"Starting U2FHID thread");
    FidoHID* __weak weakSelf = self;
    _runThread = [[NSThread alloc] initWithBlock:^{
        NSLog(@"U2FHID thread started");
        FidoHID* u2fhid = weakSelf;
        if (u2fhid != nil) {
            if (u2fhid.ctx != nil) {
                softu2f_run(u2fhid.ctx);
            }
            if (u2fhid.runThread != nil) {
                [u2fhid.runThread cancel];
                u2fhid.runThread = nil;
            }
        }
        NSLog(@"U2FHID thread stopped");
        gU2fHid = nil;
    }];
    // 2. 启动线程
    [_runThread start];
    return true;
}

- (bool) stop {
    bool ok = false;
    if (_runThread) {
        NSLog(@"Stopping U2FHID thread");
        softu2f_shutdown(_ctx);
        // 等一下
        for (int i = 0; i < 3 ; i++) {
            if (_runThread.isFinished) {
                ok = true;
                break;
            } else {
                [NSThread sleepForTimeInterval:1];
            }
        }
        _runThread = nil;
    }
    return ok;
}
// 因為 FIDO2 用不到 CTAPHID_MSG，所以我就直接替換
- (bool) sendMsg:(NSData*) data CID:(uint32_t) cid {
    softu2f_hid_message msg;
    //msg.cmd = CTAPHID_MSG;
    msg.cmd = CTAPHID_CBOR;  // FIDO2 (MI_Windows 也用這個)
    msg.bcnt = (uint16_t) data.length;
    msg.cid = cid;
    msg.data = (__bridge CFDataRef) data;
    return softu2f_hid_msg_send(_ctx, &msg);
}
//- (bool) sendCbor:(NSData*) cbor CID:(uint32_t) cid {
//    softu2f_hid_message msg;
//    msg.cmd = CTAPHID_CBOR;  // FIDO2 (MI_Windows 也用這個)
//    msg.bcnt = (uint16_t) data.length;
//    msg.cid = cid;
//    msg.data = (__bridge CFDataRef) data;
//    return softu2f_hid_msg_send(_ctx, &msg);
//}
#pragma mark - FIDO2 -
- (bool) sendFido2Error:(CTAPHID_ERR_CODE) errCode CID:(uint32_t) cid {
    return softu2f_hid_err_send(_ctx, cid, errCode);
}
//
- (bool) sendKeepAliveStatus:(uint8_t) status CID:(uint32_t) cid {
    //LOGD(@"status = %x, cid = %u", status, cid);
    softu2f_hid_message msg;
    msg.cid = cid;
    msg.bcnt = 1;
    msg.cmd = CTAPHID_KEEPALIVE;
    msg.data = CFDataCreateWithBytesNoCopy(NULL, &status, 1, NULL);
    return softu2f_hid_msg_send(_ctx, &msg);
}
@end
