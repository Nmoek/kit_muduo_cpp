/**
 * @file log.h
 * @brief  日志器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-16 15:38:44
 * @copyright Copyright (c) 2025 Kewin Li
 */

#ifndef __LOG_H__
#define __LOG_H__

#include <string>
#include <pthread.h>
#include <list>
#include <memory>
#include <mutex>

#include "base/log_level.h"
#include "base/log_appender.h"
#include "base/log_formatter.h"
#include "base/log_attr.h"
#include "base/util.h"
#include "base/singleton.h"

/********1、流式输出 ********/
#define LOG_LEVEL_OUT(logger, level, module) \
    kit_muduo::LogAttrWrap(std::make_shared<kit_muduo::LogAttr>(logger, level, logger->getName(), module, __FILE__, __LINE__, 0, kit_muduo::GetThreadTid(), kit_muduo::GetThreadPid(), kit_muduo::GetThreadName().c_str(), kit_muduo::GetTimeStampMs())).getSS()


#define KIT_DEBUG(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::DEBUG, module)
#define KIT_INFO(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::INFO, module)
#define KIT_WARN(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::WARN, module)
#define KIT_ERROR(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::ERROR, module)
#define KIT_FATAL(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::FATAL, module)

/********2、变参输出********/
#define LOG_LEVEL_FMT_OUT(logger, level, module, fmt, ...) \
    kit_muduo::LogAttrWrap(std::make_shared<kit_muduo::LogAttr>(logger, level, logger->getName(), module, __FILE__, __LINE__, 0, kit_muduo::GetThreadTid(), kit_muduo::GetThreadPid(), kit_muduo::GetThreadName().c_str(), kit_muduo::GetTimeStampMs())).getAttr()->format(fmt, ##__VA_ARGS__ )

#define KIT_FMT_DEBUG(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::DEBUG, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_INFO(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::INFO, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_WARN(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::WARN, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_ERROR(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::ERROR, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_FATAL(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::FATAL, module, fmt, ##__VA_ARGS__)


/********3、全局日志器操作********/
#define KIT_DEF_LOGGER() \
    kit_muduo::LogManagerInstance::GetInstance().getDefLogger()

#define KIT_LOGGER(name) \
    kit_muduo::LogManagerInstance::GetInstance().getLogger(name)


namespace kit_muduo {

/**
 * @brief 日志器
 */
class Logger
{

public:
    using Ptr = std::shared_ptr<Logger>;

    Logger(const std::string &name = "root");

    ~Logger() = default;

    /**
     * @brief  委托关系 logger委托logAppender 进行实际的日志打印
     * @param[in] level  当前日志级别
     * @param[in] pattr  当前日志属性
     */
    void log(LogAttr::Ptr pattr);

    /**
     * @brief 添加日志输出器
     * @param[in] pappender
     */
    void addAppender(LogAppender::Ptr pappender);

    /**
     * @brief  删除日志输出器
     * @param[in] pappender
     */
    void delAppender(LogAppender::Ptr pappender);

    /**
     * @brief 获取日志器名称
     * @return std::string
     */
    std::string getName() const { return _name; }

    /**
     * @brief 设置日志器级别
     * @param level
     */
    void setLevel(const LogLevel::Level level) { _level = level; }

    /**
     * @brief 获取日志器级别
     * @return LogLevel::Level
     */
    LogLevel::Level getLevel() const { return _level; }

private:
    /// @brief 日志器名字 默认=“root”
    std::string _name{""};
    /// @brief 日志器级别
    LogLevel::Level _level{LogLevel::DEBUG};
    /// @brief 日志输出器合集
    std::list<LogAppender::Ptr> _appenders;
    /// @brief 输出器锁
    std::mutex _appendersMtx;
};


/**
 * @brief 日志属性包装器
 */
class LogAttrWrap
{
public:
    using Ptr = std::shared_ptr<LogAttrWrap>;

    LogAttrWrap(LogAttr::Ptr attr);

    ~LogAttrWrap();

    /**
     * @brief 获取日志字符流
     * @return std::stringstream&
     */
    std::stringstream& getSS() const { return _attr->getSS(); }

    /**
     * @brief 获取日志属性
     * @return LogAttr::Ptr
     */
    LogAttr::Ptr getAttr() const { return _attr; }

private:
    /// @brief 日志属性
    LogAttr::Ptr _attr;
};

/**
 * @brief 日志器管理
 */
class LogManager
{
public:
    /**
     * @brief 默认构造
     */
    LogManager();

    /**
     * @brief 默认析构
     */
    ~LogManager() = default;

    /**
     * @brief 获取默认日志器(root)
     * @return Logger::Ptr
     */
    Logger::Ptr getDefLogger() const;

    /**
     * @brief 添加日志器
     * @param[in] name
     * @param[in] logger
     */
    void addLogger(const std::string &name, Logger::Ptr logger);

    /**
     * @brief 添加日志器
     * @param[in] name
     */
    Logger::Ptr addLogger(const std::string &name);

    /**
     * @brief 获取日志器
     * @param[in] name
     * @return Logger::Ptr
     */
    Logger::Ptr getLogger(const std::string& name);

    /**
     * @brief 删除日志器
     * @param[in] name
     */
    void delLogger(const std::string& name);

private:
    /// @brief 日志器集合
    std::unordered_map<std::string, Logger::Ptr> _loggers;
    /// @brief 默认日志器
    Logger::Ptr _defaultLogger;
    /// @brief 日志器集合锁
    mutable std::mutex _loggersMtx;
};
/// @brief 日志管理单例
using LogManagerInstance = Singleton<LogManager>;


} // namespace kit_muduo
#endif