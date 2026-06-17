/**
 * @file base_log.h
 * @brief base模块日志定义
 * @author Kewin Li
 * @version 1.0
 * @date 2025-05-22 20:33:30
 * @copyright Copyright (c) 2025 Kewin Li
 */

 #include "base/log.h"

/*********流式输出**********/
#define BASE_DEBUG(module) \
    KIT_DEBUG(KIT_LOGGER("base"), module)
#define BASE_INFO(module) \
    KIT_INFO(KIT_LOGGER("base"), module)
#define BASE_WARN(module) \
    KIT_WARN(KIT_LOGGER("base"), module)
#define BASE_ERROR(module) \
    KIT_ERROR(KIT_LOGGER("base"), module)
#define BASE_FATAL(module) \
    KIT_FATAL(KIT_LOGGER("base"), module)

/**********变参输出***********/
#define BASE_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("base"), module, fmt, ##__VA_ARGS__)
#define BASE_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("base"), module, fmt, ##__VA_ARGS__)
#define BASE_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("base"), module, fmt, ##__VA_ARGS__)
#define BASE_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("base"), module, fmt, ##__VA_ARGS__)
#define BASE_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("base"), module, fmt, ##__VA_ARGS__)

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

/*******thread pool模块*********/
#define TPOOL_DEBUG()     BASE_DEBUG("ThreadPool")
#define TPOOL_INFO()      BASE_INFO("ThreadPool")
#define TPOOL_WARN()      BASE_WARN("ThreadPool")
#define TPOOL_ERROR()     BASE_ERROR("ThreadPool")
#define TPOOL_FATAL()     BASE_FATAL("ThreadPool")

#define TPOOL_F_DEBUG(fmt, ...)     BASE_F_DEBUG("ThreadPool", fmt, ##__VA_ARGS__)
#define TPOOL_F_INFO(fmt, ...)      BASE_F_INFO("ThreadPool", fmt, ##__VA_ARGS__)
#define TPOOL_F_WARN(fmt, ...)      BASE_F_WARN("ThreadPool", fmt, ##__VA_ARGS__)
#define TPOOL_F_ERROR(fmt, ...)     BASE_F_ERROR("ThreadPool", fmt, ##__VA_ARGS__)
#define TPOOL_F_FATAL(fmt, ...)     BASE_F_FATAL("ThreadPool", fmt, ##__VA_ARGS__)
/*******thread pool模块模块*********/

/*******content parser模块*********/
#define PARSER_DEBUG()     BASE_DEBUG("parser")
#define PARSER_INFO()      BASE_INFO("parser")
#define PARSER_WARN()      BASE_WARN("parser")
#define PARSER_ERROR()     BASE_ERROR("parser")
#define PARSER_FATAL()     BASE_FATAL("parser")

#define PARSER_F_DEBUG(fmt, ...)     BASE_F_DEBUG("parser", fmt, ##__VA_ARGS__)
#define PARSER_F_INFO(fmt, ...)      BASE_F_INFO("parser", fmt, ##__VA_ARGS__)
#define PARSER_F_WARN(fmt, ...)      BASE_F_WARN("parser", fmt, ##__VA_ARGS__)
#define PARSER_F_ERROR(fmt, ...)     BASE_F_ERROR("parser", fmt, ##__VA_ARGS__)
#define PARSER_F_FATAL(fmt, ...)     BASE_F_FATAL("parser", fmt, ##__VA_ARGS__)

/*******content parser模块*********/

/*******content codec模块*********/
#define CODEC_DEBUG()     BASE_DEBUG("codec")
#define CODEC_INFO()      BASE_INFO("codec")
#define CODEC_WARN()      BASE_WARN("codec")
#define CODEC_ERROR()     BASE_ERROR("codec")
#define CODEC_FATAL()     BASE_FATAL("codec")

#define CODEC_F_DEBUG(fmt, ...)     BASE_F_DEBUG("codec", fmt, ##__VA_ARGS__)
#define CODEC_F_INFO(fmt, ...)      BASE_F_INFO("codec", fmt, ##__VA_ARGS__)
#define CODEC_F_WARN(fmt, ...)      BASE_F_WARN("codec", fmt, ##__VA_ARGS__)
#define CODEC_F_ERROR(fmt, ...)     BASE_F_ERROR("codec", fmt, ##__VA_ARGS__)
#define CODEC_F_FATAL(fmt, ...)     BASE_F_FATAL("codec", fmt, ##__VA_ARGS__)

/*******content codec模块*********/
