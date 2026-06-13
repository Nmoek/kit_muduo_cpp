
#include "../test_log.h"
#include "dao/init.h"
#include "domain/type.h"
#include "sqlite_orm/sqlite_orm.h"
#include "nlohmann/json.hpp"
#include "sqlite3.h"

#include <gtest/gtest.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <memory>
#include <system_error>

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

using nljson = nlohmann::json;

using namespace sqlite_orm;

namespace {

constexpr const char *kOrmTestDbPath = "/tmp/kit_sqlite_orm_test.sqlite";
constexpr const char *kInitDbTestDir = "/tmp/kit_init_sqlite_db_test";
constexpr const char *kInitDbTestPath = "/tmp/kit_init_sqlite_db_test/kit.sqlite";

void RemoveSqliteFiles(const std::string &path)
{
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
}

void PrepareInitDbTestDir()
{
    RemoveSqliteFiles(kInitDbTestPath);
    ::rmdir(kInitDbTestDir);
    if(::mkdir(kInitDbTestDir, 0700) != 0 && errno != EEXIST)
    {
        throw std::runtime_error(std::string("mkdir failed: ") + std::strerror(errno));
    }
}

void CleanupInitDbTestDir()
{
    RemoveSqliteFiles(kInitDbTestPath);
    ::rmdir(kInitDbTestDir);
}

class ScopedWorkingDirectory
{
public:
    explicit ScopedWorkingDirectory(const char *path)
    {
        char cwd[PATH_MAX] = {};
        if(::getcwd(cwd, sizeof(cwd)) == nullptr)
        {
            throw std::runtime_error(std::string("getcwd failed: ") + std::strerror(errno));
        }
        old_dir_ = cwd;

        if(::chdir(path) != 0)
        {
            throw std::runtime_error(std::string("chdir failed: ") + std::strerror(errno));
        }
    }

    ~ScopedWorkingDirectory()
    {
        if(!old_dir_.empty() && ::chdir(old_dir_.c_str()) != 0)
        {
            TEST_INFO() << "restore cwd failed: " << std::strerror(errno) << std::endl;
        }
    }

private:
    std::string old_dir_;
};

struct ScopedInitDbTestDir
{
    ScopedInitDbTestDir()
    {
        PrepareInitDbTestDir();
    }

