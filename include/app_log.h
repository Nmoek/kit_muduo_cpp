/**
 * @file app_log.h
 * @brief  业务日志
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-08 01:08:43
 * @copyright Copyright (c) 2025 Kewin Li
 */
#include "base/log.h"

static auto g_app_logger = KIT_LOGGER("app");

/*********流式输出**********/
#define APP_DEBUG(module) \
    KIT_DEBUG(g_app_logger, module)
#define APP_INFO(module) \
    KIT_INFO(g_app_logger, module)
#define APP_WARN(module) \
    KIT_WARN(g_app_logger, module)
#define APP_ERROR(module) \
    KIT_ERROR(g_app_logger, module)
#define APP_FATAL(module) \
    KIT_FATAL(g_app_logger, module)

/**********变参输出***********/
#define APP_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(g_app_logger, module, fmt, ##__VA_ARGS__)
#define APP_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(g_app_logger, module, fmt, ##__VA_ARGS__)
#define APP_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(g_app_logger, module, fmt, ##__VA_ARGS__)
#define APP_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(g_app_logger, module, fmt, ##__VA_ARGS__)
#define APP_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(g_app_logger, module, fmt, ##__VA_ARGS__)

/*******device recover模块*********/
#define RECOVER_DEBUG()     APP_DEBUG("recover")
#define RECOVER_INFO()      APP_INFO("recover")
#define RECOVER_WARN()      APP_WARN("recover")
#define RECOVER_ERROR()     APP_ERROR("recover")
#define RECOVER_FATAL()     APP_FATAL("recover")

#define RECOVER_F_DEBUG(fmt, ...)     APP_F_DEBUG("recover", fmt, ##__VA_ARGS__)
#define RECOVER_F_INFO(fmt, ...)      APP_F_INFO("recover", fmt, ##__VA_ARGS__)
#define RECOVER_F_WARN(fmt, ...)      APP_F_WARN("recover", fmt, ##__VA_ARGS__)
#define RECOVER_F_ERROR(fmt, ...)     APP_F_ERROR("recover", fmt, ##__VA_ARGS__)
#define RECOVER_F_FATAL(fmt, ...)     APP_F_FATAL("recover", fmt, ##__VA_ARGS__)

/*******device recover模块*********/

