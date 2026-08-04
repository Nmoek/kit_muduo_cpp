/**
 * @file log_config.cpp
 * @brief 日志配置项
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-03 05:37:18
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log_config.h"
#include "base/log_formatter.h"

namespace kit_muduo {

namespace {

void ValidateFormatter(const std::string& pattern)
{
    auto formatter = std::make_shared<LogFormatter>(pattern);
    if(!formatter->valid())
    {
        throw ConfigError({},
            "invalid log formatter: " + formatter->error());
    }
}

} // namespace

LogConfig DefaultLogConfig()
{
    // TODO 忽然发现日志器太零散了需要整理一下
    // root
    LoggerConfig root;
    root.name = "root";
    root.level = LogLevel::DEBUG;
    root.formatter = kLogFormatDefaultPattern;
    root.appenders.push_back(LogAppenderConfig{
        .type = LogAppenderType::kStdout,
        .level = LogLevel::DEBUG,
    });
    // base
    LoggerConfig base;
    base.name = "base";
    base.level = LogLevel::INFO;
    base.formatter = kLogFormatDefaultPattern;
    base.appenders.push_back({
        .type = LogAppenderType::kStdout,
        .level = LogLevel::WARN,
    });
    base.appenders.push_back({
        .type = LogAppenderType::kFile,
        .level = LogLevel::INFO,
        .file_path = "log/base.log",
        .flush_threshold = 1024
    });
    // net
    LoggerConfig net;
    net.name = "net";
    net.level = LogLevel::INFO;
    net.formatter = kLogFormatDefaultPattern;
    net.appenders.push_back({
        .type = LogAppenderType::kStdout,
        .level = LogLevel::WARN,
    });
    net.appenders.push_back({
        .type = LogAppenderType::kFile,
        .level = LogLevel::INFO,
        .file_path = "log/net.log",
        .flush_threshold = 1024
    });
    // web
    LoggerConfig web;
    web.name = "web";
    web.level = LogLevel::DEBUG;
    web.formatter = kLogFormatDefaultPattern;
    web.appenders.push_back({
        .type = LogAppenderType::kStdout,
        .level = LogLevel::DEBUG,
    });
    web.appenders.push_back({
        .type = LogAppenderType::kFile,
        .level = LogLevel::DEBUG,
        .file_path = "log/web.log",
        .flush_threshold = 1024
    });
    // domain
    LoggerConfig domain;
    domain.name = "domain";
    domain.level = LogLevel::INFO;
    domain.formatter = kLogFormatDefaultPattern;
    domain.appenders.push_back({
        .type = LogAppenderType::kStdout,
        .level = LogLevel::INFO,
    });
    domain.appenders.push_back({
        .type = LogAppenderType::kFile,
        .level = LogLevel::INFO,
        .file_path = "log/domain.log",
        .flush_threshold = 1024
    });
    return LogConfig{{
        std::move(root),
        std::move(base),
        std::move(net),
        std::move(web),
        std::move(domain),
    }};
}

void ValidateLogConfig(const LogConfig& config)
{
    std::set<std::string> names;
    bool has_root = false;

    for(const auto& logger : config.loggers)
    {
        // 日志器名字不能为空
        if(logger.name.empty())
        {
            throw ConfigError({}, "logger name must not be empty");
        }
        // 日志器名字不能重复
        if(!names.emplace(logger.name).second)
        {
            throw ConfigError({}, "duplicate logger: " + logger.name);
        }
        // 日志器级别正确
        if(!LogLevel::IsValid(logger.level))
        {
            throw ConfigError({}, "invalid logger level: " + logger.name);
        }

        has_root = has_root || logger.name == "root";
        if(!logger.formatter.empty())
        {
            ValidateFormatter(logger.formatter);
        }

        for(const auto& appender : logger.appenders)
        {
            if(!LogLevel::IsValid(appender.level))
            {
                throw ConfigError({}, "invalid appender level: " + logger.name);
            }
            if(!appender.formatter.empty())
            {
                ValidateFormatter(appender.formatter);
            }
            if(appender.type == LogAppenderType::kFile
                && appender.file_path.empty())
            {
                throw ConfigError({},
                    "file appender requires file: " + logger.name);
            }
            if(appender.type == LogAppenderType::kStdout
                && !appender.file_path.empty())
            {
                throw ConfigError({},
                    "stdout appender must not set file: " + logger.name);
            }
        }
    }
    if(!has_root)
    {
        throw ConfigError({}, "logging configuration requires root logger");
    }
}


}