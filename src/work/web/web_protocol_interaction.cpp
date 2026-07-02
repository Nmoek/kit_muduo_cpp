/**
 * @file web_protocol_interaction.cpp
 * @brief 测试协议项交互详情 web层接口
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-29 17:22:47
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "net/call_backs.h"
#include "net/http/http_servlet.h"
#include "web/web_log.h"
#include "web/web_protocol_interaction.h"
#include "net/http/http_server.h"
#include "net/websocket/websocket_server.h"
#include "net/websocket/websocket_session.h"
#include "runtime/runtime_controller.h"
#include "svc_protocol.h"
#include "net/http/http_context.h"
#include "web/web_common.h"
#include "domain/protocol.h"
#include "domain/type.h"

#include <functional>
#include <utility>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_muduo::ws;

namespace kit_domain {

namespace {
#define WEBSOCKET_CB1(NAME) ([this](auto && arg1){\
    NAME(\
        std::forward<decltype(arg1)>(arg1) \
    );\
})

#define WEBSOCKET_CB2(NAME) ([this](auto && arg1, auto && arg2){\
    NAME(\
        std::forward<decltype(arg1)>(arg1), \
        std::forward<decltype(arg2)>(arg2) \
    );\
})

#define WEBSOCKET_CB3(NAME) ([this](auto && arg1, auto && arg2, auto && arg3){\
    NAME(\
        std::forward<decltype(arg1)>(arg1), \
        std::forward<decltype(arg2)>(arg2), \
        std::forward<decltype(arg3)>(arg3) \
    );\
})

}

ProtocolInteractionHandler::ProtocolInteractionHandler(std::shared_ptr<ProtocolSvcInterface> svc, std::shared_ptr<RuntimeControllerInterface> runtime_controller)
    :pc_svc_(std::move(svc))
    ,runtime_controller_(runtime_controller)
{

}


void ProtocolInteractionHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
    server->Ws("/ws/protocol-interactions", std::bind(&ProtocolInteractionHandler::onOpen, this, std::placeholders::_1, std::placeholders::_2));

}


bool ProtocolInteractionHandler::onOpen(kit_muduo::WebSocketSessionPtr session, kit_muduo::HttpContextPtr ctx) noexcept
{
    // 1. 校验业务协议项权限
    auto req = ctx->request();

    int64_t protocol_id = 0;
    if(!ParsePositiveArithmetic(ctx->queryParam("protocol_id"), protocol_id))
    {
        PCINTERAC_F_ERROR("query param 'protocol_id' invalid");
        WriteJsonError(ctx, -200, "query param 'protocol_id' invalid");
        return false;
    }

    int32_t limit = 100;
    if(!ParsePositiveArithmetic(ctx->queryParam("limit"), limit))
    {
        PCINTERAC_F_ERROR("query param 'limit' invalid");
        WriteJsonError(ctx, -200, "query param 'limit' invalid");
        return false;
    }
    limit = std::max(1, std::min(limit, 100));

    ProtocolAccessInfo access_info;
    if(!pc_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        WriteForbidden(ctx);
        return false;
    }


    // 2. TODO 初始化 Interaction 相关的ring buffer资源

    // 3. TODO 绑定Websocket相关回调函数

    // session->setWSTextMessageCb(WEBSOCKET_CB2(onText));
    // session->setCloseCb(WEBSOCKET_CB1(onClose));
    // session->setWSErrorCb(WEBSOCKET_CB3(onError));

    return true;
}

}