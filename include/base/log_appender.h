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
     * @brief 日志输出
     * @param[in] pattr 当前日志属性
     */
    virtual void log(LogAttr::Ptr pattr) = 0;

    /**
     * @brief 日志输出(带锁)
     * @param[in] pattr 当前日志属性
     */
    void append(LogAttr::Ptr pattr);

    /**
     * @brief 设置日志格式器
     * @param[in] pfarmatter
     */
    void setFomatter(LogFormatter::Ptr pfarmatter);

    /**
     * @brief 设置日志格式器
     * @param[in] pattern 模版字符串
     */
    void setFomatter(const std::string & pattern);

    /**
     * @brief 获取日志格式器
     * @return LogFormatter::Ptr
     */
    LogFormatter::Ptr getFormatter() const;

protected:
    /// @brief 日志输出器级别
    LogLevel::Level _level;
    /// @brief 日志格式器
    LogFormatter::Ptr _formatter;
    /// @brief 日志格式器锁(LogFormatter 内部不支持增删改查,因此内部不用锁,锁最外层即可)
    mutable std::mutex _formatterMtx;
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
};

/**
 * @brief 文件输出
 */
class FileAppender: public LogAppender
{
public:
    using Ptr = std::shared_ptr<FileAppender>;

    FileAppender(const std::string &fileName);

    ~FileAppender() = default;

    bool reopen();

    void log(LogAttr::Ptr pattr) override;

private:
    /// @brief 输出文件路径
    std::string _fileName;
    /// @brief  文件流句柄
    std::fstream _f;
};

//TODO: 网络传输(分布式)

}
#endif