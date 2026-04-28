/**
 * @file init.h
 * @brief dao层 初始化接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-23 15:55:27
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DAO_INIT_H__
#define __KIT_DAO_INIT_H__


#include "dao/project.h"
#include "dao/protocol.h"
#include "sqlite_orm/sqlite_orm.h"

#include <memory>

namespace kit_dao {

/// @brief  sqlite3 orm库的表头定义
#define SQLITE_ORM_TABLE_INIT_DEF()  \
    sqlite_orm::make_storage("kit.sqlite", \
        sqlite_orm::make_table("projects", \
            sqlite_orm::make_column("id", &Project::m_id, sqlite_orm::primary_key().autoincrement()), \
            sqlite_orm::make_column("ctime", &Project::m_ctime), \
            sqlite_orm::make_column("utime", &Project::m_utime), \
            sqlite_orm::make_column("name", &Project::m_name, sqlite_orm::not_null()), \
            sqlite_orm::make_column("mode", &Project::m_mode), \
            sqlite_orm::make_column("protocol_type", &Project::m_protocolType), \
            sqlite_orm::make_column("listen_port", &Project::m_listenPort), \
            sqlite_orm::make_column("target_ip", &Project::m_targetIp), \
            sqlite_orm::make_column("user_id", &Project::m_userId), \
            sqlite_orm::make_column("status", &Project::m_status), \
            sqlite_orm::make_column("pattern_type", &Project::m_patternType), \
            sqlite_orm::make_column("pattern_info", &Project::m_patternInfo, sqlite_orm::not_null()) \
        ), \
        sqlite_orm::make_table("protocols", \
            sqlite_orm::make_column("id", &Protocol::m_id, sqlite_orm::primary_key().autoincrement()), \
            sqlite_orm::make_column("ctime", &Protocol::m_ctime), \
            sqlite_orm::make_column("utime", &Protocol::m_utime), \
            sqlite_orm::make_column("name", &Protocol::m_name, sqlite_orm::not_null()), \
            sqlite_orm::make_column("type", &Protocol::m_type), \
            sqlite_orm::make_column("project_id", &Protocol::m_projectId), \
            sqlite_orm::make_column("status", &Protocol::m_status), \
            sqlite_orm::make_column("req_body_type", &Protocol::m_reqBodyType), \
            sqlite_orm::make_column("resp_body_type", &Protocol::m_respBodyType), \
            sqlite_orm::make_column("req_body_status", &Protocol::m_reqBodyDataStatus), \
            sqlite_orm::make_column("resp_body_status", &Protocol::m_respBodyDataStatus), \
            sqlite_orm::make_column("req_cfg", &Protocol::m_reqCfg, sqlite_orm::not_null()), \
            sqlite_orm::make_column("resp_cfg", &Protocol::m_respCfg, sqlite_orm::not_null()), \
            sqlite_orm::make_column("req_body_data", &Protocol::m_reqBodyData, sqlite_orm::not_null()), \
            sqlite_orm::make_column("resp_body_data", &Protocol::m_respBodyData, sqlite_orm::not_null()), \
            sqlite_orm::make_column("is_endian", &Protocol::m_isEndian) \
        ))



using SqliteOrmType = decltype(SQLITE_ORM_TABLE_INIT_DEF());

/**
 * @brief sqlite数据库初始化
 * @return std::unique_ptr<SqliteOrmType> 
 */
std::shared_ptr<SqliteOrmType> InitSqliteDb();

} // namespace kit_domain
#endif // __KIT_DAO_INIT_H__