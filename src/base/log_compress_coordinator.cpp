/**
 * @file log_compress_coordinator.cpp
 * @brief 日志压缩协调器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 20:08:23
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_compress_coordinator.h"

namespace kit_muduo {


LogCompressCoordinator::LogCompressCoordinator(std::unique_ptr<MmapBatchDispatcher> mmap, std::unique_ptr<FullRecompressWorker> full)
    :mmap_(std::move(mmap))
    ,full_(std::move(full))
{
    
}

void LogCompressCoordinator::start(bool realtime_enabled)
{
    if(started_)
    {
        return;
    }

    try {
    
        if(full_)
        {
            full_->start();
        }
        
        if(mmap_)
        {
            mmap_->start(realtime_enabled);
        }
    } catch (...) {
        if (full_)
        {
            full_->wait();
        }
        throw;
    }
    started_ = true;
}


}