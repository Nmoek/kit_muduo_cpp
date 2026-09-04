/**
 * @file log_async.cpp
 * @brief 日志异步处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-31 02:41:44
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_async.h"
#include "base/log_inner.h"
#include "base/log_attr.h"
#include "base/thread.h"
#include "base/log_config.h"
#include "base/log.h"

#include <atomic>
#include <chrono>
#include <memory>

namespace kit_muduo {

LogAsyncWorker::LogAsyncWorker(LogAsyncDispatcher& dispatcher)
    :dispatcher_(dispatcher)
    ,thread_([this](){ workLoop(); }, "log-asy-worker")
    ,stopping_(false)
    ,drained_(false)
{

}

LogAsyncWorker::~LogAsyncWorker()
{
    try {
        thread_.join();
    } catch(...) {
        LOG_INNER_EXCPTION("log async worker join failed\n");
    }

}


void LogAsyncWorker::start()
{
    thread_.start();
}

void LogAsyncWorker::stop() noexcept
{
    stopping_.store(true, std::memory_order_release);
}

bool LogAsyncWorker::drain(int64_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(drain_mtx_);
    return drain_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this](){ 
        return drained_.load(std::memory_order_acquire);
    });
}

void LogAsyncWorker::workLoop() noexcept
{
    for(;;)
    {
        LogAttr::Ptr attr;
        while(dispatcher_.tryPop(attr))
        {
            handle(std::move(attr));
        }

        if (stopping_.load(std::memory_order_acquire)
            && dispatcher_.queueEmpty())
        {
            break;
        }
        dispatcher_.waitFor(200); // 弱同步兜底 防止唤醒丢失
    }
    drained_.store(true, std::memory_order_release);
    drain_cv_.notify_all();
}

void LogAsyncWorker::handle(LogAttr::Ptr attr) noexcept
{
    try {
        if(attr && attr->getLogger())
        {
            attr->getLogger()->logUnchecked(std::move(attr));
        }

    } catch (const std::exception& e) {
        LOG_INNER_ERROR("LogWriter dispatch failed: %s\n", e.what());

    } catch (...) {
        LOG_INNER_ERROR("LogWriter dispatch failed: unknown exception\n");
    }
}



/****************LogAsyncDispatcher*******************/

LogAsyncDispatcher::LogAsyncDispatcher(size_t queue_capacity)
    :queue_(queue_capacity)
    ,worker_(*this)
    ,async_config_(std::make_shared<const LogAsyncConfig>(LogAsyncConfig{}))
{

}

LogAsyncDispatcher::LogAsyncDispatcher(LogAsyncConfig async_config)
    :queue_(async_config.queue_capacity)
    ,worker_(*this)
    ,async_config_(std::make_shared<const LogAsyncConfig>(std::move(async_config)))
{

}

void LogAsyncDispatcher::start() 
{
    accepting_.store(true, std::memory_order_release);
    worker_.start();
}



bool LogAsyncDispatcher::shutdown()
{
    bool expected = true;
    if(!accepting_.compare_exchange_strong(expected, false,std::memory_order_acq_rel))
    {
        return worker_.drain(config()->stop_drain_timeout_ms);
    }

    worker_.stop();
    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();
    
    // 等待写入线程收尾
    return worker_.drain(config()->stop_drain_timeout_ms);
}

LogSubmitResult LogAsyncDispatcher::submit(std::shared_ptr<LogAttr> attr) noexcept
{
    if (!attr || !attr->isSealed() || !accepting_.load(std::memory_order_acquire))
    {
        return {LogSubmitStatus::kStopped, 0};
    }
    const auto level = attr->getLevel();
    const auto bytes = attr->getContent().size();


    if(tryEnqueue(attr))
    {
        return {LogSubmitStatus::kQueued, bytes};
    }

    auto timeout_on_level = WaitForLevel(level);

    if(timeout_on_level.count() > 0)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout_on_level;
        for(;;)
        {
            if(!accepting_.load(std::memory_order_acquire))
            {
                return {LogSubmitStatus::kStopped, bytes};
            }

            if(tryEnqueue(attr))
            {
                return {LogSubmitStatus::kQueued, bytes};
            }

            // TODO 这里生产时监测字节数 可能造成大日志丢弃
            std::unique_lock<std::mutex> lock(mtx_);
            const bool ready = not_full_cv_.wait_until(lock, deadline, [this, bytes](){
                return !accepting_.load(std::memory_order_acquire)
                    || 
                    (queued_bytes_.load(std::memory_order_acquire) + bytes <= config()->max_queue_bytes && queueSize() < capacity());
            });
            lock.unlock(); // 一定要解锁
            if(!ready)
            {
                return drop(std::move(attr));
            }
        }

    }

    return drop(attr);
}


