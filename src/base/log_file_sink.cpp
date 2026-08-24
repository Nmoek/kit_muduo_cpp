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
#include "base/log_inner.h"
#include "base/time_stamp.h"
#include "base/util.h"
#include "date/date.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <system_error>

namespace kit_muduo {

namespace {

/// @brief 用于只报告1次错误宏
#define LOG_FILE_SINK_REPORTED(OP, MSG) do{\
    if(!error_reported_) { \
        error_reported_ = true; \
        LOG_INNER_ERROR("log file sink %s failed [%s] %s\n", OP, normalize_path_.c_str(), MSG); \
}}while(0)


std::string MakeArchiveData(const std::string &formatter)
{
    const auto real_time_ms = TimeStamp::NowMs();
    const date::sys_time<std::chrono::seconds> utc{
        date::floor<std::chrono::seconds>(std::chrono::milliseconds{real_time_ms})
    };
    // 手动将0时区往后调8h 给日志打印使用
    const auto fixed_utc = utc + std::chrono::hours{8};
    std::ostringstream ss;
    ss << date::format(formatter, fixed_utc);
    return ss.str();
}

/**
 * @brief 正则表达式转义问题
 * @param value 
 * @return std::string 
 */
std::string EscapeRegexLiteral(std::string_view value)
{
    static constexpr std::string_view kRegexMetaChars = R"(\.^$|()[]{}*+?)";

    std::string result;
    result.reserve(value.size());

    for (const char ch : value)
    {
        if (kRegexMetaChars.find(ch) != std::string_view::npos)
        {
            result.push_back('\\');
        }

        result.push_back(ch);
    }

    return result;
}

} // namespace

LogFileSink::LogFileSink(LogFileSinkRegister *reg, const std::string &normalize_path)
    :reg_(reg)
    ,normalize_path_(normalize_path)
    ,backend_((LogFileBackend::NewDefaultBackend()))
{
    assert(reg_);
}

LogFileSinkResult LogFileSink::append(const std::string& data,
    LogLevel::Level level,
    bool truncated,
    size_t original_bytes)
{
    LogFileSinkResult result;
    result.requested_bytes = data.size();
    result.truncated = truncated;
    result.original_bytes = original_bytes;

    std::lock_guard<std::mutex> lock(mtx_);
    auto open_result = ensureOpenUnlocked();
    if(!open_result.ok())
    {
        result.status = open_result.status;
        result.message = std::move(open_result.message);
        return result;
    }

    // 判断是否需要进行轮转处理
    if(shouldRotateUnlocked(data.size()))
    {
        if (!rotateUnlocked())
        {
            result.status = LogFileSinkResultStatus::kRotateFailed;
            result.message = "log file rotate error";

            return result;
        }
    }

    auto backend_result = backend_->append(data);
    if(!backend_result.ok())
    {
        LOG_FILE_SINK_REPORTED("write", backend_result.message.c_str());

        backend_->close();

        result.status = LogFileSinkResultStatus::kWriteFailed;
        result.requested_bytes = backend_result.requested_bytes;
        result.written_bytes = backend_result.written_bytes;
        result.message = std::move(backend_result.message);
        return result;
    }
    // 实际写入情况
    result.requested_bytes = backend_result.requested_bytes;
    result.written_bytes = backend_result.written_bytes;

    bytes_since_flush_ += result.requested_bytes;
    current_file_size_ .fetch_add(result.written_bytes, std::memory_order_relaxed);

    auto file_config = reg_->fileConfig();
    if(!file_config)
    {
        // HACK 使用默认配置
        file_config = std::make_shared<const LogFileConfig>(LogFileConfig{});
    }

    bool flush_by_bytes = bytes_since_flush_ >= file_config->flush_threshold;

    int64_t mono_now = TimeStamp::MonotonicNowMs();
    bool flush_by_time = mono_now - last_flush_time_ >=  file_config->flush_interval_ms;
    bool flush_by_level = level >= file_config->flush_on_level;

    if(flush_by_bytes || flush_by_time || flush_by_level)
    {
        result.flush_attempted = true;
        if(!flushUnlocked())
        {
            result.status = LogFileSinkResultStatus::kFlushFailed;
            return result;
        }
    }

    return result;
}


