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

#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"
#include "net/call_backs.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <atomic>

namespace kit_muduo::ws{
class WebSocketServer;

}

namespace kit_domain {

class ProjectSvcInterface;
class ProtocolSvcInterface;
class RuntimeControllerInterface;


enum class LivePushState
{
    kInit = 0,
    kActive,
    kPaused,
    kClosed,
};

struct LiveConnectionInfo
{
    /// @brief session Id
    uint64_t session_id{0};
    /// @brief 观测项目Id
    int64_t project_id{0};
    /// @brief 观测协议Id
    int64_t protocol_id{0};
    /// @brief 订阅Id
    uint64_t subscriber_id{0};
    /// @brief 业务实时通信状态
    std::atomic<LivePushState> push_state{LivePushState::kClosed};
    /// @brief 主动发送的消息序列号
    std::atomic_uint64_t send_msg_seq_{0};
    /// @brief 被动收到的消息序列号
    std::atomic_uint64_t accepted_msg_seq_{0};
};
using LiveConnectionInfoPtr = std::shared_ptr<LiveConnectionInfo>;


struct LiveClientMsg
{
    std::string type;
    uint64_t session_id;
    uint64_t client_seq;
    uint64_t timestamp; // 单位 ms
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LiveClientMsg, type, session_id, client_seq, timestamp)
};

struct LiveServerMsg
{
    std::string type;
    std::string state;
    uint64_t accepted_seq;
    uint64_t timestamp; // 单位 ms
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LiveServerMsg, type, state, accepted_seq, timestamp)
};

class ProtocolInteractionHandler
{
public:
    ProtocolInteractionHandler(std::shared_ptr<ProtocolSvcInterface> svc, 
        std::shared_ptr<RuntimeControllerInterface> runtime_controller, 
        std::shared_ptr<ProtocolInteractionHub> hub);
    ~ProtocolInteractionHandler() = default;

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

    void sendInteraction(kit_muduo::WebSocketSessionPtr session, ProtocolInteractionRecord record);
    void sendAckMsg(kit_muduo::WebSocketSessionPtr session, const LiveConnectionInfo& info);

private:
    /**
     * @brief websocket session建立前业务准备
     * @param session 
     * @param ctx 
     * @return true 
     * @return false 
     */
    bool onPrepare(kit_muduo::WebSocketSessionPtr session, kit_muduo::HttpContextPtr ctx) noexcept;
    void onSubscribe(kit_muduo::WebSocketSessionPtr session, ProtocolInteractionRecord record);
    /*******WebSocket 回调处理****/
    void onOpen(kit_muduo::WebSocketSessionPtr session);
    void onText(kit_muduo::WebSocketSessionPtr session, const std::string &payload) noexcept;
    void onClose(kit_muduo::WebSocketSessionPtr session) noexcept;
    void onError(kit_muduo::WebSocketSessionPtr session, kit_muduo::ws::CloseCode code, const std::string &reason) noexcept;
    /*******WebSocket 回调处理****/

    bool parseClientControlMessage(LiveConnectionInfo & info, const std::string &payload, LiveClientMsg &msg);

    void sendBusinessError(kit_muduo::WebSocketSessionPtr session, const std::string &bs_code, const std::string &message);
    void sendControlSeqError(kit_muduo::WebSocketSessionPtr session, const std::string &bs_code, const std::string &message, const LiveConnectionInfo &info);
private:
    std::shared_ptr<ProtocolSvcInterface> pc_svc_;
    std::shared_ptr<RuntimeControllerInterface> runtime_controller_;
    std::shared_ptr<ProtocolInteractionHub> hub_;
};


}
#endif //_KIT_WEB_PROTOCOL_INTERACTION_H__