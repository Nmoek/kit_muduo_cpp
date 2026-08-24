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
#include <limits>

namespace kit_muduo {

namespace {

/**************日志配置限制值**************/
constexpr uint64_t kMinFlushThreshold = 1 * 1024;
constexpr uint64_t kMaxFlushThreshold = 10 * 1024 * 1024;
constexpr uint64_t kMinFlushIntervalMs = 0;
constexpr uint64_t kMaxFlushIntervalMs = 30 * 1000;
constexpr uint32_t kMinRetateBytes = 50 * 1024 * 1024;
constexpr uint32_t kMaxRetateBytes = 300 * 1024 * 1024;

constexpr uint32_t kMinBackupFiles = 0;
constexpr uint32_t kMaxBackupFiles = 10;

constexpr uint32_t kMinRecordBytes = 1 * 1024;
constexpr uint32_t kMaxRecordBytes = 256 * 1024;
/**************日志配置限制值**************/

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

// TODO 日志器默认配置应该放在 日志管理构造的开头 
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
    });

    return LogConfig{
        // TODO 日志文件配置
        .file{

        },
        .loggers{
        std::move(root),
        std::move(base),
        std::move(net),
        std::move(web),
        std::move(domain),
    }};
}

void ValidateLogConfig(const LogConfig& config)
{
    std::unordered_set<std::string> names;
    bool has_root = false;

    const LogFileConfig& file = config.file;

    if(config.mode != LogMode::kSync && config.mode != LogMode::kAsync)
    {
        throw ConfigError({},
            "log mode invalid {'sync', 'async'}");
    }

    if(config.max_record_bytes < kMinRecordBytes
        || config.max_record_bytes > kMaxRecordBytes)
    {
        throw ConfigError({}, "log file max record bytes invalid [1, 256] KB");
    }


    //  file配置限制
    if(file.flush_threshold < kMinFlushThreshold
        || file.flush_threshold > kMaxFlushThreshold)
    {
        throw ConfigError({},
            "log file flush threshold invalid [1 KiB, 10 MiB]");
    }
    
    if(file.flush_interval_ms <= kMinFlushIntervalMs 
        || file.flush_interval_ms > kMaxFlushIntervalMs)
    {
        throw ConfigError({},
            "log file flush interval invalid (0, 30,000] ms");
    }

    if(!LogLevel::IsValid(file.flush_on_level))
    {
        throw ConfigError({},
            "log file flush on level invalid");
    }


    if(file.rotate_max_bytes < kMinRetateBytes
        || file.rotate_max_bytes > kMaxRetateBytes)
    {
        throw ConfigError({}, "log file rotate bytes invalid [50, 300] MB");
    }

    // 允许为0 说明不轮转
    if(file.rotate_max_backup_files < kMinBackupFiles
        || file.rotate_max_backup_files > kMaxBackupFiles)
    {
        throw ConfigError({}, "log file max backup files invalid [1, 10]");
    }



    // loggers校验
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
