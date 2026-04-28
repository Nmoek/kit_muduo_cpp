/**
 * @file dao_log.h
 * @brief dao层日志器
 * @author ljk5
 * @version 1.0
 * @date 2025-07-22 15:46:28
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "base/log.h"


/*********流式输出**********/
#define DAO_DEBUG(module) \
    KIT_DEBUG(KIT_LOGGER("dao"), module)
#define DAO_INFO(module) \
    KIT_INFO(KIT_LOGGER("dao"), module)
#define DAO_WARN(module) \
    KIT_WARN(KIT_LOGGER("dao"), module)
#define DAO_ERROR(module) \
    KIT_ERROR(KIT_LOGGER("dao"), module)
#define DAO_FATAL(module) \
    KIT_FATAL(KIT_LOGGER("dao"), module)

/**********变参输出***********/
#define DAO_F_DEBUG(module, fmt, ...) \
    KIT_FMT_DEBUG(KIT_LOGGER("dao"), module, fmt, ##__VA_ARGS__)
#define DAO_F_INFO(module, fmt, ...) \
    KIT_FMT_INFO(KIT_LOGGER("dao"), module, fmt, ##__VA_ARGS__)
#define DAO_F_WARN(module, fmt, ...) \
    KIT_FMT_WARN(KIT_LOGGER("dao"), module, fmt, ##__VA_ARGS__)
#define DAO_F_ERROR(module, fmt, ...) \
    KIT_FMT_ERROR(KIT_LOGGER("dao"), module, fmt, ##__VA_ARGS__)
#define DAO_F_FATAL(module, fmt, ...) \
    KIT_FMT_FATAL(KIT_LOGGER("dao"), module, fmt, ##__VA_ARGS__)

/*******db模块*********/
#define DAODB_DEBUG()     DAO_DEBUG("db")
#define DAODB_INFO()      DAO_INFO("db")
#define DAODB_WARN()      DAO_WARN("db")
#define DAODB_ERROR()     DAO_ERROR("db")
#define DAODB_FATAL()     DAO_FATAL("db")

#define DAODB_F_DEBUG(fmt, ...)     DAO_F_DEBUG("db", fmt, ##__VA_ARGS__)
#define DAODB_F_INFO(fmt, ...)      DAO_F_INFO("db", fmt, ##__VA_ARGS__)
#define DAODB_F_WARN(fmt, ...)      DAO_F_WARN("db", fmt, ##__VA_ARGS__)
#define DAODB_F_ERROR(fmt, ...)     DAO_F_ERROR("db", fmt, ##__VA_ARGS__)
#define DAODB_F_FATAL(fmt, ...)     DAO_F_FATAL("db", fmt, ##__VA_ARGS__)
/*******db模块*********/

/*******project模块*********/
#define DAOPJ_DEBUG()     DAO_DEBUG("project")
#define DAOPJ_INFO()      DAO_INFO("project")
#define DAOPJ_WARN()      DAO_WARN("project")
#define DAOPJ_ERROR()     DAO_ERROR("project")
#define DAOPJ_FATAL()     DAO_FATAL("project")

#define DAOPJ_F_DEBUG(fmt, ...)     DAO_F_DEBUG("project", fmt, ##__VA_ARGS__)
#define DAOPJ_F_INFO(fmt, ...)      DAO_F_INFO("project", fmt, ##__VA_ARGS__)
#define DAOPJ_F_WARN(fmt, ...)      DAO_F_WARN("project", fmt, ##__VA_ARGS__)
#define DAOPJ_F_ERROR(fmt, ...)     DAO_F_ERROR("project", fmt, ##__VA_ARGS__)
#define DAOPJ_F_FATAL(fmt, ...)     DAO_F_FATAL("project", fmt, ##__VA_ARGS__)
/*******project模块*********/

/*******protocol模块*********/
#define DAOPC_DEBUG()     DAO_DEBUG("protocol")
#define DAOPC_INFO()      DAO_INFO("protocol")
#define DAOPC_WARN()      DAO_WARN("protocol")
#define DAOPC_ERROR()     DAO_ERROR("protocol")
#define DAOPC_FATAL()     DAO_FATAL("protocol")

#define DAOPC_F_DEBUG(fmt, ...)     DAO_F_DEBUG("protocol", fmt, ##__VA_ARGS__)
#define DAOPC_F_INFO(fmt, ...)      DAO_F_INFO("protocol", fmt, ##__VA_ARGS__)
#define DAOPC_F_WARN(fmt, ...)      DAO_F_WARN("protocol", fmt, ##__VA_ARGS__)
#define DAOPC_F_ERROR(fmt, ...)     DAO_F_ERROR("protocol", fmt, ##__VA_ARGS__)
#define DAOPC_F_FATAL(fmt, ...)     DAO_F_FATAL("protocol", fmt, ##__VA_ARGS__)
/*******protocol模块*********/