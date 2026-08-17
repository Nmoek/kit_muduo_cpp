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

#include <atomic>
#include <string>
#include <pthread.h>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>

#include "base/log_config.h"
#include "base/log_file_sink.h"
#include "base/log_level.h"
#include "base/log_appender.h"
#include "base/log_formatter.h"
#include "base/log_attr.h"
#include "base/time_stamp.h"
#include "base/util.h"

/********1、流式输出 ********/
#define LOG_LEVEL_OUT(logger, level, module) \
for(auto _logger = (logger); _logger && _logger->shouldLog(level); _logger.reset()) \
    kit_muduo::LogAttrWrap(std::make_shared<kit_muduo::LogAttr>(_logger, level, _logger->getName(), module, __FILE__, __LINE__, 0, kit_muduo::GetThreadTid(), kit_muduo::GetThreadPid(), kit_muduo::GetThreadName().c_str(), kit_muduo::TimeStamp::NowMs())).getSS()


#define KIT_DEBUG(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::DEBUG, module)
#define KIT_INFO(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::INFO, module)
#define KIT_WARN(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::WARN, module)
#define KIT_ERROR(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::ERROR, module)
#define KIT_FATAL(logger, module) LOG_LEVEL_OUT(logger, kit_muduo::LogLevel::FATAL, module)

/********2、变参输出********/
#define LOG_LEVEL_FMT_OUT(logger, level, module, fmt, ...) \
for(auto _logger = (logger); _logger && _logger->shouldLog(level); _logger.reset()) \
    kit_muduo::LogAttrWrap(std::make_shared<kit_muduo::LogAttr>(_logger, level, _logger->getName(), module, __FILE__, __LINE__, 0, kit_muduo::GetThreadTid(), kit_muduo::GetThreadPid(), kit_muduo::GetThreadName().c_str(), kit_muduo::TimeStamp::NowMs())).getAttr()->format(fmt, ##__VA_ARGS__ )

#define KIT_FMT_DEBUG(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::DEBUG, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_INFO(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::INFO, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_WARN(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::WARN, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_ERROR(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::ERROR, module, fmt, ##__VA_ARGS__)
#define KIT_FMT_FATAL(logger, module, fmt, ...) LOG_LEVEL_FMT_OUT(logger, kit_muduo::LogLevel::FATAL, module, fmt, ##__VA_ARGS__)


/********3、全局日志器操作********/
#define KIT_ROOT_LOGGER() \
    kit_muduo::LogManager::GetInstance().getRootLogger()

#if MUDUO_LOG_CACHE_MODULE_LOGGER
#define KIT_LOGGER(NAME) \
    kit_muduo::log_detail::GetLoggerHelper(NAME)
#else
#define KIT_LOGGER(NAME) \
    kit_muduo::LogManager::GetInstance().getLogger(NAME)
#endif


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
     * @brief 清空日志输出器
     */
    void clearAppender();

    /**
     * @brief 获取日志器名称
     * @return std::string
     */
    std::string getName() const { return name_; }

    /**
     * @brief 设置日志器级别
     * @param level
     */
    void setLevel(const LogLevel::Level level) { level_.store(level); }

    /**
     * @brief 获取日志器级别
     * @return LogLevel::Level
     */
    LogLevel::Level getLevel() const { return static_cast<LogLevel::Level>(level_.load()); }

    /**
     * @brief 小优化: 日志内容生成前就提前阻断
     * @return true 
     * @return false 
     */
    bool shouldLog(LogLevel::Level level) const;

private:
    friend class LogManager;
    friend class LogAttrWrap;

    enum class OutputRoute
    {
        kOwnAppenders,  // 已配置 输出器
        kRootFallback,  // 继承 root默认配置
        kMuted,         // 显示删除 输出器 静音状态
    };

    /**
     * @brief  委托关系 logger委托logAppender 进行实际的日志打印
     * @param[in] pattr  当前日志属性
     */
    void logUnchecked(LogAttr::Ptr attr);

    /**
     * @brief 批量替换输出器(事务性质)
     * @param appenders 
     */
    void replaceAppenders(std::list<LogAppender::Ptr> appenders);

    void useRootFallback(const Logger::Ptr& root);
    Logger::Ptr getRootFallback() const;

    bool shouldLogWithRootFallback(LogLevel::Level level) const;


private:
    /// @brief 日志器名字 默认=“root”
    std::string name_;
    /// @brief 日志器级别
    std::atomic_int32_t level_;
    /// @brief 日志输出器合集
    std::list<LogAppender::Ptr> appenders_;
    /// @brief 输出器锁
    mutable std::mutex appenders_mtx_;
    /// @brief 默认日志器弱引用
    std::weak_ptr<Logger> root_fallback_;
    /// @brief 当前日志器配置情况
    std::atomic<OutputRoute> output_route_{OutputRoute::kRootFallback};
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
    std::stringstream& getSS() const { return attr_->getSS(); }

    /**
     * @brief 获取日志属性
     * @return LogAttr::Ptr
     */
    LogAttr::Ptr getAttr() const { return attr_; }

private:
    /// @brief 日志属性
    LogAttr::Ptr attr_;
};

