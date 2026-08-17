/**
 * @file log_file_sink.cpp
 * @brief 日志文件管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-16 17:23:35
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "base/log_config.h"
#include "base/log_file_sink.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace kit_muduo {

namespace {

std::string NormalizeLogFilePath(const std::string& file_path)
{
    if(file_path.empty())
    {
        throw std::invalid_argument("log file path must not be empty");
    }

    std::error_code error;
    auto path = std::filesystem::absolute(
        std::filesystem::path{file_path}, error);
    if(error)
    {
        throw std::runtime_error(
            "cannot make log file path absolute: " + file_path
            + "; " + error.message());
    }

    path = std::filesystem::weakly_canonical(path, error);
    if(error)
    {
        throw std::runtime_error(
            "cannot normalize log file path: " + file_path
            + "; " + error.message());
    }

    return path.lexically_normal().string();
}

} // namespace

LogFileSink::LogFileSink(const std::string &normalize_path, 
    const std::shared_ptr<const LogFileConfig>& base_file_confg)
    :base_file_confg_(base_file_confg)
    ,normalize_path_(normalize_path)
{

}

LogFileSinkResult LogFileSink::append(const std::string& data)
{
    LogFileSinkResult result;
    result.requested_bytes = data.size();
    
    std::lock_guard<std::mutex> lock(mtx_);
    if(!ensureOpenUnlocked(nullptr))
    {
        reportErrorUnlocked("reopen");
        result.status = LogFileSinkStatus::kOpenFailed;
        return result;
    }

    ofs_.write(data.data(), data.size());
    if(!ofs_.good())
    {
        reportErrorUnlocked("write");
        ofs_.close();
        result.status = LogFileSinkStatus::kWriteFailed;
        return result;
    }

    bytes_since_flush_ += data.size();
    current_file_size_ += data.size();

    if(bytes_since_flush_ >= base_file_confg_->flush_threshold)
    {

        if(!flushUnlocked())
        {
            result.status = LogFileSinkStatus::kFlushFailed;
            return result;
        }
        result.flush_attempted = true;
        bytes_since_flush_ = 0;
    }

    return result;
    // TODO 轮转 压缩等处理都可能放在这
}


void LogFileSink::flush()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(!flushUnlocked())
    {
        return;
    }
    bytes_since_flush_ = 0;
}

bool LogFileSink::ensureOpen(std::string* error_message)
{
    std::lock_guard<std::mutex> lock(mtx_);
    return ensureOpenUnlocked(error_message);
}

bool LogFileSink::reopen(std::string* error_message)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(ofs_.is_open())
    {
        ofs_.close();
    }
    return openUnlocked(error_message);
}

bool LogFileSink::ensureOpenUnlocked(std::string* error_message)
{
    if(ofs_.is_open() && ofs_.good())
    {
        return true;
    }
    if(ofs_.is_open())
    {
        ofs_.close();
    }
    return openUnlocked(error_message);
}

bool LogFileSink::openUnlocked(std::string* error_message)
{
    if(normalize_path_.empty())
    {
        if(error_message)
        {
            *error_message = "file path empty";
        }
        return false;
    }
    ofs_.clear();
    errno = 0;
    ofs_.open(normalize_path_, std::ios::out | std::ios::app | std::ios::binary);
    if(!ofs_.is_open())
    {
        if(error_message)
        {
            *error_message ="cannot open file: " + normalize_path_
                + ", errno=" + std::to_string(errno)
                + ", message=" + std::strerror(errno);
        }
        return false;
    }

    bytes_since_flush_ = 0;

    ofs_.seekp(0, std::ios::end);
    const auto position = ofs_.tellp();
    if(position == std::streampos(-1) || !ofs_.good())
    {
        if(error_message)
        {
            *error_message ="log file size read error: " + normalize_path_;
        }
        ofs_.close();
        return false;
    }
    current_file_size_ = static_cast<uint64_t>(position);
    error_reported_ = false;

    return true;
}

bool LogFileSink::flushUnlocked()
{
    ofs_.flush();
    if(!ofs_.good())
    {
        reportErrorUnlocked("flush");
        return false;
    }
    return true;
}

void LogFileSink::reportErrorUnlocked(const char* operation)
{
    if(error_reported_)
    {
        return;
    }

    error_reported_ = true;
    std::cerr << "log file " << operation
              << " failed: " << normalize_path_ << '\n';
}

LogFileSinkRegister::LogFileSinkRegister()
    :file_config_(std::make_shared<const LogFileConfig>())
{

}


LogFileSink::Ptr LogFileSinkRegister::acquire(const std::string &file_path)
{
    const std::string normalize_path = NormalizeLogFilePath(file_path);

    std::lock_guard<std::mutex> rlock(mtx_);
    const auto it = log_file_sinks_.find(normalize_path);
    if(it != log_file_sinks_.end())
    {
        if(auto sink = it->second.lock())
        {
            return sink;
        }
    }
    auto sink = create(normalize_path);

    log_file_sinks_[normalize_path] = sink;
    return sink;
}

LogFileSink::Ptr LogFileSinkRegister::create(const std::string &normalize_path)
{
    const std::filesystem::path path{normalize_path};
    std::error_code error;

    const bool exists = std::filesystem::exists(path, error);
    if(error)
    {
        throw std::runtime_error(
            "cannot inspect log file: " + normalize_path
            + "; " + error.message());
    }
    if(exists)
    {
        const bool regular = std::filesystem::is_regular_file(path, error);

        if(error)
        {
            throw std::runtime_error(
                "cannot inspect log file: " + normalize_path
                + "; " + error.message());
        }
        if(!regular)
        {
            throw std::runtime_error(
                "log file path must refer to a regular file: "
                + normalize_path);
        }

    }
    else
    {
        const std::filesystem::path parent = path.parent_path();
        std::filesystem::create_directories(parent, error);
        if(error)
        {

            throw std::runtime_error(
                "cannot create log directory: " + parent.string()
                + "; " + error.message());
        }
    }

    auto file_config = std::atomic_load_explicit(
    &file_config_,
    std::memory_order_acquire);

    auto sink = std::make_shared<LogFileSink>(path, std::move(file_config));
    std::string open_error;
    if(!sink->reopen(&open_error))
    {
        throw std::runtime_error("cannot open log file: " + open_error);
    }
    return sink;
}


void LogFileSinkRegister::setFileConfig(LogFileConfig config) 
{ 
    auto config_ptr = std::make_shared<const LogFileConfig>(std::move(config));

    std::atomic_exchange_explicit(&file_config_, config_ptr, std::memory_order_release);
}


const std::shared_ptr<const LogFileConfig>& LogFileSinkRegister::fileConfig() const noexcept 
{ 
    return file_config_; 
}

}
