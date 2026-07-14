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

#include <atomic>
#include <deque>
#include <mutex>

namespace kit_domain {


class ProtocolInteractionSink
{
public:
    virtual ~ProtocolInteractionSink() = default;
    virtual void publish(ProtocolInteractionRecord recode) = 0;
    virtual void clearProtocol(int64_t project_id, int64_t protocol_id) = 0;
    virtual void clearProject(int64_t project_id) = 0;
};


struct ProtocolInteractionSubscribeFilter
{
    int64_t project_id{0};
    int64_t protocol_id{0};
    bool include_project_notice{false};
};

struct ProtocolInteractionSubscription
{
    /// @brief 订阅Id
    uint64_t subscriber_id{0};
    /// @brief 数据从哪里开始发送的位置
    uint64_t start_record_seq{0};
};

class ProtocolInteractionHub: kit_muduo::Noncopyable, public ProtocolInteractionSink
{
public:
    using PjId = int64_t;
    using PcId = int64_t; 
    using CallBack = std::function<void(const ProtocolInteractionRecord&)>;

    explicit ProtocolInteractionHub(size_t max_records_num = 100);
    ~ProtocolInteractionHub() override = default;

    void publish(ProtocolInteractionRecord record) override;
    void clearProtocol(int64_t project_id, int64_t protocol_id) override;
    void clearProject(int64_t project_id) override;

    ProtocolInteractionSubscription subscribe(ProtocolInteractionSubscribeFilter filter, CallBack cb);

    void unsubcribe(uint64_t subscriber_id);

    /**
     * @brief 注意: 该结构只是用于调试观测
     * @return uint64_t 
     */
    uint64_t currentSeq() const;


private:
    struct RecordKey
    {
        PjId project_id{0};
        PcId protocol_id{0};

        bool operator==(const RecordKey& k) const
        {
            return project_id == k.project_id
                && protocol_id == k.protocol_id;
        }
    };

    struct RecordKeyHash
    {
        static constexpr uint64_t R = 0x9e3779b97f4a7c15ULL ;
        size_t operator()(const RecordKey &key) const
        {
            size_t h1 = std::hash<uint64_t>{}(key.project_id);
            size_t h2 = std::hash<uint64_t>{}(key.protocol_id);
            return h1 ^ (h2 + R + (h1 << 6) + (h1 >> 2));
        }
    };

    struct SubscriberEntry
    {
        ProtocolInteractionSubscribeFilter filter;
        uint64_t start_record_seq;
        CallBack out_cb;
    };

    using CallBackVec = std::vector<CallBack>;

    void pushWithLimit(std::deque<ProtocolInteractionRecord> &bucket, ProtocolInteractionRecord record);

    bool matchSubscriber(const SubscriberEntry &entry, const ProtocolInteractionRecord &record) const;

private:
    /// @brief 每个buffer最大容纳记录
    size_t max_records_num_{100};
    /// @brief 整个订阅管理器锁
    mutable std::mutex mtx_;
    /// @brief 交互记录Id
    std::atomic_uint64_t cur_record_seq_{0};
    /// @brief 订阅者Id
    uint64_t next_subscriber_id_{1};
    /// @brief 协议项交互buffer
    std::unordered_map<RecordKey, std::deque<ProtocolInteractionRecord>, RecordKeyHash> protocol_records_;
    /// @brief 项目级通知buffer
    std::unordered_map<PjId, std::deque<ProtocolInteractionRecord>> project_notices_;
    /// @brief 订阅者集合
    std::unordered_map<uint64_t, SubscriberEntry> subscribers_;

};



}
#endif //__KIT_PROTOCOL_INTERACTION_HUB_H__