/**
 * @file app_log.h
 * @brief  业务日志
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 01:08:43
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/log.h"


/*********流式输出**********/
#define APP_DEBUG() \
    KIT_DEBUG(KIT_LOGGER("app"), "app")
#define APP_INFO() \
    KIT_INFO(KIT_LOGGER("app"), "app")
#define APP_WARN() \
    KIT_WARN(KIT_LOGGER("app"), "app")
#define APP_ERROR() \
    KIT_ERROR(KIT_LOGGER("app"), "app")
#define APP_FATAL() \
    KIT_FATAL(KIT_LOGGER("app"), "app")

/**********变参输出***********/
#define APP_F_DEBUG(fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("app"), "app", fmt, ##__VA_ARGS__)
#define APP_F_INFO(fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("app"), "app", fmt, ##__VA_ARGS__)
#define APP_F_WARN(fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("app"), "app", fmt, ##__VA_ARGS__)
#define APP_F_ERROR(fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("app"), "app", fmt, ##__VA_ARGS__)
#define APP_F_FATAL(fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("app"), "app", fmt, ##__VA_ARGS__)


