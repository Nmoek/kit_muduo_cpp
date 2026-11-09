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

#include "base/compression.h"
#include "base/log_file_backend.h"

#include <atomic>
#include <optional>
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
class LogAsyncWakeup;

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
    int32_t rotate_seq{0};
    /// @brief 归档名的日期(防止编号异常重复 退化为比较日期)
    std::string date_str;
    /// @brief 归档路径对象
    std::filesystem::path archive_path;
    /// @brief 是否进行压缩补偿
    bool is_compress_compensate{false};
};


struct LogFileFlushState
{
    /// @brief 自上次成功同步以来，backend 实际接受的未同步字节数
    uint64_t pending_sync_bytes{0};
    /// @brief 单调时钟毫秒：首次待同步期限；失败后更新为失败结束时间 + 间隔
    std::optional<int64_t> next_flush_time;
    /// @brief 上次刷新失败时，字节阈值不能绕过 next_flush_time 提前自动重试
    bool last_flush_failed{false};

    void accept(uint64_t bytes, int64_t now_ms, uint64_t interval_ms);
    std::optional<int64_t> nextAttemptTime(uint64_t byte_threshold) const;
    bool shouldAutoFlush(int64_t now_ms, uint64_t byte_threshold) const;
    void failed(int64_t now_ms, uint64_t interval_ms);
    void clear() noexcept { *this = {}; }
};

class LogFileSink
{
public:
    using Ptr = std::shared_ptr<LogFileSink>;

    LogFileSink(LogFileSinkRegister *reg, const std::string &normalize_path, std::unique_ptr<LogFileBackend> backend);
    ~LogFileSink() = default;

    LogFileSinkResult append(const std::string& log_data,
        bool truncated = false,
        size_t original_bytes = 0);
    LogFileSinkResult flush();
    std::optional<int64_t> nextFlushTime() const;
    LogFileSinkResult flushIfDue(int64_t now);
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

    LogFileSinkResult finishArchiveCompletions();

    LogFileSinkResult closeForShutdown();

private:
    LogFileSinkResult ensureOpenUnlocked();
    LogFileSinkResult openUnlocked();
    LogFileSinkResult flushUnlocked();

    bool shouldRotateUnlocked(const LogFileConfig& file_conifg, size_t incoming_bytes) const noexcept;

    bool rotateUnlocked(const LogFileConfig& file_conifg);

    bool scanArchives(std::vector<LogFileArchive> &archives);

    bool newArchivePathUnlocked(std::string &new_archive_path);

    void cleanupOldArchivesUnlocked(const LogFileConfig&file_config, std::vector<LogFileArchive> &archives);

    /**
     * @brief 对归档文件进行压缩补偿 需放在清理归档动作之后
     * @param archives 
     */
    void compensateOldArchivesUnlocked(std::vector<LogFileArchive> &archives);

    void rotateFailureHandleUnlocked(const LogBackendResult& result);

    void pollArchiveCompletions();

private:
    static constexpr int32_t kRotateReqMax = 1000;
    
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
    LogFileFlushState flush_state_;
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


    LogFileSink::Ptr acquire(const std::string &file_path, LogCompressCoordinator& compress_coordinator);

    // TODO 后续这个接口涉及热更新
    void setConfig(LogFileConfig config);
    void setConfig(std::shared_ptr<const LogFileConfig> config);

    const std::shared_ptr<const LogFileConfig> config() const noexcept;

    void commit(SinksMap &&sinks);

    const SinksMap& logFileSinks() const;

    std::vector<LogFileSink::Ptr> snapshotSinks() const;

    LogFileSinkResult flushAll();

    LogFileSinkResult flush(const std::string& file_path);



    LogFileSinkResult reopenAll();

    LogFileSinkResult reopen(const std::string& file_path);

    void closeAll() noexcept;

    void setFlushWakeup(std::shared_ptr<LogAsyncWakeup> wakeup);
    void clearFlushWakeup(const std::shared_ptr<LogAsyncWakeup>& wakeup);
    void notifyFlushSchedule() const noexcept;
    std::optional<int64_t> nextFlushTime() const;
    void flushDue(int64_t now);


private:
    friend class LogManager;

    LogFileSink::Ptr create(const std::string &normalize_path, LogCompressCoordinator& compress_coordinator);

    LogFileSink::Ptr find(const std::string &file_path);


private:
    /// @brief <归一化后路径, 日志文件对象>
    SinksMap log_file_sinks_;
    // 串行化创建/替换集合；创建时不占用快照查询使用的 mtx_。
    std::mutex mutation_mtx_;
    mutable std::mutex mtx_;
    std::shared_ptr<const LogFileConfig> file_config_;
    std::shared_ptr<LogAsyncWakeup> flush_wakeup_;
};



}
#endif //__KIT_LOG_FILE_SINK_H__
