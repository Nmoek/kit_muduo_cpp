/**
 * @file log_file_sink.cpp
 * @brief 日志文件管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-16 17:23:35
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "base/log_compress_coordinator.h"
#include "base/log_config.h"
#include "base/log_async.h"
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
#include <unistd.h>

namespace kit_muduo {

namespace {

// 声明在 sink 锁之前，析构时锁已释放；唤醒句柄不反向访问 sink。
struct NotifyFlushSchedule
{
    LogFileSinkRegister* reg;
    ~NotifyFlushSchedule() { reg->notifyFlushSchedule(); }
};

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
    ss << date::format(formatter, fixed_utc) << "-" << std::setw(3) << std::setfill('0') << real_time_ms % 1000;
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

void LogFileFlushState::accept(uint64_t bytes, int64_t now_ms, uint64_t interval_ms)
{
    if (bytes == 0) return;
    if (pending_sync_bytes == 0) next_flush_time = now_ms + interval_ms;
    pending_sync_bytes += bytes;
}

std::optional<int64_t> LogFileFlushState::nextAttemptTime(uint64_t byte_threshold) const
{
    if (pending_sync_bytes == 0)
    {
        return std::nullopt;
    }
    if (last_flush_failed)
    {
        return next_flush_time;
    }
    // 0 表示字节已达阈值，可立即执行；失败后的重试不进入此分支。
    return pending_sync_bytes >= byte_threshold ? std::optional<int64_t>{0} : next_flush_time;
}

bool LogFileFlushState::shouldAutoFlush(int64_t now_ms, uint64_t byte_threshold) const
{
    const auto next = nextAttemptTime(byte_threshold);
    return next && now_ms >= *next;
}

void LogFileFlushState::failed(int64_t now_ms, uint64_t interval_ms)
{
    if(pending_sync_bytes != 0)
    {
        next_flush_time = now_ms + interval_ms;
        last_flush_failed = true;
    }
}

LogFileSink::LogFileSink(LogFileSinkRegister *reg, const std::string &normalize_path, std::unique_ptr<LogFileBackend> backend)
    :reg_(reg)
    ,normalize_path_(normalize_path)
    ,backend_(std::move(backend))
{
    assert(reg_);
}

LogFileSinkResult LogFileSink::append(const std::string& data,
    bool truncated, size_t original_bytes)
{
    NotifyFlushSchedule notify{reg_};
    std::lock_guard<std::mutex> lock(mtx_);
    LogFileSinkResult result;
    result.requested_bytes = data.size();
    result.truncated = truncated;
    result.original_bytes = original_bytes;
    const auto opened = ensureOpenUnlocked();
    if (!opened.ok())
    {
        result.status = opened.status;
        result.message = opened.message;
        return result;
    }
    const auto file_config = reg_->config();

    if (shouldRotateUnlocked(*file_config, data.size()))
    {
        if(!rotateUnlocked(*file_config))
        {
            result.status = LogFileSinkResultStatus::kRotateFailed;
            result.message = "log file rotate error";
            return result;
        }
    }
    const auto written_result = backend_->append(data);

    result.requested_bytes = written_result.requested_bytes;
    result.written_bytes = written_result.written_bytes;

    flush_state_.accept(written_result.written_bytes, TimeStamp::MonotonicNowMs(), file_config->flush_interval_ms);

    accepted_generation_bytes_.fetch_add(written_result.written_bytes, std::memory_order_relaxed);
    health_.written_bytes += written_result.written_bytes;
    if (!written_result.ok())
    {
        // 部分接受也属于待同步数据；不得 close 并丢失该 fd/slot 的重试机会。
        LOG_FILE_SINK_REPORTED("write", written_result.message.c_str());
        result.status = LogFileSinkResultStatus::kWriteFailed;
        result.message = written_result.message;
        ++health_.write_failures;
        return result;
    }
    ++health_.written_records;
    if (truncated)
    {
        ++health_.truncated_records;
    }

    const auto now = TimeStamp::MonotonicNowMs();
    if (flush_state_.pending_sync_bytes >= file_config->flush_threshold
        && flush_state_.shouldAutoFlush(now, file_config->flush_threshold))
    {
        const auto flushed = flushUnlocked();
        result.flush_attempted = true;
        result.status = flushed.status;
        result.message = flushed.message;
    }
    return result;
}

LogFileSinkResult LogFileSink::flush()
{
    NotifyFlushSchedule notify{reg_};
    std::lock_guard<std::mutex> lock(mtx_);
    return flushUnlocked();
}

std::optional<int64_t> LogFileSink::nextFlushTime() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return flush_state_.nextAttemptTime(reg_->config()->flush_threshold);
}

LogFileSinkResult LogFileSink::flushIfDue(int64_t now)
{
    NotifyFlushSchedule notify{reg_};
    std::lock_guard<std::mutex> lock(mtx_);
    if (!flush_state_.shouldAutoFlush(now, reg_->config()->flush_threshold))
    {
        return LogFileSinkResult::Ok();
    }
    return flushUnlocked();
}

LogFileSinkResult LogFileSink::ensureOpen()
{
    NotifyFlushSchedule notify{reg_};
    std::lock_guard<std::mutex> lock(mtx_);
    return ensureOpenUnlocked();
}

LogFileSinkResult LogFileSink::reopen()
{
    NotifyFlushSchedule notify{reg_};
    std::lock_guard<std::mutex> lock(mtx_);
    
    if(backend_->isOpen())
    {
        const auto flush_result = flushUnlocked();

        if (!flush_result.ok())
        {
            return flush_result;
        }

        backend_->close();
        if (backend_->isOpen())
        {
            return LogFileSinkResult::Failure(LogFileSinkResultStatus::kOpenFailed,
                "backend close deferred; retry reopen after pending work");
        }
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
    result.current_file_size =   accepted_generation_bytes_.load(std::memory_order_acquire);

    return result;
}

LogFileSinkResult LogFileSink::finishArchiveCompletions()
{
    std::lock_guard<std::mutex> lock(mtx_);
    backend_->pollArchiveCompletions();
    if(backend_->hasPendingArchives())
    {
        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kFlushFailed,
            "archive completion/cleanup still pending");
    }
    return LogFileSinkResult::Ok();
}

LogFileSinkResult LogFileSink::closeForShutdown()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if(backend_->hasPendingArchives())
    {
        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kFlushFailed,
            "cannot close backend with pending archives");
    }
    backend_->close();
    if(backend_->isOpen())
    {
        return LogFileSinkResult::Failure(LogFileSinkResultStatus::kFlushFailed,
            "backend refused final close");
    }
    return LogFileSinkResult::Ok();
}

LogFileSinkResult LogFileSink::ensureOpenUnlocked()
{
    backend_->pollArchiveCompletions();

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

    flush_state_.clear();
    flush_state_.accept(backend_->openSize(), TimeStamp::MonotonicNowMs(),
                        reg_->config()->flush_interval_ms);
    accepted_generation_bytes_.store(backend_->openSize(), std::memory_order_release);
    error_reported_ = false;

    return LogFileSinkResult::Ok();
}

LogFileSinkResult LogFileSink::flushUnlocked()
{
    const auto backend_result = backend_->flush();
    LogFileSinkResult result;
    result.flush_attempted = true;
    if (!backend_result.ok())
    {
        flush_state_.failed(TimeStamp::MonotonicNowMs(), reg_->config()->flush_interval_ms);

        ++health_.flush_failures;
        health_.last_error = backend_result.message;

        LOG_FILE_SINK_REPORTED("flush", backend_result.message.c_str());

        result.status = backend_result.status == LogBackendStatus::kSyncFailed
            ? LogFileSinkResultStatus::kSyncFailed : LogFileSinkResultStatus::kFlushFailed;
        result.message = backend_result.message;
        return result;
    }
    flush_state_.clear();
    error_reported_ = false;
    return result;
}

bool LogFileSink::shouldRotateUnlocked(const LogFileConfig& file_config, size_t incoming_bytes) const noexcept
{
    const auto accepted_bytes = accepted_generation_bytes_.load(std::memory_order_relaxed);
    if(0 == file_config.rotate_max_backup_files || 0 == accepted_bytes)
    {
        return false;
    }

    // 单条超过阈值仍完整写入空 generation；减法只在不会下溢时执行。
    return incoming_bytes > file_config.rotate_max_bytes ||
        accepted_bytes > file_config.rotate_max_bytes - incoming_bytes;
}

bool LogFileSink::rotateUnlocked(const LogFileConfig& file_config)
{
    backend_->pollArchiveCompletions();

    // 扫描路径中归档文件
    // [重要] 每次都扫描的目的
    // 1. 确认当前轮转编号最大值
    // 2. 防止外部用户操作 删除/修改归档文件
    std::vector<LogFileArchive> archives;
    if(!scanArchives(archives))
    {
        rotateFailureHandleUnlocked(LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
            "scan archives failed"));
        return false;
    }

    std::string archive_log;
    if (!newArchivePathUnlocked(archive_log))
    {
        rotateFailureHandleUnlocked(LogBackendResult::Failure(LogBackendStatus::kWriteFailed,
            "create archive path failed"));
        return false;
    }

    LogFileRotateRequest request
    {
        backend_->generation(),
        backend_->lastSequence(),
        normalize_path_,
        std::move(archive_log),
        file_config.compress_rotated
    };
    
    const auto old_generation = backend_->generation();

    const auto result = backend_->rotate(request);

    const bool archive_committed = backend_->generation() != old_generation;
    if(archive_committed)
    {
        accepted_generation_bytes_.store(0, std::memory_order_release);
        flush_state_.clear();
    }

    if(!result.ok())
    { 
        rotateFailureHandleUnlocked(result);
        return false;
    }
    // 清理超出上限的归档文件
    cleanupOldArchivesUnlocked(file_config, archives);

    // 压缩补偿
    if(file_config.compress_rotated)
    {
        compensateOldArchivesUnlocked(archives);
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
    std::string patther_format{"^" + escaped_prefix + "_([0-9]{3,})_([0-9]{8}-[0-9]{6}-[0-9]{3})\\.log(?:\\.(zst))?$"}; //  TODO 压缩后缀硬编码

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
        a.is_compress_compensate = reg_field[3].str().empty();  // 压缩后缀为空代表要补偿
        archives.emplace_back(std::move(a));
    }

    if(archives.empty())
    {
        LOG_INNER_INFO("log rotate scan archives empty\n");
        return true;
    }

    // 归档名 排序按照日期时间字符串，相同时再按序号
    std::sort(archives.begin(), archives.end(), [](const LogFileArchive &a, const LogFileArchive &b){
        if(a.date_str == b.date_str)
        {
            // 编号相同时 优先删无压缩后缀的文件
            if(a.rotate_seq == b.rotate_seq)
            {
                return a.archive_path.string().size() > b.archive_path.string().size();
            }
            return a.rotate_seq > b.rotate_seq;
        }

        return a.date_str > b.date_str;
    });

    next_rotate_seq_ = archives.empty() ? 0 : archives.front().rotate_seq + 1;

    for(auto &a : archives)
    {
        LOG_INNER_DEBUG("log archives info: %s compress[%d]\n", a.archive_path.string().c_str(), static_cast<int>(a.is_compress_compensate));
    }

    return true;
}

bool LogFileSink::newArchivePathUnlocked(std::string &new_archive_path)
{
    const std::string &date_str = MakeArchiveData("%Y%m%d-%H%M%S");

    const std::filesystem::path active{normalize_path_};
    const auto directory = active.parent_path().empty() ? std::filesystem::path{"."} : active.parent_path();

    // 获取文件名 不含拓展
    const std::string& file_name = active.stem().string();

    char tmp[64] = {0};
    std::filesystem::path target_path;
    std::error_code error1, error2;
    for(;;)
    {
        next_rotate_seq_ %= kRotateReqMax;

        memset(tmp, 0, sizeof(tmp));
        snprintf(tmp, sizeof(tmp)-1, "%s_%03d_%s.log", file_name.c_str(), next_rotate_seq_, date_str.c_str());

        target_path = directory / tmp;
        bool exists1 = std::filesystem::exists(target_path, error1);

        std::string compress_tmp{tmp};
        compress_tmp += ".zst"; //TODO 压缩后缀硬编码
        std::filesystem::path compress_path = directory / compress_tmp;
        bool exists2 = std::filesystem::exists(compress_path, error2);
        if(error1 || error2)
        {
            LOG_INNER_ERROR("archive path inspect failed[%s]: %s | %s\n", target_path.string().c_str(), error1.message().c_str(), error2.message().c_str());
            return false;
        }
        if(exists1 || exists2)
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

void LogFileSink::cleanupOldArchivesUnlocked(const LogFileConfig& file_config, 
    std::vector<LogFileArchive> &archives)
{
    size_t index = archives.size();
    // 注意 这里减1是当前新生成的归档文件没有扫描加入到当前列表中
    while(archives.size() + 1 > file_config.rotate_max_backup_files && index)
    {
        --index;
        // TODO 这里无法和FullRecompree压缩记录中的信息联动
        const auto &archive_path = archives.at(index).archive_path;
        if(backend_->archiveInUse(archive_path))
        {
            LOG_INNER_DEBUG("log rorate full recompressing: %s \n", archive_path.c_str());
            continue;
        }
        std::error_code error;
        bool ok = std::filesystem::remove(archive_path , error);
        if(error || !ok)
        {
            LOG_INNER_WARN("log rotate remove overflow file error[%s]: %s\n",
                archive_path.filename().c_str(), error.message().c_str());
            continue;
        }
        // 文件可能已经被已完成的 worker 删除；无错误时从扫描快照移除即可。
        // 指定下标是因为不能无条件从队尾直接删除，在途归档可能还在处理
        archives.erase(archives.begin() + index);
    }
}

void LogFileSink::compensateOldArchivesUnlocked(std::vector<LogFileArchive> &archives)
{
    for(auto &a : archives)
    {
        const std::string& archive_path = a.archive_path.string();
        if(!a.is_compress_compensate || backend_->archiveInUse(archive_path))
        {
            LOG_INNER_DEBUG("log rorate full recompressing: %s \n", archive_path.c_str());
            continue;
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(a.archive_path, error);
        if (error || !exists)
        {
            continue; // 扫描之后可能已经实时发布并删除原文。
        }

        if(!backend_->recompress(archive_path))
        {
            LOG_INNER_WARN("log archives recompress submit error: %s\n", archive_path.c_str());
        }
    }
}


void LogFileSink::rotateFailureHandleUnlocked(const LogBackendResult& result)
{
    ++health_.rotate_failures;
    ++health_.consecutive_failures;

    // 轮转失败不等于 active 通道必然失效。例如，zstd 发布失败时
    // active 文件仍可能已经重新打开；只有 backend 已关闭才标记 Failed。
    health_.state = backend_->isOpen()
        ? LogFileSinkHealthState::kDegraded
        : LogFileSinkHealthState::kFailed;

    health_.last_error = result.message.empty()
        ? "log backend rotate failed without details"
        : result.message;

    // 复用 4-0 的 stderr 单次报告机制，不能递归写普通日志。
    LOG_FILE_SINK_REPORTED("rotate", health_.last_error.c_str());
}

void LogFileSink::pollArchiveCompletions()
{
    std::lock_guard<std::mutex> lock(mtx_);
    backend_->pollArchiveCompletions();
}


/************LogFileSinkRegister*********** */