LogFileSinkResult LogFileSink::flush()
{
    std::lock_guard<std::mutex> lock(mtx_);
    LogFileSinkResult result;
    result.flush_attempted = true;
    if(!flushUnlocked())
    {
        result.status = LogFileSinkResultStatus::kFlushFailed;
        return result;
    }
    return result;
}

LogFileSinkResult LogFileSink::durableFlush()
{
    std::lock_guard<std::mutex> lock(mtx_);
    LogFileSinkResult result;
    result.flush_attempted = true;

    if(!durableFlushUnlocked())
    {
        result.status = LogFileSinkResultStatus::kSyncFailed;
        return result;
    }

    bytes_since_flush_ = 0;
    return result;
}


LogFileSinkResult LogFileSink::ensureOpen()
{
    std::lock_guard<std::mutex> lock(mtx_);
    return ensureOpenUnlocked();
}

LogFileSinkResult LogFileSink::reopen()
{
    std::lock_guard<std::mutex> lock(mtx_);
    
    if(backend_->isOpen())
    {
        const auto flush_result = backend_->flush();

        if (!flush_result.ok())
        {
            return LogFileSinkResult::Failure(LogFileSinkResultStatus::kFlushFailed, flush_result.message);
        }

        backend_->close();
    }

    return openUnlocked();
}

void LogFileSink::close()
{
    std::lock_guard<std::mutex> lock(mtx_);
    backend_->close();
}

LogFileSinkHealth LogFileSink::healthSnapshot() const
{ 
    std::lock_guard<std::mutex> lock(mtx_);
    LogFileSinkHealth result = health_;
    result.current_file_size =   current_file_size_.load(std::memory_order_acquire);

    return result;
}


LogFileSinkResult LogFileSink::ensureOpenUnlocked()
{
    if(backend_->isOpen())
    {
        return LogFileSinkResult::Ok();
    }
    return openUnlocked();
}

LogFileSinkResult LogFileSink::openUnlocked()
{
    if(normalize_path_.empty())
    {
        LOG_FILE_SINK_REPORTED("open", "");

        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kOpenFailed, "file normalize path empty");
    }
    
    auto backend_result = backend_->open(normalize_path_);
    if(!backend_result.ok())
    {
        LOG_FILE_SINK_REPORTED("open", backend_result.message.c_str());

        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kOpenFailed,
            "cannot open file [" + normalize_path_ + "]:" + backend_result.message);
    }

    bytes_since_flush_ = 0;
    current_file_size_.store(backend_->openSize(), std::memory_order_release);
    error_reported_ = false;
    last_flush_time_ = TimeStamp::MonotonicNowMs();

    return LogFileSinkResult::Ok();
}

bool LogFileSink::flushUnlocked()
{
    auto backend_result = backend_->flush();
    if(!backend_result.ok())
    {
        LOG_FILE_SINK_REPORTED("flush", backend_result.message.c_str());
        return false;
    }

    bytes_since_flush_ = 0;
    last_flush_time_ = TimeStamp::MonotonicNowMs();
    return true;
}

bool LogFileSink::durableFlushUnlocked()
{
    auto backend_result = backend_->durableFlush();
    if(!backend_result.ok())
    {
        LOG_FILE_SINK_REPORTED("durable flush", backend_result.message.c_str());
        return false;
    }
    bytes_since_flush_ = 0;
    last_flush_time_ = TimeStamp::MonotonicNowMs();
    return true;
}


bool LogFileSink::shouldRotateUnlocked(size_t incoming_bytes) const noexcept
{
    const auto file_conifg = reg_->fileConfig();

    return file_conifg
            && current_file_size_.load(std::memory_order_relaxed) + incoming_bytes > file_conifg->rotate_max_bytes;
}