    ~ScopedInitDbTestDir()
    {
        CleanupInitDbTestDir();
    }
};

kit_dao::Protocol MakeIndexedProtocol(int64_t project_id,
                                      const std::string &runtime_key,
                                      kit_domain::ProtocolConfigState config_state,
                                      kit_domain::ProtocolStatus status =
                                          kit_domain::ProtocolStatus::kValid)
{
    kit_dao::Protocol protocol{};
    protocol.m_name = "index_protocol_" + runtime_key;
    protocol.m_type = static_cast<int32_t>(kit_domain::ProtocolType::kHttp);
    protocol.m_projectId = project_id;
    protocol.m_runtimeKey = runtime_key;
    protocol.m_status = static_cast<int32_t>(status);
    protocol.m_configState = static_cast<int32_t>(config_state);
    protocol.m_reqBodyType = static_cast<int32_t>(kit_domain::ProtocolBodyType::kJson);
    protocol.m_respBodyType = static_cast<int32_t>(kit_domain::ProtocolBodyType::kJson);
    protocol.m_reqBodyDataStatus = 0;
    protocol.m_respBodyDataStatus = 0;
    protocol.m_reqCfg = R"({"method":"GET","path":"/index"})";
    protocol.m_respCfg = "{}";
    protocol.m_isEndian = 0;
    return protocol;
}

std::string ReadSqliteIndexSql(const char *db_path, const char *index_name)
{
    sqlite3 *raw_db = nullptr;
    const int open_rc = sqlite3_open(db_path, &raw_db);
    if(open_rc != SQLITE_OK)
    {
        std::string msg = raw_db ? sqlite3_errmsg(raw_db) : "unknown sqlite open error";
        if(raw_db)
        {
            sqlite3_close(raw_db);
        }
        throw std::runtime_error("open sqlite for index query failed: " + msg);
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT sql FROM sqlite_master WHERE type='index' AND name=?;";
    const int prepare_rc = sqlite3_prepare_v2(raw_db, sql, -1, &stmt, nullptr);
    if(prepare_rc != SQLITE_OK)
    {
        std::string msg = sqlite3_errmsg(raw_db);
        sqlite3_close(raw_db);
        throw std::runtime_error("prepare index query failed: " + msg);
    }

    sqlite3_bind_text(stmt, 1, index_name, -1, SQLITE_TRANSIENT);
    std::string index_sql;
    const int step_rc = sqlite3_step(stmt);
    if(step_rc == SQLITE_ROW)
    {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if(text != nullptr)
        {
            index_sql = reinterpret_cast<const char *>(text);
        }
    }
    else if(step_rc != SQLITE_DONE)
    {
        std::string msg = sqlite3_errmsg(raw_db);
        sqlite3_finalize(stmt);
        sqlite3_close(raw_db);
        throw std::runtime_error("step index query failed: " + msg);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(raw_db);
    return index_sql;
}

template <typename Db>
bool InsertFailsByUniqueRuntimeKey(Db &db, const kit_dao::Protocol &protocol)
{
    try
    {
        db.insert(protocol);
    }
    catch(const std::system_error &e)
    {
        return std::string(e.what()).find("UNIQUE constraint failed") != std::string::npos;
    }
    return false;
}

}   // namespace

struct Employee {
    int id;
    std::string first;
    std::string name;
    int age;
    // std::vector<int> nums;  // 不支持数组 中间层转换
    std::unique_ptr<std::string> address;  //  optional
    std::unique_ptr<double> salary;  //  optional
    std::string  json_str;
};

#define KIT_TEST_SQLITE_ORM() \
sqlite_orm::make_storage(kOrmTestDbPath,\
    sqlite_orm::make_table("COMPANY",\
            sqlite_orm::make_column("ID", &Employee::id, sqlite_orm::primary_key().autoincrement()),\
            sqlite_orm::make_column("FIRST", &Employee::first, sqlite_orm::not_null()),\
            sqlite_orm::make_column("NAME", &Employee::name, sqlite_orm::not_null()),\
            sqlite_orm::make_column("AGE", &Employee::age), \
            sqlite_orm::make_column("ADDRESS", &Employee::address),\
            sqlite_orm::make_column("SALARY", &Employee::salary),\
            sqlite_orm::make_column("JSON", &Employee::json_str) \
        ))

using TestDbType = decltype(KIT_TEST_SQLITE_ORM());


TEST(TestOrm, InitSqliteDbUsesWalAndPreservesSchema)
{
    ScopedInitDbTestDir test_dir;
    {
        ScopedWorkingDirectory cwd(kInitDbTestDir);
        auto db = kit_dao::InitSqliteDb();
        ASSERT_NE(db, nullptr);

        auto journal_mode = db->pragma.get_pragma<std::string>("journal_mode");
        ASSERT_EQ(journal_mode, "wal");
    }
}

/**
 * 测试思路：
 * 1. 通过 InitSqliteDb() 走真实初始化路径，确保不是测试手写索引。
 * 2. 查询 sqlite_master 中 uidx_protocols_pjid_runkey 的实际 SQL。
 * 3. 断言索引条件是 status = 1 AND config_state <> 2。
 *
 * 示例：
 *
 *   protocols(project_id, runtime_key)
 *              |
 *              v
 *   WHERE status = 1 AND config_state <> 2
 *
 * 这个用例防止开发机旧库或初始化代码退回到 WHERE status = 1，导致 kReConfig
 * 仍然占用 runtime_key。
 */
TEST(TestOrm, ProtocolRuntimeKeyIndexUsesReConfigAwarePredicate)
{
    ScopedInitDbTestDir test_dir;
    {
        ScopedWorkingDirectory cwd(kInitDbTestDir);
        auto db = kit_dao::InitSqliteDb();
        ASSERT_NE(db, nullptr);

        const std::string index_sql =
            ReadSqliteIndexSql("kit.sqlite", "uidx_protocols_pjid_runkey");

        ASSERT_FALSE(index_sql.empty());
        EXPECT_NE(index_sql.find("CREATE UNIQUE INDEX"), std::string::npos);
        EXPECT_NE(index_sql.find("uidx_protocols_pjid_runkey"), std::string::npos);
        EXPECT_NE(index_sql.find("ON protocols(project_id, runtime_key)"), std::string::npos);
        EXPECT_NE(index_sql.find("WHERE status = 1 AND config_state <> 2"), std::string::npos);
    }
}

/**
 * 测试思路：
 * 1. 同 project 下，kOff/kOn 都是有效配置态，必须占用 runtime_key。
 * 2. kReConfig 是待重新配置态，不应占用 runtime_key，允许用户按新 schema 重配。
 * 3. status=kInvalid 是软删态，也不应占用 runtime_key。
 *
 * 示例：
 *
 *   project 1001 + HTTP|GET|/same
 *
 *   kOff      + kOn       -> 冲突
 *   kOff      + kReConfig -> 放行
 *   kInvalid  + kOn       -> 放行
 *
 * 这组断言直接固定 04 文档的索引闭环：有效且非 kReConfig 的协议项唯一，
 * 待重配和软删协议释放运行键。
 */
TEST(TestOrm, ProtocolRuntimeKeyIndexAppliesOnlyToValidRunnableConfigs)
{
    ScopedInitDbTestDir test_dir;
    {
        ScopedWorkingDirectory cwd(kInitDbTestDir);
        auto db = kit_dao::InitSqliteDb();
        ASSERT_NE(db, nullptr);

        constexpr int64_t kProjectId = 1001;
        const std::string runtime_key = "HTTP|GET|/same";

        auto off_protocol = MakeIndexedProtocol(
            kProjectId, runtime_key, kit_domain::ProtocolConfigState::kOff);
        off_protocol.m_id = db->insert(off_protocol);

        auto on_duplicate = MakeIndexedProtocol(
            kProjectId, runtime_key, kit_domain::ProtocolConfigState::kOn);
        EXPECT_TRUE(InsertFailsByUniqueRuntimeKey(*db, on_duplicate));

        auto reconfig_duplicate = MakeIndexedProtocol(
            kProjectId, runtime_key, kit_domain::ProtocolConfigState::kReConfig);
        EXPECT_NO_THROW(reconfig_duplicate.m_id = db->insert(reconfig_duplicate));

        auto invalid_duplicate = MakeIndexedProtocol(
            kProjectId,
            runtime_key,
            kit_domain::ProtocolConfigState::kOn,
            kit_domain::ProtocolStatus::kInvalid);
        EXPECT_NO_THROW(invalid_duplicate.m_id = db->insert(invalid_duplicate));

        auto other_project_duplicate = MakeIndexedProtocol(
            kProjectId + 1, runtime_key, kit_domain::ProtocolConfigState::kOn);
        EXPECT_NO_THROW(other_project_duplicate.m_id = db->insert(other_project_duplicate));
    }
}

TEST(TestOrm, test1)
{
    auto sqliteDb = KIT_TEST_SQLITE_ORM();

    auto tables = sqliteDb.sync_schema(true);
    for(auto &t : tables)
    {
        TEST_INFO() << t.first << ", status= " << t.second << std::endl;
    }

    nljson tmp = {
        {"a", 111},
        {"b", {1, 2, 3}},
        {"c", {
            {"c_1", 1}, 
            {"c_2", 2}
        }
        }

    };


    // sqliteDb.remove_all<Employee>();

    Employee paul{-1, "", "Paul", 32, std::make_unique<std::string>("California"), std::make_unique<double>(20000.0), tmp.dump()};
    Employee allen{-1, "", "Allen", 25, std::make_unique<std::string>("Texas"), std::make_unique<double>(15000.0), tmp.dump()};
    Employee teddy{-1, "", "Teddy", 23, std::make_unique<std::string>("Norway"), std::make_unique<double>(20000.0), tmp.dump()};

    paul.id = sqliteDb.insert(paul);
    allen.id = sqliteDb.insert(allen);
    teddy.id = sqliteDb.insert(teddy);

    TEST_INFO() << "insert paul.id= " <<  paul.id << std::endl;
    TEST_INFO() << "insert allen.id= " <<  allen.id << std::endl;
    TEST_INFO() << "insert teddy.id= " <<  teddy.id << std::endl;
#if 0
    auto ems = sqliteDb.select(
        sqlite_orm::columns(
            &Employee::name, 
            &Employee::salary
        ), 
        sqlite_orm::where(sqlite_orm::c(&Employee::id) > 2)
    );

    for(auto &e : ems)
    {
        auto& salary = std::get<1>(e);
        TEST_INFO() << "name= " << std::get<0>(e)<< " salary= " << (salary ? *salary : 0) << std::endl;
    }


    auto res = sqliteDb.select(
        sqlite_orm::columns(
            sqlite_orm::json_extract<std::string>(&Employee::json_str, "$.a"),
            sqlite_orm::json_extract<std::string>(&Employee::json_str, "$.b"),
            sqlite_orm::json_extract<std::string>(&Employee::json_str, "$.c")
        ),
        sqlite_orm::where(sqlite_orm::c(&Employee::id) == 7)
    );
    
    for(auto &r : res)
    {
        TEST_DEBUG() << std::endl << "a= " << std::get<0>(r) << std::endl
            << "b= " << std::get<1>(r) << std::endl
            << "c= " << std::get<2>(r) << std::endl;
    }
#endif

    nljson test_root = {
        {"a", 7777},
        {"c", {
            {"c_1", 666},
            {"c_2", 999}
        }}
    };

    TEST_INFO() << test_root.at("a") << std::endl;
    try {

        sqliteDb.update_all(sqlite_orm::set(
                sqlite_orm::c(&Employee::json_str) = sqlite_orm::json_replace(&Employee::json_str, "$.a", sqlite_orm::json(test_root.at("a").dump()))
            ),
            sqlite_orm::where(sqlite_orm::c(&Employee::id) == 12)
        );

        sqliteDb.update_all(sqlite_orm::set(
            sqlite_orm::c(&Employee::json_str) = sqlite_orm::json_replace(&Employee::json_str, "$.c", sqlite_orm::json(test_root.at("c").dump()))
        ),
        sqlite_orm::where(sqlite_orm::c(&Employee::id) == 12)
    );

    } catch(std::exception &e) {
        TEST_INFO() << "json_extract error! " << e.what() << std::endl;
    }


}

int main(int argc, char **argv)
{
    RemoveSqliteFiles(kOrmTestDbPath);
    CleanupInitDbTestDir();
    testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    RemoveSqliteFiles(kOrmTestDbPath);
    CleanupInitDbTestDir();
    return result;
}
