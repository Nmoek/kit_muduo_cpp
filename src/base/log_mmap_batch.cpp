/**
 * @file log_mmap_batch.cpp
 * @brief 日志mmap分批处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 20:00:45
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_mmap_batch.h"
#include <chrono>
#include <exception>


namespace kit_muduo {

LogBatchOrderedSinkDispatcher::LogBatchOrderedSinkDispatcher(const MmapPoolOptions& options)
    :
    active_pool_(1)
    ,compression_pool_(1)
{
    setLogPoolConfig(active_pool_, options.active_max_threads, options.active_queue_capacity, options.idle_seconds);
    
    setLogPoolConfig(compression_pool_, options.compression_max_threads, options.compression_queue_capacity, options.idle_seconds);
}

LogBatchOrderedSinkDispatcher::~LogBatchOrderedSinkDispatcher()
{
    wait();
}

void LogBatchOrderedSinkDispatcher::registerSink(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_)
    {
        throw std::logic_error("register sink before start");
    }
    if (path.empty() || !sinks_.emplace(path, std::make_shared<Sink>()).second)
    {
        throw std::invalid_argument("empty or duplicate normalized path");
    }
}

    // 生命周期由控制线程串行调用；业务入队可以与 stopAccepting 并发。
void LogBatchOrderedSinkDispatcher::start(bool compression_enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_)
    {
        return;
    }
    if (closed_)
    {
        throw std::logic_error("log batch dispatcher cannot restart");
    }

    try {
        active_pool_.start();
        if(compression_enabled)
        {
            compression_pool_.start();
        }
    } catch(const std::exception &e) {
        active_pool_.stop();
        if(compression_enabled)
        {
            compression_pool_.stop();
        }
        throw;
    } catch (...) {
        active_pool_.stop();
        if(compression_enabled)
        {
            compression_pool_.stop();
        }
        throw;
    }
    compression_enabled_ = compression_enabled;
    started_ = true;
    accepting_ = true;
}

bool LogBatchOrderedSinkDispatcher::submitPair(const std::string& path, Task active_task, Task compression_task) noexcept
{
    return enqueue(path, std::move(active_task), std::move(compression_task));
}

bool LogBatchOrderedSinkDispatcher::submitCompression(const std::string& path, Task task) noexcept
{
    return enqueue(path, {}, std::move(task));
}

void LogBatchOrderedSinkDispatcher::stopAccepting() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
}

bool LogBatchOrderedSinkDispatcher::drain(uint64_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return outstanding_ == 0 && runners_ == 0;
    });
}

void LogBatchOrderedSinkDispatcher::wait() noexcept
{
    stopAccepting();
    // TODO  超时控制
    drain(30000);
    // 被取消、未计入 runners_ 的 admission 闭包也须等池回收后才能销毁 this。
    active_pool_.stop();
    compression_pool_.stop();
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
}

uint64_t LogBatchOrderedSinkDispatcher::unexpectedFailures() const noexcept
{
    return unexpected_failures_.load(std::memory_order_relaxed);
}

bool LogBatchOrderedSinkDispatcher::enqueue(const std::string& path, Task active, Task compression) noexcept
{
    std::shared_ptr<Task> a;
    std::shared_ptr<Task> c;
    try {
        if (active) a = std::make_shared<Task>(std::move(active));
        if (compression) c = std::make_shared<Task>(std::move(compression));
    } catch (...) {
        return false;
    }
    if (!a && !c) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sinks_.find(path);
    if (!accepting_ || it == sinks_.end() ||
        (c && !compression_enabled_))
    {
        return false;
    }
    auto sink = it->second;
    auto& al = sink->lanes[0];
    auto& cl = sink->lanes[1];
    if ((a && al.pending.size() >= kLaneCapacity) ||
        (c && cl.pending.size() >= kLaneCapacity))
    {
        return false;
    }

    bool added_a = false;
    bool added_c = false;
    try {
        if (a) { al.pending.push_back(a); added_a = true; }
        if (c) { cl.pending.push_back(c); added_c = true; }
    } catch (...) {
        if (added_a) al.pending.pop_back();
        return false;
    }

    const bool ready_a = !a || ensurePumpLocked(sink, 0);
    const bool ready_c = !c || ensurePumpLocked(sink, 1);

    if (!ready_a || !ready_c)
    {
        // 泵尚不能取得 mutex_，已调度的另一侧只能稍后看到回滚后的队列。
        if (added_a) al.pending.pop_back();
        if (added_c) cl.pending.pop_back();
        return false;
    }
    outstanding_ += size_t(added_a) + size_t(added_c);
    return true; // unlock 是发布点；此后两个分支才可以消费本次工作。
}

bool LogBatchOrderedSinkDispatcher::ensurePumpLocked(const std::shared_ptr<Sink>& sink, size_t branch) noexcept
{
    Lane& lane = sink->lanes[branch];
    if (lane.scheduled) return true;
    try {
        // 此票据也处理“池已入队，随后 addThread 抛异常”的不确定提交。
        auto admitted = std::make_shared<bool>(false);
        ThreadPool& pool = branch == 0 ? active_pool_ : compression_pool_;
        const auto result = pool.trySubmitTask(0, [this, sink, branch, admitted] {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!*admitted) return;
            popqueue(sink, branch, lock);
        });
        if (!result.ok()) return false;
        *admitted = true; // 所有访问都受 mutex_ 保护
        lane.scheduled = true;
        ++runners_;
        return true;
    } catch (...) {
        return false; // admitted 仍 false；若闭包已经入池，它只做空返回。
    }
}

void LogBatchOrderedSinkDispatcher::popqueue(const std::shared_ptr<Sink>& sink, size_t branch, std::unique_lock<std::mutex>& lock) noexcept
{
    Lane& lane = sink->lanes[branch];
    while (!lane.pending.empty())
    {
        auto task = lane.pending.front();
        lock.unlock();
        try {
            (*task)(); // 文件写入、压缩和 branch completion 均在发布锁外
        } catch (...) {
            // 业务闭包负责终结对应 completion；此计数是最后一道防线。
            unexpected_failures_.fetch_add(1, std::memory_order_relaxed);
        }
        task.reset();
        lock.lock();
        lane.pending.pop_front();
        --outstanding_;
        cv_.notify_all();
    }
    lane.scheduled = false; // 与生产者共用 mutex_，无丢失唤醒窗口。
    --runners_;
    cv_.notify_all();
}

/***********MmapBatchDispatcher*************/

MmapBatchDispatcher::MmapBatchDispatcher(const MmapPoolOptions& options)
    : ordered_(options)
{

}



}