bool LogFileSink::rotateUnlocked()
{
    if(!flushUnlocked())
    {
        LOG_INNER_ERROR("rotate flush error\n");
        return false;
    }

    // 扫描路径中归档文件
    std::vector<LogFileArchive> archives;
    if(!scanArchives(archives))
    {
        LOG_INNER_ERROR("log rotate scan error\n");
        return false;
    }

    // 创建归档名
    std::string new_archive_path;
    if(!newArchivePathUnlocked(new_archive_path))
    {
        return false;
    }

    // 归档名创建成功再关闭
    backend_->close();

    LOG_INNER_DEBUG("log file rotate new new_archive_path: %s \n", new_archive_path.c_str());

    // 更新当前active名称
    std::error_code error;
    std::filesystem::rename(normalize_path_, new_archive_path, error);
    if(error)
    {
        // 重新打开旧路径
        (void)openUnlocked();

        LOG_INNER_ERROR("log file rotate rename error [%s]: %s\n", new_archive_path.c_str(), error.message().c_str());
        return false;
    }

    // 新建active文件
    auto open_result = openUnlocked();
    if(!open_result.ok())
    {
        LOG_INNER_ERROR("log file rotate reopen active error [%s]: %s\n", normalize_path_.c_str(), open_result.message.c_str());
        return false;
    }

    // FileSink 状态重置
    bytes_since_flush_ = 0;
    current_file_size_.store(0, std::memory_order_relaxed);
    error_reported_ = false;

    const auto file_config = reg_->fileConfig();
    if(file_config && file_config->rotate_max_backup_files > 0)
    {
        // 清理旧超限归档
        // 清理失败先不报错
        if(!cleanupOldArchivesUnlocked(*file_config, archives))
        {
            LOG_INNER_WARN("log file rotate cleanup old archives error!\n");
        }
    }
    else
    {
        LOG_INNER_DEBUG("log file config null / backup=0\n");
    }

    return true;
}


bool LogFileSink::scanArchives(std::vector<LogFileArchive> &archives)
{
    const std::filesystem::path active{normalize_path_};

    const auto directory = active.parent_path().empty() ? std::filesystem::path{"."}
        : active.parent_path();

    // 正则转义处理
    const std::string escaped_prefix = EscapeRegexLiteral(active.stem().string());

    /*正则提取/修改 目标路径 
        其实只需要找到 文件名 + 编号
        日期只是方便根据时间定位问题
        net_000_20260101-003000.log
        '.' '*' 注意正则转义问题
    */
    std::string patther_format{"^" + escaped_prefix + "_([0-9]{3,})_([0-9]{8}-[0-9]{6})\\.log$"};

    const std::regex archive_core_pattern{patther_format};

    std::error_code error;
    std::filesystem::directory_iterator it{directory, error};
    const std::filesystem::directory_iterator end;
    if (error)
    {
        LOG_INNER_ERROR("log directory scan archives failed: %s \n", error.message().c_str());
        return false;
    }

    // 从大到小排列
    for (; it != end; it.increment(error))
    {
        if (error)
        {
            LOG_INNER_ERROR("log directory scan archives failed: %s \n", error.message().c_str());
            return false;
        }

        const auto path = it->path();
        const auto filename = path.filename().string();

        LOG_INNER_DEBUG("log directory scan path: %s \n", path.c_str());

        std::smatch reg_field;
        if(!std::regex_match(filename, reg_field, archive_core_pattern)
            || !it->is_regular_file(error) || error)
        {
            LOG_INNER_DEBUG("log directory scan regex match error: %s\n", error.message().c_str());
            continue;
        }

        LogFileArchive a;
        const std::string &seq = reg_field[1].str();
        const std::string date_str = reg_field[2].str();
        // 先收集 汇总后删除
        if(!ParsePositiveArithmetic(seq, a.rotate_seq))
        {
            LOG_INNER_DEBUG("log directory scan parse  seq error! seq: '%s'\n", seq.c_str());
            a.rotate_seq = 0;
        }
        a.archive_path = std::move(path);
        a.date_str = std::move(date_str);
        archives.emplace_back(std::move(a));
    }

    if(archives.empty())
    {
        LOG_INNER_DEBUG("log rotate scan archives empty\n");
        return true;
    }

    std::sort(archives.begin(), archives.end(), [](const LogFileArchive &a, const LogFileArchive &b){
        if(a.rotate_seq == b.rotate_seq)
        {
            return a.archive_path.string() > b.archive_path.string();
        }
        return a.rotate_seq > b.rotate_seq;
    });

    next_rotate_seq_ = archives.front().rotate_seq + 1;

    for(auto &a : archives)
    {
        LOG_INNER_DEBUG("log archives info: %s \n", a.archive_path.string().c_str());
    }

    return true;
}

