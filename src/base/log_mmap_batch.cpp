/**
 * @file log_mmap_batch.cpp
 * @brief 日志mmap分批处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 20:00:45
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_mmap_batch.h"
#include "base/log_inner.h"
#include "base/log_mmap_completion.h"

#include <chrono>
#include <exception>
#include <utility>


namespace kit_muduo {

namespace {

LogTaskExecutor::Options MakeLogExecutorOptions(int32_t max_threads,
    int32_t queue_capacity, int32_t idle_seconds, const char* thread_name_prefix)
{
    if (max_threads < 1 || queue_capacity < 1 || idle_seconds < 1)
    {
        throw std::invalid_argument("invalid log pool options");
    }
    // 初始 1 个线程；max_threads 是动态扩容上限，不是初始数量。
    return {1, static_cast<size_t>(max_threads),
        static_cast<size_t>(queue_capacity), idle_seconds, thread_name_prefix};
}

} // namespace

LogBatchOrderedSinkDispatcher::LogBatchOrderedSinkDispatcher(MmapPoolOptions options)
    : active_pool_(MakeLogExecutorOptions(options.active_max_threads,
          options.active_queue_capacity, options.idle_seconds, "log-active"))
    ,compression_pool_(MakeLogExecutorOptions(options.compression_max_threads,
          options.compression_queue_capacity, options.idle_seconds, "log-compress"))
{
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
    if (closed_)
    {
        throw std::logic_error("log batch dispatcher cannot restart");
    }
    if (started_) return;

    try {
        active_pool_.start();
        if(compression_enabled)
        {
            compression_pool_.start();
        }
    } catch (...) {
        closed_ = true;
        active_pool_.join();
        compression_pool_.join();
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

bool LogBatchOrderedSinkDispatcher::submitActive(const std::string& path, Task task) noexcept
{
    return enqueue(path, std::move(task), {});
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
    {
        std::unique_lock<std::mutex> lock(mutex_);
        accepting_ = false;
        closed_ = true;
        // 最终回收无超时；限时退出由 manager 先 drain 决定是否继续。
        cv_.wait(lock, [this] { return outstanding_ == 0 && runners_ == 0; });
    }
    active_pool_.join();
    compression_pool_.join();
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
    if (!accepting_ || path.empty() || (c && !compression_enabled_))
    {
        return false;
    }
    std::shared_ptr<Sink> sink = nullptr;
    const auto it = sinks_.find(path);
    // HACK 注意 这里目前改为加入则创建新的队列  目的是解决初始化时的顺序问题
    if (it == sinks_.end())
    {
        try {
            sink = std::make_shared<Sink>();
            sinks_.emplace(path, sink);
        } catch (...) {
            return false;
        }
    }
    else
    {
        sink = it->second;
    }
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
    // unlock 是发布点；此后两个分支才可以消费本次工作。
    return true;
}

bool LogBatchOrderedSinkDispatcher::ensurePumpLocked(const std::shared_ptr<Sink>& sink, size_t branch) noexcept
{
    Lane& lane = sink->lanes[branch];
    if (lane.scheduled) return true;
    try {
        LogTaskExecutor& pool = branch == 0 ? active_pool_ : compression_pool_;
        const bool accepted = pool.trySubmit([this, sink, branch] {
            std::unique_lock<std::mutex> lock(mutex_);
            popqueue(sink, branch, lock);
        });
        if (!accepted) return false;
        // pump 必须先获取 mutex_，此处设置和双分支回滚均先于消费发布。
        lane.scheduled = true;
        ++runners_;
        return true;
    } catch (...) {
        return false; // Task 参数构造失败；执行器尚未接收。
    }
}

void LogBatchOrderedSinkDispatcher::popqueue(const std::shared_ptr<Sink>& sink, size_t branch, std::unique_lock<std::mutex>& lock) noexcept
{
    Lane& lane = sink->lanes[branch];
    while (!lane.pending.empty())
    {
        // 队头保留空占位以维持容量统计，闭包最后的强引用在锁外释放。
        auto task = std::move(lane.pending.front());
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

MmapBatchDispatcher::MmapBatchDispatcher(MmapPoolOptions options)
    : ordered_(std::move(options))
{

}

bool MmapBatchDispatcher::submitBatch(std::shared_ptr<MmapSealedBatch> batch) noexcept
{
    if(!batch)
    {
        return false;
    }

    try {
        LogBatchOrderedSinkDispatcher::Task active = [batch] { ProcessActive(batch); };
        LogBatchOrderedSinkDispatcher::Task compression;

        if(batch->compressionEnabled())
        {
            compression = [batch] { ProcessCompression(batch); };
        }

        if(!ordered_.submitPair(batch->target().activePath(),
            std::move(active), std::move(compression)))
        {
            return false;
        }
    } catch (...) {
        // 此范围只包含闭包构造和事务提交；提交后的错误不能再伪报为“未接受”。
        LOG_INNER_EXCPTION("log mmap dispatcher submit pair task exception! gene[%lu], seq[%lu]\n", batch->generation(), batch->sequence());
        return false;
    }

    // 调用者是单 LogWriter；generation 已预注册，下面不分配 map 节点。
    batch->target().submitted(batch->generation(), batch->sequence());

    // 设计妙点: 这里处理是为了后续状态检查的统一性，即使没有压缩也推进压缩进度
    if(!batch->compressionEnabled())
    {
        batch->completion().skipCompression("compression disabled");
        batch->target().compressionCompleted(batch->generation(), batch->sequence());
    }
    return true;
}


bool MmapBatchDispatcher::submitBatchEnd(MmapBatchEndTask task) noexcept
{
    if(!task.target || !task.result)
    {
        return false;
    }
    try {
        const std::string path = task.target->activePath();

        return ordered_.submitCompression(path,
            [task = std::move(task)] { ProcessEnd(task); });
    } catch (...) {
        LOG_INNER_EXCPTION("log mmap dispatcher submit end task exception! gene[%lu], seq[%lu]\n", task.generation, task.last_sequence);
        return false;
    }
}

bool MmapBatchDispatcher::submitActiveRotate(MmapActiveRotateTask task) noexcept
{
    if(!task.target || !task.result || task.archive_log_path.empty())
    {
        return false;
    }

    try {
        const std::string path = task.target->activePath();
        return ordered_.submitActive(path,
            [task = std::move(task)] { ProcessActiveRotate(task); });
    } catch(...) {
        return false;
    }
}

void MmapBatchDispatcher::ProcessActive(const std::shared_ptr<MmapSealedBatch>& batch) noexcept
{
    if(!batch->completion().startActive())
    {
        return;
    }
    std::error_code error;
    try {
        error = batch->target().appendActive(
            batch->generation(), batch->sequence(), batch->payload());
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
    }
    if(error)
    {
        LOG_INNER_ERROR("log mmap precess active error: %s! gene[%lu], seq[%lu]\n", error.message().c_str(), batch->generation(), batch->sequence());

        batch->target().setActiveFailed();
    }

    // 注意: 完成后不能再读取 payload；backend 此时可能回收该 slot。
    batch->completion().completeActive(error);
    batch->target().activeCompleted(batch->generation(), batch->sequence());
}

void MmapBatchDispatcher::ProcessCompression(const std::shared_ptr<MmapSealedBatch>& batch) noexcept
{
    if(!batch->completion().startCompression())
    {
        return;
    }
    std::error_code error;
    try {
        error = batch->target().continueCompression(
            batch->generation(), batch->sequence(), batch->payload());
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
    }
    if(error)
    {
        LOG_INNER_ERROR("log mmap precess compression error: %s! gene[%lu], seq[%lu]\n", error.message().c_str(), batch->generation(), batch->sequence());

        batch->target().setDegraded(batch->generation(), "realtime compression failed");
    }

    batch->completion().completeCompression(error);
    batch->target().compressionCompleted(batch->generation(), batch->sequence());
}

void MmapBatchDispatcher::ProcessEnd(const MmapBatchEndTask& task) noexcept
{
    std::error_code error;
    try {
        const auto p = task.target->progress(task.generation);
        if (p.active_completed_sequence < task.last_sequence
            || p.compression_completed_sequence < task.last_sequence)
        {
            error = std::make_error_code(std::errc::operation_not_permitted);
        }
        else
        {
            error = task.target->finishCompression(task.generation);
        }
    } catch (...) {
        error = std::make_error_code(std::errc::io_error);
    }
    task.result->complete(error);
}

void MmapBatchDispatcher::ProcessActiveRotate(const MmapActiveRotateTask& task) noexcept
{
    const auto outcome = task.target->rotateActive(task.archive_log_path, task.deadline);
    task.result->complete(std::move(outcome));
}

} // namepsace kit_muduo
