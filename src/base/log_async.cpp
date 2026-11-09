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
#include "base/log_config.h"
#include "base/log.h"
#include "base/log_file_sink.h"
#include "base/time_stamp.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <system_error>

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <pthread.h>
#include <stdexcept>
#include <utility>

namespace kit_muduo {

LogAsyncWakeup::LogAsyncWakeup()
{
    fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "log eventfd");
}

LogAsyncWakeup::~LogAsyncWakeup()
{
    if (fd_ >= 0) ::close(fd_);
}

void LogAsyncWakeup::notify() const noexcept
{
    const uint64_t value = 1;
    ssize_t result;
    do { result = ::write(fd_, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    if (result == sizeof(value) || (result < 0 && errno == EAGAIN)) return;
    // 受控生命周期内此错误意味着事件通道已损坏，不能伪称日志仍能正常排空。
    LOG_INNER_ERROR("log eventfd write failed: %d\n", errno);
    std::terminate();
}

LogAsyncWorker::LogAsyncWorker(LogAsyncDispatcher& dispatcher, LogFileSinkRegister& sinks)
    :dispatcher_(dispatcher)
    ,file_sinks_(sinks)
{

}

LogAsyncWorker::~LogAsyncWorker()
{
    stop();
    join();
    closeEvents();
}


void LogAsyncWorker::start()
{
    if (stopping_.load(std::memory_order_acquire))
    {
        throw std::logic_error("log async worker cannot restart");
    }
    if (started_)
    {
        return;
    }
    drained_.store(false, std::memory_order_release);
    try {
        wakeup_ = std::make_shared<LogAsyncWakeup>();
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0)
        {
            throw std::system_error(errno, std::generic_category(), "log epoll");
        }
        timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timer_fd_ < 0)
        {
            throw std::system_error(errno, std::generic_category(), "log timerfd");
        }
        for (int fd : {wakeup_->fd(), timer_fd_})
        {
            epoll_event event{};
            event.events = EPOLLIN;
            event.data.fd = fd;
            if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) != 0)
            {
                throw std::system_error(errno, std::generic_category(), "log epoll add");
            }
        }

        file_sinks_.setFlushWakeup(wakeup_);
        
        thread_ = std::thread([this] {
            const int error = ::pthread_setname_np(::pthread_self(), "log-async");
            if (error != 0)
            {
                LOG_INNER_WARN("log-asy-worker thread name failed: %d\n", error);
            }
            workLoop();
        });
    } catch (...) {
        closeEvents();
        stopping_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(drain_mtx_);
        drained_.store(true, std::memory_order_release);
        drain_cv_.notify_all();
        throw;
    }
    started_ = true;
}

void LogAsyncWorker::stop() noexcept
{
    stopping_.store(true, std::memory_order_release);
    notify();
}

bool LogAsyncWorker::drain(int64_t timeout_ms) noexcept
{
    std::unique_lock<std::mutex> lock(drain_mtx_);
    return drain_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this](){ 
        return drained_.load(std::memory_order_acquire);
    });
}

void LogAsyncWorker::join()
{
    if(thread_.joinable())
    {
        thread_.join();
    }
}

void LogAsyncWorker::closeEvents() noexcept
{
    file_sinks_.clearFlushWakeup(wakeup_);

    if (timer_fd_ >= 0) ::close(std::exchange(timer_fd_, -1));
    if (epoll_fd_ >= 0) ::close(std::exchange(epoll_fd_, -1));
    wakeup_.reset();
}

