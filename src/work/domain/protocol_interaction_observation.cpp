/**
 * @file protocol_interaction_observation.cpp
 * @brief 协议项交互观察原始数据adapter层
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-15 18:19:33
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/domain_log.h"
#include "domain/runtime_result.h"
#include <atomic>
#include <mutex>
#include "domain/protocol_interaction_observation.h"

namespace kit_domain {



InteractionRecordCache::InteractionRecordCache(InteractionRecordCacheKey key, size_t capacity)
    :key_(std::move(key))
    ,is_active_(true)
    ,capacity_(capacity)
    ,cache_instance_id_(s_next_cache_instance_id_.fetch_add(1, std::memory_order_relaxed))
{

}


bool InteractionRecordCache::tryAppend(InteractionRecord &record)
{
    std::lock_guard<std::mutex> lock(mtx_);

    if(!is_active_)
    {
        INTERAC_F_INFO("interaction cache not active! key[%ld][%ld]\n", key_.project_id, key_.protocol_id);
        return false;
    }

    if(!chechkCacheKey(record)
        || record.cache_instance_id != cache_instance_id_)
    {
        PUBLISHER_F_WARN("interaction record not match cache! record:seq[%lu], pjId[%ld], pcId[%ld], cacheId[%lu], time_ms[%ld] || cache:pjId[%ld], pcId[%ld], scope[%d]\n", 
            record.seq,
            record.project_id,
            record.protocol_id,
            record.cache_instance_id,
            record.time_ms,
            key_.project_id, key_.protocol_id, static_cast<int>(key_.scope));
        return false;
    }


    // 注意：这里是丢数据的来源(其实真正丢数据 应该取决于网络环境 而不是主动丢弃)
    // 这里可以做优化：records_.size() > capacity_ + 20 做一个软边界 而不是硬边界
    
    record.cache_instance_id = cache_instance_id_;
    record.seq = next_seq_;
    last_seq_ = record.seq;
    ++next_seq_;
    records_.push_back(record);
    while(records_.size() > capacity_)
    {
        records_.pop_front();
    }

    return true;
}


InteractionRecordCache::LockedSnapshot InteractionRecordCache::lockAndCollect(std::optional<uint64_t> after_cache_instance_id,
    std::optional<uint64_t> after_seq)
{
    // RAII锁住
    InteractionRecordCache::LockedSnapshot locked{std::unique_lock<std::mutex>(mtx_)};

    locked.is_active_ = is_active_;
    locked.is_valid_ = IsValidInteractionCursorPair(
        after_cache_instance_id,
        after_seq);

    if(!locked.is_valid_)
    {
        PUBLISHER_F_WARN(
            "interaction cache cursor cache_instance_id and seq must be supplied as a valid pair\n");
        return locked;
    }

    if(!locked.is_active_)
    {
        PUBLISHER_F_INFO("interaction cache not active\n");
        return locked;
    }

    InteractionCacheSnapshot& snapshot = locked.snapshot_;

    snapshot.cache_instance_id = cache_instance_id_;
    snapshot.last_seq = last_seq_;

    const bool has_cursor = after_cache_instance_id.has_value();
    
    // cache id变化  运行态可能已经重建
    if(has_cursor && after_cache_instance_id.value() != cache_instance_id_)
    {
        // 同 ID ProtocolItem 被重建，旧 cache 的 cursor 不能用于新窗口。
        // 将有效 cursor 改为 0，从新窗口的最早可用记录补发。
        snapshot.cursor_reset = true;
        after_seq = 0;
    }

    if(!has_cursor || records_.empty())
    {
        PUBLISHER_F_DEBUG("interaction cahce cant capture-up! key[%ld][%ld]\n", key_.project_id, key_.protocol_id);
        return locked;
    }

    const uint64_t oldest_seq = records_.front().seq;
    // 判断是否发生断层
    locked.snapshot_.catch_up_gap =
        after_seq.value() < oldest_seq
        && oldest_seq - after_seq.value() > 1;

    for (const auto& record : records_)
    {
        if (record.seq > after_seq.value())
        {
            locked.snapshot_.incr_records.push_back(record);
        }
    }

    // 不可能超过20 缓存总容量=20
    assert(snapshot.incr_records.size() <= capacity_);

    PUBLISHER_F_DEBUG("interaction cahce snapshot: key[%ld][%ld], be[%lu], ed[%lu]\n", key_.project_id, key_.protocol_id, after_seq.value(), snapshot.last_seq);

    return locked;

}

bool InteractionRecordCache::isActive() const 
{
    std::lock_guard<std::mutex> lock(mtx_);
    return is_active_;
}

void InteractionRecordCache::close() 
{
    std::lock_guard<std::mutex> lock(mtx_);
    is_active_ = false;
}


bool InteractionRecordCache::chechkCacheKey(const InteractionRecord &record)
{
    return key_.scope == record.scope
        && key_.project_id == record.project_id
        && key_.protocol_id == record.protocol_id;
}


}
