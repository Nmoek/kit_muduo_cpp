/**
 * @file web_protocol_interaction.cpp
 * @brief 测试协议项交互详情 web层接口
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-29 17:22:47
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/time_stamp.h"
#include "cppcodec/base64_rfc4648.hpp"
#include "net/event_loop.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"
#include "net/call_backs.h"
#include "net/endian.h"
#include "net/http/http_servlet.h"
#include "net/http/http_util.h"
#include "net/net_data_converter.h"
#include "nlohmann/json.hpp"
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
#include "domain/project_server.h"
#include "domain/protocol_item.h"


#include <atomic>
#include <cassert>
#include <charconv>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <type_traits>
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

template <typename T>
bool ParseStrictInteger(const std::string &value, T &out)
{
    static_assert(std::is_integral_v<T>, "T must be an integral type");

    if(value.empty())
    {
        return false;
    }

    T parsed{};
    const char *begin = value.data();
    const char *end = begin + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if(ec != std::errc{} || ptr != end)
    {
        return false;
    }

    out = parsed;
    return true;
}

template <typename T>
bool ParseOptionalQueryInteger(const HttpRequestPtr &req,
    const char *name,
    std::optional<T> &out,
    bool require_nonzero = false)
{
    if(!req->hasQureyParam(name))
    {
        return true;
    }

    T value{};
    if(!ParseStrictInteger(req->getQureyParam(name), value)
        || (require_nonzero && value == 0))
    {
        PCINTERAC_F_ERROR("parse query '%s' error!\n", name);
        return false;
    }

    out = value;
    return true;
}


std::shared_ptr<std::vector<uint8_t>> BuildAttachmentBinaryMessage(const InteractionRecord& record, const BinarySidecar &sidecar)
{
    const InteractionAttachmentRef &ref = sidecar.attachment_ref;
    const auto &bytes = sidecar.bytes;
    if(!bytes)
    {
        PCINTERAC_F_ERROR("attachment binary empty! \n");
        return nullptr;
    }
    nlohmann::json header;
    header["type"] =  "attachment";
    header["scope"] =  record.scope;
    header["project_id"] = record.project_id;
    header["protocol_id"] =  record.protocol_id;
    header["cache_instance_id"] =  record.cache_instance_id;
    header["record_seq"] = record.seq;
    header["attachment_id"] = ref.attachment_id;
    header["captured_size"] = bytes->size();
    header["sha1"] = ref.sha1;
 
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

bool ParseQueryUpgradeInitData(const HttpRequestPtr req, UpgradeInitReq &out)
{
    if(!req)
    {
        PCINTERAC_F_ERROR("parse query request null!\n");
        return false;
    }

    out = UpgradeInitReq{};

    int64_t protocol_id = 0;
    if(!req->hasQureyParam("protocol_id")
        || !ParseStrictInteger(req->getQureyParam("protocol_id"), protocol_id)
        || protocol_id <= 0)
    {
        PCINTERAC_F_ERROR("parse query 'protocol_id' error!\n");
        return false;
    }
    out.protocol_id = protocol_id;

    if(req->hasQureyParam("include_project_notice"))
    {
        int32_t include_project_notice = 0;
        if(!ParseStrictInteger(
                req->getQureyParam("include_project_notice"),
                include_project_notice)
            || (include_project_notice != 0 && include_project_notice != 1))
        {
            PCINTERAC_F_ERROR("parse query 'include_project_notice' error!\n");
            return false;
        }
        out.include_project_notice = include_project_notice == 1 ? true : false;
    }

    if(!ParseOptionalQueryInteger(
            req,
            "after_protocol_cache_instance_id",
            out.after_protocol_cache_instance_id,
            true))
    {
        return false;
    }
    if(!ParseOptionalQueryInteger(
            req,
            "after_protocol_seq",
            out.after_protocol_seq))
    {
        return false;
    }
    if(!ParseOptionalQueryInteger(
            req,
            "after_project_cache_instance_id",
            out.after_project_cache_instance_id,
            true))
    {
        return false;
    }
    if(!ParseOptionalQueryInteger(
            req,
            "after_project_seq",
            out.after_project_seq))
    {
        return false;
    }
    if (!IsValidInteractionCursorPair(
            out.after_protocol_cache_instance_id,
            out.after_protocol_seq)
        || !IsValidInteractionCursorPair(
            out.after_project_cache_instance_id,
            out.after_project_seq))
    {
        PCINTERAC_F_ERROR(
            "cache_instance_id and after_seq must be supplied as a valid pair\n");
        return false;
    }
    return true;
}

}

bool InteractionLiveContext::checkState(InteractionLiveCommand command)
{
    const auto state = this->state.load(std::memory_order_relaxed);
    switch (state)
    {
    case InteractionLiveState::kActive:
        return command == InteractionLiveCommand::kPause
            || command == InteractionLiveCommand::kQueryState;

    case InteractionLiveState::kPaused:
        return command == InteractionLiveCommand::kResume
            || command == InteractionLiveCommand::kQueryState;

    case InteractionLiveState::kCatchingUp:
        return command == InteractionLiveCommand::kQueryState;

    case InteractionLiveState::kInit:
    case InteractionLiveState::kClosed:
    default:
        return false;
    }
}

void InteractionLiveContext::setState(InteractionLiveState next_state)
{
    const auto state = this->state.load(std::memory_order_relaxed);
    this->state.store(next_state, std::memory_order_relaxed);
    switch (state)
    {
        case InteractionLiveState::kInit:
        {
            // kInit --> kCatchingUp
            // kInit --> kClosed
            if(InteractionLiveState::kCatchingUp == next_state
                || InteractionLiveState::kClosed == next_state)
            {
                ++generation;
            }
            break;
        }
        case InteractionLiveState::kCatchingUp:
        {
            // kCatchingUp --> kClosed
            if(InteractionLiveState::kClosed == next_state)
            {
                ++generation;
            }
            break;
        }
        case InteractionLiveState::kActive:
        {
            // kActive --> kPaused
            // kActive --> kClosed
            if(InteractionLiveState::kPaused == next_state
                || InteractionLiveState::kClosed == next_state)
            {
                ++generation;
            }
            break;
        }
        case InteractionLiveState::kPaused:
        {
            // kPaused --> kCatchingUp
            if(InteractionLiveState::kCatchingUp == next_state)
            {
                ++generation;
            }
            break;
        }
        case InteractionLiveState::kClosed:
        default:
            return;
    }
}

void InteractionLiveContext::markLiveCursor(const InteractionRecord& record)
{
    InteractionLiveCursor* cursor = nullptr;
    if (record.scope == InteractionScope::kProtocol)
    {
        cursor = &protocol_cursor;
    }
    else if (record.scope == InteractionScope::kProject)
    {
        cursor = &project_cursor;
    }
    else
    {
        return;
    }

    cursor->cache_instance_id = record.cache_instance_id;
    cursor->seq = record.seq;
}

void InteractionLiveContext::markLiveCatchUpCursor(InteractionScope scope, const InteractionCacheSnapshot& snapshot)
{
    InteractionLiveCursor* cursor = nullptr;
    if (scope == InteractionScope::kProtocol)
    {
        cursor = &protocol_cursor;
    }
    else if (scope == InteractionScope::kProject)
    {
        cursor = &project_cursor;
    }
    else
    {
        return;
    }
    cursor->cache_instance_id = snapshot.cache_instance_id;
    cursor->seq = snapshot.last_seq;
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

bool ProtocolInteractionHandler::onPrepare(kit_muduo::WebSocketSessionPtr session, kit_muduo::HttpContextPtr ctx) noexcept
{
    // 1. 校验业务协议项权限
    auto req = ctx->request();

    UpgradeInitReq init_req;
    if(!ParseQueryUpgradeInitData(req, init_req))
    {
        PCINTERAC_F_ERROR("parse query error!\n");
        ctx->response()->setStateCode(StateCode::k400BadRequest);

        WriteJsonError(ctx, -200, "parse query error");
        return false;
    }


    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx,  pc_svc_.get(), init_req.protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return false;
    }

    // 2. LiveConnectInfo初始化
    auto live = std::make_shared<InteractionLiveContext>();
    live->session_id = session->sessionId();
    live->project_id = access_info.project_id;
    live->protocol_id = access_info.protocol_id;
    live->state = InteractionLiveState::kInit;
    live->init_req = std::move(init_req);
    live->owner_loop = session->getLoop();

    if(!addLiveContext(live->session_id, live))
    {
        ctx->response()->setStateCode(StateCode::k500InternalServerError);

        WriteJsonError(ctx, -300, "duplicate webwcoket session");
        return false;
    }


    // 3. 绑定Websocket相关回调函数
    session->setWSOnOpenCb(WEBSOCKET_CB1(onOpen));
    session->setWSTextMessageCb(WEBSOCKET_CB2(onText));
    session->setCloseCb(WEBSOCKET_CB1(onClose));
    session->setWSErrorCb(WEBSOCKET_CB3(onError));


    return true;
}

void ProtocolInteractionHandler::onHubLiveArrive(std::weak_ptr<kit_muduo::ws::WebSocketSession> weak_session, std::weak_ptr<InteractionLiveContext> weak_live, uint64_t generation,  InteractionRecord record) noexcept
{
    auto session = weak_session.lock();
    auto live = weak_live.lock();
    if(!session || !live)
    {
        PCINTERAC_F_INFO("interaction not active\n");
        return;
    }


    // 特别注意: 这里相当于一直循环入队 直到恢复kActive
    if(InteractionLiveState::kCatchingUp == live->state.load(std::memory_order_relaxed))
    {
        PCINTERAC_F_DEBUG("interaction live record queue... seq[%lu] pjId[%ld], pcId[%lu], time_ms[%ld]\n", record.seq, record.project_id, record.protocol_id, record.time_ms);

        live->owner_loop->queueInLoop([this, weak_session, weak_live, generation, mv_record = std::move(record)](){
            onHubLiveArrive(weak_session, weak_live, generation, std::move(mv_record));
        });
        return;
    }

    // 真正执行点
    live->owner_loop->runInLoop([this, weak_session, weak_live, generation, mv_record = std::move(record)](){
        auto session = weak_session.lock();
        auto live = weak_live.lock();
        if(!session || !live)
        {
            return;
        }

        onHubLiveArriveInLoop(session, live, generation, std::move(mv_record));
    });

    
}

void ProtocolInteractionHandler::onOpen(kit_muduo::WebSocketSessionPtr session) noexcept
{
    uint64_t session_id = session->sessionId();
    auto live = findLiveContext(session_id);
    if(!live)
    {
        PCINTERAC_F_INFO("interaction live not active\n");
        session->close(CloseCode::kServerError, "interaction live not found");
        return;
    }

    std::weak_ptr<WebSocketSession> weak_session{session};
    std::weak_ptr<InteractionLiveContext> weak_live{live};
    
    // 注意open必须排队 101响应还未到达
    live->owner_loop->queueInLoop([this, weak_session, weak_live](){
        auto session = weak_session.lock();
        auto live = weak_live.lock();
        if(!session || !live)
        {
            return;
        }
        onOpenInLoop(session, live);
    });
    
}


// 注意：这里命令控制需要做成:ack幂等
void ProtocolInteractionHandler::onText(kit_muduo::WebSocketSessionPtr session, const std::string &payload) noexcept
{
    uint64_t session_id = session->sessionId();
    auto live = findLiveContext(session_id);
    if(!live)
    {
        PCINTERAC_F_INFO("interaction live not active\n");

        // 直接关闭websocket
        session->close(CloseCode::kServerError, "interaction live not found");
        return;
    }


    LiveCommandMsg msg;
    if(!parseLiveCommandMsg(*live, payload, msg))
    {
        sendBusinessError(session, "bad_message");
        return;
    }

    std::weak_ptr<kit_muduo::ws::WebSocketSession> weak_session{session};
    std::weak_ptr<InteractionLiveContext> weak_live{live};

    live->owner_loop->runInLoop([this, weak_session, weak_live, mv_msg = std::move(msg)](){
        auto session = weak_session.lock();
        auto live = weak_live.lock();
        if(!session || !live)
        {
            return;
        }
        onClientCommandInLoop(session, live, std::move(mv_msg));
    });


}

void ProtocolInteractionHandler::onClose(kit_muduo::WebSocketSessionPtr session) noexcept
{
    PCINTERAC_F_INFO("websocket closing... %s \n", session->peerAddr().toIpPort().c_str());

    auto live = removeLiveContext(session->sessionId());
    if(!live)
    {
        return;
    }

    live->owner_loop->runInLoop([this, live](){
        onCloseInLoop(live);
    });
}



void ProtocolInteractionHandler::onError(kit_muduo::WebSocketSessionPtr session, kit_muduo::ws::CloseCode code, const std::string &reason) noexcept
{
    PCINTERAC_F_ERROR("websocket error! code[%d]: %s \n", static_cast<uint16_t>(code), reason.c_str());

    auto live = removeLiveContext(session->sessionId());
    if(!live)
    {
        return;
    }

    live->owner_loop->runInLoop([this, live](){
        onCloseInLoop(live);
    });
}

void ProtocolInteractionHandler::sendLiveReady(
    kit_muduo::WebSocketSessionPtr session,
    InteractionLiveContextPtr live,
    const SubscribeWithCatchUpResult& capture_result,
    const std::string& trigger)
{
    if(!session || !live)
    {
        return;
    }

    const auto &protocol_snapshot = capture_result.protocol_cache_snapshot;
    const auto &project_snapshot_opt = capture_result.project_cache_snapshot;

    // 告诉客户端业务连接已经建立
    nlohmann::json root;
    root["type"] = "live_ready";
    root["trigger"] = trigger;
    root["project_id"] = live->project_id;
    root["protocol_id"] = live->protocol_id;
    root["session_id"] = session->sessionId();

    // protocol缓存快照信息
    root["protocol_cache_info"]["cache_instance_id"] = protocol_snapshot.cache_instance_id;
    root["protocol_cache_info"]["last_seq"] = protocol_snapshot.last_seq;
    root["protocol_cache_info"]["live_start_seq"] = protocol_snapshot.last_seq + 1;
    root["protocol_cache_info"]["catch_up_count"] = protocol_snapshot.incr_records.size();
    root["protocol_cache_info"]["catch_up_gap"] = protocol_snapshot.catch_up_gap;
    root["protocol_cache_info"]["cursor_reset"] = protocol_snapshot.cursor_reset;

    // project缓存快照信息
    root["project_cache_info"] = nlohmann::json::object();
    if(project_snapshot_opt.has_value())
    {
        root["project_cache_info"]["cache_instance_id"] = project_snapshot_opt->cache_instance_id;
        root["project_cache_info"]["last_seq"] = project_snapshot_opt->last_seq;
        root["project_cache_info"]["live_start_seq"] = project_snapshot_opt->last_seq + 1;
        root["project_cache_info"]["catch_up_count"] = project_snapshot_opt->incr_records.size();
        root["project_cache_info"]["catch_up_gap"] = project_snapshot_opt->catch_up_gap;
        root["project_cache_info"]["cursor_reset"] = project_snapshot_opt->cursor_reset;
    }
    root["timestamp"] = kit_muduo::TimeStamp::NowMs();

    session->sendText(root.dump());
}

void ProtocolInteractionHandler::sendInteraction(
    kit_muduo::WebSocketSessionPtr session,
    InteractionLiveContextPtr live,
    const InteractionRecord& record,
    const std::string& delivery)
{
    if(!session || !live)
    {
        return;
    }

    nlohmann::json root;
    root["type"] = "interaction";
    root["delivery"] = delivery;
    root["record"] = record;

    WebSocketSession::BinaryGroup group;
    for(const auto &bs : record.binary_sidecars)
    {
        if(bs.bytes && !bs.bytes->empty())
        {
            auto binary_msg_bytes = BuildAttachmentBinaryMessage(record, bs);
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

void ProtocolInteractionHandler::sendCommandAckMsg(
    kit_muduo::WebSocketSessionPtr session, 
    const InteractionLiveContextPtr& live,
    const LiveCommandMsg &req,
    bool ok,
    const std::string &error_message)
{
    if(!session || !live)
    {
        return;
    }

    LiveCommandAckMsg msg;
    msg.type = "state";
    msg.command = req.command;
    msg.ok = ok;
    msg.error_message = !ok ? error_message : "";
    msg.state = live->state.load(std::memory_order_relaxed);
    msg.client_seq = req.client_seq;
    msg.accepted_seq = live->accepted_msg_seq.load(std::memory_order_relaxed);
    msg.protocol_cursor = live->protocol_cursor;
    msg.project_cursor = live->project_cursor;
    msg.timestamp = TimeStamp::NowMs();

    nlohmann::json root = msg;
    session->sendText(root.dump());
}


void ProtocolInteractionHandler::cleanupLive(int64_t project_id, std::optional<int64_t> protocol_id)
{
    std::lock_guard<std::mutex> lock(mtx_);

    // 精准到protocol删除
    if(protocol_id.has_value())
    {
        for(auto &it : live_contexts_)
        {
            if(it.second && protocol_id.value() == it.second->protocol_id && project_id == it.second->project_id)
            {
                PCINTERAC_F_DEBUG("interaction live cleanup success! pjId[%ld], pcId[%ld]\n", project_id, protocol_id.value());

                InteractionLiveContextPtr live = it.second;
                it.second.reset();
                live->owner_loop->queueInLoop([this, live](){
                    onCloseInLoop(live);
                });

                live_contexts_.erase(it.first);
                break;
            }
        }

        return;
    }
    // project下全部删除
    for(auto it = live_contexts_.begin();it != live_contexts_.end();)
    {
        if(it->second && it->second->project_id == project_id)
        {
            PCINTERAC_F_DEBUG("interaction live cleanup all project success! pjId[%ld]\n", project_id);
            InteractionLiveContextPtr live = it->second;
            it->second.reset();
            live->owner_loop->queueInLoop([this, live](){
                onCloseInLoop(live);
            });

            it = live_contexts_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}



void ProtocolInteractionHandler::onOpenInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live)
{
    assert(live && live->isInOwnerLoop());

    if (live->state != InteractionLiveState::kInit)
    {
        live->setState(InteractionLiveState::kClosed);
        session->close(CloseCode::kServerError, "interaction live state invalid");
        return;
    }

    // kInit --> kCatchingUp
    live->setState(InteractionLiveState::kCatchingUp);

    std::string error_reason;
    if(!catchUpHelperInLoop(session, live, "open", error_reason))
    {
        PCINTERAC_F_ERROR("interaction catch up error: %s\n", error_reason.c_str());
        // kCatchingUp --> kClosed
        live->setState(InteractionLiveState::kClosed);
        session->close(CloseCode::kServerError, error_reason);
        return;
    }

    // kCatchingUp --> kActive
    live->setState(InteractionLiveState::kActive);

}


void ProtocolInteractionHandler::onHubLiveArriveInLoop(kit_muduo::WebSocketSessionPtr session , InteractionLiveContextPtr live, uint64_t generation,  InteractionRecord record)
{
    assert(live && live->isInOwnerLoop());

    if(live->generation != generation)
    {
        PCINTERAC_F_INFO("interaction record expired! %lu --> %lu\n", live->generation, generation);
        return;
    }
    const auto state = live->state.load(std::memory_order_relaxed);
    if(InteractionLiveState::kActive != state)
    {
        PCINTERAC_F_ERROR("interaction live not active!\n");
        return;
    }

    sendInteraction(session, live, std::move(record), "live");
    live->markLiveCursor(record);
}


void ProtocolInteractionHandler::onClientCommandInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live, LiveCommandMsg msg)
{
    assert(live && live->isInOwnerLoop());

    const uint64_t accepted_msg_seq = live->accepted_msg_seq.load(std::memory_order_relaxed);

    // 重复 、过期消息
    if(msg.client_seq <= accepted_msg_seq)
    {
        sendCommandAckMsg(session, live, msg, false, "seq duplicate  or expired");
        return;
    }

    // 跳变序列号说明不合法
    if(msg.client_seq != accepted_msg_seq + 1)
    {
        sendCommandAckMsg(session, live, msg, false, "seq gap");
        return;
    }

    if(!live->checkState(msg.command))
    {
        PCINTERAC_F_ERROR("interaction state invalid!\n");
        sendCommandAckMsg(session, live, msg, false, "state invalid");
        return;
    }

    if (InteractionLiveCommand::kQueryState == msg.command)
    {
        live->accepted_msg_seq.store(msg.client_seq, std::memory_order_relaxed);
        
        sendCommandAckMsg(session, live, msg, true);
        return;
    }
    else if (InteractionLiveCommand::kPause == msg.command)
    {
        // 取消订阅
        const uint64_t old_subscriber_id = live->subscriber_id;
        live->subscriber_id = 0;
        live->setState(InteractionLiveState::kPaused);
        live->accepted_msg_seq.store(msg.client_seq, std::memory_order_relaxed);

        if (old_subscriber_id != 0)
        {
            hub_->unsubcribe(old_subscriber_id);
        }
        sendCommandAckMsg(session, live, msg, true);
        return;
    }
    else if (InteractionLiveCommand::kResume == msg.command)
    {
        // 复用catchup 流程
        onResumeInLoop(session, live, msg);
        return;
    }

    sendBusinessError(session, "bad_message");

    return;
}

void ProtocolInteractionHandler::onCloseInLoop(InteractionLiveContextPtr live)
{
    assert(live && live->isInOwnerLoop());

    live->setState(InteractionLiveState::kClosed);
    if (live->subscriber_id != 0)
    {
        hub_->unsubcribe(live->subscriber_id);
    }
    live->subscriber_id = 0;
}


bool ProtocolInteractionHandler::catchUpHelperInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live, const std::string& trigger, std::string &error_reason)
{
    const auto &init_req = live->init_req;
    std::optional<uint64_t> after_protocol_cache_instance_id{std::nullopt};
    std::optional<uint64_t> after_protocol_seq{std::nullopt};
    std::optional<uint64_t> after_project_cache_instance_id{std::nullopt};
    std::optional<uint64_t> after_project_seq{std::nullopt};

    auto pj_server = runtime_controller_->findServer(live->project_id);
    if(!pj_server)
    {
        PCINTERAC_F_ERROR("runtime not find project server! \n");
        error_reason = "runtime error";
        return false;
    }

    auto runtime_result = pj_server->GetProtocolItem(live->protocol_id);
    if(!runtime_result.ok() || !runtime_result.val)
    {
        PCINTERAC_F_ERROR("runtime get protocol item error! %d:%s\n", runtime_result.error.toInt(), runtime_result.error.toMsg().c_str());

        error_reason = runtime_result.error.toMsg();
        return false;
    }
    const auto& pc_item = runtime_result.val;

    if("open" == trigger)
    {
        after_protocol_cache_instance_id = init_req.after_protocol_cache_instance_id;
        after_protocol_seq = init_req.after_protocol_seq;
        after_project_cache_instance_id = init_req.after_project_cache_instance_id;
        after_project_seq = init_req.after_project_seq;
    }
    else if("resume" == trigger)
    {
        after_protocol_cache_instance_id = live->protocol_cursor.cache_instance_id;
        after_protocol_seq = live->protocol_cursor.afterSeq();
        after_project_cache_instance_id = live->project_cursor.cache_instance_id;
        after_project_seq = live->project_cursor.afterSeq();
    }
    else
    {
        PCINTERAC_F_ERROR("interaction trigger invalid: %s\n", trigger.c_str());
        error_reason = "trigger invalid";
        return false;
    }

    InteractionSubscribeFilter filter{
        .project_id = live->project_id,
        .protocol_id = live->protocol_id,
        .include_project_notice = init_req.include_project_notice,
    };

    InteractionRecordCacheContainer container{
        .protocol_cache = pc_item->cache(),
        .project_cache = pj_server->cache(),
        .after_protocol_cache_instance_id = after_protocol_cache_instance_id,
        .after_protocol_seq = after_protocol_seq,
        .after_project_cache_instance_id = after_project_cache_instance_id,
        .after_project_seq = after_project_seq,
    };

    std::weak_ptr<WebSocketSession> weak_session{session};
    std::weak_ptr<InteractionLiveContext> weak_live{live};

    auto capture_result = hub_->subscribeWithCatchUp(filter, std::move(container), 
        [this, weak_session, weak_live, generation = live->generation](InteractionRecord record){

            PCINTERAC_F_DEBUG("new hub live data arrive ==> seq[%lu], pjId[%ld], pcId[%ld], cacheId[%lu], time_ms[%ld]\n", record.seq, record.project_id, record.protocol_id, record.cache_instance_id, record.time_ms);

            /**  订阅回调分两种情况:
                1. 已订阅但还处于catchup阶段 需要将当前的实时数据排队
                2. 已订阅 已经active阶段 直接发送
            */
            onHubLiveArrive(weak_session, weak_live, generation, std::move(record));

        }
    );
    // 订阅失败 websession层 直接关闭
    if(!capture_result.ok() || capture_result.subscription.subscriber_id <= 0)
    {
        error_reason = "interaction subscribe error";
        return false;
    }
    live->subscriber_id = capture_result.subscription.subscriber_id;

    const auto& protocol_cache_snapshot = capture_result.protocol_cache_snapshot;
    const auto& project_cache_snapshot_opt = capture_result.project_cache_snapshot;


    // 发送 live_ready消息
    sendLiveReady(session, live, capture_result, trigger);

    // 补发增量数据 catch_up
    for(const InteractionRecord& record : protocol_cache_snapshot.incr_records)
    {
        sendInteraction(session, live, record, "catch_up");
    }
    if(project_cache_snapshot_opt.has_value())
    {
        for (const InteractionRecord& record : project_cache_snapshot_opt->incr_records)
        {
            sendInteraction(session, live, record, "catch_up");
        }
    }

    // protocol 游标更新
    live->markLiveCatchUpCursor(InteractionScope::kProtocol, protocol_cache_snapshot);

    // project 游标更新
    if(project_cache_snapshot_opt.has_value())
    {
        live->markLiveCatchUpCursor(InteractionScope::kProject, project_cache_snapshot_opt.value());
    }

    return true;
}