LogFileSinkRegister::LogFileSinkRegister()
    :file_config_(std::make_shared<const LogFileConfig>(LogFileConfig{}))
{

}


LogFileSink::Ptr LogFileSinkRegister::acquire(const std::string &file_path, LogCompressCoordinator& compress_coordinator)
{
    NotifyFlushSchedule notify{this};
    std::lock_guard<std::mutex> mutation_lock(mutation_mtx_);
    const std::string normalize_path = NormalizeFilePath(file_path);
    if (auto sink = find(normalize_path)) return sink;

    auto sink = create(normalize_path, compress_coordinator);
    if(!sink)
    {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        log_file_sinks_[normalize_path] = sink;
    }
    return sink;
}

void LogFileSinkRegister::setConfig(LogFileConfig config) 
{ 
    setConfig(std::make_shared<const LogFileConfig>(std::move(config)));
}

void LogFileSinkRegister::setConfig(std::shared_ptr<const LogFileConfig> config)
{
    std::atomic_exchange_explicit(&file_config_, config, std::memory_order_release);
    notifyFlushSchedule();
}


const std::shared_ptr<const LogFileConfig> LogFileSinkRegister::config() const noexcept 
{ 
    return std::atomic_load_explicit(
    &file_config_,
    std::memory_order_acquire);
}