bool LogAsyncDispatcher::tryPop(std::shared_ptr<LogAttr>& attr) noexcept
{
    if(!queue_.tryPop(attr))
    {
        return false;
    }

    releaseBytes(attr->getContent().size());
    not_full_cv_.notify_one();
    
    return true;
}

bool LogAsyncDispatcher::waitFor(int64_t timeout_ms)
{
    std::unique_lock<std::mutex> lock(mtx_);
    return not_empty_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this](){
        return !accepting_.load(std::memory_order_acquire)
        || !queueEmpty();
    });
}

void LogAsyncDispatcher::wait()
{
    std::unique_lock<std::mutex> lock(mtx_);
    not_empty_cv_.wait(lock, [this](){
        return !accepting_.load(std::memory_order_acquire)
            || !queueEmpty();
    });
}

void LogAsyncDispatcher::setConfig(LogAsyncConfig config)
{
    setConfig(std::make_shared<const LogAsyncConfig>(std::move(config)));
}

void LogAsyncDispatcher::setConfig(std::shared_ptr<const LogAsyncConfig> config)
{
    std::atomic_exchange_explicit(&async_config_, config, std::memory_order_release);
}

const std::shared_ptr<const LogAsyncConfig> LogAsyncDispatcher::config() const noexcept
{
    return std::atomic_load_explicit(&async_config_, std::memory_order_acquire);
}

inline bool LogAsyncDispatcher::tryReserveBytes(size_t bytes) noexcept
{
    // 队列中字节数
    auto current = queued_bytes_.load(std::memory_order_relaxed);
    while (current + bytes <= config()->max_queue_bytes) 
    {
        if (queued_bytes_.compare_exchange_weak(current, current + bytes, std::memory_order_acq_rel)) 
        {
            return true;
        }
    }
    return false;
}

inline void LogAsyncDispatcher::releaseBytes(size_t bytes) noexcept
{
    queued_bytes_.fetch_sub(bytes, std::memory_order_release);
}


bool LogAsyncDispatcher::tryEnqueue(std::shared_ptr<LogAttr> attr) noexcept
{
    // 是否已经拒绝接收日志
    if(!accepting_.load(std::memory_order_acquire))
    {
        return false;
    }

    const auto bytes = attr->getContent().size();

    if(!tryReserveBytes(bytes))
    {
        return false;
    }

    // 正在入队发现队列满
    if(!queue_.tryPush(std::move(attr))) 
    {
        releaseBytes(bytes);
        return false;
    }
    not_empty_cv_.notify_one();

    return true;
}

LogSubmitResult LogAsyncDispatcher::drop(LogAttr::Ptr attr) noexcept
{
    if(!attr) 
    {
        return {LogSubmitStatus::kDropped, 0};
    }

    const auto level = attr->getLevel();
    const auto bytes = attr->getContent().size();


    stats_.dropped_records.fetch_add(1, std::memory_order_relaxed);
    stats_.dropped_bytes.fetch_add(bytes, std::memory_order_relaxed);

    switch(level) 
    {
        case LogLevel::DEBUG:
            stats_.dropped_debug.fetch_add(1, std::memory_order_relaxed);
            break;

        case LogLevel::INFO:
            stats_.dropped_info.fetch_add(1, std::memory_order_relaxed);
            break;

        case LogLevel::WARN:
            stats_.dropped_warn.fetch_add(1, std::memory_order_relaxed);
            break;

        case LogLevel::ERROR:
            stats_.dropped_error.fetch_add(1, std::memory_order_relaxed);
            break;

        case LogLevel::FATAL:
            stats_.dropped_fatal.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }

    return {LogSubmitStatus::kDropped, bytes};
}



std::chrono::milliseconds LogAsyncDispatcher::WaitForLevel(LogLevel::Level level) noexcept 
{
    switch (level)
    {
        case LogLevel::DEBUG:
        case LogLevel::INFO:
            return std::chrono::milliseconds::zero();

        case LogLevel::WARN:
            return std::chrono::milliseconds(kWarnLevelWaitMs);

        case LogLevel::ERROR:
        case LogLevel::FATAL:
            return std::chrono::milliseconds(kErrorLevelWaitMs);
        default:
            return std::chrono::milliseconds::zero();
    }

}




}
