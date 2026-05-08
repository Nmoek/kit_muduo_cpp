
#include "../test_log.h"
#include "dao/init.h"
#include "sqlite_orm/sqlite_orm.h"
#include "nlohmann/json.hpp"

#include <gtest/gtest.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <memory>

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
    PrepareInitDbTestDir();
    {
        ScopedWorkingDirectory cwd(kInitDbTestDir);
        auto db = kit_dao::InitSqliteDb();
        ASSERT_NE(db, nullptr);

        auto journal_mode = db->pragma.get_pragma<std::string>("journal_mode");
        ASSERT_EQ(journal_mode, "wal");
    }
    CleanupInitDbTestDir();
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
