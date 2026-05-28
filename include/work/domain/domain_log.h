/**
 * @file domain_log.h
 * @brief 业务领域层日志器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-15 18:50:35
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/log.h"

/*********流式输出**********/
#define DOMAIN_DEBUG(module) \
    KIT_DEBUG(KIT_LOGGER("domain"), module)
#define DOMAIN_INFO(module) \
    KIT_INFO(KIT_LOGGER("domain"), module)
#define DOMAIN_WARN(module) \
    KIT_WARN(KIT_LOGGER("domain"), module)
#define DOMAIN_ERROR(module) \
    KIT_ERROR(KIT_LOGGER("domain"), module)
#define DOMAIN_FATAL(module) \
    KIT_FATAL(KIT_LOGGER("domain"), module)

/**********变参输出***********/
#define DOMAIN_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("domain"), module, fmt, ##__VA_ARGS__)
#define DOMAIN_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("domain"), module, fmt, ##__VA_ARGS__)
#define DOMAIN_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("domain"), module, fmt, ##__VA_ARGS__)
#define DOMAIN_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("domain"), module, fmt, ##__VA_ARGS__)
#define DOMAIN_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("domain"), module, fmt, ##__VA_ARGS__)


/*******project server模块*********/
#define PJSERVER_DEBUG()     DOMAIN_DEBUG("pj_server")
#define PJSERVER_INFO()      DOMAIN_INFO("pj_server")
#define PJSERVER_WARN()      DOMAIN_WARN("pj_server")
#define PJSERVER_ERROR()     DOMAIN_ERROR("pj_server")
#define PJSERVER_FATAL()     DOMAIN_FATAL("pj_server")

#define PJSERVER_F_DEBUG(fmt, ...)     DOMAIN_F_DEBUG("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_INFO(fmt, ...)      DOMAIN_F_INFO("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_WARN(fmt, ...)      DOMAIN_F_WARN("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_ERROR(fmt, ...)     DOMAIN_F_ERROR("pj_server", fmt, ##__VA_ARGS__)
#define PJSERVER_F_FATAL(fmt, ...)     DOMAIN_F_FATAL("pj_server", fmt, ##__VA_ARGS__)
/*******project server模块*********/


/*******protocol item模块*********/
#define PCITEM_DEBUG()     DOMAIN_DEBUG("pj_server")
#define PCITEM_INFO()      DOMAIN_INFO("pc_item")
#define PCITEM_WARN()      DOMAIN_WARN("pc_item")
#define PCITEM_ERROR()     DOMAIN_ERROR("pc_item")
#define PCITEM_FATAL()     DOMAIN_FATAL("pc_item")

#define PCITEM_F_DEBUG(fmt, ...)     DOMAIN_F_DEBUG("pc_item", fmt, ##__VA_ARGS__)
#define PCITEM_F_INFO(fmt, ...)      DOMAIN_F_INFO("pc_item", fmt, ##__VA_ARGS__)
#define PCITEM_F_WARN(fmt, ...)      DOMAIN_F_WARN("pc_item", fmt, ##__VA_ARGS__)
#define PCITEM_F_ERROR(fmt, ...)     DOMAIN_F_ERROR("pc_item", fmt, ##__VA_ARGS__)
#define PCITEM_F_FATAL(fmt, ...)     DOMAIN_F_FATAL("pc_item", fmt, ##__VA_ARGS__)
/*******protocol item模块*********/



/*******custom tcp模块*********/
#define CUSTOM_DEBUG()     DOMAIN_DEBUG("custom")
#define CUSTOM_INFO()      DOMAIN_INFO("custom")
#define CUSTOM_WARN()      DOMAIN_WARN("custom")
#define CUSTOM_ERROR()     DOMAIN_ERROR("custom")
#define CUSTOM_FATAL()     DOMAIN_FATAL("custom")

#define CUSTOM_F_DEBUG(fmt, ...)     DOMAIN_F_DEBUG("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_INFO(fmt, ...)      DOMAIN_F_INFO("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_WARN(fmt, ...)      DOMAIN_F_WARN("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_ERROR(fmt, ...)     DOMAIN_F_ERROR("custom", fmt, ##__VA_ARGS__)
#define CUSTOM_F_FATAL(fmt, ...)     DOMAIN_F_FATAL("custom", fmt, ##__VA_ARGS__)
/*******custom tcp模块*********/


/*******runtime模块*********/
#define RUNTIME_DEBUG()     DOMAIN_DEBUG("runtime")
#define RUNTIME_INFO()      DOMAIN_INFO("runtime")
#define RUNTIME_WARN()      DOMAIN_WARN("runtime")
#define RUNTIME_ERROR()     DOMAIN_ERROR("runtime")
#define RUNTIME_FATAL()     DOMAIN_FATAL("runtime")

#define RUNTIME_F_DEBUG(fmt, ...)     DOMAIN_F_DEBUG("runtime", fmt, ##__VA_ARGS__)
#define RUNTIME_F_INFO(fmt, ...)      DOMAIN_F_INFO("runtime", fmt, ##__VA_ARGS__)
#define RUNTIME_F_WARN(fmt, ...)      DOMAIN_F_WARN("runtime", fmt, ##__VA_ARGS__)
#define RUNTIME_F_ERROR(fmt, ...)     DOMAIN_F_ERROR("runtime", fmt, ##__VA_ARGS__)
#define RUNTIME_F_FATAL(fmt, ...)     DOMAIN_F_FATAL("runtime", fmt, ##__VA_ARGS__)
/*******runtime模块*********/