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

#include <initializer_list>
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

struct LogFileConfig
{
    /// @brief 日志写入刷新阈值 1MB
    uint64_t flush_threshold{1 * 1024 * 1024};
    /// @brief TODO 文件大小轮转阈值
    uint64_t rotate_max_bytes{300 * 1024 * 1024};
    /// @brief TODO 文件轮转数量
    int32_t max_backup_files{5};
    /// @brief TODO 文件是否进行压缩处理
    bool compress_rotated{true};
};

struct LogConfig
{
    LogFileConfig file;
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
struct ConfigCodec<LogFileConfig, Policy>
{
    using Node = typename Policy::Node;

    static LogFileConfig Decode(const Node &node)
    {
        LogFileConfig result;

        log_config_detail::DecodeObject<Policy>(node,
        {"flush_threshold", "rotate_max_bytes", "max_backup_files", "compress_rotated"},
        [&](const std::string& field, const Node& child) {

            if(field == "flush_threshold")
            {
                result.flush_threshold = ConfigCodec<uint64_t, Policy>::Decode(child);
            }
            else if(field == "rotate_max_bytes")
            {
                result.rotate_max_bytes = ConfigCodec<uint64_t, Policy>::Decode(child);
            }
            else if(field == "max_backup_files")
            {
                result.max_backup_files = ConfigCodec<int32_t, Policy>::Decode(child);
            }
            else if(field == "compress_rotated")
            {
                result.compress_rotated = ConfigCodec<bool, Policy>::Decode(child);
            }
        });

        return result;
    }

    static Node Encode(const LogFileConfig &value)
    {
        auto node = Policy::MakeMap();

        Policy::Put(node, "flush_threshold",
            ConfigCodec<uint64_t, Policy>::Encode(value.flush_threshold));
        Policy::Put(node, "rotate_max_bytes",
            ConfigCodec<uint64_t, Policy>::Encode(value.rotate_max_bytes));
        Policy::Put(node, "max_backup_files",
            ConfigCodec<int32_t, Policy>::Encode(value.max_backup_files));
        Policy::Put(node, "compress_rotated",
            ConfigCodec<bool, Policy>::Encode(value.compress_rotated));
        
        return node;
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
        {"file", "loggers"},
        [&](const std::string& field, const Node& child) {

            if(field == "file")
            {
                result.file =  ConfigCodec<LogFileConfig, Policy>::Decode(child);
            }
            else if(field == "loggers")
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

        Policy::Put(node, "file",
            ConfigCodec<LogFileConfig, Policy>::Encode(value.file));
        Policy::Put(node, "loggers",
            ConfigCodec<std::vector<LoggerConfig>, Policy>::Encode(value.loggers));
        return node;
    }
};


}
#endif //__KIT_LOG_CONFIG_H__