/**
 * @brief 日志器管理
 */
class LogManager
{
public:
    /**
     * @brief 日志器管理全局单例
     * @return LogManager& 
     */
    static LogManager& GetInstance();

    /**
     * @brief 默认析构
     */
    ~LogManager() = default;

    /**
     * @brief 获取默认日志器(root)
     * @return Logger::Ptr
     */
    Logger::Ptr getRootLogger() const;

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
     * @brief 获取日志器(带锁)
     * @param[in] name
     * @return Logger::Ptr
     */
    Logger::Ptr getLogger(const std::string& name);

    /**
     * @brief BUG 删除日志器，这个语义不存在
     * @param[in] name
     */
    // void delLogger(const std::string& name);

    /**
     * @brief 日志配置应用
     * @param config 
     */
    void applyConfig(const LogConfig &config);

    /**
     * @brief 获取日志文件管理对象
     * @param path 
     * @return LogFileSink::Ptr 
     */
    LogFileSink::Ptr acquireFileSink(const std::string &file_path);
public:
    /// @brief 静态日志器获取动作集合
    // static const std::unordered_map<std::string_view, std::pair<std::function<Logger::Ptr()>, LoggerConfig> > kGetLoggerFuncs;

private:
    /**
     * @brief 默认构造
     */
    LogManager();

    Logger::Ptr findOrCreateLoggerUnLocked(const std::string& name);


private:
    /// @brief 默认日志器
    const Logger::Ptr root_logger_;
    /// @brief 日志器集合
    std::unordered_map<std::string, Logger::Ptr> loggers_;
    /// @brief 日志器集合锁
    mutable std::mutex loggers_mtx_;
    /// @brief 日志文件管理
    LogFileSinkRegister file_register_;
};
/// @brief 日志管理单例
#define LOGMANAGER_INSTANCE() (LogManager::GetInstance())

namespace log_detail {

// TODO 需要改造
Logger::Ptr GetBaseLogger();
Logger::Ptr GetNetLogger();
Logger::Ptr GetWebLogger();
Logger::Ptr GetDomainLogger();

inline Logger::Ptr GetLoggerHelper(std::string_view name)
{
    // TODO 读写分离思想
#define XX(NAME, FUNC) \
    if(#NAME == name) { return FUNC(); }

    XX(base, GetBaseLogger)
    XX(net, GetNetLogger)
    XX(web, GetWebLogger)
    XX(domain, GetDomainLogger)
#undef XX
    // 退化为慢路径
    return kit_muduo::LogManager::GetInstance().getLogger(std::string{std::move(name)});
}

} // namespce log_detail


} // namespace kit_muduo
#endif
