/**
 * @file app_config.cpp
 * @brief 整体服务配置加载处理
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-02 19:01:30
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "app_config.h"
#include "base/log_config.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace kit_muduo;

namespace kit_app {

namespace {

void ValidateStaticRoot(const std::string& configured_root)
{
    std::error_code error;
    auto root = std::filesystem::absolute(configured_root, error);
    if(error)
    {
        throw ConfigError(ConfigContext{
            .source = "effective",
            .node_path = "system.http.static_root_path"
        },"cannot resolve static root: " + error.message());
    }

    root = std::filesystem::weakly_canonical(root, error);
    if(error || !std::filesystem::is_directory(root, error) || error)
    {
        throw ConfigError(
        ConfigContext{
            .source = "effective",
            .node_path = "system.http.static_root_path"
        },"static root must be an accessible directory");
    }

}

std::optional<std::string> ReadProcessEnvironment(const char* name)
{
    const char* value = std::getenv(name);
    if(value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
}

AppConfigLoadInput ReadProductionAppConfigLoadInput()
{
    AppConfigLoadInput input;

    if(const auto config_file = ReadProcessEnvironment("KIT_CONFIG_FILE"))
    {
        if(config_file->empty())
        {
            throw ConfigError(app_config_detail::EnvContext(
                "KIT_CONFIG_FILE", ""),
                "environment variable must not be empty");
        }
        input.yaml_file = *config_file;
    }

    for(const char* name : {
            "KIT_HTTP_HOST",
            "KIT_HTTP_PORT",
            "KIT_DB_PATH",
            "KIT_DB_POOL_CAPACITY",
            "KIT_DB_BUSY_TIMEOUT_MS",
        })
    {
        if(const auto value = ReadProcessEnvironment(name))
        {
            input.environment.emplace(name, *value);
        }
    }

    return input;
}

AppConfigVars& RegisteredGlobalAppConfigVars()
{
    static AppConfigVars vars = RegisterAppConfigVars(
        AppConfigRegistry::Instance());
    return vars;
}

} //namespace 

void WriteDefaultAppConfigFileIfMissing(
    const std::string& file_path,
    const std::string& yaml_text)
{
    const std::filesystem::path path(file_path);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if(error)
    {
        throw ConfigError(ConfigContext{
            .source = file_path,
        }, "cannot inspect config file: " + error.message());
    }

    if(exists)
    {
        const bool regular_file = std::filesystem::is_regular_file(path, error);
        if(error)
        {
            throw ConfigError(ConfigContext{
                .source = file_path,
            }, "cannot inspect config file type: " + error.message());
        }
        if(!regular_file)
        {
            throw ConfigError(ConfigContext{
                .source = file_path,
            }, "config file path must refer to a regular file");
        }
        return;
    }

    const std::filesystem::path parent = path.parent_path();
    if(!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if(error)
        {
            throw ConfigError(ConfigContext{
                .source = file_path,
            }, "cannot create config directory: " + error.message());
        }
    }

    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if(!output.is_open())
    {
        const std::error_code open_error(errno, std::generic_category());
        const std::string reason = open_error.value() == 0
            ? "cannot create default config file"
            : "cannot create default config file: " + open_error.message();
        throw ConfigError(ConfigContext{
            .source = file_path,
        }, reason);
    }

    output << yaml_text;
    output.flush();
    if(!output.good())
    {
        throw ConfigError(ConfigContext{
            .source = file_path,
        }, "cannot write default config file");
    }
}

void ValidateAppConfigVars(const AppConfigVars& vars)
{
    const auto require = [](bool valid, 
        const std::string& path, 
        const std::string& reason) 
    {
        if(!valid)
        {
            throw ConfigError(ConfigContext{
                .source = "effective", 
                .node_path = path
            }, reason);
        }
    };

    const auto host = *vars.system.http.host->value();
    const auto port = *vars.system.http.port->value();
    const auto root = *vars.system.http.static_root_path->value();
    require(!host.empty(), "system.http.host", "host must not be empty");
    require(port != 0, "system.http.port", "port must be in range [1, 65535]");
    require(!root.empty(), "system.http.static_root_path",
        "static root must not be empty");
    require(*vars.system.http.io_threads->value() > 0,
        "system.http.io_threads", "I/O threads must be positive");
    require(*vars.system.business.max_threads->value() >= 0,
        "system.business.max_threads", "must be zero or positive");
    require(*vars.system.business.max_task_queue->value() >= 0,
        "system.business.max_task_queue", "must be zero or positive");
    require(*vars.system.business.thread_idle_seconds->value() > 0,
        "system.business.thread_idle_seconds", "must be positive");
    require(*vars.system.business.submit_timeout_ms->value() >= 0,
        "system.business.submit_timeout_ms", "must not be negative");

    require(!vars.system.sqlite_db.path->value()->empty(),
        "system.sqlite_db.path", "database path must not be empty");
    require(*vars.system.sqlite_db.pool_capacity->value() > 0,
        "system.sqlite_db.pool_capacity", "pool capacity must be positive");
    require(*vars.system.sqlite_db.busy_timeout_ms->value() >= 0,
        "system.sqlite_db.busy_timeout_ms", "must not be negative");
    const auto synchronous = *vars.system.sqlite_db.synchronous->value();
    require(synchronous >= 0 && synchronous <= 2,
        "system.sqlite_db.synchronous", "must be 0, 1 or 2");

    require(*vars.work.runtime.loop_capacity->value() > 0,
        "work.runtime.loop_capacity", "must be positive");
    require(*vars.work.interaction.queue_capacity->value() > 0,
        "work.interaction.queue_capacity", "must be positive");
    require(*vars.work.interaction.stop_drain_timeout_ms->value() >= 0,
        "work.interaction.stop_drain_timeout_ms", "must not be negative");
    require(*vars.work.interaction.capture_max_text_bytes->value() > 0,
        "work.interaction.capture_max_text_bytes", "must be positive");
    require(*vars.work.interaction.capture_max_hex_bytes->value() > 0,
        "work.interaction.capture_max_hex_bytes", "must be positive");
    require(*vars.work.interaction.capture_max_binary_attachment_bytes->value() > 0,
        "work.interaction.capture_max_binary_attachment_bytes",
        "must be positive");

    ValidateStaticRoot(root);

    // 日志项校验
    ValidateLogConfig(*vars.system.logs->value());
}


void InitGlobalAppConfig()
{
    auto &config = AppConfigRegistry::Instance();
    const auto input = ReadProductionAppConfigLoadInput();
    InitAppConfig(config, RegisteredGlobalAppConfigVars(), input);
}

const AppConfigVars& GetAppConfigVars()
{
    auto& config = AppConfigRegistry::Instance();
    if(!config.isFrozen())
    {
        throw std::logic_error("application configuration is not initialized");
    }
    return RegisteredGlobalAppConfigVars();
}


}
