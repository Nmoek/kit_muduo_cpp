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
#include <cstdio>
#include <iostream>
#include <string>

namespace {

int Exec(sqlite3 *db, const char *sql)
{
    char *err_msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
    if(rc != SQLITE_OK)
    {
        TEST_DEBUG() << "exec failed, sql=" << sql
            << ", err=" << (err_msg ? err_msg : "") << std::endl;
        sqlite3_free(err_msg);
    }
    return rc;
}

int QueryText(sqlite3 *db, const char *sql, std::string *out)
{
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if(rc != SQLITE_OK)
    {
        TEST_DEBUG() << "prepare failed, sql=" << sql
            << ", err=" << sqlite3_errmsg(db) << std::endl;
        return rc;
    }

    rc = sqlite3_step(stmt);
    if(rc == SQLITE_ROW)
    {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        *out = text ? reinterpret_cast<const char *>(text) : "";
        rc = SQLITE_OK;
    }
    else
    {
        TEST_DEBUG() << "query produced no row, sql=" << sql
            << ", rc=" << rc << std::endl;
    }

    const int finalize_rc = sqlite3_finalize(stmt);
    return rc == SQLITE_OK ? finalize_rc : rc;
}

bool HasCompileOption(const char *option)
{
    if(sqlite3_compileoption_used(option) != 0)
    {
        return true;
    }

    TEST_DEBUG() << "compile option missing: " << option << std::endl;
    return false;
}

}   // namespace

int main() 
{
    TEST_DEBUG() << "SQLite version: "<< sqlite3_libversion() << std::endl;
    
    // 测试 JSON 功能
    sqlite3 *db;
    if(sqlite3_open(":memory:", &db) != SQLITE_OK)
    {
        TEST_DEBUG() << "open memory db failed" << std::endl;
        return 1;
    }
    
    int rc = Exec(db, "SELECT json('{\"test\":123}');");
    if (rc == SQLITE_OK) {
        TEST_DEBUG() << "JSON1 support: Enabled" << std::endl;
    } else {
        TEST_DEBUG() << "JSON1 support: Disabled" << std::endl;
        sqlite3_close(db);
        return 1;
    }
    
    sqlite3_close(db);

    bool compile_options_ok = true;
    compile_options_ok &= HasCompileOption("ENABLE_MEMSYS5");
    compile_options_ok &= HasCompileOption("DEFAULT_CACHE_SIZE=20000");
    compile_options_ok &= HasCompileOption("DEFAULT_MMAP_SIZE=268435456");
    compile_options_ok &= HasCompileOption("DEFAULT_SYNCHRONOUS=1");
    compile_options_ok &= HasCompileOption("DEFAULT_WAL_SYNCHRONOUS=1");
    if(!compile_options_ok)
    {
        return 1;
    }

    sqlite3 *file_db;
    std::remove("/tmp/kit_sqlite3_wal_test.sqlite");
    std::remove("/tmp/kit_sqlite3_wal_test.sqlite-wal");
    std::remove("/tmp/kit_sqlite3_wal_test.sqlite-shm");
    if(sqlite3_open("/tmp/kit_sqlite3_wal_test.sqlite", &file_db) != SQLITE_OK)
    {
        TEST_DEBUG() << "open file db failed" << std::endl;
        return 1;
    }

    std::string journal_mode;
    rc = QueryText(file_db, "PRAGMA journal_mode=WAL;", &journal_mode);
    if(rc != SQLITE_OK || journal_mode != "wal")
    {
        TEST_DEBUG() << "WAL mode failed, rc=" << rc
            << ", journal_mode=" << journal_mode << std::endl;
        sqlite3_close(file_db);
        return 1;
    }
    TEST_DEBUG() << "journal_mode: " << journal_mode << std::endl;

    sqlite3_close(file_db);
    std::remove("/tmp/kit_sqlite3_wal_test.sqlite");
    std::remove("/tmp/kit_sqlite3_wal_test.sqlite-wal");
    std::remove("/tmp/kit_sqlite3_wal_test.sqlite-shm");
    return 0;
}