void LogAsyncWorker::workLoop() noexcept
{
    try
    {
        for (;;)
        {
            std::optional<int64_t> deadline;
            if(!stopping_.load(std::memory_order_acquire))
            {
                file_sinks_.flushDue(TimeStamp::MonotonicNowMs());
                deadline = file_sinks_.nextFlushTime();
            }
            itimerspec timer{}; // 如果没被赋值 0传入会取消定时器
            if (deadline)
            {
                const auto value = std::max<int64_t>(1, *deadline);
                timer.it_value.tv_sec = value / 1000;
                timer.it_value.tv_nsec = (value % 1000) * 1000000;
            }
            if (::timerfd_settime(timer_fd_, TFD_TIMER_ABSTIME, &timer, nullptr) != 0)
            {
                throw std::system_error(errno, std::generic_category(), "log arm timer");
            }

            LogAttr::Ptr attr;
            for (size_t count = 0; count < kLogAsyncHandleLimits && dispatcher_.tryPop(attr); ++count)
            {
                handle(std::move(attr));

                if (deadline && TimeStamp::MonotonicNowMs() >= *deadline)
                {
                    break;
                }
            }
            // 收到停止信号 且 队列中没有日志数据就退出
            if (stopping_.load(std::memory_order_acquire) && dispatcher_.queueEmpty())
            {
                break;
            }

            // 读完通知计数后仍可能有剩余日志；只有队列为空epoll才阻塞。
            epoll_event events[2]{};
            int ready;
            do { 
                ready = ::epoll_wait(epoll_fd_, events, 2, dispatcher_.queueEmpty() ? -1 : 0);
            }while (ready < 0 && errno == EINTR);

            if (ready < 0)
            {
                throw std::system_error(errno, std::generic_category(), "log epoll wait");
            }
            for (int i = 0; i < ready; ++i)
            {
                uint64_t count;
                ssize_t result;
                do { 
                    result = ::read(events[i].data.fd, &count, sizeof(count));
                }while (result < 0 && errno == EINTR);
                if (result != sizeof(count) && !(result < 0 && errno == EAGAIN))
                {
                    throw std::system_error(errno, std::generic_category(), "log event read");
                }
            }
        }
    } catch (const std::exception& error) {
        LOG_INNER_ERROR("log event loop failed: %s\n", error.what());
        std::terminate(); // 不把事件通道失效报告成已排空。
    }

    {
        std::lock_guard<std::mutex> lock(drain_mtx_);
        drained_.store(true, std::memory_order_release);
    }
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

LogAsyncDispatcher::LogAsyncDispatcher(LogFileSinkRegister& sinks, LogAsyncConfig async_config)
    :queue_(async_config.queue_capacity)
    ,worker_(*this, sinks)
    ,async_config_(std::make_shared<const LogAsyncConfig>(std::move(async_config)))
{

}

LogAsyncDispatcher::~LogAsyncDispatcher()
{
    // 必须在成员析构前收拢 worker，不能等其成员析构时再访问已销毁的 CV。
    stopAndWake();
    worker_.join();
}

void LogAsyncDispatcher::start()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (closed_)
    {
        throw std::logic_error("log async dispatcher cannot restart");
    }
    if (accepting_.load(std::memory_order_acquire)) return;
    try {
        worker_.start();
    } catch (...) {
        closed_ = true;
        throw;
    }
    accepting_.store(true, std::memory_order_release);
}

void LogAsyncDispatcher::stopAndWake() noexcept
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        accepting_.store(false, std::memory_order_release);
        closed_ = true;
        worker_.stop();
    }

    not_full_cv_.notify_all();
}

bool LogAsyncDispatcher::shutdown()
{
    stopAndWake();
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

            // 即使容量谓词反复成立、入队又输给其他生产者，也不能无限重试。
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return drop(std::move(attr));
            }

            if(tryEnqueue(attr))
            {
                return {LogSubmitStatus::kQueued, bytes};
            }

            if(!waitForCapacity(bytes, deadline))
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
    
    return true;
}

void LogAsyncDispatcher::setConfig(LogAsyncConfig config)
{
    setConfig(std::make_shared<const LogAsyncConfig>(std::move(config)));
}

void LogAsyncDispatcher::setConfig(std::shared_ptr<const LogAsyncConfig> config)
{
    std::atomic_exchange_explicit(&async_config_, config, std::memory_order_release);
    notifyCapacityAvailable();
}

const std::shared_ptr<const LogAsyncConfig> LogAsyncDispatcher::config() const noexcept
{
    return std::atomic_load_explicit(&async_config_, std::memory_order_acquire);
}

void LogAsyncDispatcher::join()
{
    worker_.join();
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

void LogAsyncDispatcher::releaseBytes(size_t bytes) noexcept
{
    queued_bytes_.fetch_sub(bytes, std::memory_order_release);
    notifyCapacityAvailable();
}

bool LogAsyncDispatcher::waitForCapacity(
    size_t bytes, std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock<std::mutex> lock(mtx_);
    // 必须先登记再检查；若释放方先完成 RMW，这里 acquire 到此前释放的容量。
    waiting_producers_.fetch_add(1, std::memory_order_acq_rel);
    const bool ready = not_full_cv_.wait_until(lock, deadline, [this, bytes]() {
        return !accepting_.load(std::memory_order_acquire)
            || (queued_bytes_.load(std::memory_order_acquire) + bytes
                    <= config()->max_queue_bytes
                && queueSize() < capacity());
    });
    waiting_producers_.fetch_sub(1, std::memory_order_acq_rel);
    return ready;
}

void LogAsyncDispatcher::notifyCapacityAvailable() noexcept
{
    // 有意使用 RMW 而非 load：发布此前的容量释放，与等待者登记建立顺序。
    if (waiting_producers_.fetch_add(0, std::memory_order_acq_rel) == 0)
    {
        return;
    }
    {
        // 若等待者正在持锁检查，等其原子地解锁并进入等待后再通知。
        std::lock_guard<std::mutex> lock(mtx_);
    }
    // 每条日志所需预算不同，不能只随机唤醒一个当前仍放不下的等待者。
    not_full_cv_.notify_all();
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
    worker_.notify();

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
