/**
 * @file regular_log_file_backend.h
 * @brief 常规写文件 日志持久化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 03:47:57
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_REGULAR_LOG_FILE_BACKEND_H__
#define __KIT_REGULAR_LOG_FILE_BACKEND_H__

#include "base/log_file_backend.h"

#include <future>
#include <memory>

namespace kit_muduo {


class RegularLogFileBackend final : public LogFileBackend
{
public:
    ~RegularLogFileBackend();

    LogBackendResult open(const std::string& normalize_path) override;

    void close() noexcept override;

    LogBackendResult append(std::string_view log_data) override;

    LogBackendResult flush() noexcept override;

    LogBackendResult durableFlush() noexcept override;

    LogBackendResult rotate(const LogFileRotateRequest& request, int64_t timeout_ms = -1) noexcept override;

    bool isOpen() const noexcept override;

    uint64_t openSize() const noexcept override;

    uint64_t generation() const noexcept override;
    
    uint64_t lastSequence() const noexcept override;
    
private:
    LogBackendResult openInner(const std::string& normalize_path);

    CompressionResult compressArchiveLogFile(const std::string& archive_log_path, const std::string& archive_zst_path);

private:
    /// @brief 常规文件fd句柄
    int32_t fd_{-1};
    /// @brief 当前对象是否处于打开状态
    bool is_open_{false};
    /// @brief 当前打开文件时的大小
    uint64_t open_size_{0};

    struct LogFileCompressResult
    {
        std::string archive_log_path;
        std::string archive_compression_path;
        std::future<CompressionResult> compress_task_f;
        std::atomic_bool compress_completed{false};
    };
    std::shared_ptr<LogFileCompressResult> compress_result_{nullptr};
};


}
#endif //__KIT_REGULAR_LOG_FILE_BACKEND_H__