bool LogFileSink::newArchivePathUnlocked(std::string &new_archive_path)
{
    const std::string &date_str = MakeArchiveData("%Y%m%d-%H%M%S");

    const std::filesystem::path active{normalize_path_};
    const auto directory = active.parent_path().empty() ? std::filesystem::path{"."}
        : active.parent_path();

    // 获取文件名 不含拓展
    const std::string& file_name = active.stem().string();

    char tmp[64] = {0};
    std::filesystem::path target_path;
    std::error_code error;
    for(;;)
    {
        memset(tmp, 0, sizeof(tmp));
        snprintf(tmp, sizeof(tmp)-1, "%s_%03d_%s.log", file_name.c_str(), next_rotate_seq_, date_str.c_str());

        target_path = directory / tmp;
        bool exists = std::filesystem::exists(target_path, error);
        if(error)
        {
            LOG_INNER_ERROR("archive path inspect failed[%s]: ", target_path.string().c_str(), error.message().c_str());
            return false;
        }
        if(exists)
        {
            // 存在重名可能性
            ++next_rotate_seq_;
            continue;
        }

        new_archive_path = target_path.string();
        break;
    }

    return true;
}

bool LogFileSink::cleanupOldArchivesUnlocked(const LogFileConfig&file_config, 
    std::vector<LogFileArchive> &archives)
{

    std::error_code error;
    // 注意 这里减1是有一个新生成的归档文件没有在当前列表中
    while(archives.size() > file_config.rotate_max_backup_files - 1)
    {
        const auto &archive = archives.back();
        bool ok = std::filesystem::remove(archive.archive_path, error);
        if(error || !ok)
        {
            LOG_INNER_WARN("log rotate remove overflow file error[%s]: %s\n",
                archive.archive_path.filename().c_str(), error.message().c_str());
        }
        archives.pop_back();
    }


    return true;
}



LogFileSinkRegister::LogFileSinkRegister()
    :file_config_(std::make_shared<const LogFileConfig>())
{

}


LogFileSink::Ptr LogFileSinkRegister::acquire(const std::string &file_path)
{
    const std::string normalize_path = NormalizeFilePath(file_path);

    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = log_file_sinks_.find(normalize_path);
    if(it != log_file_sinks_.end())
    {
        if(auto sink = it->second.lock())
        {
            return sink;
        }
    }

    // BUG 如果变为统一加载无误后再统一变更提交，这里发现不存在就创建的逻辑就完全无意义

    auto sink = create(normalize_path);
    if(!sink)
    {
        return nullptr;
    }

    log_file_sinks_[normalize_path] = sink;
    return sink;
}

void LogFileSinkRegister::setFileConfig(LogFileConfig config) 
{ 
    setFileConfig(std::make_shared<const LogFileConfig>(std::move(config)));
}

void LogFileSinkRegister::setFileConfig(std::shared_ptr<const LogFileConfig> config)
{
    std::atomic_exchange_explicit(&file_config_, config, std::memory_order_release);
}


const std::shared_ptr<const LogFileConfig> LogFileSinkRegister::fileConfig() const noexcept 
{ 
    auto config = std::atomic_load_explicit(
    &file_config_,
    std::memory_order_acquire);
    return config; 
}

void LogFileSinkRegister::commit(SinksMap &&sinks)
{
    std::lock_guard<std::mutex> lock(mtx_);
    log_file_sinks_ = std::move(sinks);
}

const LogFileSinkRegister::SinksMap& LogFileSinkRegister::logFileSinks() const 
{
    std::lock_guard<std::mutex> lock(mtx_);
    return log_file_sinks_; 
}


std::vector<LogFileSink::Ptr> LogFileSinkRegister::snapshotSinks() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<LogFileSink::Ptr> result;
    result.reserve(log_file_sinks_.size());

    for(auto &it : log_file_sinks_)
    {
        if(auto sink = it.second.lock())
        {
            result.emplace_back(std::move(sink));
        }
    }
    return result;
}

