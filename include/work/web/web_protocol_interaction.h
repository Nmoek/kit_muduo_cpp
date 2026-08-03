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
#include "domain/protocol_interaction_observation.h"
#include "net/call_backs.h"
#include "nlohmann/json.hpp"
#include "net/event_loop.h"
#include "net/websocket/websocket_session.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace kit_muduo {

namespace ws{
class WebSocketServer;
}
}

namespace kit_domain {

class ProjectSvcInterface;
class ProtocolSvcInterface;
class RuntimeControllerInterface;

/**
 * @brief Upgrade升级时请求Body携带初始化信息
 */
struct UpgradeInitReq
{
    int64_t protocol_id{0};
    std::optional<uint64_t> after_protocol_cache_instance_id{std::nullopt};
    std::optional<uint64_t> after_protocol_seq{std::nullopt};
    bool include_project_notice{true};
    std::optional<uint64_t> after_project_cache_instance_id{std::nullopt};
    std::optional<uint64_t> after_project_seq{std::nullopt};
};


enum class InteractionLiveState
{
    kInit = 1,
    kCatchingUp,
    kActive,
    kPaused,
    kClosed,
};
NLOHMANN_JSON_SERIALIZE_ENUM(InteractionLiveState,{
    {static_cast<InteractionLiveState>(0),  "unknown"},
    {InteractionLiveState::kInit,           "init"},
    {InteractionLiveState::kCatchingUp,     "catching_up"},
    {InteractionLiveState::kActive,         "active"},
    {InteractionLiveState::kPaused,         "paused"},
    {InteractionLiveState::kClosed,         "closed"}
})

enum class InteractionLiveCommand
{
    // open/close 特别注意隐含在websocket的open/close之中
    kUnknown,
    kPause,
    kResume,
    kQueryState,
};
NLOHMANN_JSON_SERIALIZE_ENUM(InteractionLiveCommand,{
    {InteractionLiveCommand::kUnknown,        "unknown"},
    {InteractionLiveCommand::kPause,          "pause"},
    {InteractionLiveCommand::kResume,         "resume"},
    {InteractionLiveCommand::kQueryState,     "query_state"}
})

struct InteractionLiveCursor
{
    std::optional<uint64_t> cache_instance_id{std::nullopt};
    uint64_t seq{0};

    bool hasCursor() const
    {
        return cache_instance_id.has_value();
    }

    std::optional<uint64_t> afterSeq() const
    {
        return cache_instance_id.has_value() ? std::optional<uint64_t>{seq} : std::nullopt;
    }

    friend void from_json(const nlohmann::json &j, InteractionLiveCursor &cursor)
    {
        auto it = j.find("cache_instance_id");
        if(it != j.end())
        {
            cursor.cache_instance_id = it.value().get<uint64_t>();
        }

        j.at("seq").get_to<uint64_t>(cursor.seq);
    }
    friend void to_json(nlohmann::json &j, const InteractionLiveCursor &cursor)
    {
        if(cursor.cache_instance_id.has_value())
        {
            j["cache_instance_id"] = cursor.cache_instance_id.value();
        }

        j["seq"] = cursor.seq;
    }
};

struct LiveDetachResult
{
    uint64_t subscriber_id{0};
    uint64_t generation{0};
};


struct InteractionLiveContextSnapshot
{
    InteractionLiveState state{InteractionLiveState::kInit};
    uint64_t subscriber_id{0};
    uint64_t generation{0};
    InteractionLiveCursor protocol_cursor;
    InteractionLiveCursor project_cursor;
    uint64_t accepted_msg_seq{0};
    uint64_t send_msg_seq{0};
};

/**
 * @brief Live实时交互数据控制配置
 */
struct InteractionLiveConfig
{
    static constexpr size_t kDefaultCatchUpBatchBytes = 5 * 1024 * 1024; // 5M
    static constexpr size_t kDefaultPendingLiveMaxRecords = 64;
    static constexpr size_t kDefaultPendingLiveMaxBytes = 64 * 1024 * 1024; // 64M

