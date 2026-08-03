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
    LoggerConfig root;
    root.name = "root";
    root.level = LogLevel::DEBUG;
    root.formatter = kLogFormatDefaultPattern;
    root.appenders.push_back(LogAppenderConfig{
        .type = LogAppenderType::kStdout,
        .level = LogLevel::DEBUG,
    });
    return LogConfig{{std::move(root)}};
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