/**
 * @file log_appender.h
 * @brief 日志输出器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 18:22:47
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __LOG_APPENDER_H__
#define __LOG_APPENDER_H__

#include <atomic>
#include <memory>
#include <fstream>
#include <mutex>

#include "base/log_level.h"
#include "base/log_attr.h"
#include "base/log_formatter.h"

namespace kit_muduo {


class LogAppender
{
public:
    using Ptr = std::shared_ptr<LogAppender>;

    LogAppender();

    LogAppender(LogLevel::Level level, LogFormatter::Ptr formatter);

    virtual ~LogAppender() = default;

    /**
     * @brief 日志输出(带锁)
     * @param[in] pattr 当前日志属性
     */
    void append(LogAttr::Ptr pattr);

    /**
     * @brief 设置日志格式器
     * @param[in] pfarmatter
     */
    void setFormatter(LogFormatter::Ptr pfarmatter);

    /**
     * @brief 设置日志格式器
     * @param[in] pattern 模版字符串
     */
    void setFormatter(const std::string & pattern);

    /**
     * @brief 获取日志格式器
     * @return LogFormatter::Ptr
     */
    LogFormatter::Ptr getFormatter() const;

    /**
     * @brief 设置日志输出器级别
     * @param level
     */
    void setLevel(const LogLevel::Level level) noexcept { level_.store(level); }

    /**
     * @brief 获取日志输出器级别
     * @return LogLevel::Level
     */
    LogLevel::Level getLevel() const noexcept { return static_cast<LogLevel::Level>(level_.load()); }

protected:
    /**
     * @brief 日志输出
     * @param[in] pattr 当前日志属性
     */
    virtual void log(LogAttr::Ptr pattr) = 0;
    virtual void log(const std::string& log_data) = 0;



protected:
    /// @brief 日志输出器级别
    std::atomic_int32_t level_;
    /// @brief 日志格式器
    LogFormatter::Ptr formatter_;
    /// @brief 日志格式器锁(LogFormatter 内部不支持增删改查,因此内部不用锁,锁最外层即可)
    mutable std::mutex mtx_;
};

/**
 * @brief 控制台输出
 */
class ConsoleAppender: public LogAppender
{
public:
    using Ptr = std::shared_ptr<ConsoleAppender>;

    ~ConsoleAppender() = default;

    void log(LogAttr::Ptr pattr) override;
    void log(const std::string& log_data) override;

public:
    static std::mutex& GetConsoleMtx();

};

/**
 * @brief 文件输出
    TODO 重点改造:
    1. 分级落盘策略
    2. 文件限制大小 + 压缩 + 轮转
    3. 异步日志刷盘
 */
class FileAppender: public LogAppender
{
public:
    using Ptr = std::shared_ptr<FileAppender>;

    FileAppender(const std::string &fileName);

    ~FileAppender() = default;

    bool openForAppend(std::string* error_message = nullptr);

    void log(LogAttr::Ptr pattr) override;
    void log(const std::string& log_data) override;

    void setFlushThreshold(uint64_t max_size) { flush_threshold_ = max_size; }
    uint64_t flushThreshold() const noexcept { return flush_threshold_; }

public:
    static constexpr uint64_t kDefaultFlushThreshold = 1*1024*1024;

private:
    /// @brief 输出文件路径
    std::string file_name_;
    /// @brief  文件流句柄
    std::ofstream file_;
    /// @brief 当前一次性写入的大小
    uint64_t cur_size_{0};
    uint64_t flush_threshold_{kDefaultFlushThreshold};
};

//TODO: 网络传输(分布式)

}
#endif