    /// @brief 交互窗口队列容量 默认20
    size_t queue_capacity{20};
    /// @brief 工作线程最晚停止超时
    int64_t stop_drain_timeout_ms{300};

    
    /// @brief catchup阶段批次打包 每批次bytes大小上限 默认5M
    size_t catch_up_batch_bytes{kDefaultCatchUpBatchBytes};
    /// @brief catchup阶段 live数据缓存队列数量上限
    size_t pending_live_max_records{kDefaultPendingLiveMaxRecords};
    /// @brief catchup阶段 live数据缓存队列bytes大小上限
    size_t pending_live_max_bytes{kDefaultPendingLiveMaxBytes};
};

struct LiveCommandMsg
{
    std::string type; // 目前固定为"command"
    InteractionLiveCommand command;
    uint64_t session_id;
    uint64_t client_seq;
    uint64_t timestamp; // 单位 ms
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LiveCommandMsg, type, command, session_id, client_seq, timestamp)
};

struct LiveCommandAckMsg
{
    std::string type;
    InteractionLiveCommand command;
    bool ok{true};
    std::string error_message;
    InteractionLiveState state;
    uint64_t client_seq{0};
    uint64_t accepted_seq{0};
    InteractionLiveCursor protocol_cursor;
    InteractionLiveCursor project_cursor;
    uint64_t timestamp; // 单位 ms

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LiveCommandAckMsg, type, command, ok, error_message, state, client_seq, accepted_seq, protocol_cursor, project_cursor, timestamp)
};


enum class CatchUpTrigger
{
    kOpen,
    kResume
};

enum class CatchUpRecordSource
{
    kProtocolSnapshot,
    kProjectSnapshot,
    kPendingLive,
};

class PendingLiveRecord
{
public:

    explicit PendingLiveRecord(InteractionRecord record);

    /**
     * @brief 承载 record 及其 pending 账本 reservation，最后一个共享引用析构时自动结算。
     * @param record 
     * @param reserved_sidecar_bytes 
     * @param release_callback 
     */
    PendingLiveRecord(InteractionRecord record, size_t reserved_sidecar_bytes, std::function<void()> release_callback);

    ~PendingLiveRecord() noexcept;

    PendingLiveRecord(const PendingLiveRecord&) = delete;
    PendingLiveRecord& operator=(const PendingLiveRecord&) = delete;

    void setReservationActiveTrue() noexcept { reservation_active_ = true; }

    bool reservationActive() const { return reservation_active_; }

    InteractionRecord& record() { return record_; }

    size_t reservedSidecarBytes() const { return reserved_sidecar_bytes_; }

private:
    InteractionRecord record_;
    size_t reserved_sidecar_bytes_{0};
    std::function<void()> release_callback_;
    bool reservation_active_{false};
};
using PendingLiveRecordPtr = std::shared_ptr<PendingLiveRecord>;

struct PreparedInteractionGroup
{
    CatchUpRecordSource source{CatchUpRecordSource::kProtocolSnapshot};
    kit_muduo::ws::WebSocketSession::MessageGroup group;

    /********仅 kPendingLive 使用******/
    //当 group 成为 deferred 后，原 record 可以从 pending deque 移出，仍能在发送成功后推进 cursor。
    InteractionScope scope{InteractionScope::kUnknown};
    uint64_t cache_instance_id{0};
    uint64_t seq{0};
    /// @brief 保持 pending reservation 存活到 deferred/batch 完成交付或被丢弃。
    PendingLiveRecordPtr pending_live_record;
    /********仅 kPendingLive 使用******/

};

struct CatchUpDeliveryState
{
    uint64_t generation{0};
    CatchUpTrigger trigger{CatchUpTrigger::kOpen};

    InteractionCacheSnapshot protocol_cache_snapshot;
    std::optional<InteractionCacheSnapshot> project_cache_snapshot;

    size_t protocol_index{0};
    size_t project_index{0};

    /**
     * @brief 已构造但无法加入当前非空 batch 的下一条。下一 tick 直接复用，避免重复 JSON 序列化和附件拷贝。
     */
    std::optional<PreparedInteractionGroup> deferred_group;

    /**
     * @brief open 为 nullopt；resume 保存当前 command message，只在全部交付完成后 ACK。
     */
    std::optional<LiveCommandMsg> resume_msg;
    
