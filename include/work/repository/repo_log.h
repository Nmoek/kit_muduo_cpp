/**
 * @file repo_log.h
 * @brief repo层日志器
 * @author ljk5
 * @version 1.0
 * @date 2025-07-22 15:46:28
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "base/log.h"
static auto g_repo_logger = KIT_LOGGER("repository");


/*********流式输出**********/
#define REPO_DEBUG(module) \
    KIT_DEBUG(g_repo_logger, module)
#define REPO_INFO(module) \
    KIT_INFO(g_repo_logger, module)
#define REPO_WARN(module) \
    KIT_WARN(g_repo_logger, module)
#define REPO_ERROR(module) \
    KIT_ERROR(g_repo_logger, module)
#define REPO_FATAL(module) \
    KIT_FATAL(g_repo_logger, module)

/**********变参输出***********/
#define REPO_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(g_repo_logger, module, fmt, ##__VA_ARGS__)
#define REPO_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(g_repo_logger, module, fmt, ##__VA_ARGS__)
#define REPO_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(g_repo_logger, module, fmt, ##__VA_ARGS__)
#define REPO_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(g_repo_logger, module, fmt, ##__VA_ARGS__)
#define REPO_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(g_repo_logger, module, fmt, ##__VA_ARGS__)

/*******project模块*********/
#define REPOPJ_DEBUG()     REPO_DEBUG("project")
#define REPOPJ_INFO()      REPO_INFO("project")
#define REPOPJ_WARN()      REPO_WARN("project")
#define REPOPJ_ERROR()     REPO_ERROR("project")
#define REPOPJ_FATAL()     REPO_FATAL("project")

#define REPOPJ_F_DEBUG(fmt, ...)     REPO_F_DEBUG("project", fmt, ##__VA_ARGS__)
#define REPOPJ_F_INFO(fmt, ...)      REPO_F_INFO("project", fmt, ##__VA_ARGS__)
#define REPOPJ_F_WARN(fmt, ...)      REPO_F_WARN("project", fmt, ##__VA_ARGS__)
#define REPOPJ_F_ERROR(fmt, ...)     REPO_F_ERROR("project", fmt, ##__VA_ARGS__)
#define REPOPJ_F_FATAL(fmt, ...)     REPO_F_FATAL("project", fmt, ##__VA_ARGS__)
/*******project模块*********/

/*******protocol模块*********/
#define REPOPC_DEBUG()     REPO_DEBUG("protocol")
#define REPOPC_INFO()      REPO_INFO("protocol")
#define REPOPC_WARN()      REPO_WARN("protocol")
#define REPOPC_ERROR()     REPO_ERROR("protocol")
#define REPOPC_FATAL()     REPO_FATAL("protocol")

#define REPOPC_F_DEBUG(fmt, ...)     REPO_F_DEBUG("protocol", fmt, ##__VA_ARGS__)
#define REPOPC_F_INFO(fmt, ...)      REPO_F_INFO("protocol", fmt, ##__VA_ARGS__)
#define REPOPC_F_WARN(fmt, ...)      REPO_F_WARN("protocol", fmt, ##__VA_ARGS__)
#define REPOPC_F_ERROR(fmt, ...)     REPO_F_ERROR("protocol", fmt, ##__VA_ARGS__)
#define REPOPC_F_FATAL(fmt, ...)     REPO_F_FATAL("protocol", fmt, ##__VA_ARGS__)
/*******protocol模块*********/