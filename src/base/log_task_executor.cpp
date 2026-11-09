/**
 * @file log_task_executor.cpp
 * @brief 日志系统专用 线程管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-10-29 16:53:21
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "base/log_task_executor.h"
#include "base/log_inner.h"

#include <chrono>
#include <cstdio>
#include <pthread.h>
#include <stdexcept>
#include <utility>

namespace kit_muduo {

LogTaskExecutor::LogTaskExecutor(Options options)
    : options_(options)
{
    if (options_.initial_threads == 0
        || options_.max_threads < options_.initial_threads
        || options_.queue_capacity == 0 || options_.idle_seconds <= 0)
    {
        throw std::invalid_argument("invalid log executor options");
    }
    workers_.resize(options_.max_threads);
}

LogTaskExecutor::~LogTaskExecutor()
{
    join();
}

void LogTaskExecutor::start()
{
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_)
    {
        throw std::logic_error("log executor cannot restart");
    }
    if (started_) return;

    try {
        for (size_t i = 0; i < options_.initial_threads; ++i)
        {
            startWorkerLocked(i);
        }
    } catch (...) {
        closed_ = true;
        work_cv_.notify_all();
        lock.unlock();
        for (auto& worker : workers_)
        {
            if (worker.thread.joinable()) worker.thread.join();
        }
        throw;
    }
    started_ = true;
    accepting_ = true;
}

bool LogTaskExecutor::trySubmit(Task task) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!accepting_ || !task || queue_.size() >= options_.queue_capacity)
    {
        return false;
    }
    try {
        // 先分配空节点，再以 noexcept swap 转移任务所有权。
        queue_.emplace_back();
    } catch (...) {
        return false;
    }
    queue_.back().swap(task);
    work_cv_.notify_one();
    // 入队是接收提交点；扩容失败只能降级为已有线程执行。
    growLocked(lock);
    return true;
}

bool LogTaskExecutor::needsWorkerLocked() const
{
    return accepting_ && live_workers_ < options_.max_threads
        && queue_.size() > live_workers_ - running_tasks_;
}

void LogTaskExecutor::startWorkerLocked(size_t index)
{
    auto& worker = workers_[index];
    worker.state = WorkerState::kRunning;
    ++live_workers_; // 创建中的线程也占用额度。
    try {
        worker.thread = std::thread([this, index] {
            // Linux 名称最多 15 字节；在子线程内、执行任务前命名，覆盖 CACHE 扩容。
            char name[16]{};
            std::snprintf(name, sizeof(name), "%s-%zu",
                options_.thread_name_prefix.c_str(), index);
            const int error = ::pthread_setname_np(::pthread_self(), name);
            if (error != 0)
            {
                // pthread API 直接返回错误码，不读取 errno，也不递归提交日志。
                try {
                    LOG_INNER_WARN("log executor thread name '%s' failed: %d\n",
                        name, error);
                } catch (...) {
                    // 内部打印也可能分配失败，不能因此跳过工作循环。
                }
            }
            workLoop(index);
        });
    } catch (...) {
        --live_workers_;
        worker.state = WorkerState::kEmpty;
        throw;
    }
}

void LogTaskExecutor::growLocked(std::unique_lock<std::mutex>& lock) noexcept
{
    if (!needsWorkerLocked()) return;

    size_t index = workers_.size();
    for (size_t i = 0; i < workers_.size(); ++i)
    {
        if (workers_[i].state == WorkerState::kEmpty
            || workers_[i].state == WorkerState::kExited)
        {
            index = i;
            break;
        }
    }
    if (index == workers_.size()) return;

    auto& worker = workers_[index];
    if (worker.state == WorkerState::kExited)
    {
        worker.state = WorkerState::kReaping;
        ++reaping_workers_;
        std::thread retired = std::move(worker.thread);
        lock.unlock();
        retired.join(); // 仅回收已经退出工作循环的线程，且不持队列锁。
        lock.lock();
        worker.state = WorkerState::kEmpty;
        --reaping_workers_;
        reaped_cv_.notify_all();
        // 锁外回收期间可能已经 StopAccepting/Join，或由其他提交者扩容。
        if (!needsWorkerLocked()) return;
    }

    try {
        startWorkerLocked(index);
    } catch (...) {
        lock.unlock();
        try {
            LOG_INNER_WARN("log executor growth failed; accepted task retained\n");
        } catch (...) {
            // 诊断失败也不能改变已接收任务的归属。
        }
        lock.lock();
    }
}

void LogTaskExecutor::workLoop(size_t index) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;)
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(options_.idle_seconds);
        while (queue_.empty() && !closed_)
        {
            const auto ready = [this] { return closed_ || !queue_.empty(); };
            if (live_workers_ <= options_.initial_threads)
            {
                work_cv_.wait(lock, ready);
            }
            else if (!work_cv_.wait_until(lock, deadline, ready)
                && live_workers_ > options_.initial_threads)
            {
                --live_workers_;
                workers_[index].state = WorkerState::kExited;
                return;
            }
        }
        if (queue_.empty() && closed_) break;

        Task task;
        task.swap(queue_.front());
        queue_.pop_front();
        ++running_tasks_;
        lock.unlock();
        try {
            task();
        } catch (...) {
            try {
                LOG_INNER_EXCPTION("log executor task threw; worker continuing\n");
            } catch (...) {
                // 保证继续执行捕获对象释放和运行计数收尾。
            }
        }
        task = {}; // 捕获对象的析构也不能在队列锁内执行。
        lock.lock();
        --running_tasks_;
        if (queue_.empty() && running_tasks_ == 0) drained_cv_.notify_all();
    }
    --live_workers_;
    workers_[index].state = WorkerState::kExited;
}

void LogTaskExecutor::stopAccepting() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    closed_ = true;
    work_cv_.notify_all();
}

bool LogTaskExecutor::drain(uint64_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    return drained_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return queue_.empty() && running_tasks_ == 0;
    });
}

void LogTaskExecutor::join()
{
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    {
        std::unique_lock<std::mutex> lock(mutex_);
        accepting_ = false;
        closed_ = true;
        work_cv_.notify_all();
        reaped_cv_.wait(lock, [this] { return reaping_workers_ == 0; });
    }
    // 入口已关闭且回收者已退出；不再有提交者改写 thread 句柄。
    for (auto& worker : workers_)
    {
        if (worker.thread.joinable()) worker.thread.join();
    }
}

} // namespace kit_muduo
