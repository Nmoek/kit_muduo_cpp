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

#include <vector>
#include <string>

namespace kit_dao {

std::shared_ptr<SqliteOrmType> InitSqliteDb()
{
    // TODO 配置数据库路径
    auto db = std::make_shared<SqliteOrmType>(SQLITE_ORM_TABLE_INIT_DEF());

    auto tables = db->sync_schema();
    for(const auto &t : tables)
    {
        DAODB_INFO() << "table[" << t.first << "]:" << t.second << std::endl;
    }

    DAODB_INFO() << "sqlite3 version: " << db->libversion() << std::endl;;
    
    try {
        auto test = db->select(sqlite_orm::json_extract<std::string>(R"({"name": "test"})", "$.name"));
        DAODB_INFO() << "JSON1 支持已启用" << std::endl;
    } catch (const std::exception& e) {
        DAODB_INFO() << "JSON1 不支持: " << e.what() << std::endl;
    }

    // 数据库配置打印
    auto journal_mode = db->pragma.get_pragma<std::string>("journal_mode");
    auto compile_options = db->pragma.get_pragma<std::vector<std::string>>("compile_options");
    
    DAODB_DEBUG() << "journal_mode:" << journal_mode << std::endl;

    for(auto & c : compile_options)
        DAODB_DEBUG() << "compile_options:" << c << std::endl;

    // 移动构造
    return db;
}

}   // namespace kit_dao