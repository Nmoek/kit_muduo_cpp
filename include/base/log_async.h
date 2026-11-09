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
#include "base/log_level.h"
#include "base/log_attr.h"
#include "base/log_config.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <thread>

namespace kit_muduo {

class LogAsyncDispatcher;
class LogFileSinkRegister;
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

// 可共享的通知句柄；sink 只持有它，不捕获 worker 或裸 fd。
class LogAsyncWakeup
{
public:
    LogAsyncWakeup();
    ~LogAsyncWakeup();
    LogAsyncWakeup(const LogAsyncWakeup&) = delete;
    LogAsyncWakeup& operator=(const LogAsyncWakeup&) = delete;
    int fd() const noexcept { return fd_; }
    void notify() const noexcept;
private:
    int fd_{-1};
};

/**
 * @brief 日志异步处理线程
 */
class LogAsyncWorker
{
public:
    LogAsyncWorker(LogAsyncDispatcher& dispatcher, LogFileSinkRegister& sinks);
    ~LogAsyncWorker();

    void start();
    void stop() noexcept;
    bool drain(int64_t timeout_ms) noexcept;
    void join();
    void notify() const noexcept { if (wakeup_) wakeup_->notify(); }

private:
    /// @brief 异步单写线程一次唤醒允许处理的最大上限日志条数
    static constexpr int32_t kLogAsyncHandleLimits = 256;

    void workLoop() noexcept;
    void handle(LogAttr::Ptr attr) noexcept;
    void closeEvents() noexcept;

private:
    LogAsyncDispatcher& dispatcher_;
    LogFileSinkRegister& file_sinks_;
    std::shared_ptr<LogAsyncWakeup> wakeup_;
    int epoll_fd_{-1};
    int timer_fd_{-1};
    std::thread thread_;
    bool started_{false}; // 生命周期操作由所属 dispatcher 的控制线程串行调用。
    std::atomic_bool stopping_{false};
    std::atomic_bool drained_{true}; // 尚未启动时没有待排空工作。

    std::mutex drain_mtx_;
    std::condition_variable drain_cv_;

};

class LogAsyncDispatcher 
{
public:

    explicit LogAsyncDispatcher(LogFileSinkRegister& sinks, LogAsyncConfig async_config = {});
    ~LogAsyncDispatcher();

    // 生命周期由控制线程串行调用；独立使用时须先收拢生产者再 shutdown。
    void start();

    bool shutdown();

    LogSubmitResult submit(LogAttr::Ptr attr) noexcept;
    bool tryPop(LogAttr::Ptr& attr) noexcept;

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

    void join();

private:
    void stopAndWake() noexcept;
    inline bool tryReserveBytes(size_t bytes) noexcept;
    void releaseBytes(size_t bytes) noexcept;
    void notifyCapacityAvailable() noexcept;
    bool waitForCapacity(size_t bytes, std::chrono::steady_clock::time_point deadline);

    bool tryEnqueue(std::shared_ptr<LogAttr> attr) noexcept;
    LogSubmitResult drop(LogAttr::Ptr attr) noexcept;


    static std::chrono::milliseconds WaitForLevel(LogLevel::Level level) noexcept;

private:
    friend class LogSubmissionGuard;
    friend class LogAsyncWorker;
    friend class LogAsyncDispatcherTestPeer;

    // TODO 后续再考虑是否暴露对外配置
    // 高等级等待时间是内部策略，不暴露为配置字段。
    static constexpr uint64_t kWarnLevelWaitMs = 10;
    static constexpr uint64_t kErrorLevelWaitMs = 50;

    BoundedLockFreeQueue<LogAttr::Ptr> queue_;
    LogAsyncWorker worker_;

    std::atomic<bool> accepting_{false};
    bool closed_{false}; // 受 mtx_ 保护；停止或启动失败后不可重启。
    std::atomic<uint64_t> queued_bytes_{0};
    // 覆盖登记后的条件检查、休眠和醒来检查；与通知方通过 RMW 建立顺序。
    std::atomic<size_t> waiting_producers_{0};

    std::shared_ptr<const LogAsyncConfig> async_config_;

    /// @brief 生命周期及生产者等待协议锁；正常入队/出队不持有此锁。
    std::mutex mtx_;
    /// @brief 任务队列不满条件变量
    std::condition_variable not_full_cv_;

    /// @brief 日志健康监测  TODO 后续完善
    LogAsyncHealthStats stats_;

};


}
#endif // __KIT_LOG_ASYNC_H__
