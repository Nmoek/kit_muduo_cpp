/**
 * @file app_config.h
 * @brief 整体服务配置加载处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-02 18:56:58
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_APP_CONFIG_H__
#define __KIT_APP_CONFIG_H__

#include "base/config.h"
#include "base/config_context.h"
#include "base/lexical_cast.h"
#include "base/log_config.h"
#include "base/thread_pool.h"
#include "base/util.h"

#include <optional>
#include <unordered_map>

namespace kit_app {

using AppConfigRegistry  = kit_muduo::YamlConfig;

template<typename T>
using AppConfigVar = AppConfigRegistry::Var<T>;


/**
 * @brief 应用整体配置项
 */
struct AppConfigVars
{
    struct //systerm
    {
        struct { //http
            AppConfigVar<std::string>::Ptr host;
            AppConfigVar<uint16_t>::Ptr port;
            AppConfigVar<std::string>::Ptr static_root_path;
            AppConfigVar<int32_t>::Ptr io_threads;
        }http;

        struct { //business
            AppConfigVar<int32_t>::Ptr max_threads;
            AppConfigVar<int32_t>::Ptr max_task_queue;
            AppConfigVar<int32_t>::Ptr thread_idle_seconds;
            AppConfigVar<int32_t>::Ptr submit_timeout_ms;
        }business;

        // log
        AppConfigVar<kit_muduo::LogConfig>::Ptr log;

        struct //sqlite_db
        {
            AppConfigVar<std::string>::Ptr path;
            AppConfigVar<size_t>::Ptr pool_capacity;
            AppConfigVar<int32_t>::Ptr busy_timeout_ms;
            AppConfigVar<int32_t>::Ptr synchronous;
            AppConfigVar<bool>::Ptr sync_schema;
        }sqlite_db;
    }system;

    struct //work
    {
        struct{ // runtime
            AppConfigVar<size_t>::Ptr loop_capacity;

        }runtime;

        struct{ //interaction
            AppConfigVar<size_t>::Ptr queue_capacity;
            AppConfigVar<int64_t>::Ptr stop_drain_timeout_ms;
            
            AppConfigVar<size_t>::Ptr capture_max_text_bytes;
            AppConfigVar<size_t>::Ptr capture_max_hex_bytes;
            AppConfigVar<size_t>::Ptr capture_max_binary_attachment_bytes;
        }interaction;

    }work;

};

using Environment = std::unordered_map<std::string, std::string>;

struct AppConfigLoadInput
{
    std::optional<std::string> yaml_file;
    Environment environment;
};