    bool snapshot_cursor_committed{false};
};

struct InteractionLiveContext
{
    /// @brief session Id
    uint64_t session_id{0};
    /// @brief 观测项目Id
    int64_t project_id{0};
    /// @brief 观测协议Id
    int64_t protocol_id{0};
    /// @brief upgrade链路初始化数据
    UpgradeInitReq init_req;

    // WebSocketSession 所属 IO loop。live 状态只允许在该 loop 上读写。
    kit_muduo::EventLoop* owner_loop{nullptr};
    std::weak_ptr<kit_muduo::ws::WebSocketSession> weak_session;

    /// @brief 订阅Id 0表示没有有效订阅
    uint64_t subscriber_id{0};
    /// @brief 订阅生命周期版本。open/resume/pause/close 这类会改变订阅有效性的动作都要推进
    uint64_t generation{0};
    /// @brief protocol发送游标
    InteractionLiveCursor protocol_cursor;
    /// @brief project发送游标
    InteractionLiveCursor project_cursor;
    /// @brief 业务实时通信状态
    std::atomic<InteractionLiveState> state{InteractionLiveState::kInit};
    /// @brief 主动发送的消息序列号
    std::atomic_uint64_t send_msg_seq{0};
    /// @brief 被动收到的消息序列号
    std::atomic_uint64_t accepted_msg_seq{0};
    /// @brief catchup数据交付状态控制
    std::unique_ptr<CatchUpDeliveryState> catchup_state;

    /// @brief 关键: limit条件计算时必须加个锁
    std::mutex pending_limit_mtx;
    uint64_t pending_accepting_generation{0};
    std::deque<PendingLiveRecordPtr> pending_live_records;
    size_t pending_live_count{0};
    size_t pending_live_bytes{0};

    bool isInOwnerLoop() const { return owner_loop && owner_loop->isInLoopThread(); }

    /**
     * @brief 业务命令执行时检查状态
     * @param command 
     * @return true 
     * @return false 
     */
    bool checkState(InteractionLiveCommand command);

    /**
     * @brief 状态转移处理
     * @param next_state 
     */
    void setState(InteractionLiveState next_state);


    /**
     * @brief 标定实时推送后的游标
     * @param live 
     * @param record 
     */
    void markLiveCursor(const InteractionRecord& record);

    /**
     * @brief 标记CatchUp后的游标
     * @param live 
     * @param scope 
     * @param snapshot 
     */
    void markLiveCatchUpCursor(InteractionScope scope,
        const InteractionCacheSnapshot& snapshot);
};
using InteractionLiveContextPtr = std::shared_ptr<InteractionLiveContext>;

class ProtocolInteractionHandler
{
public:
    ProtocolInteractionHandler(std::shared_ptr<ProtocolSvcInterface> svc, 
        std::shared_ptr<RuntimeControllerInterface> runtime_controller, 
        std::shared_ptr<ProtocolInteractionHub> hub,
        InteractionLiveConfig live_config = {});
    ~ProtocolInteractionHandler() = default;

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

    /**
     * @brief websocket session建立前业务准备
     * @param session 
     * @param ctx 
     * @return true 
     * @return false 
     */
    bool onPrepare(kit_muduo::WebSocketSessionPtr session, kit_muduo::HttpContextPtr ctx) noexcept;
    /// 注意： 这是一个热路径接口 要非常小心
    void onHubLiveArrive(std::weak_ptr<kit_muduo::ws::WebSocketSession> weak_session, std::weak_ptr<InteractionLiveContext> weak_live, uint64_t generation,  InteractionRecord record);
    /*******WebSocket 回调处理****/
    void onOpen(kit_muduo::WebSocketSessionPtr session) noexcept;
    void onText(kit_muduo::WebSocketSessionPtr session, const std::string &payload) noexcept;
    void onClose(kit_muduo::WebSocketSessionPtr session) noexcept;
    void onError(kit_muduo::WebSocketSessionPtr session, kit_muduo::ws::CloseCode code, const std::string &reason) noexcept;
    /*******WebSocket 回调处理****/

