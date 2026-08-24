/**
 * @file log.cpp
 * @brief 日志系统初始化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-08-20 16:50:03
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "ioc/log.h"

#include "app_config.h"

using namespace kit_muduo;

namespace kit_app {


void InitLog()
{
    const auto& log_config = *APP_CONFIG_VARS_SYSTEM_LOG()->value();
    const auto &result = LogManager::GetInstance().initialize(log_config);
    if(!result.ok())
    {
        throw std::runtime_error("log initialize failed: " + result.message);
    }
}



}