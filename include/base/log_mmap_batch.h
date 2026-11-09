/**
 * @file log_mmap_batch.h
 * @brief 日志mmap封装批次处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 15:57:39
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_MMAP_BATCH_H__
#define __KIT_LOG_MMAP_BATCH_H__


#include "base/noncopyable.h"
#include "base/log_task_executor.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>

namespace kit_muduo {

class MmapSealedBatch;
class MmapBatchTarget;

struct MmapPoolOptions 
{
    int32_t active_max_threads{4};
    int32_t compression_max_threads{4};
    int32_t active_queue_capacity{64};
    int32_t compression_queue_capacity{64};
    int32_t idle_seconds{30};
};

class MmapBatchResult: Noncopyable
{
public:
    void complete(std::error_code error) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(!done_);
        error_ = error;
        done_ = true;
        cv_.notify_all();
    }

    bool waitFor(int64_t timeout_ms, std::error_code& error) const
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(-1 == timeout_ms)
        {
            cv_.wait(lock, [this] { return done_; });
        }
        else
        {
            if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return done_; }))
            {
                return false;
            }
        }

        error = error_;
        return true;
    }
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    bool done_{false};
    std::error_code error_;
};

struct MmapBatchEndTask
{
    std::shared_ptr<MmapBatchTarget> target;
    uint64_t generation{0};
    uint64_t last_sequence{0};
    std::shared_ptr<MmapBatchResult> result;
};

struct MmapActiveRotateOutcome
{
    bool archive_committed{false};
    bool active_open{false};
    std::error_code error;
    // 保留 rename 失败后恢复旧 fd 的第二个错误。
    std::error_code reopen_error; 
};

class MmapActiveRotateResult : Noncopyable
{
public:
    void complete(MmapActiveRotateOutcome outcome) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(!done_);
        outcome_ = std::move(outcome);
        done_ = true;
        cv_.notify_all();
    }

    MmapActiveRotateOutcome wait() noexcept
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_; });
        return outcome_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{false};
    MmapActiveRotateOutcome outcome_;
};

using MmapDeadline = std::chrono::steady_clock::time_point;

struct MmapActiveRotateTask
{
    std::shared_ptr<MmapBatchTarget> target;
    std::string archive_log_path;
    MmapDeadline deadline{MmapDeadline::max()};
    std::shared_ptr<MmapActiveRotateResult> result;
};

/**
 * @brief MMAP分批处理调度
 */
class MmapBatchScheduler
{
public:
    virtual ~MmapBatchScheduler() = default;
    virtual bool submitBatch(std::shared_ptr<MmapSealedBatch> batch) noexcept = 0;
    virtual bool submitBatchEnd(MmapBatchEndTask task) noexcept = 0;
    virtual bool submitActiveRotate(MmapActiveRotateTask task) noexcept = 0;

};

class LogBatchOrderedSinkDispatcher: Noncopyable
{
public:
    using Task = std::function<void()>;

    explicit LogBatchOrderedSinkDispatcher(MmapPoolOptions options = {});

    ~LogBatchOrderedSinkDispatcher();

    void registerSink(const std::string& path);

    void start(bool compression_enabled = true);

    bool submitPair(const std::string& path, Task active_task, Task compression_task) noexcept;

    bool submitCompression(const std::string& path, Task task) noexcept;

    bool submitActive(const std::string& path, Task task) noexcept;


    void stopAccepting() noexcept;

    bool drain(uint64_t timeout_ms) noexcept;

    void wait() noexcept;

    uint64_t unexpectedFailures() const noexcept;

private:
    struct Lane
    {
        std::deque<std::shared_ptr<Task>> pending;
        bool scheduled{false};
    };
    struct Sink
    {
        std::array<Lane, 2> lanes;
    };
    static constexpr size_t kLaneCapacity = 3;

    bool enqueue(const std::string& path, Task active, Task compression) noexcept;

    bool ensurePumpLocked(const std::shared_ptr<Sink>& sink, size_t branch) noexcept;

    void popqueue(const std::shared_ptr<Sink>& sink, size_t branch,
        std::unique_lock<std::mutex>& lock) noexcept;

private:
    LogTaskExecutor active_pool_;
    LogTaskExecutor compression_pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, std::shared_ptr<Sink>> sinks_;
    size_t outstanding_{0};
    size_t runners_{0};
    bool started_{false};
    bool accepting_{false};
    bool compression_enabled_{false};
    bool closed_{false};
    std::atomic<uint64_t> unexpected_failures_{0};

};

class MmapBatchDispatcher final: public MmapBatchScheduler, Noncopyable
{
public:
    explicit MmapBatchDispatcher(MmapPoolOptions options = {});

    bool submitBatch(std::shared_ptr<MmapSealedBatch> batch) noexcept override;
    bool submitBatchEnd(MmapBatchEndTask task) noexcept override;
    bool submitActiveRotate(MmapActiveRotateTask task) noexcept override;

    void registerSink(const std::string& path) { ordered_.registerSink(path); }
    void start(bool compression_enabled = true) { ordered_.start(compression_enabled); }
    void stopAccepting() noexcept { ordered_.stopAccepting(); }
    bool drain(uint64_t timeout_ms) noexcept { return ordered_.drain(timeout_ms); }
    void wait() noexcept { ordered_.wait(); }

private:
    static void ProcessActive(const std::shared_ptr<MmapSealedBatch>& batch) noexcept;
    static void ProcessCompression(const std::shared_ptr<MmapSealedBatch>& batch) noexcept;
    static void ProcessEnd(const MmapBatchEndTask& task) noexcept;
    static void ProcessActiveRotate(const MmapActiveRotateTask& task) noexcept;


private:
    LogBatchOrderedSinkDispatcher ordered_;
};

} // namespace kit_muduo
#endif //__KIT_LOG_MMAP_BATCH_H__
