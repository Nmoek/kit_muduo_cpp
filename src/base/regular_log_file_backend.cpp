/**
 * @file regular_log_file_backend.cpp
 * @brief 常规写文件 日志持久化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 04:04:16
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/regular_log_file_backend.h"
#include "base/log_full_recompress.h"
#include "base/log_inner.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace kit_muduo {

namespace {


inline uint64_t WriteAll(int32_t fd, const char* data, uint64_t len)
{
    uint64_t total = len;
    uint64_t has_written = 0;
    int32_t retry = 1;
    while(total > has_written)
    {
        const auto res = ::write(fd, data + has_written, len - has_written);
        if(res < 0)
        {
            if(EINTR == errno)
            {
                continue;
            }
            return has_written;
        }
        else if(0 == res)
        {
            --retry;
            if(retry <= 0)
            {
                break;
            }
        }
        has_written += res;
    }
    return has_written;
}


} // namespace

RegularLogFileBackend::RegularLogFileBackend(FullRecompressScheduler& scheduler)
    :scheduler_(scheduler)
{

}

RegularLogFileBackend::~RegularLogFileBackend()
{
    close();
}

LogBackendResult RegularLogFileBackend::open(const std::string& normalize_path)
{
    return openInner(normalize_path);
}

void RegularLogFileBackend::close() noexcept
{
    if(!is_open_)
    {
        return;
    }
    if(fd_ >= 0)
    {
        ::close(fd_);
    }
    fd_ = -1;
    is_open_ = false;
    open_size_ = 0;
}

LogBackendResult RegularLogFileBackend::append(std::string_view log_data)
{
    if(!is_open_)
    {
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "Regular fd closed");
    }

    uint64_t total = log_data.size();
    uint64_t written = WriteAll(fd_, log_data.data(), total);
    if(total != written)
    {
        auto result = LogBackendResult::Failure(LogBackendStatus::kWriteFailed, strerror(errno));
        result.requested_bytes = total;
        result.written_bytes = written;
        return result;
    }
    
    auto result = LogBackendResult::Ok();
    result.requested_bytes = total;
    result.written_bytes = written;

    return result;
}

LogBackendResult RegularLogFileBackend::flush() noexcept
{
    if(!is_open_ || fd_ < 0)
    {
        return LogBackendResult::Failure(LogBackendStatus::kFlushFailed, "log file not open");
    }

    // 直接 ::write 不需要刷新
    return LogBackendResult::Ok();
}

LogBackendResult RegularLogFileBackend::durableFlush() noexcept
{
    if(!is_open_ || fd_ < 0)
    {
        return LogBackendResult::Failure(LogBackendStatus::kSyncFailed, "log file not open");
    }
    
    // 仅刷新写入数据 元数据不刷新
    if(::fdatasync(fd_) < 0)
    {
        return LogBackendResult::Failure(LogBackendStatus::kSyncFailed, strerror(errno));
    }

    return LogBackendResult::Ok();
}

LogBackendResult RegularLogFileBackend::rotate(const LogFileRotateRequest& request, int64_t timeout_ms) noexcept
{
    auto result = flush();
    if(!result.ok())
    {
        return result;
    }
    close();

    std::error_code error;
    std::filesystem::rename(request.active_path, request.archive_log_path, error);
    if(error)
    {
        // 重新打开旧的active文件
        (void)open(request.active_path);
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "rotate rename error: " + error.message()); 
    }
    ++generation_; // rename 已发生；reopen 失败重试不得再次 rename。
    
    if(request.compression_enabled) 
    {
        FullRecompressTask task;
        task.task_id = FullRecompressTask::NextTaskId();
        task.generation = request.generation;
        task.archive_log_path = request.archive_log_path;
        task.archive_compression_path = request.archive_log_path + "." + scheduler_.compressSuffix();
        if(!submitFullRecompress(std::move(task)))
        {
            LOG_INNER_ERROR("log full recompress submit error!\n");
        }
    }
    
    // 即使打开失败，归档已经提交，不能再次 rename 同一个 active。
    return open(request.active_path);;
}

bool RegularLogFileBackend::isOpen() const noexcept
{
    return is_open_;
}

uint64_t RegularLogFileBackend::openSize() const noexcept
{
    return open_size_;
}

uint64_t RegularLogFileBackend::generation() const noexcept
{
    return generation_;
}
    
uint64_t RegularLogFileBackend::lastSequence() const noexcept
{
    // 注意 普通文件写 没有分批写入概念
    return 0;
}

bool RegularLogFileBackend::recompress(std::string archive_path) noexcept
{
    FullRecompressTask task;
    task.task_id = FullRecompressTask::NextTaskId();
    task.generation = 0;
    task.archive_log_path = std::move(archive_path);
    task.archive_compression_path = task.archive_log_path + "." + scheduler_.compressSuffix();
    return submitFullRecompress(std::move(task));
}

LogBackendResult RegularLogFileBackend::openInner(const std::string& normalize_path)
{
    if(is_open_)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "log file has open");
    }
    
    if(!chechkAndCreateLogPath(normalize_path))
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, "log file path invalid");
    }

    int32_t fd = ::open(normalize_path.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC | O_APPEND, 0644);
    if(fd < 0)
    {
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, strerror(errno));
    }
    // 获取文件状态
    struct stat file_stat = {0};
    if(fstat(fd, &file_stat) < 0) 
    {
        ::close(fd);
        return LogBackendResult::Failure(LogBackendStatus::kOpenFailed, strerror(errno));
    }
    // 完全成功后再赋值
    fd_ = fd;
    // 文件大小
    open_size_ = file_stat.st_size;
    // 打开状态
    is_open_ = true;
    return LogBackendResult::Ok();
}

bool RegularLogFileBackend::submitFullRecompress(FullRecompressTask task) noexcept
{
    try {

        if(!scheduler_.submit(std::move(task)))
        {
            compression_degraded_ = true;
            return false;
        }
    }catch (const std::exception &e) {
        LOG_INNER_EXCPTION("regular log full recompress exception: %s\n", e.what());
        compression_degraded_ = true;
        return false;

    }catch (...) {
        LOG_INNER_EXCPTION("regular log full recompress unknown exception\n");
        compression_degraded_ = true;
        return false;
    }
    return true;
}


} // kit_muduo