    void sendLiveReady(
    kit_muduo::WebSocketSessionPtr session,
    InteractionLiveContextPtr live,
    const SubscribeWithCatchUpResult& capture_result,
    CatchUpTrigger trigger);

    bool sendInteraction(
        kit_muduo::WebSocketSessionPtr session,
        InteractionLiveContextPtr live,
        const InteractionRecord& record,
        const std::string& delivery);

    void sendCommandAckMsg(
        kit_muduo::WebSocketSessionPtr session, 
        const InteractionLiveContextPtr& live,
        const LiveCommandMsg &req,
        bool ok,
        const std::string &error_message = "");
    
    void cleanupLive(int64_t project_id, std::optional<int64_t> protocol_id);

private:

    void onOpenInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live);
    void onHubLiveArriveInLoop(
    kit_muduo::WebSocketSessionPtr session,
    InteractionLiveContextPtr live,
    uint64_t generation,
    PendingLiveRecordPtr pending_record);
    void onClientCommandInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live, LiveCommandMsg msg);
    void onCloseInLoop(InteractionLiveContextPtr live);

    bool catchUpHelperInLoop(kit_muduo::WebSocketSessionPtr session, InteractionLiveContextPtr live, const std::string& trigger, std::string &error_reason);

    void onResumeInLoop(const kit_muduo::WebSocketSessionPtr& session, const InteractionLiveContextPtr& live, const LiveCommandMsg& msg);

    bool parseLiveCommandMsg(InteractionLiveContext & info, const std::string &payload, LiveCommandMsg &msg);

    void sendBusinessError(kit_muduo::WebSocketSessionPtr session, const std::string &bs_code);

    std::optional<kit_muduo::ws::WebSocketSession::MessageGroup> buildInteractionMessageGroup(const InteractionRecord& record, const std::string& delivery);

    PendingLiveRecordPtr tryReserveCatchUpPending(
        const InteractionLiveContextPtr& live,
        uint64_t generation,
        InteractionRecord record);

    void startCatchUpPendingAdmission(const InteractionLiveContextPtr& live, uint64_t generation);

    void stopCatchUpPendingAdmission(const InteractionLiveContextPtr& live, uint64_t generation);

    void cleanupCatchUpPending(const InteractionLiveContextPtr& live, uint64_t generation);

    bool addLiveContext(uint64_t session_id, InteractionLiveContextPtr live);
    InteractionLiveContextPtr findLiveContext(uint64_t session_id);
    InteractionLiveContextPtr removeLiveContext(uint64_t session_id);

    bool beginCatchUpInLoop(
        const kit_muduo::WebSocketSessionPtr& session,
        const InteractionLiveContextPtr& live,
        CatchUpTrigger trigger,
        std::optional<LiveCommandMsg> resume_command,
        std::string& error_reason);

    void scheduleNextCatchUpBatchInLoop(
        const kit_muduo::WebSocketSessionPtr& session,
        const InteractionLiveContextPtr& live,
        uint64_t generation);

    void drainCatchUpBatchInLoop(
        const kit_muduo::WebSocketSessionPtr& session,
        const InteractionLiveContextPtr& live,
        uint64_t generation);

    void drainPendingLiveBatchInLoop(
        const kit_muduo::WebSocketSessionPtr& session,
        const InteractionLiveContextPtr& live,
        uint64_t generation);

    void finishCatchUpInLoop(
        const kit_muduo::WebSocketSessionPtr& session,
        const InteractionLiveContextPtr& live,
        uint64_t generation);

    void abortInLoop(
        const kit_muduo::WebSocketSessionPtr& session,
        const InteractionLiveContextPtr& live,
        uint64_t generation,
        const std::string& reason);

private:
    std::shared_ptr<ProtocolSvcInterface> pc_svc_;
    std::shared_ptr<RuntimeControllerInterface> runtime_controller_;
    std::shared_ptr<ProtocolInteractionHub> hub_;
    InteractionLiveConfig live_config_;

    std::mutex mtx_;
    using WsSssionId = uint64_t;
    std::unordered_map<WsSssionId, InteractionLiveContextPtr> live_contexts_;
};


}
#endif //_KIT_WEB_PROTOCOL_INTERACTION_H__
