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

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace kit_muduo {

struct LogFileConfig;
class LogFileSinkRegister;

enum class LogFileSinkStatus
{
    kOk,
    kOpenFailed,
    kWriteFailed,
    kFlushFailed,
};

struct LogFileSinkResult
{
    /// @brief 本次写入状态
    LogFileSinkStatus status{LogFileSinkStatus::kOk};
    /// @brief 本次尝试提交给 sink 的输入字节数(非持久化字节数)
    size_t requested_bytes{0};
    /// @brief 本次写入是否触发了阈值 flush。
    bool flush_attempted{false};

    bool ok() const noexcept { return status == LogFileSinkStatus::kOk; }
};

class LogFileSink
{
public:
    using Ptr = std::shared_ptr<LogFileSink>;

    LogFileSink(const std::string &normalize_path, const std::shared_ptr<const LogFileConfig>& base_file_confg);
    ~LogFileSink() = default;

    LogFileSinkResult append(const std::string& data);
    void flush();
    bool ensureOpen(std::string* error_message = nullptr);
    bool reopen(std::string* error_message = nullptr);
    const std::string& normalizedPath() const noexcept { return normalize_path_; }
    uint64_t currentFileSize() const { return current_file_size_.load(); }

private:
    bool ensureOpenUnlocked(std::string* error_message);
    bool openUnlocked(std::string* error_message);
    bool flushUnlocked();
    void reportErrorUnlocked(const char* operation);

private:
    /// @brief 指向注册器日志文件配置指针
    std::shared_ptr<const LogFileConfig> base_file_confg_;
    /// @brief 归一化后路径
    std::string normalize_path_;
    /// @brief 文件流对象
    std::ofstream ofs_;
    /// @brief 句柄操作锁
    std::mutex mtx_;
    /// @brief 记录已写入的文件大小(避免每次访问)
    std::atomic_uint64_t current_file_size_{0};
    /// @brief 本轮已写的字节数
    uint64_t bytes_since_flush_{0};
    /// @brief 当前故障阶段是否已经向 stderr 报告
    bool error_reported_{false};
    std::vector<uint8_t> log_datas_;
};




class LogFileSinkRegister
{
public:

    LogFileSinkRegister();
    ~LogFileSinkRegister() = default;

    LogFileSink::Ptr acquire(const std::string &file_path);

    // TODO 后续这个接口变为热更新时 就是观察者接口
    void setFileConfig(LogFileConfig config);

    const std::shared_ptr<const LogFileConfig>& fileConfig() const noexcept;

private:
    LogFileSink::Ptr create(const std::string &normalize_path);


private:
    /// @brief <归一化后路径, 日志文件对象>
    std::unordered_map<std::string, std::weak_ptr<LogFileSink>> log_file_sinks_;
    std::mutex mtx_;
    std::shared_ptr<const LogFileConfig> file_config_;
};



}
#endif //__KIT_LOG_FILE_SINK_H__
