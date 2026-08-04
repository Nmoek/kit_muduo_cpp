/**
 * @file log.cpp
 * @brief 日志器
 * @author Kewin Li
 * @version 1.0
 * @date 2025-04-17 19:32:10
 * @copyright Copyright (c) 2025 Kewin Li
 */

#include "base/log.h"
#include "base/log_appender.h"
#include "base/log_config.h"
#include "base/log_formatter.h"
#include <iostream>
#include <mutex>

namespace kit_muduo {

namespace {

LogFormatter::Ptr MakeFormatter(const std::string &pattern)
{
    auto formatter = std::make_shared<LogFormatter>(pattern.empty() ? kLogFormatDefaultPattern : pattern);
    if(!formatter->valid())
    {
        throw std::invalid_argument(
            "invalid log formatter: " + formatter->error());
    }
    return formatter;
}


LogAppender::Ptr MakeAppender(const LogAppenderConfig &config, const LogFormatter::Ptr &formatter, const std::string& logger_name, size_t appender_index)
{
    LogAppender::Ptr appender;
    if(LogAppenderType::kStdout == config.type)
    {
        auto c = std::make_shared<ConsoleAppender>();
        c->setLevel(config.level);
        c->setFormatter(config.formatter.empty() ? formatter : MakeFormatter(config.formatter));
        appender = std::move(c);
    }
    else
    {
        auto f = std::make_shared<FileAppender>(config.file_path);
        f->setLevel(config.level);
        f->setFormatter(config.formatter.empty() ? formatter : MakeFormatter(config.formatter));
        f->setFlushThreshold(config.flush_threshold);

        // File类型输出器需要确保路径能够打开
        std::string open_error;
        if(!f->openForAppend(&open_error))
        {
            throw ConfigError(ConfigContext{
                .source = "runtime",
                .node_path = "system.logs[" + logger_name
                    + "].appenders["
                    + std::to_string(appender_index)
                    + "].file",
            }
            ,open_error);
        }

        appender = std::move(f);

    }

    return appender;
}

struct PreparedLogger
{
    LoggerConfig config;
    LogFormatter::Ptr formatter;
    std::list<LogAppender::Ptr> appenders;
};

}

Logger::Logger(const std::string &name)
    :name_(name)
    ,level_(LogLevel::DEBUG)
{

}

/*
             |  _level(INFO) <= level都能进行输出
             v
    DEBUG   INFO   WARN  ERROR   FATAL
            INFO
*/
void Logger::log(LogAttr::Ptr attr)
{
    if(!attr || !shouldLog(attr->getLevel()))
    {
        return;
    }

    if(auto root = GetRootFallback())
    {
        root->log(std::move(attr));
        return;
    }

    std::unique_lock<std::mutex> lock(appenders_mtx_);
    for(auto &a : appenders_)
    {
        if(a)
        {
            a->append(attr);
        }
    }

}


void Logger::addAppender(LogAppender::Ptr pappender)
{
    std::unique_lock<std::mutex> lock(appenders_mtx_);
    root_fallback_.reset();
    output_route_ = OutputRoute::kOwnAppenders;
    appenders_.push_back(std::move(pappender));
}

void Logger::delAppender(LogAppender::Ptr pappender)
{
    std::unique_lock<std::mutex> lock(appenders_mtx_);

    for(auto it = appenders_.begin();it != appenders_
    .end();++it)
    {
        if(*it == pappender)
        {
            appenders_.erase(it);
            return;
        }
    }
    return;
}

bool Logger::shouldLog(LogLevel::Level level) const
{
    std::lock_guard<std::mutex> lock(appenders_mtx_);

    // 静音状态不输出
    if(OutputRoute::kMuted == output_route_)
    {
        return false;
    }

    if(OutputRoute::kRootFallback == output_route_)
    {
        auto root = root_fallback_.lock();
        return root && root->shouldLog(level);
    }
    return level >= getLevel();
}

void Logger::ReplaceAppenders(std::list<LogAppender::Ptr> appenders)
{
    std::lock_guard<std::mutex> lock(appenders_mtx_);

    root_fallback_.reset();
    output_route_ = appenders.empty()
        ? OutputRoute::kMuted
        : OutputRoute::kOwnAppenders;
    appenders_ = std::move(appenders);
}

void Logger::UseRootFallback(const Logger::Ptr& root)
{
    if(!root || root.get() == this)
    {
        throw std::logic_error("root fallback requires another logger");
    }

    std::lock_guard<std::mutex> lock(appenders_mtx_);

    appenders_.clear();
    
    root_fallback_ = root;
    output_route_ = OutputRoute::kRootFallback;
}

Logger::Ptr Logger::GetRootFallback() const
{
    std::lock_guard<std::mutex> lock(appenders_mtx_);

    if(OutputRoute::kRootFallback != output_route_)
    {
        return nullptr;
    }
    return root_fallback_.lock();
}


/**************LogAttrWrap****************/

LogAttrWrap::LogAttrWrap(LogAttr::Ptr attr)
    :attr_(attr)
{ }

LogAttrWrap::~LogAttrWrap()
{
    if(attr_->getLogger()->getLevel() <= attr_->getLevel())
    {
        attr_->getLogger()->log(attr_);
    }
}



/**************LogManager****************/


LogManager& LogManager::GetInstance()
{
    static LogManager m;
    return m;
}

LogManager::LogManager()
    :root_logger_(std::make_shared<Logger>("root"))
{
    loggers_.emplace("root", root_logger_);

    applyConfig(DefaultLogConfig());
}


Logger::Ptr LogManager::getRootLogger() const
{
    return root_logger_;
}

void LogManager::addLogger(const std::string &name, Logger::Ptr logger)
{
    std::unique_lock<std::mutex> lock(loggers_mtx_);
    loggers_[name] = logger;
}

Logger::Ptr LogManager::addLogger(const std::string &name)
{
    std::unique_lock<std::mutex> lock(loggers_mtx_);
    auto logger = std::make_shared<Logger>(name);
    logger->UseRootFallback(root_logger_);
    loggers_[name] = logger;
    return logger;
}

Logger::Ptr LogManager::getLogger(const std::string& name)
{
    std::lock_guard<std::mutex> lock(loggers_mtx_);
    return findOrCreateLoggerUnLocked(name);
}


Logger::Ptr LogManager::findOrCreateLoggerUnLocked(const std::string& name)
{
    if("root" == name)
    {
        return root_logger_;
    }

    auto it = loggers_.find(name);
    if(it != loggers_.end())
    {
        return it->second;
    }

    auto logger = std::make_shared<Logger>(name);
    logger->UseRootFallback(root_logger_);
    loggers_.emplace(name, logger);
    return logger;
}

void LogManager::applyConfig(const LogConfig &config)
{
    ValidateLogConfig(config);

    std::unordered_map<std::string, PreparedLogger> prepared;
    size_t i = 0;

    // 统一创建输出器
    for(auto &config : config.loggers)
    {
        PreparedLogger item;
        item.config = config;
        item.formatter = MakeFormatter(config.formatter);
        for(auto &appender_config : config.appenders)
        {
            item.appenders.push_back(MakeAppender(appender_config, item.formatter, config.name, i++));
        }
        prepared.emplace(config.name, std::move(item));
    }

    std::lock_guard<std::mutex> lock(loggers_mtx_);

    auto &root_item = prepared.at("root");
    root_logger_->setLevel(root_item.config.level);
    root_logger_->ReplaceAppenders(std::move(root_item.appenders));

    // 输出器创建成功后批量替换
    for(auto &[name, item] : prepared)
    {
        if("root" == name)
        {
            continue;
        }

        auto logger = findOrCreateLoggerUnLocked(name);
        logger->setLevel(item.config.level);
        logger->ReplaceAppenders(std::move(item.appenders));
    }

    // 注意 日志器管理不存在删除语义
    // 被移除的专属配置改为 root fallback，同名 logger 保持对象身份。
    for(auto &[name, logger] : loggers_)
    {
        if(name != "root" && prepared.find(name) == prepared.end())
        {
            std::cerr << "============ not define: " << name << "==================\n";
            logger->UseRootFallback(root_logger_);
        }
    }

}


} // namespace kit

