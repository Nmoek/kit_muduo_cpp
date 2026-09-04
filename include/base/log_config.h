/**
 * @file log_config.h
 * @brief 日志配置项
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-03 04:56:04
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_LOG_CONFIG_H__
#define __KIT_LOG_CONFIG_H__

#include "base/config_context.h"
#include "base/log_level.h"
#include "base/config_codec.h"

#include <cctype>
#include <initializer_list>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace kit_muduo {

enum class LogAppenderType { kStdout, kFile };


struct LogAppenderConfig
{
    LogAppenderType type{LogAppenderType::kStdout};
    LogLevel::Level level{LogLevel::INFO};
    std::string formatter;

    // 只有输出对象是 file 才需要这些字段
    std::string file_path;
};

struct LoggerConfig
{
    std::string name;
    LogLevel::Level level{LogLevel::INFO};
    std::string formatter;
    std::vector<LogAppenderConfig> appenders;
};

enum class LogInitFailurePolicy { kFail, kStderr };

inline LogInitFailurePolicy LogInitFailurePolicyFromString(std::string value)
{
    std::for_each(value.begin(), value.end(), [](auto &&c){
        c = std::tolower(c);
    });

    if("fail" == value) { return LogInitFailurePolicy::kFail; }
    if("stderr" == value) { return LogInitFailurePolicy::kStderr; }

    throw std::invalid_argument("log init failure policy invalid");
}

inline std::string LogInitFailurePolicyToString(const LogInitFailurePolicy value)
{
    if(LogInitFailurePolicy::kFail == value) { return "fail"; }
    if(LogInitFailurePolicy::kStderr == value) { return "stderr"; }
    throw std::invalid_argument("log init failure policy invalid");
}

inline bool IsLogInitFailurePolicyValid(LogInitFailurePolicy value)
{
    return LogInitFailurePolicy::kFail <= value && value <= LogInitFailurePolicy::kStderr;
}


struct LogFileConfig
{
    /// @brief 日志写入刷新阈值 默认1MB
    uint64_t flush_threshold{1 * 1024 * 1024};
    /// @brief 日志写入刷新时间间隔 默认3s
    uint64_t flush_interval_ms{3000};
    /// @brief 日志刷新等级(碰到就立即刷新) 默认ERROR
    LogLevel::Level flush_on_level{LogLevel::ERROR};
    /// @brief 文件大小轮转阈值
    uint64_t rotate_max_bytes{300 * 1024 * 1024};
    /// @brief 文件轮转数量
    uint32_t rotate_max_backup_files{5};
    /// @brief 文件是否进行压缩处理
    bool compress_rotated{true};
    /// @brief 日志初始化策略 默认启动失败就终止程序
    LogInitFailurePolicy init_failure_policy{LogInitFailurePolicy::kFail};
};

/// @brief 异步处理配置
struct LogAsyncConfig
{
    /// @brief 队列元素个数容量
    size_t queue_capacity{8192};
    /// @brief 队列总字节数
    uint64_t max_queue_bytes{16u * 1024u * 1024u};
    /// @brief 停止时最多等待时间 单位ms
    uint64_t stop_drain_timeout_ms{3000};
};

struct LogConfig
{
    /// @brief 日志单条记录上限(超限阶段) 默认256K
    uint32_t max_record_bytes{256 * 1024};
    /// @brief 日志异步配置
    LogAsyncConfig async;
    /// @brief 日志文件写入配置
    LogFileConfig file;
    /// @brief 各个日志器等级、
    std::vector<LoggerConfig> loggers;
};

LogConfig DefaultLogConfig();

void ValidateLogConfig(const LogConfig& log_config);

namespace log_config_detail {

template<typename Policy, typename CallBack>
void DecodeObject(const typename Policy::Node &node, std::initializer_list<const char*> allowed, CallBack &&callback)
{
    using Node = typename Policy::Node;

    if(!Policy::IsMap(node))
    {
        throw ConfigError(Policy::Context(node), "expected object");
    }
    
    std::unordered_set<std::string> seen;
    Policy::VisitMap(node, 
        [&](const std::string &key, const Node& child) {

        if(!seen.emplace(key).second)
        {
            throw ConfigError(Policy::Context(child),
                "duplicate field: " + key);
        }

        auto it = std::find_if(allowed.begin(), allowed.end(),[&](const char* value) { 
            return key == value; 
        });
        if(allowed.end() == it)
        {
            throw ConfigError(Policy::Context(child),
                "unknown field: " + key);
        }

        callback(key, child);
    });
}


} // log_config_detail


template<class Policy>
struct ConfigCodec<LogAppenderType, Policy>
{
    using Node = typename Policy::Node;

    static LogAppenderType Decode(const Node& node)
    {
        const auto value = ConfigCodec<std::string, Policy>::Decode(node);

        if(value == "stdout") return LogAppenderType::kStdout;
        if(value == "file") return LogAppenderType::kFile;

        throw ConfigError(Policy::Context(node),
            "appender type must be stdout or file");
    }

    static Node Encode(LogAppenderType value)
    {
        switch(value)
        {
            case LogAppenderType::kStdout:
                return ConfigCodec<std::string, Policy>::Encode("stdout");
            case LogAppenderType::kFile:
                return ConfigCodec<std::string, Policy>::Encode("file");
        }
        throw ConfigError({}, "invalid appender type enum");
    }
};

template<class Policy>
struct ConfigCodec<LogLevel::Level, Policy>
{
    using Node = typename Policy::Node;

    static LogLevel::Level Decode(const Node& node)
    {

        try { 
            const auto value = ConfigCodec<std::string, Policy>::Decode(node);
            return LogLevel::FromString(value); 
            
        } catch(const std::exception& error){
            throw ConfigError(Policy::Context(node), error.what());
        }
    }

    static Node Encode(LogLevel::Level value)
    {
        if(!LogLevel::IsValid(value))
        {
            throw ConfigError({}, "invalid log level enum");
        }

        return ConfigCodec<std::string, Policy>::Encode(LogLevel::ToString(value));
    }
};

template<typename Policy>
struct ConfigCodec<LogAppenderConfig, Policy>
{
    using Node = typename Policy::Node;
    static LogAppenderConfig Decode(const Node& node)
    {
        LogAppenderConfig result;
        bool has_type = false;
        log_config_detail::DecodeObject<Policy>(node,
            {"type", "level", "formatter", "file_path"},
            [&](const std::string &key, const Node &child) {
                
            if(key == "type")
            {
                result.type = ConfigCodec<LogAppenderType, Policy>::Decode(child);
                has_type = true;
            }
            else if(key == "level")
            {
                result.level = ConfigCodec<LogLevel::Level, Policy>::Decode(child);
            }
            else if(key == "formatter")
            {
                result.formatter = ConfigCodec<std::string, Policy>::Decode(child);
            }
            else if(key == "file_path")
            {
                result.file_path = ConfigCodec<std::string, Policy>::Decode(child);
            }
        });

        if(!has_type)
        {
            throw ConfigError(Policy::Context(node),
                "logger appender requires type");
        }
        return result;
    }

    static Node Encode(const LogAppenderConfig& value)
    {
        auto node = Policy::MakeMap();

        Policy::Put(node, "type",
            ConfigCodec<LogAppenderType, Policy>::Encode(value.type));
        Policy::Put(node, "level",
            ConfigCodec<LogLevel::Level, Policy>::Encode(value.level));
        if(!value.formatter.empty())
        {
            Policy::Put(node, "formatter",
                ConfigCodec<std::string, Policy>::Encode(value.formatter));
        }
        if(LogAppenderType::kFile == value.type)
        {
            if(!value.file_path.empty())
            {
                Policy::Put(node, "file_path",
                    ConfigCodec<std::string, Policy>::Encode(value.file_path));
            }
        }

        return node;
    }
};

template<typename Policy>
struct ConfigCodec<LoggerConfig, Policy>
{
    using Node = typename Policy::Node;

    static LoggerConfig Decode(const Node &node)
    {
        LoggerConfig result;
        bool has_name = false;

        log_config_detail::DecodeObject<Policy>(node,
        {"name", "level", "formatter", "appenders"},
        [&](const std::string& field, const Node& child) {

            if(field == "name")
            {
                result.name = ConfigCodec<std::string, Policy>::Decode(child);
                has_name = true;
            }
            else if(field == "level")
            {
                result.level = ConfigCodec<LogLevel::Level, Policy>::Decode(child);
            }
            else if(field == "formatter")
            {
                result.formatter = ConfigCodec<std::string, Policy>::Decode(child);
            }
            else if(field == "appenders")
            {
                result.appenders = ConfigCodec<
                    std::vector<LogAppenderConfig>, Policy>::Decode(child);
            }
        });
        if(!has_name || result.name.empty())
        {
            throw ConfigError(Policy::Context(node),
                "logger must requires name");
        }
        return result;
    }

    static Node Encode(const LoggerConfig &value)
    {
        auto node = Policy::MakeMap();
        if(value.name.empty())
        {
            throw ConfigError({},
                "logger must requires name");
        }
        Policy::Put(node, "name",
            ConfigCodec<std::string, Policy>::Encode(value.name));
        Policy::Put(node, "level",
            ConfigCodec<LogLevel::Level, Policy>::Encode(value.level));
        Policy::Put(node, "appenders",
            ConfigCodec<std::vector<LogAppenderConfig>, Policy>::Encode(value.appenders));
        Policy::Put(node, "formatter",
            ConfigCodec<std::string, Policy>::Encode(value.formatter));
        
        return node;
    }
};

template<typename Policy>
struct ConfigCodec<LogAsyncConfig, Policy>
{
    using Node = typename Policy::Node;

    static LogAsyncConfig Decode(const Node &node)
    {
        LogAsyncConfig result;

        log_config_detail::DecodeObject<Policy>(node,
        {"queue_capacity", 
            "max_queue_bytes", 
            "stop_drain_timeout_ms"},
        [&](const std::string& field, const Node& child) {
        #define XX(NAME, TYPE) \
            if(#NAME == field) \
            { \
                result.NAME = ConfigCodec<TYPE, Policy>::Decode(child); \
            }

            XX(queue_capacity, size_t)
            XX(max_queue_bytes, uint64_t)
            XX(stop_drain_timeout_ms, uint64_t)

        #undef XX
        });

        return result;
    }

    static Node Encode(const LogAsyncConfig &value)
    {
        auto node = Policy::MakeMap();
    #define XX(NAME, TYPE) \
        Policy::Put(node, #NAME, ConfigCodec<TYPE, Policy>::Encode(value.NAME));

        XX(queue_capacity, size_t)
        XX(max_queue_bytes, uint64_t)
        XX(stop_drain_timeout_ms, uint64_t)

    #undef XX
        return node;
    }
};

template<typename Policy>
struct ConfigCodec<LogFileConfig, Policy>
{
    using Node = typename Policy::Node;

    static LogFileConfig Decode(const Node &node)
    {
        LogFileConfig result;

        log_config_detail::DecodeObject<Policy>(node,
        {"flush_threshold", 
            "flush_interval_ms",
            "flush_on_level",
            "rotate_max_bytes", 
            "rotate_max_backup_files", 
            "compress_rotated",
            "init_failure_policy"},
        [&](const std::string& field, const Node& child) {
        #define XX(NAME, TYPE) \
            if(#NAME == field) \
            { \
                result.NAME = ConfigCodec<TYPE, Policy>::Decode(child); \
            }

            XX(flush_threshold, uint64_t)
            XX(flush_interval_ms, uint64_t)
            XX(flush_on_level, LogLevel::Level)
            XX(rotate_max_bytes, uint64_t)
            XX(rotate_max_backup_files, int32_t)
            XX(compress_rotated, bool)
            XX(init_failure_policy, LogInitFailurePolicy)

        #undef XX
        });

        return result;
    }

    static Node Encode(const LogFileConfig &value)
    {
        auto node = Policy::MakeMap();
    #define XX(NAME, TYPE) \
        Policy::Put(node, #NAME, ConfigCodec<TYPE, Policy>::Encode(value.NAME));

        XX(flush_threshold, uint64_t)
        XX(flush_interval_ms, uint64_t)
        XX(flush_on_level, LogLevel::Level)
        XX(rotate_max_bytes, uint64_t)
        XX(rotate_max_backup_files, int32_t)
        XX(compress_rotated, bool)
        XX(init_failure_policy, LogInitFailurePolicy)
    #undef XX
        return node;
    }
};


template<class Policy>
struct ConfigCodec<LogInitFailurePolicy, Policy>
{
    using Node = typename Policy::Node;

    static LogInitFailurePolicy Decode(const Node& node)
    {

        try { 
            const auto value = ConfigCodec<std::string, Policy>::Decode(node);
            return LogInitFailurePolicyFromString(std::move(value)); 
            
        } catch(const std::exception& error){
            throw ConfigError(Policy::Context(node), error.what());
        }
    }

    static Node Encode(LogInitFailurePolicy value)
    {
        if(!IsLogInitFailurePolicyValid(value))
        {
            throw ConfigError({}, "invalid log init failure policy enum");
        }

        return ConfigCodec<std::string, Policy>::Encode(LogInitFailurePolicyToString(value));
    }
};



template<typename Policy>
struct ConfigCodec<LogConfig, Policy>
{
    using Node = typename Policy::Node;

    static LogConfig Decode(const Node &node)
    {
        LogConfig result;

        log_config_detail::DecodeObject<Policy>(node,
        {"max_record_bytes", "async", "file", "loggers"},
        [&](const std::string& field, const Node& child) {

            if("max_record_bytes" == field)
            {
                result.max_record_bytes =  ConfigCodec<uint32_t, Policy>::Decode(child);
            }
            if("async" == field)
            {
                result.async =  ConfigCodec<LogAsyncConfig, Policy>::Decode(child);
            }
            if("file" == field)
            {
                result.file =  ConfigCodec<LogFileConfig, Policy>::Decode(child);
            }
            else if("loggers" == field)
            {
                result.loggers = ConfigCodec<std::vector<LoggerConfig>, Policy>::Decode(child);
            }

        });

        return result;
    }

    static Node Encode(const LogConfig& value)
    {
        ValidateLogConfig(value);
        auto node = Policy::MakeMap();

        Policy::Put(node, "max_record_bytes",
            ConfigCodec<uint32_t, Policy>::Encode(value.max_record_bytes));
        Policy::Put(node, "async",
            ConfigCodec<LogAsyncConfig, Policy>::Encode(value.async));
        Policy::Put(node, "file",
            ConfigCodec<LogFileConfig, Policy>::Encode(value.file));
        Policy::Put(node, "loggers",
            ConfigCodec<std::vector<LoggerConfig>, Policy>::Encode(value.loggers));
        return node;
    }
};


}
#endif //__KIT_LOG_CONFIG_H__
