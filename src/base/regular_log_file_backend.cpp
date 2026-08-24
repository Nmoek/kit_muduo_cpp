/**
 * @file regular_log_file_backend.cpp
 * @brief 常规写文件 日志持久化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 04:04:16
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/regular_log_file_backend.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
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


} // kit_muduo