/**
 * @file runtime_log.h
 * @brief 运行态层日志器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-07 16:08:03
 * @copyright Copyright (c) 2026 Kewin Li
 */

#include "base/log.h"


/*********流式输出**********/
#define RUN_DEBUG(module) \
    KIT_DEBUG(KIT_LOGGER("runtime"), module)
#define RUN_INFO(module) \
    KIT_INFO(KIT_LOGGER("runtime"), module)
#define RUN_WARN(module) \
    KIT_WARN(KIT_LOGGER("runtime"), module)
#define RUN_ERROR(module) \
    KIT_ERROR(KIT_LOGGER("runtime"), module)
#define RUN_FATAL(module) \
    KIT_FATAL(KIT_LOGGER("runtime"), module)

/**********变参输出***********/
#define RUN_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("runtime"), module, fmt, ##__VA_ARGS__)
#define RUN_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("runtime"), module, fmt, ##__VA_ARGS__)
#define RUN_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("runtime"), module, fmt, ##__VA_ARGS__)
#define RUN_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("runtime"), module, fmt, ##__VA_ARGS__)
#define RUN_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("runtime"), module, fmt, ##__VA_ARGS__)

/*******project_runtime_manager模块*********/
#define RUNPJMA_DEBUG()     RUN_DEBUG("pjma")
#define RUNPJMA_INFO()      RUN_INFO("pjma")
#define RUNPJMA_WARN()      RUN_WARN("pjma")
#define RUNPJMA_ERROR()     RUN_ERROR("pjma")
#define RUNPJMA_FATAL()     RUN_FATAL("pjma")

#define RUNPJMA_F_DEBUG(fmt, ...)     RUN_F_DEBUG("pjma", fmt, ##__VA_ARGS__)
#define RUNPJMA_F_INFO(fmt, ...)      RUN_F_INFO("pjma", fmt, ##__VA_ARGS__)
#define RUNPJMA_F_WARN(fmt, ...)      RUN_F_WARN("pjma", fmt, ##__VA_ARGS__)
#define RUNPJMA_F_ERROR(fmt, ...)     RUN_F_ERROR("pjma", fmt, ##__VA_ARGS__)
#define RUNPJMA_F_FATAL(fmt, ...)     RUN_F_FATAL("pjma", fmt, ##__VA_ARGS__)

/*******project_runtime_manager模块*********/