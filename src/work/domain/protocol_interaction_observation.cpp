/**
 * @file protocol_interaction_observation.cpp
 * @brief 协议项交互观察原始数据adapter层
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-15 18:19:33
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/domain_log.h"
#include "domain/protocol_interaction.h"
#include "domain/runtime_result.h"
#include <atomic>
#include <limits>
#include <mutex>
#include "domain/protocol_interaction_observation.h"

namespace kit_domain {

namespace {

inline void MarkAttachmentRefsUnavailable(std::vector<InteractionAttachmentRef> &refs) noexcept
{
    for(auto &ref : refs)
    {
        ref.binary_available = false;
    }
}

inline void MarkSideAttachmentsUnavailable(InteractionSide &side) noexcept
{
    MarkAttachmentRefsUnavailable(side.body.attachments);

    if(side.raw_packet.has_value())
    {
        MarkAttachmentRefsUnavailable(side.raw_packet->attachments);
    }
}

} // namespace

size_t CalculateInteractionSidecarBytes(const InteractionRecord& record) noexcept
{
    size_t total = 0;
    for(const auto &sidecar : record.binary_sidecars)
    {
        if(!sidecar.bytes)
        {
            continue;
        }
        const size_t bytes_len = sidecar.bytes->size();
        if(bytes_len > std::numeric_limits<size_t>::max() - total)
        {
            return std::numeric_limits<size_t>::max();
        }

        total += bytes_len;
    }
    return total;
}


void MarkCachedAttachmentsUnavailable(InteractionRecord& record) noexcept
{
    MarkSideAttachmentsUnavailable(record.request);
    MarkSideAttachmentsUnavailable(record.response);

    record.binary_sidecars.clear();
}


InteractionRecordCache::InteractionRecordCache(InteractionRecordCacheKey key, InteractionRecordCacheConfig config)
    :key_(std::move(key))
    ,config_(std::move(config))
    ,is_active_(true)
    ,cache_instance_id_(s_next_cache_instance_id_.fetch_add(1, std::memory_order_relaxed))
{
    if(config_.max_records == 0)
    {
        throw std::invalid_argument( "interaction cache max_records disallowed 0");
    }

    // max_sidecar_bytes == 0 是合法配置，表示 cache 只保存 metadata。

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
    record.seq = next_seq_++;
    last_seq_ = record.seq;

    InteractionRecord cached_record{record};
    size_t incoming_sidecar_bytes = CalculateInteractionSidecarBytes(cached_record);

    if(incoming_sidecar_bytes > config_.max_sidecar_bytes)
    {
        MarkCachedAttachmentsUnavailable(cached_record);
        incoming_sidecar_bytes = 0;
        ++metadata_only_count_;
    }

    while(!records_.empty())
    {
        const bool exceeds_record_limit = records_.size() >= config_.max_records;
        const bool exceeds_byte_limit = incoming_sidecar_bytes > config_.max_sidecar_bytes - retained_sidecar_bytes_;

        if(!exceeds_record_limit && !exceeds_byte_limit)
        {
            break;
        }

        // 只要不满足 数量 和 bytes大小都要淘汰
        // 注意 如果因为bytes大小不满足淘汰的需要记录数量
        evictOldestRecordUnLocked(exceeds_byte_limit);
    }

    // records_ 为空后仍不能放入的情况，只可能来自配置或整数边界。
    // 使用 metadata-only 做最后防御，不允许击穿字节上限。
    if(incoming_sidecar_bytes > config_.max_sidecar_bytes)
    {
        MarkCachedAttachmentsUnavailable(cached_record);
        incoming_sidecar_bytes = 0;
        ++metadata_only_count_;
    }

    records_.push_back(CachedInteractionRecord{
        .record = std::move(cached_record),
        .sidecar_bytes = incoming_sidecar_bytes,
    });

    retained_sidecar_bytes_ += incoming_sidecar_bytes;

    assert(records_.size() <= config_.max_records);
    assert(retained_sidecar_bytes_ <= config_.max_sidecar_bytes);

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
        PUBLISHER_F_WARN("interaction cache cursor is a valid pair\n");
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

    const uint64_t oldest_seq = records_.front().record.seq;
    // 判断是否发生断层
    locked.snapshot_.catch_up_gap =
        after_seq.value() < oldest_seq
        && oldest_seq - after_seq.value() > 1;

    for (const auto& cached : records_)
    {
        if (cached.record.seq > after_seq.value())
        {
            locked.snapshot_.incr_records.push_back(cached.record);
        }
    }

    // 不可能超过 缓存总容量=20
    assert(snapshot.incr_records.size() <= config_.max_records);

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
    records_.clear();
    retained_sidecar_bytes_ = 0;
}

size_t InteractionRecordCache::retainedSidecarBytes() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return retained_sidecar_bytes_;
}

size_t InteractionRecordCache::recordCount() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return records_.size();
}

uint64_t InteractionRecordCache::byteEvictionCount() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return byte_eviction_count_;
}

uint64_t InteractionRecordCache::metadataOnlyCount() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return metadata_only_count_;
}



bool InteractionRecordCache::chechkCacheKey(const InteractionRecord &record)
{
    return key_.scope == record.scope
        && key_.project_id == record.project_id
        && key_.protocol_id == record.protocol_id;
}

void InteractionRecordCache::evictOldestRecordUnLocked(bool caused_by_bytes)
{
    assert(!records_.empty());

    const size_t old_bytes = records_.front().sidecar_bytes;
    // 不可能出现超限记录在队列里
    assert(retained_sidecar_bytes_ >= old_bytes);

    retained_sidecar_bytes_ -= old_bytes;
    records_.pop_front();

    if(caused_by_bytes)
    {
        ++byte_eviction_count_;
    }
}

}