LogFileSinkResult LogFileSinkRegister::flushAll()
{
    auto sinks = snapshotSinks();
    std::string error_msg;
    LogFileSinkResult result;
    for(auto &sink : sinks)
    {
        if(!sink)
        {
            continue;
        }
        result = sink->flush();
        if(!result.ok())
        {
            error_msg += "flush error[" + sink->normalizedPath() + "]:" + result.message + "\n";
            continue;
        }
    }

    return error_msg.empty() ? 
        LogFileSinkResult::Ok() 
        : LogFileSinkResult::Failure(LogFileSinkResultStatus::kFlushFailed, error_msg);
}

LogFileSinkResult LogFileSinkRegister::flush(const std::string& file_path)
{
    const std::string &normalize_path = NormalizeFilePath(file_path);

    auto sink = find(normalize_path);
    if(!sink)
    {
        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kNotFound, "log file not found: " + normalize_path);
    }

    return sink->flush();
}

LogFileSinkResult LogFileSinkRegister::durableFlushAll()
{
    auto sinks = snapshotSinks();
    std::string error_msg;
    LogFileSinkResult result;
    for(auto &sink : sinks)
    {
        if(!sink)
        {
            continue;
        }
        result = sink->durableFlush();
        if(!result.ok())
        {
            error_msg += "durable flush error[" + sink->normalizedPath() + "]:" + result.message + "\n";
            continue;
        }
    }

    return error_msg.empty() ? 
        LogFileSinkResult::Ok() 
        : LogFileSinkResult::Failure(LogFileSinkResultStatus::kSyncFailed, error_msg);
}

LogFileSinkResult LogFileSinkRegister::durableFlush(const std::string& file_path)
{
    const std::string &normalize_path = NormalizeFilePath(file_path);

    auto sink = find(normalize_path);
    if(!sink)
    {
        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kNotFound, "log file not found: " + normalize_path);
    }

    return sink->durableFlush();
}

LogFileSinkResult LogFileSinkRegister::reopenAll()
{
    auto sinks = snapshotSinks();
    std::string error_msg;

    LogFileSinkResult open_result;
    for(auto &sink : sinks)
    {
        if(!sink)
        {
            continue;
        }

        open_result = sink->reopen();
        if(!open_result.ok())
        {
            error_msg += "reopen error[" + sink->normalizedPath() + "]: " + open_result.message +"\n";
            continue;
        }
    }

    return error_msg.empty() ? 
        LogFileSinkResult::Ok() 
        : LogFileSinkResult::Failure(LogFileSinkResultStatus::kOpenFailed, error_msg);
}

LogFileSinkResult LogFileSinkRegister::reopen(const std::string& file_path)
{
    const std::string &normalize_path = NormalizeFilePath(file_path);

    auto sink = find(normalize_path);
    if(!sink)
    {
        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kNotFound, "log file not found: " + normalize_path);
    }

    return sink->reopen();
}

void LogFileSinkRegister::closeAll() noexcept
{
    auto sinks = snapshotSinks();
    for(auto &sink : sinks)
    {
        if(sink)
        {
            sink->close();
        }
    }
}

LogFileSink::Ptr LogFileSinkRegister::create(const std::string &normalize_path)
{
    auto sink = std::make_shared<LogFileSink>(this, normalize_path);

    auto open_result = sink->reopen();
    if(!open_result.ok())
    {
        LOG_INNER_ERROR("log file sink reopen error [%s]:: %s \n", normalize_path.c_str(), open_result.message.c_str());

        return nullptr;
    }
    return sink;
}


LogFileSink::Ptr LogFileSinkRegister::find(const std::string &file_path)
{
    const std::string normalize_path = NormalizeFilePath(file_path);

    std::lock_guard<std::mutex> lock(mtx_);
    const auto it = log_file_sinks_.find(normalize_path);
    if(it != log_file_sinks_.end())
    {
        if(auto sink = it->second.lock())
        {
            return sink;
        }
    }
    return nullptr;
}

} // namespace kit_muduo
