/**
 * @file web_log.h
 * @brief  web接口层日志器
 * @author ljk5
 * @version 1.0
 * @date 2025-07-18 18:15:19
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "base/log.h"


/*********流式输出**********/
#define SVC_DEBUG(module) \
    KIT_DEBUG(KIT_LOGGER("service"), module)
#define SVC_INFO(module) \
    KIT_INFO(KIT_LOGGER("service"), module)
#define SVC_WARN(module) \
    KIT_WARN(KIT_LOGGER("service"), module)
#define SVC_ERROR(module) \
    KIT_ERROR(KIT_LOGGER("service"), module)
#define SVC_FATAL(module) \
    KIT_FATAL(KIT_LOGGER("service"), module)

/**********变参输出***********/
#define SVC_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("service"), module, fmt, ##__VA_ARGS__)
#define SVC_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("service"), module, fmt, ##__VA_ARGS__)
#define SVC_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("service"), module, fmt, ##__VA_ARGS__)
#define SVC_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("service"), module, fmt, ##__VA_ARGS__)
#define SVC_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("service"), module, fmt, ##__VA_ARGS__)

/*******project模块*********/
#define SVCPJ_DEBUG()     SVC_DEBUG("project")
#define SVCPJ_INFO()      SVC_INFO("project")
#define SVCPJ_WARN()      SVC_WARN("project")
#define SVCPJ_ERROR()     SVC_ERROR("project")
#define SVCPJ_FATAL()     SVC_FATAL("project")

#define SVCPJ_F_DEBUG(fmt, ...)     SVC_F_DEBUG("project", fmt, ##__VA_ARGS__)
#define SVCPJ_F_INFO(fmt, ...)      SVC_F_INFO("project", fmt, ##__VA_ARGS__)
#define SVCPJ_F_WARN(fmt, ...)      SVC_F_WARN("project", fmt, ##__VA_ARGS__)
#define SVCPJ_F_ERROR(fmt, ...)     SVC_F_ERROR("project", fmt, ##__VA_ARGS__)
#define SVCPJ_F_FATAL(fmt, ...)     SVC_F_FATAL("project", fmt, ##__VA_ARGS__)
/*******project模块*********/

/*******protocol*********/
#define SVCPC_DEBUG()     SVC_DEBUG("protocol")
#define SVCPC_INFO()      SVC_INFO("protocol")
#define SVCPC_WARN()      SVC_WARN("protocol")
#define SVCPC_ERROR()     SVC_ERROR("protocol")
#define SVCPC_FATAL()     SVC_FATAL("protocol")

#define SVCPC_F_DEBUG(fmt, ...)     SVC_F_DEBUG("protocol", fmt, ##__VA_ARGS__)
#define SVCPC_F_INFO(fmt, ...)      SVC_F_INFO("protocol", fmt, ##__VA_ARGS__)
#define SVCPC_F_WARN(fmt, ...)      SVC_F_WARN("protocol", fmt, ##__VA_ARGS__)
#define SVCPC_F_ERROR(fmt, ...)     SVC_F_ERROR("protocol", fmt, ##__VA_ARGS__)
#define SVCPC_F_FATAL(fmt, ...)     SVC_F_FATAL("protocol", fmt, ##__VA_ARGS__)
/*******protocol模块*********/

/*******auth鉴权模块*********/
#define SVCAUTH_DEBUG()     SVC_DEBUG("protocol")
#define SVCAUTH_INFO()      SVC_INFO("auth")
#define SVCAUTH_WARN()      SVC_WARN("auth")
#define SVCAUTH_ERROR()     SVC_ERROR("auth")
#define SVCAUTH_FATAL()     SVC_FATAL("auth")

#define SVCAUTH_F_DEBUG(fmt, ...)     SVC_F_DEBUG("auth", fmt, ##__VA_ARGS__)
#define SVCAUTH_F_INFO(fmt, ...)      SVC_F_INFO("auth", fmt, ##__VA_ARGS__)
#define SVCAUTH_F_WARN(fmt, ...)      SVC_F_WARN("auth", fmt, ##__VA_ARGS__)
#define SVCAUTH_F_ERROR(fmt, ...)     SVC_F_ERROR("auth", fmt, ##__VA_ARGS__)
#define SVCAUTH_F_FATAL(fmt, ...)     SVC_F_FATAL("auth", fmt, ##__VA_ARGS__)
/*******auth鉴权模块*********/