/**
 * @file log_task_executor.h
 * @brief 日志系统专用 线程管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-10-29 16:53:06
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_TASK_EXECUTOR_H__
#define __KIT_LOG_TASK_EXECUTOR_H__

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kit_muduo {

// 日志内部的 CACHE 执行器；不依赖会输出普通日志的 Thread/ThreadPool。
class LogTaskExecutor
{
public:
    using Task = std::function<void()>;

    struct Options
    {
        size_t initial_threads{1};
        size_t max_threads{4};
        size_t queue_capacity{64};
        int32_t idle_seconds{30};
        std::string thread_name_prefix{"log-task"};
    };

    explicit LogTaskExecutor(Options options);
    ~LogTaskExecutor();

    LogTaskExecutor(const LogTaskExecutor&) = delete;
    LogTaskExecutor& operator=(const LogTaskExecutor&) = delete;
    LogTaskExecutor(LogTaskExecutor&&) = delete;
    LogTaskExecutor& operator=(LogTaskExecutor&&) = delete;

    void start();
    // false: 从未接收；true: 已接收，即使随后扩容失败也不改变结果。
    // Task 参数构造发生在调用前，调用方仍须处理该处可能的异常。
    bool trySubmit(Task task) noexcept;
    void stopAccepting() noexcept;
    bool drain(uint64_t timeout_ms) noexcept;
    // 无超时回收，可重复调用；不得由本执行器的 worker 调用。
    void join();

private:
    enum class WorkerState { kEmpty, kRunning, kExited, kReaping };
    struct Worker
    {
        std::thread thread;
        WorkerState state{WorkerState::kEmpty};
    };

    bool needsWorkerLocked() const;
    void startWorkerLocked(size_t index);
    void growLocked(std::unique_lock<std::mutex>& lock) noexcept;
    void workLoop(size_t index) noexcept;

    const Options options_;
    // Start/Join 串行化；worker 与提交线程从不获取该锁。
    std::mutex lifecycle_mutex_;
    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable drained_cv_;
    std::condition_variable reaped_cv_;
    std::deque<Task> queue_;
    std::vector<Worker> workers_;
    size_t live_workers_{0};
    size_t running_tasks_{0};
    size_t reaping_workers_{0};
    bool started_{false};
    bool accepting_{false};
    bool closed_{false};
};

} // namespace kit_muduo
#endif // __KIT_LOG_TASK_EXECUTOR_H__
