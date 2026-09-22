/**
 * @file log_full_recompress.h
 * @brief 日志全量压缩
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-20 15:55:44
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_FULL_RECOMPRESS_H__
#define __KIT_LOG_FULL_RECOMPRESS_H__


#include "base/compression.h"
#include "base/noncopyable.h"
#include "base/thread.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <queue>
#include <string>
#include <fcntl.h>
#include <system_error>
#include <unistd.h>
#include <unordered_set>

namespace kit_muduo {


struct FullRecompressTask
{
    uint64_t task_id{0};
    uint64_t generation{0};
    std::string archive_log_path;
    std::string archive_compression_path;

    static uint64_t NextTaskId() noexcept
    {
        static std::atomic_uint64_t next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }
};

/**
 * @brief 全量压缩调度
 */
class FullRecompressScheduler
{
public:
    virtual ~FullRecompressScheduler() = default;
    virtual bool submit(FullRecompressTask task) noexcept = 0;
    virtual std::string compressSuffix() const noexcept = 0;
};

class FullRecompressWorker final: Noncopyable, public FullRecompressScheduler
{
public:
    struct Options
    {
        /// @brief 全量压缩队列容量
        size_t queue_capacity{32};
        /// @brief 分块读归档文件缓冲大小
        size_t read_buffer_bytes{256 * 1024};
    };
    struct Result
    {
        CompressionResult compression_result;
        bool published{false};
        std::error_code cleanup_error;
    };

    explicit FullRecompressWorker(std::shared_ptr<CompressionCodec> codec);
    FullRecompressWorker(std::shared_ptr<CompressionCodec> codec, Options options);
    ~FullRecompressWorker() override;

    void start();

    bool submit(FullRecompressTask task) noexcept override;
    std::string compressSuffix() const noexcept override { return compress_codec_->suffix(); }

    void stopAccepting() noexcept;

    bool drain(uint64_t timeout_ms) noexcept;

    void wait() noexcept;

private:
    Result compressOne(const FullRecompressTask& task) noexcept;

    void workLoop() noexcept;

private:
    std::shared_ptr<CompressionCodec> compress_codec_;
    Options options_;
    Thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<FullRecompressTask> queue_;
    std::unordered_set<std::string> queued_records_;
    size_t outstanding_{0};
    bool started_{false};
    bool accepting_{false};
    bool closed_{false};
};

}
#endif //__KIT_LOG_FULL_RECOMPRESS_H__