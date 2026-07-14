/**
 * @file web_protocol_interaction.cpp
 * @brief 测试协议项交互详情 web层接口
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-29 17:22:47
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/time_stamp.h"
#include "net/event_loop.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"
#include "net/call_backs.h"
#include "net/endian.h"
#include "net/http/http_servlet.h"
#include "net/net_data_converter.h"
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


#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
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


std::shared_ptr<std::vector<uint8_t>> BuildAttachmentBinaryMessage(uint64_t record_seq, const BinarySidecar &sidecar)
{
    const InteractionAttachmentRef &ref = sidecar.attachment_ref;
    const auto &bytes = sidecar.bytes;
    if(!bytes)
    {
        PCINTERAC_F_ERROR("attachment binary empty! \n");
        return nullptr;
    }
    nlohmann::json header{
        {"type", "attachment"},
        {"record_seq", record_seq},
        {"attachment_id", ref.attachment_id},
        {"captured_size", bytes->size()},
        {"sha1", ref.sha1},
    };
    const std::string &header_text = header.dump();
    uint32_t header_len = static_cast<uint32_t>(header_text.size());

    auto out = std::make_shared<std::vector<uint8_t>>();
    out->reserve(sizeof(header_len) + header_text.size() + bytes->size());

    
    const auto& header_len_byte = ValueToBytes(SwapToBigEndian<uint32_t>(header_len));


    out->insert(out->end(), header_len_byte.begin(), header_len_byte.end());
    out->insert(out->end(), header_text.begin(), header_text.end());
    out->insert(out->end(), bytes->begin(), bytes->end());

    return out;
}


inline std::string LivePushStateToString(LivePushState state)
{
    switch(state)
    {
        case LivePushState::kPaused:
            return "paused";
        case LivePushState::kActive:
            return "resumed";
        case LivePushState::kClosed:
            return "closed";
        default:
            return "unknown";
    }
    
}

}

ProtocolInteractionHandler::ProtocolInteractionHandler(std::shared_ptr<ProtocolSvcInterface> svc, 
    std::shared_ptr<RuntimeControllerInterface> runtime_controller, 
    std::shared_ptr<ProtocolInteractionHub> hub)
    :pc_svc_(std::move(svc))
    ,runtime_controller_(runtime_controller)
    ,hub_(hub)
{

}


void ProtocolInteractionHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
    server->Ws("/ws/protocol-interactions/live", std::bind(&ProtocolInteractionHandler::onPrepare, this, std::placeholders::_1, std::placeholders::_2));
}


void ProtocolInteractionHandler::sendInteraction(kit_muduo::WebSocketSessionPtr session, ProtocolInteractionRecord record)
{
    auto live_info = std::static_pointer_cast<LiveConnectionInfo>(session->other());
    if(!live_info || LivePushState::kActive != live_info->push_state)
    {
        PCINTERAC_F_ERROR("protocol interaction live not active!\n");
        return;
    }

    nlohmann::json root;
    root["type"] = "interaction";
    root["record"] = record;

    WebSocketSession::BinaryGroup group;
    for(const auto &bs : record.binary_sidecars)
    {
        if(bs.bytes && !bs.bytes->empty())
        {
            auto binary_msg_bytes = BuildAttachmentBinaryMessage(record.seq, bs);
            if(binary_msg_bytes)
            {
                group.push_back(binary_msg_bytes);
            }
            else 
            {
                PCINTERAC_F_WARN("build attachment binary message null: seq[%lu], pjId[%ld], pcId[%ld], peer[%s]\n", record.seq,
                    record.project_id,
                    record.protocol_id,
                    record.peer_addr.c_str());
            }

        }
    }
    session->sendMessageGroup(root.dump(), group);
}

void ProtocolInteractionHandler::sendAckMsg(kit_muduo::WebSocketSessionPtr session, const LiveConnectionInfo& info)
{
    const auto state = info.push_state.load();
    if(LivePushState::kClosed == state)
    {
        PCINTERAC_F_ERROR("interaction live not active!\n");
        return;
    }
    LiveServerMsg msg;
    msg.type = "state";
    msg.state = LivePushStateToString(state);
    msg.accepted_seq = info.accepted_msg_seq_.load();
    msg.timestamp = TimeStamp::NowMs();
    nlohmann::json root = msg;
    session->sendText(root.dump());
}

bool ProtocolInteractionHandler::onPrepare(kit_muduo::WebSocketSessionPtr session, kit_muduo::HttpContextPtr ctx) noexcept
{
    // 1. 校验业务协议项权限
    auto req = ctx->request();

    int64_t protocol_id = 0;
    if(!ParsePositiveArithmetic(ctx->queryParam("protocol_id"), protocol_id))
    {
        PCINTERAC_F_ERROR("query param 'protocol_id' invalid: %ld\n", protocol_id);
        WriteJsonError(ctx, -200, "query param 'protocol_id' invalid");
        return false;
    }

    // TODO limit参数暂时无用
#if 0
    int32_t limit = 0;
    if(!ParsePositiveArithmetic(ctx->queryParam("limit"), limit))
    {
        PCINTERAC_F_ERROR("query param 'limit' invalid");
        WriteJsonError(ctx, -200, "query param 'limit' invalid");
        return false;
    }
    limit = std::max(1, std::min(limit, 100));
#endif

    ProtocolAccessInfo access_info;
    if(!pc_svc_->GetAccessInfo(ctx, protocol_id, access_info))
    {
        WriteForbidden(ctx);
        return false;
    }

    // 2. 事件订阅
    auto live_info = std::make_shared<LiveConnectionInfo>();
    live_info->session_id = session->sessionId();
    live_info->project_id = access_info.project_id;
    live_info->protocol_id = access_info.protocol_id;
    live_info->push_state = LivePushState::kInit;

    session->setOther(live_info);

    // 3. 绑定Websocket相关回调函数
    session->setWSOnOpenCb(WEBSOCKET_CB1(onOpen));
    session->setWSTextMessageCb(WEBSOCKET_CB2(onText));
    session->setCloseCb(WEBSOCKET_CB1(onClose));
    session->setWSErrorCb(WEBSOCKET_CB3(onError));


    return true;
}

void ProtocolInteractionHandler::onSubscribe(kit_muduo::WebSocketSessionPtr session, ProtocolInteractionRecord record)
{
    if(!session || !session->isOpen())
    {
        PCINTERAC_F_DEBUG("websocket session not open \n");
        return;
    }
    sendInteraction(session, std::move(record));
}

void ProtocolInteractionHandler::onOpen(kit_muduo::WebSocketSessionPtr session)
{
    auto info = std::static_pointer_cast<LiveConnectionInfo>(session->other());
    if(!info || LivePushState::kInit != info->push_state.load())
    {
        PCINTERAC_F_INFO("interaction live not active\n");
        return;
    }

    ProtocolInteractionSubscribeFilter filter{
        .project_id = info->project_id,
        .protocol_id = info->protocol_id,
        .include_project_notice = true,
    };
    std::weak_ptr<WebSocketSession> weak_session{session};

    // 注意 先改变状态
    info->push_state = LivePushState::kActive;

    auto subscription = hub_->subscribe(filter, [this, weak_session](ProtocolInteractionRecord record){

        onSubscribe(weak_session.lock(), std::move(record));
    });
    if(subscription.subscriber_id <= 0)
    {   
        info->push_state = LivePushState::kClosed;

        // websession层 直接关闭
        session->close(CloseCode::kServerError, "interaction subscribe error");
        return;
    }
    info->subscriber_id = subscription.subscriber_id;

    // 告诉客户端业务连接已经建立
    nlohmann::json root;
    root["type"] = "live_ready";
    root["project_id"] = info->project_id;
    root["protocol_id"] = info->protocol_id;
    root["start_record_seq"] = subscription.start_record_seq;
    root["session_id"] = session->sessionId();
    root["timestamp"] = kit_muduo::TimeStamp::NowMs();

    session->sendText(root.dump());
}

// 注意：这里命令控制需要做成:ack幂等
void ProtocolInteractionHandler::onText(kit_muduo::WebSocketSessionPtr session, const std::string &payload) noexcept
{
    auto info = std::static_pointer_cast<LiveConnectionInfo>(session->other());
    if(!info)
    {
        PCINTERAC_F_INFO("interaction live not active\n");
        return;
    }
    const auto state = info->push_state.load();
    if(LivePushState::kClosed == state 
        || LivePushState::kInit == state)
    {
        PCINTERAC_F_INFO("interaction live not active\n");
        return;
    }
    LiveClientMsg msg;
    if(!parseClientControlMessage(*info, payload, msg))
    {
        sendBusinessError(session, "bad_message", "invalid websocket client message");
        return;
    }

    const uint64_t accepted_msg_seq = info->accepted_msg_seq_.load();

    // 重复 、过期消息
    if(msg.client_seq <= accepted_msg_seq)
    {
        sendAckMsg(session, *info);
        return;
    }

    // 跳变序列号说明不合法
    if(msg.client_seq != accepted_msg_seq + 1)
    {
        sendControlSeqError(session, "seq_gap", "websocket client message seq gap", *info);
        return;
    }

    if("pause" == msg.type)
    {
        info->push_state = LivePushState::kPaused;
    }
    else if("resume" == msg.type)
    {
        info->push_state = LivePushState::kActive;
    }
    else
    {
        sendBusinessError(session, "bad_message", "invalid websocket client message");
        return;
    }
    info->accepted_msg_seq_ = msg.client_seq;
    sendAckMsg(session, *info);
    return;
}

void ProtocolInteractionHandler::onClose(kit_muduo::WebSocketSessionPtr session) noexcept
{
    auto info = std::static_pointer_cast<LiveConnectionInfo>(session->other());
    if(!info)
    {
        PCINTERAC_F_ERROR("live info not found!\n");
        return;
    }
    info->push_state = LivePushState::kClosed;
    if(info->subscriber_id > 0)
    {
        hub_->unsubcribe(info->subscriber_id);
        info->subscriber_id = 0;
    }
}

void ProtocolInteractionHandler::onError(kit_muduo::WebSocketSessionPtr session, kit_muduo::ws::CloseCode code, const std::string &reason) noexcept
{
    auto info = std::static_pointer_cast<LiveConnectionInfo>(session->other());
    if(!info)
    {
        PCINTERAC_F_ERROR("live info not found!\n");
        return;
    }
    info->push_state = LivePushState::kClosed;
    if(info->subscriber_id > 0)
    {
        hub_->unsubcribe(info->subscriber_id);
        info->subscriber_id = 0;
    }
}



void ProtocolInteractionHandler::sendBusinessError(kit_muduo::WebSocketSessionPtr session, const std::string &bs_code, const std::string &message)
{
    auto info = std::static_pointer_cast<LiveConnectionInfo>(session->other());
    if(!info || LivePushState::kClosed == info->push_state)
    {
        PCINTERAC_F_ERROR("live info not active!\n");
        return;
    }
    nlohmann::json root;
    root["type"] = "error";
    root["code"] = bs_code;
    root["error_message"] = message;
    session->sendText(root.dump());
}

void ProtocolInteractionHandler::sendControlSeqError(kit_muduo::WebSocketSessionPtr session, const std::string &bs_code, const std::string &message, const LiveConnectionInfo &info)
{
    const auto state = info.push_state.load();
    if(LivePushState::kClosed == state)
    {
        PCINTERAC_F_ERROR("live info not active!\n");
        return;
    }
    nlohmann::json root;
    root["type"] = "error";
    root["code"] = bs_code;
    root["error_message"] = message;
    root["accepted_seq"] = info.accepted_msg_seq_.load(std::memory_order_relaxed);
    root["state"] = LivePushStateToString(state);
    root["timestamp"] = TimeStamp::NowMs();

    session->sendText(root.dump());
}


bool ProtocolInteractionHandler::parseClientControlMessage(LiveConnectionInfo & info, const std::string &payload, LiveClientMsg &msg)
{
    try {
        msg = nlohmann::json::parse(payload).get<LiveClientMsg>();

        if(msg.session_id != info.session_id)
        {
            PCINTERAC_F_ERROR("interaction session mismatched: %lu --> %lu\n", msg.session_id, info.session_id);
            return false;
        }

        if(msg.type != "pause" && msg.type != "resume")
        {
            PCINTERAC_F_ERROR("interaction client type invalid: %s\n", msg.type.c_str());
            return false;
        }
  
        PCINTERAC_F_DEBUG("interaction client msg: type[%s], client_seq[%ld], timestamp[%lu]\n", msg.type.c_str(), msg.client_seq, msg.timestamp);

        return true;

    } catch (const std::exception &e) {

        PCINTERAC_F_ERROR("parseClientControlMessage exception: %s \n", e.what());
        return false;
    }
}


}