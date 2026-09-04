/**
 * @file log_async.h
 * @brief 日志异步处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-30 22:57:36
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_ASYNC_H__
#define __KIT_LOG_ASYNC_H__

#include "base/bounded_lock_free_queue.h"
#include "base/sem.h"
#include "base/log_level.h"
#include "base/log_attr.h"
#include "base/thread.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <memory>

namespace kit_muduo {

class LogAsyncDispatcher;
class LogFileSinkRegister;
class Thread;
struct LogAsyncConfig;


enum class LogSubmitStatus
{ 
    kQueued,
    kDropped,
    kStopped
};

struct LogSubmitResult
{ 
    LogSubmitStatus status;
    size_t bytes{0}; 
};

/**
 * @brief 日志异步队列健康统计
 */
struct LogAsyncHealthStats
{
    std::atomic_uint64_t dropped_records{0};
    std::atomic_uint64_t dropped_bytes{0};
    std::atomic_uint64_t dropped_debug{0};
    std::atomic_uint64_t dropped_info{0};
    std::atomic_uint64_t dropped_warn{0};
    std::atomic_uint64_t dropped_error{0};
    std::atomic_uint64_t dropped_fatal{0};

    std::atomic_uint64_t queue_full_events{0};
    std::atomic_uint64_t queue_wait_timeouts{0};
    std::atomic_uint64_t writer_queue_high_watermark{0};
};

/**
 * @brief 日志异步处理线程
 */
class LogAsyncWorker
{
public:
    LogAsyncWorker(LogAsyncDispatcher& dispatcher);
    ~LogAsyncWorker();

    void start();
    void stop() noexcept;
    bool drain(int64_t timeout_ms) noexcept;

private:
    void workLoop() noexcept;
    void handle(LogAttr::Ptr attr) noexcept;

private:
    LogAsyncDispatcher& dispatcher_;
    // LogFileSinkRegister& file_sinks_;
    Thread thread_;
    std::atomic_bool stopping_{false};
    std::atomic_bool drained_{false};

    std::mutex drain_mtx_;
    std::condition_variable drain_cv_;

};

class LogAsyncDispatcher 
{
public:
    explicit LogAsyncDispatcher(size_t queue_capacity = 8192);
    explicit LogAsyncDispatcher(LogAsyncConfig async_config);

    void start();

    bool shutdown();

    LogSubmitResult submit(LogAttr::Ptr attr) noexcept;
    bool tryPop(LogAttr::Ptr& attr) noexcept;
    bool waitFor(int64_t timeout_ms);
    void wait();

    size_t capacity() const noexcept { return queue_.capacity(); }
    /**
     * @brief 队列瞬时大小
     * @return size_t 
     */
    size_t queueSize() const noexcept { return queue_.size(); }

    bool queueEmpty() const noexcept { return queueSize() == 0; }

    void setConfig(LogAsyncConfig config);
    void setConfig(std::shared_ptr<const LogAsyncConfig> config);

    const std::shared_ptr<const LogAsyncConfig> config() const noexcept;

private:
    inline bool tryReserveBytes(size_t bytes) noexcept;
    inline void releaseBytes(size_t bytes) noexcept;

    bool tryEnqueue(std::shared_ptr<LogAttr> attr) noexcept;
    LogSubmitResult drop(LogAttr::Ptr attr) noexcept;


    static std::chrono::milliseconds WaitForLevel(LogLevel::Level level) noexcept;

private:
    friend class LogSubmissionGuard;
    friend class LogAsyncWriter;

    // TODO 后续再考虑是否暴露对外配置
    // 高等级等待时间是内部策略，不暴露为配置字段。
    static constexpr uint64_t kWarnLevelWaitMs = 10;
    static constexpr uint64_t kErrorLevelWaitMs = 50;

    BoundedLockFreeQueue<LogAttr::Ptr> queue_;
    LogAsyncWorker worker_;

    std::atomic<bool> accepting_{false};
    std::atomic<uint64_t> queued_bytes_{0};

    std::shared_ptr<const LogAsyncConfig> async_config_;

    /// @brief 控制队列空载/满载情况锁
    std::mutex mtx_;
    /// @brief 任务队列不空条件变量
    std::condition_variable not_empty_cv_;
    /// @brief 任务队列不满条件变量
    std::condition_variable not_full_cv_;

    /// @brief 日志健康监测  TODO 后续完善
    LogAsyncHealthStats stats_;

};


}
#endif // __KIT_LOG_ASYNC_H__
