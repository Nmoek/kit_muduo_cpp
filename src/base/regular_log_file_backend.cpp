/**
 * @file regular_log_file_backend.cpp
 * @brief 常规写文件 日志持久化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 04:04:16
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/regular_log_file_backend.h"
#include "base/log_inner.h"
#include "base/thread_group.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <memory>
#include <sstream>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <fstream>

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

inline uint64_t ReadAll(int32_t fd, char* data, uint64_t len)
{
    uint64_t total = len;
    uint64_t has_read = 0;
    int32_t retry = 1;
    while(total > has_read)
    {
        const auto res = ::read(fd, data + has_read, len - has_read);
        if(res < 0)
        {
            if(EINTR == errno)
            {
                continue;
            }
            return has_read;
        }
        else if(0 == res)
        {
            --retry;
            if(retry <= 0)
            {
                break;
            }
        }
        has_read += res;
    }
    return has_read;
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
        return LogBackendResult::Failure(LogBackendStatus::kWriteFailed, "rotate rename error: " + error.message()); 
    }

    /// 普通写文件采用全量压缩
    if(request.compression_enabled)
    {
        // 当前策略这一轮轮转检查上一次压缩结果
        if(compress_result_)
        {
            if(compress_result_->compress_completed)
            {
                (void)compress_result_->compress_task_f.get();
            }
            else
            {
                LOG_INNER_WARN("pre compress task not finish!\n");
            }
        }
        compress_result_ = std::make_shared<LogFileCompressResult>();
        compress_result_->archive_log_path = request.archive_log_path;
        compress_result_->archive_compression_path = request.archive_log_path;
        compress_result_->archive_compression_path  += ".";
        compress_result_->archive_compression_path += compress_codec_->suffix();
        compress_result_->compress_completed = false;
        compress_result_->compress_task_f = std::async(std::launch::async, [this](){

            LOG_INNER_DEBUG("compress 11111111111111\n");
            const auto result = compressArchiveLogFile(compress_result_->archive_log_path, compress_result_->archive_compression_path);
            compress_result_->compress_completed = true;
            if(!result.ok())
            {
                LOG_INNER_ERROR("compress task [%s] error: %s, %lu/%lu \n", compress_result_->archive_compression_path.c_str(), result.message.c_str(), result.input_bytes, result.output_bytes);
            }
            LOG_INNER_DEBUG("compress 222222222222222222\n");

            return result;
        });

    }

    result = open(request.active_path);
    if(!result.ok())
    {
        return result;
    }
    ++generation_;
    return LogBackendResult::Ok();
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

CompressionResult RegularLogFileBackend::compressArchiveLogFile(const std::string& archive_log_path, const std::string& archive_zst_path)
{
    if(!compress_codec_)
    {
        return CompressionResult::Failure(
            CompressionResultStatus::kInvalidArgument,
            "compression codec is null");
    }

    const int input_fd = ::open(archive_log_path.c_str(), O_RDONLY | O_CLOEXEC);
    if(input_fd < 0)
    {
        return CompressionResult::Failure(
            CompressionResultStatus::kInternalError,
            "open archive log failed: " + std::string(std::strerror(errno)));
    }

    const std::string archive_zst_tmp_path = archive_zst_path + ".tmp";

    const int output_fd = ::open(
        archive_zst_tmp_path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0644);

    if (output_fd < 0)
    {
        const std::string message =
            "open compression temporary file failed: "
            + std::string(std::strerror(errno));

        ::close(input_fd);

        return CompressionResult::Failure(
            CompressionResultStatus::kInternalError,
            message);
    }

    auto compressor = compress_codec_->createCompressor();
    if (!compressor)
    {
        ::close(input_fd);
        ::close(output_fd);
        ::unlink(archive_zst_tmp_path.c_str());

        return CompressionResult::Failure(
            CompressionResultStatus::kInitializationFailed,
            "create stream compressor failed");
    }

    std::vector<uint8_t> input_buffer(128 * 1024);

    auto output_callback = [output_fd](Span<const uint8_t> bytes) {
        const auto written = WriteAll(output_fd,
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());

        if (written != bytes.size())
        {
            return CompressionCallbackResult::Failure("write compressed archive failed: "+ std::string(std::strerror(errno)));
        }

        return CompressionCallbackResult::Ok();
    };

    uint64_t consumed_bytes = 0;
    uint64_t produced_bytes = 0;

    for (;;)
    {
        const uint64_t read_size = ReadAll(input_fd,
            reinterpret_cast<char*>(input_buffer.data()),
            input_buffer.size());
        if (read_size < 0)
        {
            const std::string message ="read archive log failed: " + std::string(std::strerror(errno));

            ::close(input_fd);
            ::close(output_fd);
            ::unlink(archive_zst_tmp_path.c_str());
            return CompressionResult::Failure(
                CompressionResultStatus::kInternalError,
                message,
                consumed_bytes,
                produced_bytes);
        }
        if(0 == read_size)
        {
            break;
        }

        const auto result = compressor->write({
            input_buffer.data(),
            static_cast<size_t>(read_size)
        },
        output_callback);

        consumed_bytes += result.input_bytes;
        produced_bytes += result.output_bytes;

        if (!result.ok())
        {
            ::close(input_fd);
            ::close(output_fd);
            ::unlink(archive_zst_tmp_path.c_str());

            return CompressionResult::Failure(result.status,
                result.message,
                consumed_bytes,
                produced_bytes);
        }
    }

    const auto finish_result = compressor->finish(output_callback);

    consumed_bytes += finish_result.input_bytes;
    produced_bytes += finish_result.output_bytes;

    if (!finish_result.ok())
    {
        ::close(input_fd);
        ::close(output_fd);
        ::unlink(archive_zst_tmp_path.c_str());

        return CompressionResult::Failure(finish_result.status,
            finish_result.message,
            consumed_bytes,
            produced_bytes);
    }

    ::close(input_fd);

    if (::fdatasync(output_fd) < 0)
    {
        const std::string message =
            "sync compressed archive failed: "
            + std::string(std::strerror(errno));

        ::close(output_fd);
        ::unlink(archive_zst_tmp_path.c_str());

        return CompressionResult::Failure(CompressionResultStatus::kInternalError,
            message,
            consumed_bytes,
            produced_bytes);
    }

    if (::close(output_fd) < 0)
    {
        ::unlink(archive_zst_tmp_path.c_str());

        return CompressionResult::Failure(
            CompressionResultStatus::kInternalError,
                "close compressed archive failed: " + std::string(std::strerror(errno)),
                consumed_bytes,
                produced_bytes);
    }

    std::error_code rename_error;
    std::filesystem::rename(archive_zst_tmp_path,archive_zst_path, rename_error);
    if (rename_error)
    {
        ::unlink(archive_zst_tmp_path.c_str());

        return CompressionResult::Failure(
            CompressionResultStatus::kInternalError,
                "publish compressed archive failed: " + rename_error.message(),
                consumed_bytes,
                produced_bytes);
    }

    // 只有 .zst 已经成功发布后，才删除原始 .log。
    std::error_code remove_error;
    std::filesystem::remove(archive_log_path, remove_error);

    if (remove_error)
    {
        // .zst 已经成功发布，删除原始日志失败不应该撤销已发布的压缩归档。
        return CompressionResult::Failure(
            CompressionResultStatus::kInternalError,
            "remove source archive failed: "
                + remove_error.message(),
            consumed_bytes,
            produced_bytes);
    }

    return CompressionResult::Ok(consumed_bytes, produced_bytes);
}

} // kit_muduo