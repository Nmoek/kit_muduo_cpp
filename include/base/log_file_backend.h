/**
 * @file log_file_backend.h
 * @brief 日志持久化抽象
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 03:38:51
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_FILE_BACKEND_H__
#define __KIT_LOG_FILE_BACKEND_H__

#include <memory>
#include <string>

namespace  kit_muduo {

enum class LogBackendStatus
{
    kOk,
    kOpenFailed,
    kWriteFailed,
    kFlushFailed,
    kSyncFailed,
};

struct LogBackendResult
{
    LogBackendStatus status{LogBackendStatus::kOk};
    size_t requested_bytes{0};
    size_t written_bytes{0};
    std::string message;

    bool ok() const noexcept
    {
        return status == LogBackendStatus::kOk;
    }

    static LogBackendResult Ok()
    {
        return {};
    }

    static LogBackendResult Failure(LogBackendStatus status, std::string message)
    {
        LogBackendResult result;
        result.status = status;
        result.message = std::move(message);
        return result;
    }
};

/**
 * @brief 持久化操作工具类 本质上不能有任何业务上下文
 */
class LogFileBackend
{
public:
    virtual ~LogFileBackend() = default;

    /**
     * @brief 打开文件句柄
     * @param normalize_path 需要归一化路径
     * @return LogBackendResult 
     */
    virtual LogBackendResult open(const std::string& normalize_path) = 0;
    

    virtual void close() noexcept = 0;

    virtual LogBackendResult append(std::string_view bytes) = 0;

    /**
     * @brief 普通 flush 只要求把 backend 自身的用户态缓冲提交到内核。
     * @return LogBackendResult 
     */
    virtual LogBackendResult flush() noexcept = 0;

    /**
     * @brief 求调用 fdatasync 或fsyanc 等价能力。
     * @return LogBackendResult 
     */
    virtual LogBackendResult durableFlush() noexcept = 0;

    bool isOpen() const noexcept { return is_open_; }

    uint64_t openSize() const noexcept { return open_size_; };

    /**
     * @brief 检查路径并创建不存在的路径
     * @param normalize_path 
     * @return true 
     * @return false 
     */
    bool chechkAndCreateLogPath(const std::string &normalize_path);

public:
    static std::unique_ptr<LogFileBackend> NewDefaultBackend();

protected:
    bool is_open_{false};
    /// @brief 当前打开文件时的大小
    uint64_t open_size_{0};
};

}


#endif //__KIT_LOG_FILE_BACKEND_H__