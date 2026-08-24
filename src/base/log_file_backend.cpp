/**
 * @file log_file_backend.cpp
 * @brief 日志持久化抽象
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-22 15:36:43
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_file_backend.h"
#include "base/log_inner.h"
#include "base/regular_log_file_backend.h"

#include <filesystem>

namespace kit_muduo {

bool LogFileBackend::chechkAndCreateLogPath(const std::string &normalize_path)
{
    const std::filesystem::path path{normalize_path};
    std::error_code error;

    const bool exists = std::filesystem::exists(path, error);
    if(error)
    {
        LOG_INNER_ERROR("cannot inspect log file [%s]: %s\n", normalize_path.c_str(), error.message().c_str());
        return false;
    }
    if(exists)
    {
        const bool regular = std::filesystem::is_regular_file(path, error);

        if(error)
        {
            LOG_INNER_ERROR("cannot inspect log file [%s]: %s\n", normalize_path.c_str(), error.message().c_str());
            return false;
        }
        if(!regular)
        {
            LOG_INNER_ERROR("log file path must refer to a regular file: \n", normalize_path.c_str());
            return false;
        }

    }
    else
    {
        const std::filesystem::path parent = path.parent_path();
        std::filesystem::create_directories(parent, error);
        if(error)
        {
            LOG_INNER_ERROR("cannot create log directory [%s]: %s \n", parent.string().c_str(), error.message().c_str());
            return false;
        }
    }

    return true;
}


std::unique_ptr<LogFileBackend> LogFileBackend::NewDefaultBackend()
{
    // HACK 默认使用普通写文件 持久化
    return std::make_unique<RegularLogFileBackend>();
}


} // namespace kit_muduo