void LogFileSinkRegister::commit(SinksMap &&sinks)
{
    NotifyFlushSchedule notify{this};
    std::lock_guard<std::mutex> mutation_lock(mutation_mtx_);
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

LogFileSink::Ptr LogFileSinkRegister::create(const std::string &normalize_path, LogCompressCoordinator& compress_coordinator)
{
    auto sink = std::make_shared<LogFileSink>(this, normalize_path, LogFileBackend::NewDefaultBackend(compress_coordinator));

    auto open_result = sink->reopen();
    if(!open_result.ok())
    {
        LOG_INNER_ERROR("log file sink reopen error [%s]:: %s \n", normalize_path.c_str(), open_result.message.c_str());

        return nullptr;
    }
    return sink;
}


void LogFileSinkRegister::setFlushWakeup(std::shared_ptr<LogAsyncWakeup> wakeup)
{
    std::atomic_store(&flush_wakeup_, std::move(wakeup));
    notifyFlushSchedule();
}

void LogFileSinkRegister::clearFlushWakeup(const std::shared_ptr<LogAsyncWakeup>& wakeup)
{
    auto expected = wakeup;
    std::atomic_compare_exchange_strong(&flush_wakeup_, &expected,
                                       std::shared_ptr<LogAsyncWakeup>{});
}

void LogFileSinkRegister::notifyFlushSchedule() const noexcept
{
    if (auto wakeup = std::atomic_load(&flush_wakeup_)) wakeup->notify();
}

std::optional<int64_t> LogFileSinkRegister::nextFlushTime() const
{
    std::optional<int64_t> earliest;
    for (const auto& sink : snapshotSinks())
    {
        const auto next = sink->nextFlushTime();
        if (next && (!earliest || *next < *earliest))
        {
            earliest = next;
        }
    }
    return earliest;
}

void LogFileSinkRegister::flushDue(int64_t now)
{
    for (const auto& sink : snapshotSinks())
    {
        if (const auto next = sink->nextFlushTime(); next && *next <= now)
        {
            (void)sink->flushIfDue(now); // sink 保留错误与重试期限，继续检查其他文件。
        }
    }
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
