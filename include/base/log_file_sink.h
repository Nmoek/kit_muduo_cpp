/**
 * @file log_file_sink.h
 * @brief 日志文件管理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-16 16:58:06
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_LOG_FILE_SINK_H__
#define __KIT_LOG_FILE_SINK_H__

#include "base/log_file_backend.h"
#include "base/log_level.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace kit_muduo {

struct LogFileConfig;
class LogFileSinkRegister;
class LogFileBackend;

enum class LogFileSinkResultStatus
{
    kOk,

    kNotFound,

    kOpenFailed,
    kWriteFailed,
    kFlushFailed,
    kSyncFailed,
    kRotateFailed,

};

struct LogFileSinkResult
{
    /// @brief 本次写入状态
    LogFileSinkResultStatus status{LogFileSinkResultStatus::kOk};
    /// @brief 本次尝试提交给 sink 的输入字节数(非持久化字节数)
    size_t requested_bytes{0};

    /// @brief append 时表示实际写入字节数。 flush/reopen 等非 append 操作保持为 0。
    size_t written_bytes{0};

    // TODO 批量操作统计。(暂时不关心)
    // 单 sink 操作成功时可以是 attempted=1、succeeded=1；
    // 如果调用方不关心，也可以保持为 0。
    size_t attempted{0};
    size_t succeeded{0};

    /// @brief 是否触发刷新条件
    bool flush_attempted{false};
    bool truncated{false};

    size_t original_bytes{0};

    // 单 sink 操作记录具体错误；
    // 批量操作记录汇总错误。
    std::string message;

    bool ok() const noexcept
    {
        return status == LogFileSinkResultStatus::kOk;
    }

    static LogFileSinkResult Ok()
    {
        return {};
    }

    static LogFileSinkResult Failure(LogFileSinkResultStatus status,
        std::string message)
    {
        LogFileSinkResult result;
        result.status = status;
        result.message = std::move(message);
        return result;
    }
};

enum class LogFileSinkHealthState
{
    kHealthy,  // 正常健康
    kDegraded, // 退化
    kFailed,  // 异常
};

/**
 * @brief 日志文件健康监测
 */
struct LogFileSinkHealth
{
    // 成功写入的逻辑记录数量。
    uint64_t written_records{0};

    // 成功写入的字节数。
    uint64_t written_bytes{0};

    // 各类失败次数。
    uint64_t write_failures{0};
    uint64_t flush_failures{0};
    uint64_t reopen_failures{0};
    uint64_t rotate_failures{0};

    // fallback 和截断统计。
    uint64_t fallback_records{0};
    uint64_t truncated_records{0};

    // 当前 active 文件大小。
    uint64_t current_file_size{0};

    // 最近一次成功写入的时间，使用 Unix epoch milliseconds。
    uint64_t last_success_ms{0};

    // 当前连续失败次数。成功后清零。
    uint32_t consecutive_failures{0};

    // 当前健康状态。
    LogFileSinkHealthState state{LogFileSinkHealthState::kHealthy};

    // 最近一次错误信息。
    std::string last_error;
};

struct LogFileArchive
{
    /// @brief 归档所属编号 -1代表不存在归档
    int32_t rotate_seq{-1};
    /// @brief 归档名的日期(防止编号异常重复 退化为比较日期)
    std::string date_str;
    /// @brief 归档路径对象
    std::filesystem::path archive_path;
};

class LogFileSink
{
public:
    using Ptr = std::shared_ptr<LogFileSink>;

    LogFileSink(LogFileSinkRegister *reg, const std::string &normalize_path);
    ~LogFileSink() = default;

   LogFileSinkResult append(const std::string& log_data,
        LogLevel::Level level,
        bool truncated = false,
        size_t original_bytes = 0);
    LogFileSinkResult flush();
    LogFileSinkResult durableFlush();
    /**
     * @brief 确保持久化通道打开(如果发现意外关闭需要重新打开)
     * @return LogFileSinkResult 
     */
    LogFileSinkResult ensureOpen();

    /**
     * @brief 重新打开持久化通道(先刷新关闭 后重新打开)
     * @return LogFileSinkResult 
     */
    LogFileSinkResult reopen();

    void close();

    const std::string& normalizedPath() const noexcept { return normalize_path_; }
    uint64_t currentFileSize() const { return accepted_generation_bytes_.load(); }

    LogFileSinkHealth healthSnapshot() const;



private:
    LogFileSinkResult ensureOpenUnlocked();
    LogFileSinkResult openUnlocked();
    bool flushUnlocked();
    bool durableFlushUnlocked();

    bool shouldRotateUnlocked(const LogFileConfig& file_conifg, size_t incoming_bytes) const noexcept;

    bool rotateUnlocked(const LogFileConfig& file_conifg);

    bool scanArchives(std::vector<LogFileArchive> &archives);

    bool newArchivePathUnlocked(std::string &new_archive_path);

    bool cleanupOldArchivesUnlocked(const LogFileConfig&file_config, std::vector<LogFileArchive> &archives);

    void rotateFailureHandleUnlocked(const LogBackendResult& result);

private:

    /** @brief 保留文件注册器的指针
        重要作用: 减少配置副本引起的不一致问题, 方便访问注册器
    */
    LogFileSinkRegister *reg_;
    /// @brief 归一化后路径
    std::string normalize_path_;
    /// @brief 文件持久化对象
    std::unique_ptr<LogFileBackend> backend_;
    /// @brief 句柄操作锁
    mutable std::mutex mtx_;
    /// @brief 记录已交付backend的文件大小(并不等于已写入文件的大小)
    std::atomic_uint64_t accepted_generation_bytes_{0};
    /// @brief 本轮已写的字节数
    uint64_t bytes_since_flush_{0};
    /// @brief 上次触发刷新的单调时间
    int64_t last_flush_time_{0};
    /// @brief 当前故障阶段是否已经向 stderr 报告
    bool error_reported_{false};
    /// @brief 日志文件健康观测数据
    LogFileSinkHealth health_;
    /// @brief 下一次轮转的编号
    int32_t next_rotate_seq_{0};
};




class LogFileSinkRegister
{
public:
    using SinksMap = std::unordered_map<std::string, std::weak_ptr<LogFileSink>>;

    LogFileSinkRegister();
    ~LogFileSinkRegister() = default;


    LogFileSink::Ptr acquire(const std::string &file_path);

    // TODO 后续这个接口涉及热更新
    void setConfig(LogFileConfig config);
    void setConfig(std::shared_ptr<const LogFileConfig> config);

    const std::shared_ptr<const LogFileConfig> config() const noexcept;

    void commit(SinksMap &&sinks);

    const SinksMap& logFileSinks() const;

    std::vector<LogFileSink::Ptr> snapshotSinks() const;

    LogFileSinkResult flushAll();

    LogFileSinkResult flush(const std::string& file_path);

    LogFileSinkResult durableFlushAll();

    LogFileSinkResult durableFlush(const std::string& file_path);

    LogFileSinkResult reopenAll();

    LogFileSinkResult reopen(const std::string& file_path);

    void closeAll() noexcept;

private:
    friend class LogManager;

    LogFileSink::Ptr create(const std::string &normalize_path);

    LogFileSink::Ptr find(const std::string &file_path);


private:
    /// @brief <归一化后路径, 日志文件对象>
    SinksMap log_file_sinks_;
    mutable std::mutex mtx_;
    std::shared_ptr<const LogFileConfig> file_config_;
};



}
#endif //__KIT_LOG_FILE_SINK_H__
