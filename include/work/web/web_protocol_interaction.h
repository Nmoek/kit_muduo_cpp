/**
 * @file web_protocol_interaction.h
 * @brief 测试协议项交互详情 web层接口
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-29 17:07:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef _KIT_WEB_PROTOCOL_INTERACTION_H__
#define _KIT_WEB_PROTOCOL_INTERACTION_H__

#include "net/call_backs.h"

namespace kit_muduo::ws{
class WebSocketServer;
}

namespace kit_domain {

class ProjectSvcInterface;
class ProtocolSvcInterface;
class RuntimeControllerInterface;

class ProtocolInteractionHandler
{
public:
    ProtocolInteractionHandler(std::shared_ptr<ProtocolSvcInterface> svc, std::shared_ptr<RuntimeControllerInterface> runtime_controller);
    ~ProtocolInteractionHandler() = default;

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

public:

private:
    /**
     * @brief websocket session刚建立时业务处理
     * @param session 
     * @param ctx 
     * @return true 
     * @return false 
     */
    bool onOpen(kit_muduo::WebSocketSessionPtr session, kit_muduo::HttpContextPtr ctx) noexcept;

    void onText(kit_muduo::WebSocketSessionPtr session, const std::string &payload) noexcept;
    void onClose(kit_muduo::WebSocketSessionPtr session) noexcept;
    void onError(kit_muduo::WebSocketSessionPtr session, kit_muduo::ws::CloseCode code, const std::string &reason) noexcept;

private:
    std::shared_ptr<ProtocolSvcInterface> pc_svc_;
    std::shared_ptr<RuntimeControllerInterface> runtime_controller_;
};


}
#endif //_KIT_WEB_PROTOCOL_INTERACTION_H__