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
#include "domain/protocol_interaction_observation.h"
#include "domain/protocol_interaction_hub.h"

#include <exception>
#include <mutex>

namespace kit_domain {



ProtocolInteractionHub::ProtocolInteractionHub()
    :next_subscriber_id_(1)
{

}

void ProtocolInteractionHub::publish(InteractionRecord record)
{
    CallBackVec cbs;

    std::unique_lock<std::mutex> lock(mtx_);

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
    // 这里有问题 实时发布不应该再worker线程处理 应该再各自的IO线程处理
    for(auto &cb : cbs)
    {
        try {
            cb(record);
        } catch(const std::exception &e) {
            INTERAC_F_ERROR("interaction callback exception: %s \n", e.what());
        }catch(...) {
            INTERAC_F_ERROR("interaction callback unknown exception\n");
        }

    }

}

SubscribeWithCatchUpResult ProtocolInteractionHub::subscribeWithCatchUp(
    InteractionSubscribeFilter filter,
    InteractionRecordCacheContainer container,
    InteractionCallback cb)
{
    SubscribeWithCatchUpResult result;

    if(!IsValidInteractionCursorPair(
            container.after_protocol_cache_instance_id,
            container.after_protocol_seq))
    {
        INTERAC_F_ERROR(
            "protocol cursor cache_instance_id and seq must be supplied as a valid pair\n");
        result.result = false;
        return result;
    }

    if(!IsValidInteractionCursorPair(
            container.after_project_cache_instance_id,
            container.after_project_seq))
    {
        INTERAC_F_ERROR(
            "project cursor cache_instance_id and seq must be supplied as a valid pair\n");
        result.result = false;
        return result;
    }

    if(!cb)
    {
        INTERAC_F_ERROR("subscriber callback function null!\n");
        result.result = false;
        return result;
    }

    if(!container.protocol_cache)
    {
        INTERAC_F_ERROR("protocol cache null\n");
        result.result = false;
        return result;
    }

    const auto& protocol_locked = container.protocol_cache->lockAndCollect(container.after_protocol_cache_instance_id, container.after_protocol_seq);
    if (!protocol_locked.isActive())
    {
        INTERAC_F_INFO("protocol interaction cache closed! pjId[%ld], pcId[%ld]\n", filter.project_id, filter.protocol_id);
        result.result = false;

        return result;
    }
    if (!protocol_locked.isValid())
    {
        INTERAC_F_ERROR("protocol interaction cursor invalid\n");
        result.result = false;
        return result;
    }
    
    // 如果需要观测notice
    std::optional<InteractionRecordCache::LockedSnapshot> project_locked;
    if(filter.include_project_notice)
    {
        if(!container.project_cache)
        {
            INTERAC_F_ERROR("project cache null\n");
            result.result = false;
            return result;
        }
        if (container.project_cache.get() == container.protocol_cache.get())
        {
            INTERAC_F_ERROR("protocol cache and project cache must be different\n");
            result.result = false;

            return result;
        }

        project_locked.emplace(
            container.project_cache->lockAndCollect(
                container.after_project_cache_instance_id,
                container.after_project_seq));
        if (!project_locked->isActive())
        {
            INTERAC_F_INFO("project interaction cache closed\n");
            result.result = false;
            return result;
        }
        if (!project_locked->isValid())
        {
            INTERAC_F_ERROR("project interaction cursor invalid\n");
            result.result = false;
            return result;
        }
    }

    const InteractionCacheSnapshot& protocol_snapshot = protocol_locked.snapshot();
    SubscriberEntry entry{
        .filter = std::move(filter),
        .protocol_cache_instance_id = protocol_snapshot.cache_instance_id,
        .project_cache_instance_id = 0,
        .protocol_start_seq = protocol_snapshot.last_seq + 1,
        .project_start_seq = 0,
        .out_cb = std::move(cb),
    };
    result.protocol_cache_snapshot = std::move(protocol_snapshot);

    if (project_locked.has_value())
    {
        const InteractionCacheSnapshot& project_snapshot = project_locked->snapshot();

        entry.project_cache_instance_id = project_snapshot.cache_instance_id;
        entry.project_start_seq = project_snapshot.last_seq + 1;
        result.project_cache_snapshot = std::move(project_snapshot);
    }

    auto &subscription = result.subscription;
    {
        std::lock_guard<std::mutex> lock(mtx_);

        subscription.subscriber_id = next_subscriber_id_++;
        subscription.protocol_cache_instance_id = entry.protocol_cache_instance_id;
        subscription.project_cache_instance_id = entry.project_cache_instance_id;
        subscription.protocol_start_seq = entry.protocol_start_seq;
        subscription.project_start_seq = entry.project_start_seq;

        subscribers_.emplace(subscription.subscriber_id, std::move(entry));
    }
    result.result = true;
    return result;
}

void ProtocolInteractionHub::unsubcribe(uint64_t subscriber_id)
{
    std::unique_lock<std::mutex> lock(mtx_);
    subscribers_.erase(subscriber_id);
}


bool ProtocolInteractionHub::matchSubscriber(const SubscriberEntry &entry, const InteractionRecord &record) const
{
    const InteractionSubscribeFilter &filter = entry.filter;

    if(filter.project_id != record.project_id)
    {
        INTERAC_F_DEBUG("project not match! %ld --> %ld\n", filter.project_id, record.project_id);
        return false;
    }

    if(InteractionScope::kProtocol == record.scope)
    {
        INTERAC_F_DEBUG("subscriber protocol match info:\n entry[%ld][%ld][%d] [%ld][%ld]\n record[%ld][%ld][%lu][%lu][%ld]\n", 
            filter.project_id,
            filter.protocol_id,
            static_cast<int>(filter.include_project_notice),
            entry.protocol_cache_instance_id,
            entry.protocol_start_seq,
            record.project_id,
            record.protocol_id,
            record.cache_instance_id,
            record.seq,
            record.time_ms
        );

        return record.cache_instance_id == entry.protocol_cache_instance_id
            && record.protocol_id == filter.protocol_id
            && record.seq >= entry.protocol_start_seq;
    }
    else if(InteractionScope::kProject == record.scope && filter.include_project_notice)
    {
        INTERAC_F_DEBUG("subscriber project match info:\n entry[%ld][%ld][%d] [%ld][%ld]\n record[%ld][%ld][%lu][%lu][%ld]\n", 
            filter.project_id,
            filter.protocol_id,
            static_cast<int>(filter.include_project_notice),
            entry.project_cache_instance_id,
            entry.project_start_seq,
            record.project_id,
            record.protocol_id,
            record.cache_instance_id,
            record.seq,
            record.time_ms
        );
        return record.cache_instance_id == entry.project_cache_instance_id
            && filter.include_project_notice
            && record.protocol_id == 0
            && record.seq >= entry.project_start_seq;
    }

    INTERAC_F_DEBUG("!!!!!!all condition missmatch!!!!!\n");
    return false;
}

}
