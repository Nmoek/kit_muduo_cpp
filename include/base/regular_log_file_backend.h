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

private:
    LogBackendResult openInner(const std::string& normalize_path);

private:
    /// @brief 常规文件fd句柄
    int32_t fd_{-1};
};


}
#endif //__KIT_REGULAR_LOG_FILE_BACKEND_H__