/**
 * @file test_sqlite3.cpp
 * @brief 测试sqlite3相关配置是否生效
 * @author ljk5
 * @version 1.0
 * @date 2025-12-04 19:00:35
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "../test_log.h"
#include "db/sqlite3/sqlite3.h"
#include <iostream>

int main() 
{
    TEST_DEBUG() << "SQLite version: "<< sqlite3_libversion() << std::endl;
    
    // 测试 JSON 功能
    sqlite3 *db;
    sqlite3_open(":memory:", &db);
    
    int rc = sqlite3_exec(db, "SELECT json('{\"test\":123}');", NULL, NULL, NULL);
    if (rc == SQLITE_OK) {
        TEST_DEBUG() << "JSON1 support: Enabled" << std::endl;
    } else {
        TEST_DEBUG() << "JSON1 support: Disabled" << std::endl;
    }
    
    sqlite3_close(db);
    return 0;
}