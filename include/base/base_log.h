/**
 * @file base_log.h
 * @brief base模块日志定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 20:33:30
 * @copyright Copyright (c) 2025 Kewin Li
 */

 #include "base/log.h"

static auto g_base_logger = KIT_INIT_LOGGER("base");

/*********流式输出**********/
#define BASE_DEBUG(module) \
    KIT_DEBUG(g_base_logger, module)
#define BASE_INFO(module) \
    KIT_INFO(g_base_logger, module)
#define BASE_WARN(module) \
    KIT_WARN(g_base_logger, module)
#define BASE_ERROR(module) \
    KIT_ERROR(g_base_logger, module)
#define BASE_FATAL(module) \
    KIT_FATAL(g_base_logger, module)

/**********变参输出***********/
#define BASE_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(g_base_logger, module, fmt, ##__VA_ARGS__)
#define BASE_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(g_base_logger, module, fmt, ##__VA_ARGS__)
#define BASE_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(g_base_logger, module, fmt, ##__VA_ARGS__)
#define BASE_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(g_base_logger, module, fmt, ##__VA_ARGS__)
#define BASE_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(g_base_logger, module, fmt, ##__VA_ARGS__)

/*******thread模块*********/
#define THREAD_DEBUG()     BASE_DEBUG("Thread")
#define THREAD_INFO()      BASE_INFO("Thread")
#define THREAD_WARN()      BASE_WARN("Thread")
#define THREAD_ERROR()     BASE_ERROR("Thread")
#define THREAD_FATAL()     BASE_FATAL("Thread")

#define THREAD_F_DEBUG(fmt, ...)     BASE_F_DEBUG("Thread", fmt, ##__VA_ARGS__)
#define THREAD_F_INFO(fmt, ...)      BASE_F_INFO("Thread", fmt, ##__VA_ARGS__)
#define THREAD_F_WARN(fmt, ...)      BASE_F_WARN("Thread", fmt, ##__VA_ARGS__)
#define THREAD_F_ERROR(fmt, ...)     BASE_F_ERROR("Thread", fmt, ##__VA_ARGS__)
#define THREAD_F_FATAL(fmt, ...)     BASE_F_FATAL("Thread", fmt, ##__VA_ARGS__)
/*******thread模块*********/