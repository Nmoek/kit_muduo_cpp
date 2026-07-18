/**
 * @file protocol_interaction_hub.h
 * @brief  测试协议项交互详情订阅管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-05 02:40:30
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_PROTOCOL_INTERACTION_HUB_H__
#define __KIT_PROTOCOL_INTERACTION_HUB_H__

#include "base/noncopyable.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_observation.h"

#include <mutex>

namespace kit_domain {


class InteractionSink
{
public:
    virtual ~InteractionSink() = default;

    virtual void publish(InteractionRecord recode) = 0;
};


struct InteractionSubscribeFilter
{
    int64_t project_id{0};
    int64_t protocol_id{0};
    bool include_project_notice{true};
};

struct InteractionSubscription
{
    /// @brief 订阅Id
    uint64_t subscriber_id{0};
    /// @brief protocol 缓存Id
    uint64_t protocol_cache_instance_id{0};
    /// @brief project 缓存Id
    uint64_t project_cache_instance_id{0};
    /// @brief protocol数据从订阅成功此刻算，开始发送的位置
    uint64_t protocol_start_seq{0};
    /// @brief notice数据从订阅成功此刻算，开始发送的位置
    uint64_t project_start_seq{0};
};

struct SubscribeWithCatchUpResult
{
    bool result{true};
    /// @brief 订阅元数据
    InteractionSubscription subscription;
    /// @brief protocol交互增量数据快照
    InteractionCacheSnapshot protocol_cache_snapshot;
    /// @brief notice增量数据快照
    std::optional<InteractionCacheSnapshot> project_cache_snapshot;

    bool ok() const { return result == true; }

};

using InteractionCallback = std::function<void(const InteractionRecord&)>;

struct InteractionRecordCacheContainer
{
    const std::shared_ptr<InteractionRecordCache>& protocol_cache;
    const std::shared_ptr<InteractionRecordCache>& project_cache;
    std::optional<uint64_t> after_protocol_cache_instance_id;
    std::optional<uint64_t> after_protocol_seq;
    std::optional<uint64_t> after_project_cache_instance_id;
    std::optional<uint64_t> after_project_seq;
};

class ProtocolInteractionHub: kit_muduo::Noncopyable, public InteractionSink
{
public:
    using PjId = int64_t;
    using PcId = int64_t; 

    ProtocolInteractionHub();
    ~ProtocolInteractionHub() override = default;

    void publish(InteractionRecord record) override;

    SubscribeWithCatchUpResult subscribeWithCatchUp(
        InteractionSubscribeFilter filter,
        InteractionRecordCacheContainer container,
        InteractionCallback cb);

    void unsubcribe(uint64_t subscriber_id);


private:
    struct SubscriberEntry
    {
        InteractionSubscribeFilter filter;
        uint64_t protocol_cache_instance_id{0};
        uint64_t project_cache_instance_id{0};
        uint64_t protocol_start_seq{0};
        uint64_t project_start_seq{0};
        InteractionCallback out_cb;
    };

    using CallBackVec = std::vector<InteractionCallback>;

    bool matchSubscriber(const SubscriberEntry &entry, const InteractionRecord &record) const;

private:
    /// @brief 整个订阅管理器锁
    mutable std::mutex mtx_;
    /// @brief 下一个订阅者Id
    uint64_t next_subscriber_id_{1};
    /// @brief 订阅者集合
    std::unordered_map<uint64_t, SubscriberEntry> subscribers_;

};



}
#endif //__KIT_PROTOCOL_INTERACTION_HUB_H__