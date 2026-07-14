/**
 * @file protocol_interaction_hub.cpp
 * @brief 测试协议项交互详情订阅管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-05 03:03:42
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/domain_log.h"
#include "domain/protocol_interaction.h"
#include <atomic>
#include <mutex>
#include "domain/protocol_interaction_hub.h"

namespace kit_domain {



ProtocolInteractionHub::ProtocolInteractionHub(size_t max_records_num)
    :max_records_num_(max_records_num)
    ,cur_record_seq_(0)
    ,next_subscriber_id_(1)
{

}

void ProtocolInteractionHub::publish(ProtocolInteractionRecord record)
{
    CallBackVec cbs;

    std::unique_lock<std::mutex> lock(mtx_);

    if(max_records_num_ <= 0)
    {
        INTERAC_F_ERROR("max records <=0\n");
        return;
    }

    // 更新当前已经看到的序列号seq
    cur_record_seq_ .store(
        std::max(record.seq, cur_record_seq_.load(std::memory_order_relaxed)), 
        std::memory_order_relaxed
    );

    // TODO 这里的缓存完全没用上
#if 0
    if(InteractionScope::kProtocol == record.scope)
    {
        RecordKey key{
            .project_id = record.project_id,
            .protocol_id = record.protocol_id,
        };
        auto it = protocol_records_.find(key);
        if(it == protocol_records_.end())
        {
            INTERAC_F_INFO("interaction protocol record not found: pjId[%ld], pcId[%ld]\n",key.project_id, key.protocol_id);
        }
        pushWithLimit(protocol_records_[key], record);

    }
    else if(InteractionScope::kProject == record.scope)
    {
        record.protocol_id = 0;
        auto it = project_notices_.find(record.project_id);
        if(it == project_notices_.end())
        {
            INTERAC_F_INFO("interaction project notice not found: pjId[%ld]\n",record.project_id);
        }
        pushWithLimit(project_notices_[record.project_id], record);
        
    }
    else 
    {
        INTERAC_F_ERROR("interaction record scope invalid: %d\n", static_cast<int32_t>(record.scope));
        return;
    }
#endif

    // 找出和消息匹配的订阅者进行广播发布
    cbs.reserve(subscribers_.size());
    for(const auto& s : subscribers_)
    {
        const SubscriberEntry &entry = s.second;
        if(matchSubscriber(entry, record))
        {
            cbs.push_back(entry.out_cb);
        }
    }
    lock.unlock();

    // 注意 解锁后进行回调处理
    for(auto &cb : cbs)
    {
        cb(record);
    }

}

void ProtocolInteractionHub::clearProtocol(int64_t project_id, int64_t protocol_id)
{
    std::unique_lock<std::mutex> lock(mtx_);
    protocol_records_.erase(RecordKey{
        .project_id = project_id,
        .protocol_id = protocol_id,
    });
}
void ProtocolInteractionHub::clearProject(int64_t project_id)
{
    std::unique_lock<std::mutex> lock(mtx_);

    project_notices_.erase(project_id);

    for(auto it = protocol_records_.begin(); it != protocol_records_.end();)
    {
        if(it->first.project_id == project_id)
        {
            it = protocol_records_.erase(it);
        }
        else 
        {
            ++it;
        }
    }

}

ProtocolInteractionSubscription ProtocolInteractionHub::subscribe(ProtocolInteractionSubscribeFilter filter, CallBack cb)
{
    if(!cb)
    {
        INTERAC_F_ERROR("subscriber callback function null!\n");
        return {};
    }


    std::unique_lock<std::mutex> lock(mtx_);

    uint64_t cur_seq = cur_record_seq_.load(std::memory_order_relaxed) + 1;

    ProtocolInteractionSubscription subscription{
        .subscriber_id = next_subscriber_id_++,
        .start_record_seq = cur_seq,
    };
    

    SubscriberEntry entry{
        .filter = std::move(filter),
        .start_record_seq = cur_seq,
        .out_cb = std::move(cb),
    };

    subscribers_.emplace(subscription.subscriber_id, std::move(entry));
    
    return subscription;
}

void ProtocolInteractionHub::unsubcribe(uint64_t subscriber_id)
{
    std::unique_lock<std::mutex> lock(mtx_);
    subscribers_.erase(subscriber_id);
}

uint64_t ProtocolInteractionHub::currentSeq() const
{
    return cur_record_seq_;
}



void ProtocolInteractionHub::pushWithLimit(std::deque<ProtocolInteractionRecord> &bucket, ProtocolInteractionRecord record)
{
    while(bucket.size() >= max_records_num_)
    {
        bucket.pop_front();
    }

    bucket.push_back(std::move(record));
}

bool ProtocolInteractionHub::matchSubscriber(const SubscriberEntry &entry, const ProtocolInteractionRecord &record) const
{
    const ProtocolInteractionSubscribeFilter &filter = entry.filter;

    if(record.seq < entry.start_record_seq)
    {
        INTERAC_F_DEBUG("interaction req invalid: %lu --> %lu\n", record.seq, entry.start_record_seq);
        return false;
    }

    if(filter.project_id != record.project_id)
    {
        INTERAC_F_DEBUG("project not match!\n");
        return false;
    }


    if(InteractionScope::kProtocol == record.scope)
    {
        return filter.protocol_id == record.protocol_id;
    }

    if(InteractionScope::kProject == record.scope)
    {
        return filter.include_project_notice;
    }

    INTERAC_F_ERROR("all condition missmatch\n");
    return false;
}

}