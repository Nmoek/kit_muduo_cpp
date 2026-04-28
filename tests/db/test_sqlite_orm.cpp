
#include "../test_log.h"
#include "sqlite_orm/sqlite_orm.h"
#include "nlohmann/json.hpp"

#include <gtest/gtest.h>
#include <string>
#include <memory>

using nljson = nlohmann::json;

using namespace sqlite_orm;

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
sqlite_orm::make_storage("/mnt/nfs/proxy_db/test1.sqlite",\
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


TEST(TestOrm, test1)
{
    auto sqliteDb = KIT_TEST_SQLITE_ORM();

    auto tables = sqliteDb.sync_schema();
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
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}