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
#define WEB_DEBUG(module) \
    KIT_DEBUG(KIT_LOGGER("web"), module)
#define WEB_INFO(module) \
    KIT_INFO(KIT_LOGGER("web"), module)
#define WEB_WARN(module) \
    KIT_WARN(KIT_LOGGER("web"), module)
#define WEB_ERROR(module) \
    KIT_ERROR(KIT_LOGGER("web"), module)
#define WEB_FATAL(module) \
    KIT_FATAL(KIT_LOGGER("web"), module)

/**********变参输出***********/
#define WEB_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("web"), module, fmt, ##__VA_ARGS__)
#define WEB_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("web"), module, fmt, ##__VA_ARGS__)
#define WEB_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("web"), module, fmt, ##__VA_ARGS__)
#define WEB_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("web"), module, fmt, ##__VA_ARGS__)
#define WEB_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("web"), module, fmt, ##__VA_ARGS__)

/*******project模块*********/
#define PJ_DEBUG()     WEB_DEBUG("project")
#define PJ_INFO()      WEB_INFO("project")
#define PJ_WARN()      WEB_WARN("project")
#define PJ_ERROR()     WEB_ERROR("project")
#define PJ_FATAL()     WEB_FATAL("project")

#define PJ_F_DEBUG(fmt, ...)     WEB_F_DEBUG("project", fmt, ##__VA_ARGS__)
#define PJ_F_INFO(fmt, ...)      WEB_F_INFO("project", fmt, ##__VA_ARGS__)
#define PJ_F_WARN(fmt, ...)      WEB_F_WARN("project", fmt, ##__VA_ARGS__)
#define PJ_F_ERROR(fmt, ...)     WEB_F_ERROR("project", fmt, ##__VA_ARGS__)
#define PJ_F_FATAL(fmt, ...)     WEB_F_FATAL("project", fmt, ##__VA_ARGS__)
/*******project模块*********/

/*******protocol模块*********/
#define PC_DEBUG()     WEB_DEBUG("protocol")
#define PC_INFO()      WEB_INFO("protocol")
#define PC_WARN()      WEB_WARN("protocol")
#define PC_ERROR()     WEB_ERROR("protocol")
#define PC_FATAL()     WEB_FATAL("protocol")

#define PC_F_DEBUG(fmt, ...)     WEB_F_DEBUG("protocol", fmt, ##__VA_ARGS__)
#define PC_F_INFO(fmt, ...)      WEB_F_INFO("protocol", fmt, ##__VA_ARGS__)
#define PC_F_WARN(fmt, ...)      WEB_F_WARN("protocol", fmt, ##__VA_ARGS__)
#define PC_F_ERROR(fmt, ...)     WEB_F_ERROR("protocol", fmt, ##__VA_ARGS__)
#define PC_F_FATAL(fmt, ...)     WEB_F_FATAL("protocol", fmt, ##__VA_ARGS__)
/*******protocol模块*********/


/*******project server模块*********/
#define PJSERVER_DEBUG()     WEB_DEBUG("pj_server")
#define PJSERVER_INFO()      WEB_INFO("pj_server")
#define PJSERVER_WARN()      WEB_WARN("pj_server")
#define PJSERVER_ERROR()     WEB_ERROR("pj_server")
#define PJSERVER_FATAL()     WEB_FATAL("pj_server")

#define PJSERVER_F_DEBUG(fmt, ...)     WEB_F_DEBUG("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_INFO(fmt, ...)      WEB_F_INFO("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_WARN(fmt, ...)      WEB_F_WARN("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_ERROR(fmt, ...)     WEB_F_ERROR("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_FATAL(fmt, ...)     WEB_F_FATAL("pj_server", fmt, ##__VA_ARGS__)
/*******http project模块*********/


/*******custom tcp模块*********/
#define CUSTOM_DEBUG()     WEB_DEBUG("custom")
#define CUSTOM_INFO()      WEB_INFO("custom")
#define CUSTOM_WARN()      WEB_WARN("custom")
#define CUSTOM_ERROR()     WEB_ERROR("custom")
#define CUSTOM_FATAL()     WEB_FATAL("custom")

#define CUSTOM_F_DEBUG(fmt, ...)     WEB_F_DEBUG("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_INFO(fmt, ...)      WEB_F_INFO("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_WARN(fmt, ...)      WEB_F_WARN("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_ERROR(fmt, ...)     WEB_F_ERROR("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_FATAL(fmt, ...)     WEB_F_FATAL("custom", fmt, ##__VA_ARGS__)
/*******http project模块*********/