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
#include "domain/protocol_interaction_observation.h"
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
#include <optional>
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

bool AddWireBytes(
    size_t payload_bytes,
    size_t& wire_bytes) noexcept
{
    //wire_bytes 使用每个 frame 最大 14 bytes header 做保守估算。实际 header 可能更小，但 batch 预算只能按上边界算
    constexpr size_t kHeaderLen = WebSocketSession::kMaxFrameHeaderBytes;

    if(payload_bytes > std::numeric_limits<size_t>::max() - kHeaderLen)
    {
        PCINTERAC_F_FATAL("payload bytes overflow max!\n");
        return false;
    }

    const size_t frame_bytes = payload_bytes + kHeaderLen;
    if(frame_bytes > std::numeric_limits<size_t>::max() - wire_bytes)
    {
        PCINTERAC_F_FATAL("frame bytes overflow max!\n");
        return false;
    }

    wire_bytes += frame_bytes;
    return true;
}

struct CatchUpCursorInput
{
    std::optional<uint64_t> protocol_cache_instance_id;
    std::optional<uint64_t> protocol_seq;
    std::optional<uint64_t> project_cache_instance_id;
    std::optional<uint64_t> project_seq;
};

/**
 * @brief 游标种类选择辅助函数
 * @param live 
 * @param trigger 
 * @return CatchUpCursorInput 
 */
CatchUpCursorInput ResolveCatchUpCursor(
    const InteractionLiveContext& live,
    CatchUpTrigger trigger)
{
    if(trigger == CatchUpTrigger::kOpen)
    {
        return CatchUpCursorInput{
            .protocol_cache_instance_id = live.init_req.after_protocol_cache_instance_id,
            .protocol_seq = live.init_req.after_protocol_seq,
            .project_cache_instance_id = live.init_req.after_project_cache_instance_id,
            .project_seq = live.init_req.after_project_seq,
        };
    }

    return CatchUpCursorInput{
        .protocol_cache_instance_id = live.protocol_cursor.cache_instance_id,
        .protocol_seq = live.protocol_cursor.afterSeq(),
        .project_cache_instance_id = live.project_cursor.cache_instance_id,
        .project_seq = live.project_cursor.afterSeq(),
    };
}

struct NextCatchUpRecord
{
    const InteractionRecord* record{nullptr};
    CatchUpRecordSource source{CatchUpRecordSource::kProtocolSnapshot};
};

/**
 * @brief 挑选snapshot数据下一条record
 * @param state 
 * @return NextCatchUpRecord 
 */
NextCatchUpRecord PeekNextSnapshotRecord(
    const CatchUpDeliveryState& state)
{
    if(state.protocol_index < state.protocol_cache_snapshot.incr_records.size())
    {
        return NextCatchUpRecord{
            .record = &state.protocol_cache_snapshot.incr_records[state.protocol_index],
            .source = CatchUpRecordSource::kProtocolSnapshot,
        };
    }

    if(state.project_cache_snapshot.has_value()
        && state.project_index < state.project_cache_snapshot->incr_records.size())
    {
        return NextCatchUpRecord{
            .record = &state.project_cache_snapshot->incr_records[state.project_index],
            .source = CatchUpRecordSource::kProjectSnapshot,
        };
    }

    return {};
}

void UpdateSnapshotIndex(CatchUpDeliveryState& state, CatchUpRecordSource source)
{
    if(source == CatchUpRecordSource::kProtocolSnapshot)
    {
        ++state.protocol_index;
    }
    else if(source == CatchUpRecordSource::kProjectSnapshot)
    {
        ++state.project_index;
    }
}

/**
 * @brief batch批数据加入判断规则
 * @param batch_bytes 
 * @param next_group_bytes 
 * @param max_batch_bytes 
 * @param batch_empty 
 * @return true 
 * @return false 
 */
