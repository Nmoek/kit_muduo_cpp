/**
 * @file init.cpp
 * @brief dao层 初始化接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-23 16:18:47
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "dao/init.h"
#include "dao/dao_log.h"
#include "dao/sqlite_orm_pool.h"
#include "sqlite3.h"

#include <exception>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>

namespace kit_dao {

namespace {

static std::vector<std::vector<const char*>> index_sqls{
    // 0 projects
    {
        "CREATE INDEX IF NOT EXISTS idx_projects_userid_status ON projects(user_id, status);",
        "CREATE INDEX IF NOT EXISTS idx_projects_userid_runstate ON projects(user_id, runtime_state);",
        "CREATE INDEX IF NOT EXISTS idx_projects_runstate ON projects(runtime_state);",
        "CREATE INDEX IF NOT EXISTS idx_projects_status ON projects(status);",    
    },
    // 1 protocols
    {
        "CREATE INDEX IF NOT EXISTS idx_protocols_pjid_status ON protocols(project_id, status);",
        "CREATE INDEX IF NOT EXISTS idx_protocols_pjid_runenabled ON protocols(project_id, runtime_enabled);",
        "CREATE INDEX IF NOT EXISTS idx_protocols_pjid_status_runenabled ON protocols(project_id, status, runtime_enabled);",
        "CREATE UNIQUE INDEX IF NOT EXISTS uidx_protocols_pjid_runkey ON protocols(project_id, runtime_key) WHERE status = 1;",
    },
    // 2 users
    {
        "CREATE INDEX IF NOT EXISTS idx_users_role_status ON users(role, status);",

    },
    // 3 sessions
    {
        "CREATE INDEX IF NOT EXISTS idx_sessions_userid ON sessions(user_id);",
        "CREATE INDEX IF NOT EXISTS idx_sessions_extime ON sessions(expire_time);",

    }

};

}

void EnsureSqliteIndexes()
{
    sqlite3 *raw_db = nullptr;
    const int open_rc = sqlite3_open("kit.sqlite", &raw_db);
    if(open_rc != SQLITE_OK)
    {
        std::string msg = raw_db ? sqlite3_errmsg(raw_db) : "unknown sqlite open error";
        if(raw_db)
        {
            sqlite3_close(raw_db);
        }
        throw std::runtime_error("open sqlite for index failed: " + msg);
    }

    for(auto &t : index_sqls)
    {
        for(const char *sql : t)
        {
            char *err_msg = nullptr;
            const int rc = sqlite3_exec(raw_db, sql, nullptr, nullptr, &err_msg);
            if(rc != SQLITE_OK)
            {
                std::string msg = err_msg ? err_msg : "unknown sqlite error";
                sqlite3_free(err_msg);
                sqlite3_close(raw_db);
                throw std::runtime_error("create sqlite index failed: " + msg);
            }
        }

    }

    sqlite3_close(raw_db);
}

// 注意: 该接口暂时弃用
std::shared_ptr<SqliteOrmType> InitSqliteDb()
{
    // TODO 配置数据库路径
    auto db = std::make_shared<SqliteOrmType>(SQLITE_ORM_TABLE_INIT_DEF());

    db->pragma.journal_mode(sqlite_orm::journal_mode::WAL);
    auto journal_mode = db->pragma.get_pragma<std::string>("journal_mode");
    if(journal_mode != "wal" && journal_mode != "WAL")
    {
        DAODB_WARN() << "set journal_mode=WAL failed, actual journal_mode:" << journal_mode << std::endl;
    }

    auto tables = db->sync_schema(true);
    for(const auto &t : tables)
    {
        DAODB_INFO() << "table[" << t.first << "]:" << t.second << std::endl;
    }
    EnsureSqliteIndexes();

    DAODB_INFO() << "sqlite3 version: " << db->libversion() << std::endl;;
    
    try {
        auto test = db->select(sqlite_orm::json_extract<std::string>(R"({"name": "test"})", "$.name"));
        DAODB_INFO() << "JSON1 支持已启用" << std::endl;
    } catch (const std::exception& e) {
        DAODB_INFO() << "JSON1 不支持: " << e.what() << std::endl;
    }

    // 数据库配置打印
    auto synchronous = db->pragma.get_pragma<int>("synchronous");
    auto cache_size = db->pragma.get_pragma<int>("cache_size");
    auto mmap_size = db->pragma.get_pragma<int>("mmap_size");
    auto compile_options = db->pragma.get_pragma<std::vector<std::string>>("compile_options");
    
    DAODB_DEBUG() << "journal_mode:" << journal_mode << std::endl;
    DAODB_DEBUG() << "synchronous:" << synchronous << std::endl;
    DAODB_DEBUG() << "cache_size:" << cache_size << std::endl;
    DAODB_DEBUG() << "mmap_size:" << mmap_size << std::endl;

    for(auto & c : compile_options)
        DAODB_DEBUG() << "compile_options:" << c << std::endl;

    // 移动构造
    return db;
}

std::shared_ptr<SqliteOrmPool> InitSqliteDbPool(SqliteOrmPoolConfig config)
{
    return std::make_shared<SqliteOrmPool>(config);
}

}   // namespace kit_dao
