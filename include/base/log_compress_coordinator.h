/**
 * @file log_compress_coordinator.h
 * @brief 日志压缩协调器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 15:24:16
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_LOG_COMPRESS_COORDINATOR_H__
#define __KIT_LOG_COMPRESS_COORDINATOR_H__


#include "base/noncopyable.h"
#include "base/log_full_recompress.h"
#include "base/log_mmap_batch.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>


namespace kit_muduo {


class LogCompressCoordinator: Noncopyable
{
public:
    // 指针允许 stdout-only / regular-only 部署不创建 mmap 池。
    LogCompressCoordinator(std::unique_ptr<MmapBatchDispatcher> mmap, std::unique_ptr<FullRecompressWorker> full);
    ~LogCompressCoordinator() = default;
    
    void start(bool realtime_enabled = true);
 
    void stopMmapAccepting() noexcept
    {
        if (mmap_)
        {
            mmap_->stopAccepting();
        }
    }
    bool drainMmap(uint64_t timeout_ms) noexcept
    {
        return !mmap_ || mmap_->drain(timeout_ms);
    }
    void stopFullAccepting() noexcept
    {
        if (full_) full_->stopAccepting();
    }
    bool drainFull(uint64_t timeout_ms) noexcept
    {
        return !full_ || full_->drain(timeout_ms);
    }

    void wait() noexcept
    {
        // 调用前必须完成 backend 的 generation 发布/补偿交接。
        if (mmap_) mmap_->wait();
        if (full_) full_->wait();
    }
    MmapBatchScheduler& mmapScheduler()
    {
        if (!mmap_)
        {
            throw std::logic_error("mmap disabled");
        }
        return *mmap_;
    }
    FullRecompressScheduler& fullScheduler()
    {
        if (!full_)
        {
            throw std::logic_error("full compression disabled");
        }
        return *full_;
    }
private:
    std::unique_ptr<MmapBatchDispatcher> mmap_{nullptr};
    std::unique_ptr<FullRecompressWorker> full_{nullptr};
    bool started_{false};
};





} // namespace kit_muduo
#endif // __KIT_LOG_COMPRESS_COORDINATOR_H__