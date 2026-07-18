/**
 * @file protocol_interaction_observe.h
 * @brief 协议项交互观察原始数据adapter层
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-07 19:32:01
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __PROTOCOL_INTERACTION_OBSERVE_H__
#define __PROTOCOL_INTERACTION_OBSERVE_H__

#include "base/noncopyable.h"
#include "domain/protocol_interaction.h"
#include "domain/type.h"
#include "net/http/http_servlet.h"

#include <atomic>
#include <optional>

namespace kit_domain {

struct InteractionRecordCacheKey
{
    InteractionScope scope{InteractionScope::kUnknown};
    int64_t project_id{0};
    int64_t protocol_id{0};
};

struct InteractionCacheSnapshot
{
    /// @brief 缓存Id 用于区分同一 project_id/protocol_id 下的不同运行态 cache，
    uint64_t cache_instance_id{0};
    /// @brief 快照存储后的新的右边界
    uint64_t last_seq{0};
    /// @brief 是否发生快照断层
    bool catch_up_gap{false};
    /// @brief 游标是否重置
    bool cursor_reset{false};
    /// @brief 增量快照数据
    std::vector<InteractionRecord> incr_records;
};

/**
 * @brief 校验交互缓存游标是否成对且可用。
 *
 * 首次打开时两个字段都不提供；增量订阅时必须同时提供缓存实例 Id 和
 * 序号。缓存实例 Id 为 0 只表示未初始化，不能作为有效游标；序号 0
 * 则表示从当前缓存窗口的最早位置开始补发，属于合法值。
 */
inline bool IsValidInteractionCursorPair(
    const std::optional<uint64_t>& cache_instance_id,
    const std::optional<uint64_t>& seq)
{
    if(!cache_instance_id.has_value() && !seq.has_value())
    {
        return true;
    }

    return cache_instance_id.has_value()
        && cache_instance_id.value() != 0
        && seq.has_value();
}

class InteractionRecordCache
{
public:
    class LockedSnapshot
    {
    public:

        ~LockedSnapshot() = default;
        LockedSnapshot(LockedSnapshot&&) noexcept = default;
        LockedSnapshot& operator=(LockedSnapshot&&) noexcept = default;
        // 禁止拷贝
        LockedSnapshot(const LockedSnapshot&) = delete;
        LockedSnapshot& operator=(const LockedSnapshot&) = delete;

        const InteractionCacheSnapshot& snapshot() const { return snapshot_; }
        bool isActive() const { return is_active_; }
        bool isValid() const { return is_valid_; }
    private:
        friend class InteractionRecordCache;

        explicit LockedSnapshot(std::unique_lock<std::mutex> &&lock)
            :lock_(std::move(lock)) { }
        
    private:
        std::unique_lock<std::mutex> lock_;
        bool is_active_{false};
        bool is_valid_{true};
        InteractionCacheSnapshot snapshot_;
    };

public:
    explicit InteractionRecordCache(InteractionRecordCacheKey key, size_t capacity = kDefaultCacheCapacity);

    ~InteractionRecordCache() = default;

    /**
     * @brief 在一把 cache 锁内校验 identity、检查 closed、分配 seq、写入窗口。
     * @param record 
     * @return true 
     * @return false 
     */
    bool tryAppend(InteractionRecord &record);

    /**
     * @brief 返回的 LockedSnapshot 持有 cache 锁，供 Hub 在注册 subscriber 前保持原子边界。
     * @param after_seq 
     * @return LockedSnapshot 
     */
    InteractionRecordCache::LockedSnapshot lockAndCollect(std::optional<uint64_t> after_cache_instance_id,
    std::optional<uint64_t> after_seq);

    bool isActive() const;

    void close();

    uint64_t cacheInstanceId() const { return cache_instance_id_; }

private:
    bool chechkCacheKey(const InteractionRecord &record);

public:
    constexpr static size_t kDefaultCacheCapacity = 20;
    constexpr static size_t kDefaultReplyLimit = 20;

    inline static std::atomic_uint64_t s_next_cache_instance_id_{1};

private:
    mutable std::mutex mtx_;
    /// @brief 缓存索引
    InteractionRecordCacheKey key_;
    bool is_active_{true};
    size_t capacity_{kDefaultCacheCapacity};
    /// @brief 缓存唯一标识Id
    uint64_t cache_instance_id_{0};

    uint64_t next_seq_{1};
    uint64_t last_seq_{0};
    std::deque<InteractionRecord> records_;

};

struct InteractionSideCapture
{
    nlohmann::json meta = nlohmann::json::object();

    std::string head_text;

    std::vector<uint8_t> body_bytes;
    std::vector<uint8_t> raw_bytes;

    /// @brief TODO 注意这个字段的实际作用：告诉前段当前收到的请求Body应该怎么解析的问题 展示的
    ProtocolBodyType expect_body_type{ProtocolBodyType::kNone};
    std::string media_type;
    bool prefer_hex_text_for_binary{false};

    bool hasBody() const { return !body_bytes.empty(); }
    bool hasRaw() const { return !raw_bytes.empty(); }
};

struct ProtocolInteractionObservation
{
    InteractionScope scope{InteractionScope::kUnknown};
    int64_t project_id{0};
    int64_t protocol_id{0};
    uint64_t cache_instance_id{0};
    ProtocolType protocol_type{ProtocolType::kUnknown};

    int64_t time_ms{0};
    std::string peer_addr;
    InteractionResult result{InteractionResult::kRouteNotFound};
    std::string error_message;

    InteractionSideCapture request;
    InteractionSideCapture response;

    std::weak_ptr<InteractionRecordCache> weak_record_cache;

    static InteractionResult ToInterResult(const kit_muduo::http::MatchStatus &match_status)
    {
        if(kit_muduo::http::MatchStatus::kFound == match_status)
        {
            return InteractionResult::kMatched;
        }
        else if(kit_muduo::http::MatchStatus::kPathFoundMethodNotAllowed == match_status)
        {
            return InteractionResult::kMethodNotAllowed;
        }
        else if(kit_muduo::http::MatchStatus::kNotFound == match_status)
        {
            return InteractionResult::kRouteNotFound;
        }

        return InteractionResult::kInternalError;
    }
};


}
#endif // __PROTOCOL_INTERACTION_OBSERVE_H__