template<class ConfigType>
AppConfigVars RegisterAppConfigVars(ConfigType& config)
{
    AppConfigVars vars;
#define XX(NODE_NAME, NODE_DEFAULT, NODE_DESC) \
    vars.NODE_NAME = config.lookAndCreate(#NODE_NAME, NODE_DEFAULT, NODE_DESC)

    XX(system.http.host, std::string("0.0.0.0"), "background HTTP bind address");
    XX(system.http.port, static_cast<uint16_t>(5555), "background HTTP bind port");
    XX(system.http.static_root_path, std::string("web"), "background static web resource root");
    XX(system.http.io_threads, 4, "HTTP I/O thread count");

    XX(system.business.max_threads, kit_muduo::ThreadPool::kDefaultMaxThread, "bussiness thread pool max threads");
    XX(system.business.max_task_queue, kit_muduo::ThreadPool::kDefaultMaxTaskQueue, "bussiness thread pool max task queue");
    XX(system.business.thread_idle_seconds, kit_muduo::ThreadPool::kDefaultMaxIdleInterval, "bussiness worker thread idle seconds");
    XX(system.business.submit_timeout_ms, 300, "bussiness thread pool submit timeout");

    XX(system.log, kit_muduo::DefaultLogConfig(), "loggers config");


    XX(system.sqlite_db.path, std::string("kit.sqlite"), "SQLite DB path");
    XX(system.sqlite_db.pool_capacity, static_cast<size_t>(20), "SQLite DB connect pool capacity");
    XX(system.sqlite_db.busy_timeout_ms, static_cast<int32_t>(3000), "SQLite writer busy timeout");
    XX(system.sqlite_db.synchronous, static_cast<int32_t>(1), "SQLite synchronous pragma");
    XX(system.sqlite_db.sync_schema, true, "SQLite sync schema");

    XX(work.runtime.loop_capacity, static_cast<size_t>(100), "runtime lease loop capacity");

    XX(work.interaction.queue_capacity, static_cast<size_t>(4096), "interaction queue capacity");
    XX(work.interaction.stop_drain_timeout_ms, static_cast<int64_t>(1000), "interaction drain timeout");
    XX(work.interaction.capture_max_text_bytes, static_cast<size_t>(64*1024), "interaction text capture limit");
    XX(work.interaction.capture_max_hex_bytes, static_cast<size_t>(64*1024), "interaction hex capture limit");
    XX(work.interaction.capture_max_binary_attachment_bytes, static_cast<size_t>(5*1024*1024), "interaction binary capture limit");
#undef XX

    return vars;
}

namespace app_config_detail {

inline kit_muduo::ConfigContext EnvContext(
    const std::string& name,
    const std::string& path)
{
    return kit_muduo::ConfigContext{
        .source = "env:" + name,
        .node_path = path,
    };
}

template<class T>
T ParseEnvironmentValue(
    const std::string& name,
    const std::string& path,
    const std::string& value)
{
    const auto context = EnvContext(name, path);
    if(value.empty())
    {
        throw kit_muduo::ConfigError(context,
            "environment variable must not be empty");
    }

    try
    {
        return kit_muduo::LexicalCast<std::string, T>{}(value);
    }
    catch(const kit_muduo::BadLexicalCast& error)
    {
        throw kit_muduo::ConfigError(context,
            "invalid environment variable value: " + error.reason());
    }
}

template<class T, class Batch>
void PrepareIfPresent(
    Batch& batch,
    const Environment& environment,
    const char* name,
    const char* path,
    const typename AppConfigVar<T>::Ptr& variable)
{
    const auto it = environment.find(name);
    if(it == environment.end())
    {
        return;
    }

    batch.prepareValue(variable,
        ParseEnvironmentValue<T>(name, path, it->second),
        EnvContext(name, path));
}

/**
 * @brief 判断路径指向是否存在且是否是文件类型
 * @param path 
 * @return true 
 * @return false 
 */
inline bool IsExistsConfigPath(std::filesystem::path path)
{
    const std::string& file_path{path.string()};
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if(error)
    {
        throw kit_muduo::ConfigError(kit_muduo::ConfigContext{
            .source = file_path,
        }, "cannot inspect config file: " + error.message());
    }

    if(exists)
    {
        const bool regular_file = std::filesystem::is_regular_file(path, error);
        if(error)
        {
            throw kit_muduo::ConfigError(kit_muduo::ConfigContext{
                .source = file_path,
            }, "cannot inspect config file type: " + error.message());
        }
        if(!regular_file)
        {
            throw kit_muduo::ConfigError(kit_muduo::ConfigContext{
                .source = file_path,
            }, "config file path must refer to a regular file");
        }

        return true;
    }

    return false;

}

} // namespace app_config_detail

void ValidateAppConfigVars(const AppConfigVars& vars);

/**
 * @brief 缺失时将默认应用配置写入指定 YAML 文件
 */
void WriteDefaultAppConfigFileIfMissing(
    const std::filesystem::path &path,
    const std::string& yaml_text);


template<class Batch>
void PrepareEnvironmentOverrides(
    Batch& batch,
    const AppConfigVars& vars,
    const Environment& environment)
{
    app_config_detail::PrepareIfPresent<std::string>(batch, environment,
        "KIT_HTTP_HOST", "system.http.host", vars.system.http.host);
    app_config_detail::PrepareIfPresent<uint16_t>(batch, environment,
        "KIT_HTTP_PORT", "system.http.port", vars.system.http.port);
    app_config_detail::PrepareIfPresent<std::string>(batch, environment,
        "KIT_HTTP_STATIC_ROOT_PATH", "system.http.static_root_path", vars.system.http.static_root_path);
    app_config_detail::PrepareIfPresent<std::string>(batch, environment,
        "KIT_DB_PATH", "system.sqlite_db.path", vars.system.sqlite_db.path);
    app_config_detail::PrepareIfPresent<size_t>(batch, environment,
        "KIT_DB_POOL_CAPACITY", "system.sqlite_db.pool_capacity",
        vars.system.sqlite_db.pool_capacity);
    app_config_detail::PrepareIfPresent<int32_t>(batch, environment,
        "KIT_DB_BUSY_TIMEOUT_MS", "system.sqlite_db.busy_timeout_ms",
        vars.system.sqlite_db.busy_timeout_ms);
}


template<class ConfigType>
void InitAppConfig(
    ConfigType& config,
    const AppConfigVars& vars,
    const AppConfigLoadInput& input)
{
    if(config.isFrozen())
    {
        throw std::logic_error("application configuration is already initialized");
    }

    // 配置文件加载
    if(input.yaml_file.has_value())
    {
        const std::filesystem::path path(*input.yaml_file);

        // 指向文件不存在 创建并写入
        if(!app_config_detail::IsExistsConfigPath(path))
        {
            WriteDefaultAppConfigFileIfMissing(path,
                ConfigType::ToString(config.buildNodeTree()));
        }
        else // 存在则从文件加载
        {
            config.load(*input.yaml_file, kit_muduo::ConfigLoadPolicy::kReject);
        }
    }

    // 环境变量加载
    config.load([&](auto &batch){
        PrepareEnvironmentOverrides(batch, vars, input.environment);
    });

    // 配置项校验
    ValidateAppConfigVars(vars);
    // 配置冻结
    config.freeze();
}

/**
 * @brief 整体服务配置加载入口
 */
void InitGlobalAppConfig();


const AppConfigVars& GetAppConfigVars();

// system
#define APP_CONFIG_VARS_SYSTEM(VAR) \
    GetAppConfigVars().system.VAR
#define APP_CONFIG_VARS_SYSTEM_HTTP(VAR) \
    GetAppConfigVars().system.http.VAR
#define APP_CONFIG_VARS_SYSTEM_BUSINESS(VAR) \
    GetAppConfigVars().system.business.VAR
#define APP_CONFIG_VARS_SYSTEM_LOG() \
    GetAppConfigVars().system.log
#define APP_CONFIG_VARS_SYSTEM_SQLITE_DB(VAR) \
    GetAppConfigVars().system.sqlite_db.VAR

// work
#define APP_CONFIG_VARS_WORK(VAR) \
    GetAppConfigVars().work.VAR
#define APP_CONFIG_VARS_WORK_RUNTIME(VAR) \
    GetAppConfigVars().work.runtime.VAR
#define APP_CONFIG_VARS_WORK_INTRAC(VAR) \
    GetAppConfigVars().work.interaction.VAR

/************整体应用配置获取****** */

} // kit_app
#endif //__KIT_APP_CONFIG_H__