void ProtocolInteractionHandler::onResumeInLoop(
    const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    const LiveCommandMsg& msg)
{
    assert(live && live->isInOwnerLoop());

    // kInit --> kCatchingUp
    live->setState(InteractionLiveState::kCatchingUp);

    std::string error_reason;
    if(!catchUpHelperInLoop(session, live, "resume", error_reason))
    {
        PCINTERAC_F_ERROR("interaction catch up error: %s\n", error_reason.c_str());
        // kCatchingUp --> kClosed
        live->setState(InteractionLiveState::kClosed);
        session->close(CloseCode::kServerError, error_reason);
        return;
    }

    // kCatchingUp --> kActive
    live->setState(InteractionLiveState::kActive);
    live->accepted_msg_seq.store(msg.client_seq, std::memory_order_relaxed);
    sendCommandAckMsg(session, live, msg, true);
}

bool ProtocolInteractionHandler::parseLiveCommandMsg(InteractionLiveContext & live, const std::string &payload, LiveCommandMsg &out)
{
    try {
        const auto &root = nlohmann::json::parse(payload);

        PCINTERAC_F_DEBUG("interaction client msg: \n%s\n", out.type.c_str(), root.dump(4).c_str());

        root.get_to<LiveCommandMsg>(out);

        if(out.session_id != live.session_id)
        {
            PCINTERAC_F_ERROR("interaction session mismatched: %lu --> %lu\n", out.session_id, live.session_id);
            return false;
        }

        if(out.type != "command")
        {
            PCINTERAC_F_ERROR("interaction msg type invalid: %s\n", out.type.c_str());
            return false;
        }

        if(InteractionLiveCommand::kUnknown == out.command)
        {
            PCINTERAC_F_ERROR("interaction command invalid: %d\n", static_cast<int>(out.command));
            return false;
        }

        return true;

    } catch (const std::exception &e) {

        PCINTERAC_F_ERROR("parseClientControlMessage exception: %s \n", e.what());
        return false;
    }
}

void ProtocolInteractionHandler::sendBusinessError(kit_muduo::WebSocketSessionPtr session, const std::string &bs_code)
{
    if(!session)
    {
        return;
    }
    nlohmann::json root;
    root["type"] = "error";
    root["code"] = bs_code;
    session->sendText(root.dump());
}

bool ProtocolInteractionHandler::addLiveContext(uint64_t session_id, InteractionLiveContextPtr live)
{
    std::lock_guard<std::mutex> lock(mtx_);
    return live_contexts_.emplace(session_id, live).second;
}

InteractionLiveContextPtr ProtocolInteractionHandler::findLiveContext(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = live_contexts_.find(session_id);
    return it == live_contexts_.end() ? nullptr : it->second;
}

InteractionLiveContextPtr ProtocolInteractionHandler::removeLiveContext(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = live_contexts_.find(session_id);
    if(it == live_contexts_.end())
    {
        return nullptr;
    }
    auto live = std::move(it->second);
    live_contexts_.erase(it);
    return live;
}

}