bool CanAppendToBatch(size_t batch_bytes,
    size_t next_group_bytes,
    size_t max_batch_bytes,
    bool batch_empty) noexcept
{
    if(batch_empty)
    {
        // 单条 record 可以超过软上限，否则永远无法前进。
        return true;
    }

    if(batch_bytes > max_batch_bytes)
    {
        return false;
    }

    return next_group_bytes <= max_batch_bytes - batch_bytes;
}

} // namespace


PendingLiveRecord::PendingLiveRecord(InteractionRecord record)
    :record_(std::move(record))
    ,reserved_sidecar_bytes_(0)
    ,release_callback_(nullptr)
    ,reservation_active_(false)
{

}


PendingLiveRecord::PendingLiveRecord(
    InteractionRecord record,
    size_t reserved_sidecar_bytes,
    std::function<void()> release_callback)
    :record_(std::move(record))
    ,reserved_sidecar_bytes_(reserved_sidecar_bytes)
    ,release_callback_(std::move(release_callback))
{

}

PendingLiveRecord::~PendingLiveRecord() noexcept
{
    if(!reservation_active_ || !release_callback_)
    {
        return;
    }

    try {
        release_callback_();
    } catch(const std::exception& e) {
        PCINTERAC_F_ERROR("release pending reservation exception: %s\n", e.what());
    } catch(...) {
        PCINTERAC_F_ERROR("release pending reservation unknown exception\n");
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
    std::shared_ptr<ProtocolInteractionHub> hub,
    InteractionLiveConfig live_config)
    :pc_svc_(std::move(svc))
    ,runtime_controller_(runtime_controller)
    ,hub_(hub)
    ,live_config_(std::move(live_config))
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
    live->weak_session = session;

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

void ProtocolInteractionHandler::onHubLiveArrive(std::weak_ptr<kit_muduo::ws::WebSocketSession> weak_session, std::weak_ptr<InteractionLiveContext> weak_live, uint64_t generation, InteractionRecord record)
{
    auto live = weak_live.lock();
    if(!live || !live->owner_loop)
    {
        PCINTERAC_F_ERROR("interaction not active\n");
        return;
    }
    PendingLiveRecordPtr pending_record = nullptr;

    const auto state = live->state.load(std::memory_order_relaxed);

    if(InteractionLiveState::kCatchingUp == state)
    {
        pending_record = tryReserveCatchUpPending(live, generation, std::move(record));
        if(!pending_record)
        {
            return;
        }
    }
    else if(InteractionLiveState::kActive == state)
    {
        pending_record = std::make_shared<PendingLiveRecord>(std::move(record));
    }
    else 
    {
        return;
    }

    // catch-up 记录已经在进入 EventLoop pending 前预占；active 链路尚未接入该上限。

    // 这里必须排队 消除live数据跑到catchup数据前的强一致要求
    live->owner_loop->queueInLoop([this, weak_session, weak_live, generation, pending_record = std::move(pending_record)]() mutable {
        auto session = weak_session.lock();
        auto live = weak_live.lock();
        if(!session || !live)
        {
            return;
        }

        try {
            onHubLiveArriveInLoop(session, live, generation, std::move(pending_record));
        } catch(const std::exception &e) {
            PCINTERAC_F_ERROR("onHubLiveArriveInLoop exception: %s \n", e.what());

            abortInLoop(session, live, generation, e.what());
        } catch(...) {
            PCINTERAC_F_ERROR("onHubLiveArriveInLoop unknown exception \n");
            abortInLoop(session, live, generation, "unknown exception");
        }
        

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
        try {
            onOpenInLoop(session, live);
        } catch(const std::exception &e) {
            PCINTERAC_F_ERROR("onOpenInLoop exception: %s \n", e.what());

            abortInLoop(session, live, live->generation, e.what());
        } catch(...) {
            PCINTERAC_F_ERROR("onOpenInLoop unknown exception \n");

            abortInLoop(session, live, live->generation, "unknown exception");
        }        
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
        try {
            onClientCommandInLoop(session, live, std::move(mv_msg));
        } catch(const std::exception &e) {
            PCINTERAC_F_ERROR("onClientCommandInLoop exception: %s \n", e.what());

            abortInLoop(session, live, live->generation, e.what());
        } catch(...) {
            PCINTERAC_F_ERROR("onClientCommandInLoop unknown exception \n");

            abortInLoop(session, live, live->generation, " unknown exception");
        }
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
    CatchUpTrigger trigger)
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
    root["trigger"] = trigger == CatchUpTrigger::kOpen ? "open" : "resume";
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

bool ProtocolInteractionHandler::sendInteraction(
    kit_muduo::WebSocketSessionPtr session,
    InteractionLiveContextPtr live,
    const InteractionRecord& record,
    const std::string& delivery)
{
    if(!session || !live)
    {
        return false;
    }

    const auto& group = buildInteractionMessageGroup(record, delivery);
    if(!group.has_value())
    {
        PCINTERAC_F_ERROR("buildInteractionMessageGroup error!\n");
        return false;
    }

    return session->sendMessageGroups({std::move(group.value())});
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
    std::vector<InteractionLiveContextPtr> removed;

    {
        std::lock_guard<std::mutex> lock(mtx_);

        for(auto it = live_contexts_.begin();
            it != live_contexts_.end();)
        {
            const auto& live = it->second;
            const bool project_match =
                live && live->project_id == project_id;
            const bool protocol_match =
                !protocol_id.has_value()
                || (live
                    && live->protocol_id
                        == protocol_id.value());

            if(project_match && protocol_match)
            {
                PCINTERAC_F_DEBUG("interaction live cleanup success! pjId[%ld], pcId[%ld]\n", project_id, protocol_id.has_value() ? protocol_id.value() : 0);

                removed.push_back(std::move(it->second));
                it = live_contexts_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    // 锁外处理
    for(auto& live : removed)
    {
        if(!live || !live->owner_loop)
        {
            continue;
        }

        live->owner_loop->queueInLoop([this, live]() {
            auto session = live->weak_session.lock();
            
            onCloseInLoop(live);

            if(session && session->isOpen())
            {
                session->close(CloseCode::kNormalShutdown,"shutdown");
            }
        });
    }
}



void ProtocolInteractionHandler::onOpenInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live)
{
    assert(live && live->isInOwnerLoop());

    std::string error_reason;
    if(!beginCatchUpInLoop(session,
        live,
        CatchUpTrigger::kOpen,
        std::nullopt,
        error_reason))
    {
        abortInLoop(session, live, live->generation, error_reason);
    }
}


void ProtocolInteractionHandler::onHubLiveArriveInLoop(
    kit_muduo::WebSocketSessionPtr session,
    InteractionLiveContextPtr live,
    uint64_t generation,
    PendingLiveRecordPtr pending_record)
{
    assert(live && live->isInOwnerLoop());


    if(live->generation != generation)
    {
        PCINTERAC_F_INFO("interaction record expired! %lu --> %lu\n", live->generation, generation);
        return;
    }

    if(!pending_record)
    {
        PCINTERAC_F_INFO("interaction record null!\n");
        return;
    }

    const auto state = live->state.load(std::memory_order_relaxed);

    if(InteractionLiveState::kCatchingUp == state)
    {
        if(!live->catchup_state || live->catchup_state->generation != generation)
        {
            PCINTERAC_F_INFO("interaction catchup expired!\n");
            abortInLoop(session, live, generation, "catch-up state missing");
            return;
        }
        // catching-up 记录必须在 Hub 线程完成过预占。
        if(!pending_record->reservationActive())
        {
            PCINTERAC_F_ERROR("interaction catchup record dont pending reservation\n");
            return;
        }

        live->pending_live_records.push_back(std::move(pending_record));

        return;
    }

    if(InteractionLiveState::kActive == state)
    {
        InteractionRecord& live_record =  pending_record->record();
        if(sendInteraction(session, live, live_record, "live"))
        {
            live->markLiveCursor(live_record);
        }
        return;
    }

    PCINTERAC_F_ERROR("interaction live not active!\n");

    return;
}


void ProtocolInteractionHandler::onClientCommandInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live, LiveCommandMsg msg)
{
    assert(live && live->isInOwnerLoop());

    const uint64_t accepted_msg_seq = live->accepted_msg_seq.load(std::memory_order_relaxed);

    // 重复 、过期消息
    if(msg.client_seq <= accepted_msg_seq)
    {
        sendCommandAckMsg(session, live, msg, false, "seq duplicate or expired");
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
        try {
                // 复用catchup 流程
            onResumeInLoop(session, live, msg);

        } catch(const std::exception &e) {

            throw std::runtime_error(std::string("onResumeInLoop exception: ") + e.what());
        }

        return;
    }

    sendBusinessError(session, "bad_message");

    return;
}

void ProtocolInteractionHandler::onCloseInLoop(InteractionLiveContextPtr live)
{
    assert(live && live->isInOwnerLoop());

    stopCatchUpPendingAdmission(live, live->generation);
    cleanupCatchUpPending(live, live->generation);
    live->setState(InteractionLiveState::kClosed);
    live->catchup_state.reset();

    if(live->subscriber_id != 0)
    {
        hub_->unsubcribe(live->subscriber_id);
    }
    live->subscriber_id = 0;
}


void ProtocolInteractionHandler::onResumeInLoop(
    const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    const LiveCommandMsg& msg)
{
    assert(live && live->isInOwnerLoop());

    try {
        std::string error_reason;
        if(!beginCatchUpInLoop(session,
            live,
            CatchUpTrigger::kResume,
            msg,
            error_reason))
        {
            abortInLoop(session, live, live->generation, error_reason);
        }

    } catch(const std::exception &e) {
        PCINTERAC_F_ERROR("onResumeInLoop exception: %s \n", e.what());

        abortInLoop(session, live, live->generation, e.what());
    }

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


std::optional<kit_muduo::ws::WebSocketSession::MessageGroup> ProtocolInteractionHandler::buildInteractionMessageGroup(const InteractionRecord& record, const std::string& delivery)
{
    nlohmann::json root;
    root["type"] = "interaction";
    root["delivery"] = delivery;
    root["record"] = record;

    WebSocketSession::MessageGroup group;
    group.text_payload = root.dump();

    if(!AddWireBytes(group.text_payload.size(),  group.wire_bytes))
    {
        PCINTERAC_F_ERROR("interaction text payload size overflow: seq[%lu]\n", record.seq);
        return std::nullopt;
    }

    for(const auto &bs : record.binary_sidecars)
    {
        if(!bs.bytes || bs.bytes->empty())
        {
            PCINTERAC_F_WARN(
                "interaction sidecar unavailable: seq[%lu], attachment[%s]\n",
                record.seq,
                bs.attachment_ref.attachment_id.c_str());
            continue;
        }
    
        auto binary_payload  = BuildAttachmentBinaryMessage(record, bs);
        if(!binary_payload)
        {
            PCINTERAC_F_ERROR("build attachment binary message null: seq[%lu], pjId[%ld], pcId[%ld], peer[%s]\n", record.seq,
                record.project_id,
                record.protocol_id,
                record.peer_addr.c_str());
            return std::nullopt;
        }

        if(!AddWireBytes(binary_payload->size(),  group.wire_bytes))
        {
            PCINTERAC_F_ERROR("interaction binary payload size overflow: seq[%lu]\n", record.seq);
            return std::nullopt;
        }

        group.binary_payloads.push_back(std::move(binary_payload));
        
    }

    return group;
}

PendingLiveRecordPtr
ProtocolInteractionHandler::tryReserveCatchUpPending(const InteractionLiveContextPtr& live,
    uint64_t generation,
    InteractionRecord record)
{
    const size_t record_bytes = CalculateInteractionSidecarBytes(record);

    std::weak_ptr<InteractionLiveContext> weak_live{live};

    auto pending_record = std::make_shared<PendingLiveRecord>(std::move(record), record_bytes,
        [weak_live, record_bytes]() {
            auto live = weak_live.lock();
            if(!live)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(live->pending_limit_mtx);
            if(live->pending_live_count == 0
                || live->pending_live_bytes < record_bytes)
            {
                PCINTERAC_F_ERROR(
                    "interaction pending accounting underflow: "
                    "current[%lu][%lu], release[1][%lu]\n",
                    live->pending_live_count,
                    live->pending_live_bytes,
                    record_bytes);
                return;
            }

            --live->pending_live_count;
            live->pending_live_bytes -= record_bytes;
        });

    std::lock_guard<std::mutex> lock(live->pending_limit_mtx);

    if(live->pending_accepting_generation != generation)
    {
        return nullptr;
    }


    const bool count_limit_reached = live->pending_live_count >= live_config_.pending_live_max_records;

    const bool byte_limit_reached = record_bytes > live_config_.pending_live_max_bytes || live->pending_live_bytes > live_config_.pending_live_max_bytes - record_bytes;

    // 超过数量 或 大小bytes限制
    if(count_limit_reached || byte_limit_reached)
    {
        // TODO 热路径打印要去除 改为定时统计/定量统计
        PCINTERAC_F_ERROR("interaction catchup pending live limit overflow! count[%lu], size[]%lu]\n", live->pending_live_count, live->pending_live_bytes);

        return nullptr;
    }

    ++live->pending_live_count;
    live->pending_live_bytes += record_bytes;
    pending_record->setReservationActiveTrue();

    return pending_record;
}

void ProtocolInteractionHandler::startCatchUpPendingAdmission(const InteractionLiveContextPtr& live, uint64_t generation)
{
    std::lock_guard<std::mutex> lock(live->pending_limit_mtx);
    live->pending_accepting_generation = generation;
}

void ProtocolInteractionHandler::stopCatchUpPendingAdmission(const InteractionLiveContextPtr& live, uint64_t generation)
{
    std::lock_guard<std::mutex> lock(live->pending_limit_mtx);

    if(live->pending_accepting_generation == generation)
    {
        live->pending_accepting_generation = 0;
    }
}

void ProtocolInteractionHandler::cleanupCatchUpPending(const InteractionLiveContextPtr& live, uint64_t generation)
{
    assert(live && live->isInOwnerLoop());

    if(live->generation != generation)
    {
        return;
    }
    live->pending_live_records.clear();

    if(live->catchup_state && live->catchup_state->deferred_group.has_value() && CatchUpRecordSource::kPendingLive ==  live->catchup_state->deferred_group->source)
    {
        live->catchup_state->deferred_group.reset();
    }
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

bool ProtocolInteractionHandler::beginCatchUpInLoop(
    const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    CatchUpTrigger trigger,
    std::optional<LiveCommandMsg> resume_msg,
    std::string& error_reason)
{
    assert(live && live->isInOwnerLoop());

    const InteractionLiveState expectd = 
        trigger == CatchUpTrigger::kOpen ? InteractionLiveState::kInit : InteractionLiveState::kPaused;

    if(live->state.load(std::memory_order_relaxed) != expectd)
    {
        error_reason = "interaction live state invalid";
        return false;
    }

    live->setState(InteractionLiveState::kCatchingUp);
    const uint64_t generation = live->generation;
    startCatchUpPendingAdmission(live, generation);

    auto project_server =
        runtime_controller_->findServer(live->project_id);
    if(!project_server)
    {
        error_reason = "runtime project server not found";
        return false;
    }

    auto protocol_result =
        project_server->GetProtocolItem(live->protocol_id);
    if(!protocol_result.ok() || !protocol_result.val)
    {
        error_reason = protocol_result.error.toMsg();
        return false;
    }

    const CatchUpCursorInput& cursor = ResolveCatchUpCursor(*live, trigger);

    InteractionSubscribeFilter filter{
        .project_id = live->project_id,
        .protocol_id = live->protocol_id,
        .include_project_notice =
            live->init_req.include_project_notice,
    };

    InteractionRecordCacheContainer container{
        .protocol_cache = protocol_result.val->cache(),
        .project_cache = project_server->cache(),
        .after_protocol_cache_instance_id =
            cursor.protocol_cache_instance_id,
        .after_protocol_seq = cursor.protocol_seq,
        .after_project_cache_instance_id =
            cursor.project_cache_instance_id,
        .after_project_seq = cursor.project_seq,
    };

    std::weak_ptr<WebSocketSession> weak_session{session};
    std::weak_ptr<InteractionLiveContext> weak_live{live};

    SubscribeWithCatchUpResult capture_result = hub_->subscribeWithCatchUp(filter, std::move(container), 
    [this, weak_session, weak_live, generation](const InteractionRecord& record) {
        onHubLiveArrive(weak_session, weak_live, generation, record);
    });

    if(!capture_result.ok()
        || capture_result.subscription.subscriber_id == 0)
    {
        error_reason = "interaction subscribe error";
        return false;
    }
    const uint64_t subscriber_id = capture_result.subscription.subscriber_id;
    live->subscriber_id = subscriber_id;


    sendLiveReady(session, live, capture_result, trigger);

    auto catchup_state = std::make_unique<CatchUpDeliveryState>();
    catchup_state->generation = generation;
    catchup_state->trigger = trigger;
    catchup_state->protocol_cache_snapshot =
        std::move(capture_result.protocol_cache_snapshot);
    catchup_state->project_cache_snapshot =
        std::move(capture_result.project_cache_snapshot);
    catchup_state->resume_msg =
        std::move(resume_msg);

    live->catchup_state = std::move(catchup_state);

    scheduleNextCatchUpBatchInLoop(session, live, generation);

    return true;
}

void ProtocolInteractionHandler::scheduleNextCatchUpBatchInLoop(const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    uint64_t generation)
{
    assert(live && live->isInOwnerLoop());

    std::weak_ptr<WebSocketSession> weak_session{session};
    std::weak_ptr<InteractionLiveContext> weak_live{live};

    live->owner_loop->queueInLoop([this, weak_session, weak_live, generation]() {
        auto session = weak_session.lock();
        auto live = weak_live.lock();
        if(!session || !live)
        {
            return;
        }

        try {
            drainCatchUpBatchInLoop(session, live, generation);
        } catch(const std::exception &e) {
            PCINTERAC_F_ERROR("interaction drain catchup exception: %s \n", e.what());

            abortInLoop(session , live, generation, e.what());

        } catch(...) {
            PCINTERAC_F_ERROR("interaction drain catchup unknown exception\n");

            abortInLoop(session , live, generation, "drain catchup exception");
        }

    });

}

void ProtocolInteractionHandler::drainCatchUpBatchInLoop(const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    uint64_t generation)
{
    assert(live && live->isInOwnerLoop());

    if(live->generation != generation)
    {
        PCINTERAC_F_ERROR("interaction live generation invalid: %lu --> %lu\n", live->generation, generation);
        return;
    }

    if(live->state.load(std::memory_order_relaxed) != InteractionLiveState::kCatchingUp)
    {
        PCINTERAC_F_ERROR("interaction live state invalid\n");
        return;
    }

    if(!live->catchup_state || live->catchup_state->generation != generation)
    {
        PCINTERAC_F_ERROR("interaction catchup invalid\n");
        return;
    }

    CatchUpDeliveryState& catchup_state = *live->catchup_state;

    // snapshot 边界已提交后，这一 tick 只处理一个 pending batch。
    if(catchup_state.snapshot_cursor_committed)
    {
        drainPendingLiveBatchInLoop(session, live, generation);
        return;
    }

    WebSocketSession::MessageGroups batch;
    size_t batch_bytes = 0;

    for(;;)
    {
        PreparedInteractionGroup prepared;

        if(catchup_state.deferred_group.has_value())
        {
            prepared = std::move(catchup_state.deferred_group.value());
            catchup_state.deferred_group.reset();
        }
        else
        {
            const NextCatchUpRecord next = PeekNextSnapshotRecord(catchup_state);
            if(!next.record)
            {
                PCINTERAC_F_DEBUG("interaction catchup handle finish\n");
                break;
            }
            PCINTERAC_F_DEBUG("interaction catchup handle: seq[%lu], pjId[%ld], pcId[%ld]\n", next.record->seq, next.record->project_id, next.record->protocol_id);

            const auto &group = buildInteractionMessageGroup(*next.record, "catch_up");
            if(!group.has_value())
            {
                abortInLoop(session, live, generation, "build catch-up message failed");
                return;
            }

            prepared.source = next.source;
            prepared.group = std::move(group.value());
        }

        if(!CanAppendToBatch(batch_bytes, prepared.group.wire_bytes, live_config_.catch_up_batch_bytes, batch.empty()))
        {
            catchup_state.deferred_group = std::move(prepared);
            break;
        }

        const size_t prepared_bytes = prepared.group.wire_bytes;
        const CatchUpRecordSource source = prepared.source;

        batch.push_back(std::move(prepared.group));
        batch_bytes += prepared_bytes;
        UpdateSnapshotIndex(catchup_state, source);

        // 当前批次大小超限
        if(batch_bytes >= live_config_.catch_up_batch_bytes)
        {
            PCINTERAC_F_WARN("interaction catchup batch oversize: %lu\n", batch_bytes);
            break;
        }
    }

    if(!batch.empty() && !session->sendMessageGroups(batch))
    {
        abortInLoop(session, live, generation, "send catch-up batch failed");
        return;
    }

    const bool protocol_done = catchup_state.protocol_index >= catchup_state.protocol_cache_snapshot.incr_records.size();

    const bool project_done = !catchup_state.project_cache_snapshot.has_value() || catchup_state.project_index >= catchup_state.project_cache_snapshot->incr_records.size();

    // 如果存在数据未发完
    if(!protocol_done || !project_done || catchup_state.deferred_group.has_value())
    {
        scheduleNextCatchUpBatchInLoop(session, live, generation);
        return;
    }

    // 游标未更新
    if(!catchup_state.snapshot_cursor_committed)
    {
        live->markLiveCatchUpCursor(InteractionScope::kProtocol, catchup_state.protocol_cache_snapshot);

        if(catchup_state.project_cache_snapshot.has_value())
        {
            live->markLiveCatchUpCursor(InteractionScope::kProject, catchup_state.project_cache_snapshot.value());
        }

        catchup_state.snapshot_cursor_committed = true;
    }

    // 不在同一 tick 内继续发送 pending batch，
    // 否则 snapshot 最后一批 + pending 第一批可能合计超过 5 MiB。
    if(!live->pending_live_records.empty()
        || (catchup_state.deferred_group.has_value() && catchup_state.deferred_group->source == CatchUpRecordSource::kPendingLive))
    {
        scheduleNextCatchUpBatchInLoop(session, live, generation);
        return;
    }

    finishCatchUpInLoop(session, live, generation);
}


void ProtocolInteractionHandler::drainPendingLiveBatchInLoop(const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    uint64_t generation)
{
    assert(live && live->isInOwnerLoop());

    if(!session || live->generation != generation || !live->catchup_state)
    {
        return;
    }

    CatchUpDeliveryState& catchup_state = *live->catchup_state;
    WebSocketSession::MessageGroups batch;
    std::vector<PreparedInteractionGroup> delivered;
    size_t batch_bytes = 0;

    while(catchup_state.deferred_group.has_value()
        || !live->pending_live_records.empty())
    {
        PreparedInteractionGroup prepared;
        
        if(catchup_state.deferred_group.has_value())
        {
            if(catchup_state.deferred_group->source != CatchUpRecordSource::kPendingLive)
            {
                abortInLoop(session, live, generation, "invalid deferred group source");
                return;
            }
            prepared = std::move(catchup_state.deferred_group.value());
            catchup_state.deferred_group.reset();
        }
        else
        {
            PendingLiveRecordPtr pending_record =
                live->pending_live_records.front();
            const auto& record = pending_record->record();

            const auto &group = buildInteractionMessageGroup(record, "live");
            if(!group.has_value())
            {
                abortInLoop(session, live, generation, "build pending queue live message failed");
                return;
            }

            prepared.source = CatchUpRecordSource::kPendingLive;
            prepared.group = std::move(group.value());
            prepared.scope = record.scope;
            prepared.cache_instance_id = record.cache_instance_id;
            prepared.seq = record.seq;
            prepared.pending_live_record = std::move(pending_record);

            live->pending_live_records.pop_front();
        }
        
        if(!CanAppendToBatch(batch_bytes, prepared.group.wire_bytes, live_config_.catch_up_batch_bytes, batch.empty()))
        {
            catchup_state.deferred_group = std::move(prepared);
            break;
        }

        batch_bytes += prepared.group.wire_bytes;
        batch.push_back(std::move(prepared.group));
        delivered.push_back(std::move(prepared));

        // 这一批数据已经超过 批次bytes大小上限
        if(batch_bytes >= live_config_.catch_up_batch_bytes)
        {
            PCINTERAC_F_WARN("interaction pending batch oversize: %lu\n", batch_bytes);
            break;
        }
    }

    if(!batch.empty() && !session->sendMessageGroups(batch))
    {
        PCINTERAC_F_ERROR("interaction send pending live batch failed \n");
        abortInLoop(session, live, generation, "send pending live batch failed");
        return;
    }
    
    InteractionRecord cursor_record{};
    for(const auto& item : delivered)
    {
        cursor_record.scope = item.scope;
        cursor_record.cache_instance_id = item.cache_instance_id;
        cursor_record.seq = item.seq;
        live->markLiveCursor(cursor_record);
    }

    // pending 队列还有数据未处理完则开启下一轮循环
    if(catchup_state.deferred_group.has_value()
        || !live->pending_live_records.empty())
    {
        scheduleNextCatchUpBatchInLoop(session, live, generation);
        return;
    }

    finishCatchUpInLoop(session, live, generation);
}

void ProtocolInteractionHandler::finishCatchUpInLoop(const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    uint64_t generation)
{
    assert(live && live->isInOwnerLoop());
    if(!session || live->generation != generation || !live->catchup_state)
    {
        return;
    }
    if(InteractionLiveState::kCatchingUp != live->state.load(std::memory_order_relaxed))
    {
        return;
    }

    CatchUpDeliveryState& completed = *live->catchup_state;

    stopCatchUpPendingAdmission(live, generation);
    live->setState(InteractionLiveState::kActive);

    // 如果是resume操作必须返回ack消息
    if(completed.trigger == CatchUpTrigger::kResume)
    {
        // 必须有resume消息
        assert(completed.resume_msg.has_value());

        const LiveCommandMsg& msg = completed.resume_msg.value();

        live->accepted_msg_seq.store( msg.client_seq, std::memory_order_relaxed);

        sendCommandAckMsg(session, live, msg, true);
    }
    live->catchup_state.reset();
}

void ProtocolInteractionHandler::abortInLoop(
    const kit_muduo::WebSocketSessionPtr& session,
    const InteractionLiveContextPtr& live,
    uint64_t generation,
    const std::string& reason)
{
    assert(live && live->isInOwnerLoop());

    if(live->generation != generation)
    {
        PCINTERAC_F_ERROR("interaction live maybe expired: %lu --> %lu\n", live->generation, generation);
        return;
    }

    const uint64_t old_subscriber_id = live->subscriber_id;
    live->subscriber_id = 0;

    stopCatchUpPendingAdmission(live, generation);
    cleanupCatchUpPending(live, generation);

    // kCatchingUp -> kClosed 推进 generation，使旧 continuation 失效。
    live->setState(InteractionLiveState::kClosed);
    live->catchup_state.reset();

    if(old_subscriber_id != 0)
    {
        hub_->unsubcribe(old_subscriber_id);
    }

    if(session && session->isOpen())
    {
        session->close( CloseCode::kServerError, reason);
    }
}


}
